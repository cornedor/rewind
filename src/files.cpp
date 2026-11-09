#include "files.h"

#include <QCollator>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>
#include <fcntl.h>
#include <sys/stat.h>

namespace {
constexpr qint64 textLimit = 8 * 1024 * 1024;
constexpr int lineLimit = 20000;

Signature signatureFrom(const QFileInfo &info) {
    Signature signature;
    signature.isLink = info.isSymLink();
    signature.exists = info.exists() || signature.isLink;
    if (!signature.exists)
        return signature;
    signature.isDir = info.isDir() && !signature.isLink;
    signature.size = signature.isLink ? 0 : info.size();
    signature.modified = info.lastModified(QTimeZone::UTC).toSecsSinceEpoch();
    return signature;
}
} // namespace

Signature signatureOf(const QString &path) {
    if (path.isEmpty())
        return {};
    QFileInfo info(path);
    info.setCaching(false);
    return signatureFrom(info);
}

EntryStatus compareBackupWithNow(const Signature &backup, const Signature &now) {
    if (!backup.exists)
        return EntryStatus::Unknown;
    if (!now.exists)
        return EntryStatus::Deleted;
    return backup == now ? EntryStatus::Same : EntryStatus::Changed;
}

EntryStatus compareNowWithBackup(const Signature &now, const Signature &backup) {
    if (!now.exists)
        return EntryStatus::Unknown;
    if (!backup.exists)
        return EntryStatus::NotBackedUp;
    return backup == now ? EntryStatus::Same : EntryStatus::Changed;
}

void sortEntries(QVector<Entry> *entries) {
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(entries->begin(), entries->end(), [&](const Entry &a, const Entry &b) {
        if (a.isDir != b.isDir)
            return a.isDir;
        return collator.compare(a.name, b.name) < 0;
    });
}

bool readFolder(const QString &folder, const QString &absoluteFolder, QVector<Entry> *entries) {
    QDir dir(folder);
    if (!dir.exists())
        return false;
    const auto infos = dir.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                                         QDir::NoSort);
    for (const QFileInfo &info : infos) {
        const Signature signature = signatureFrom(info);
        Entry entry;
        entry.name = info.fileName();
        entry.path = absoluteFolder == "/" ? '/' + entry.name : absoluteFolder + '/' + entry.name;
        entry.source = info.filePath();
        entry.isDir = signature.isDir;
        entry.isLink = signature.isLink;
        entry.size = signature.size;
        entry.modified = signature.modified;
        *entries << entry;
    }
    sortEntries(entries);
    return true;
}

VersionMap mapVersions(const QVector<Signature> &newestFirst, const Signature &now) {
    VersionMap map;
    const int n = newestFirst.size();
    map.version.fill(0, n);
    map.starts.fill(false, n);
    QVector<Signature> distinct;
    Signature previous;
    // Walk forward in time so version 1 is the oldest one.
    for (int i = n - 1; i >= 0; --i) {
        const Signature &signature = newestFirst.at(i);
        if (!signature.exists) {
            previous = signature;
            continue;
        }
        if (!previous.exists || signature != previous) {
            map.starts[i] = true;
            distinct << signature;
        }
        map.version[i] = distinct.size();
        previous = signature;
    }
    map.count = distinct.size();
    for (int v = distinct.size(); v >= 1; --v)
        if (now.exists && distinct.at(v - 1) == now) {
            map.nowMatches = v;
            break;
        }
    return map;
}

bool looksBinary(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return file.read(8192).contains('\0');
}

QVector<DiffLine> parseUnifiedDiff(const QByteArray &diff, int *added, int *removed) {
    QVector<DiffLine> lines;
    int oldLine = 0, newLine = 0, plus = 0, minus = 0;
    static const QRegularExpression hunk(R"(^@@ -(\d+)(?:,\d+)? \+(\d+)(?:,\d+)? @@(.*)$)");
    bool inHunk = false;
    for (const QByteArray &raw : diff.split('\n')) {
        const QString line = QString::fromUtf8(raw);
        if (!inHunk && (line.startsWith("--- ") || line.startsWith("+++ ")))
            continue;
        const auto match = hunk.match(line);
        if (match.hasMatch()) {
            inHunk = true;
            oldLine = match.captured(1).toInt();
            newLine = match.captured(2).toInt();
            lines << DiffLine{DiffLine::Hunk, oldLine, newLine, match.captured(3).trimmed()};
            continue;
        }
        if (!inHunk || line.isEmpty())
            continue;
        const QChar marker = line.at(0);
        const QString text = line.mid(1);
        if (marker == '+') {
            lines << DiffLine{DiffLine::Added, 0, newLine++, text};
            ++plus;
        } else if (marker == '-') {
            lines << DiffLine{DiffLine::Removed, oldLine++, 0, text};
            ++minus;
        } else if (marker == ' ') {
            lines << DiffLine{DiffLine::Context, oldLine++, newLine++, text};
        }
    }
    if (added)
        *added = plus;
    if (removed)
        *removed = minus;
    return lines;
}

TextComparison compareText(const QString &oldPath, const QString &newPath) {
    TextComparison result;
    const bool hasOld = !oldPath.isEmpty() && QFileInfo(oldPath).isFile();
    const bool hasNew = !newPath.isEmpty() && QFileInfo(newPath).isFile();
    if (!hasOld && !hasNew) {
        result.kind = TextComparison::Missing;
        return result;
    }
    if ((hasOld && QFileInfo(oldPath).size() > textLimit) || (hasNew && QFileInfo(newPath).size() > textLimit)) {
        result.kind = TextComparison::TooLarge;
        return result;
    }
    if ((hasOld && looksBinary(oldPath)) || (hasNew && looksBinary(newPath))) {
        result.kind = TextComparison::Binary;
        return result;
    }

    QProcess diff;
    diff.start("diff", {"-u", "--", hasOld ? oldPath : "/dev/null", hasNew ? newPath : "/dev/null"});
    if (!diff.waitForFinished(30000)) {
        diff.kill();
        result.error = diff.error() == QProcess::FailedToStart ? QStringLiteral("diff isn't installed")
                                                               : QStringLiteral("diff took too long");
        return result;
    }
    if (diff.exitCode() == 0) {
        result.kind = TextComparison::Identical;
        return result;
    }
    if (diff.exitCode() != 1) {
        result.error = QString::fromUtf8(diff.readAllStandardError()).trimmed();
        return result;
    }
    result.kind = TextComparison::Text;
    result.lines = parseUnifiedDiff(diff.readAllStandardOutput(), &result.added, &result.removed);
    if (result.lines.size() > lineLimit) {
        result.lines.resize(lineLimit);
        result.truncated = true;
    }
    return result;
}

TextComparison readText(const QString &path) {
    TextComparison result;
    QFileInfo info(path);
    if (!info.isFile()) {
        result.kind = TextComparison::Missing;
        return result;
    }
    if (info.size() > textLimit) {
        result.kind = TextComparison::TooLarge;
        return result;
    }
    if (looksBinary(path)) {
        result.kind = TextComparison::Binary;
        return result;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = file.errorString();
        return result;
    }
    result.kind = TextComparison::Identical;
    int number = 1;
    while (!file.atEnd()) {
        if (result.lines.size() >= lineLimit) {
            result.truncated = true;
            break;
        }
        QByteArray line = file.readLine();
        if (line.endsWith('\n'))
            line.chop(1);
        result.lines << DiffLine{DiffLine::Context, number, number, QString::fromUtf8(line)};
        ++number;
    }
    return result;
}

QString restoredCopyName(const QString &fileName, const QDateTime &when, const QString &label) {
    // "report.pdf" becomes "report (restored …).pdf", keeping double
    // extensions like .tar.gz whole and dotfiles like .bashrc in one piece.
    int split = fileName.lastIndexOf('.');
    if (split > 0) {
        const int before = fileName.lastIndexOf('.', split - 1);
        if (before > 0 && fileName.mid(before, split - before) == ".tar")
            split = before;
    }
    const QString base = split > 0 ? fileName.left(split) : fileName;
    const QString extension = split > 0 ? fileName.mid(split) : QString();
    return QStringLiteral("%1 (%2 %3)%4").arg(base, label, when.toString("yyyy-MM-dd HH.mm"), extension);
}

QString freePath(const QString &path) {
    if (!QFileInfo::exists(path) && !QFileInfo(path).isSymLink())
        return path;
    const QFileInfo info(path);
    const QString name = info.fileName();
    const int split = name.lastIndexOf('.');
    const QString base = split > 0 ? name.left(split) : name;
    const QString extension = split > 0 ? name.mid(split) : QString();
    for (int n = 2;; ++n) {
        const QString candidate = info.dir().filePath(QStringLiteral("%1 %2%3").arg(base).arg(n).arg(extension));
        if (!QFileInfo::exists(candidate) && !QFileInfo(candidate).isSymLink())
            return candidate;
    }
}

qint64 treeSize(const QString &path) {
    QFileInfo info(path);
    if (!info.isDir() || info.isSymLink())
        return info.isSymLink() ? 0 : info.size();
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        if (!it.fileInfo().isSymLink())
            total += it.fileInfo().size();
    }
    return total;
}

namespace {
// Mode and times straight from stat(2): Qt's permission flags mix in what the
// current user may do, which turns a 644 file into 744.
void copyMetadata(const QString &from, const QString &to) {
    struct stat st;
    if (::stat(QFile::encodeName(from).constData(), &st) != 0)
        return;
    const QByteArray target = QFile::encodeName(to);
    ::chmod(target.constData(), st.st_mode & 07777);
    const struct timespec times[2] = {st.st_atim, st.st_mtim};
    ::utimensat(AT_FDCWD, target.constData(), times, 0);
}

bool copyFile(const QString &from, const QString &to, RestoreProgress *progress, QString *error) {
    QFile in(from), out(to);
    if (!in.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("%1: %2").arg(from, in.errorString());
        return false;
    }
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QStringLiteral("%1: %2").arg(to, out.errorString());
        return false;
    }
    QByteArray buffer(1 << 20, Qt::Uninitialized);
    for (;;) {
        if (progress && progress->cancel) {
            *error = QStringLiteral("cancelled");
            return false;
        }
        const qint64 n = in.read(buffer.data(), buffer.size());
        if (n < 0) {
            *error = QStringLiteral("%1: %2").arg(from, in.errorString());
            return false;
        }
        if (n == 0)
            break;
        if (out.write(buffer.constData(), n) != n) {
            *error = QStringLiteral("%1: %2").arg(to, out.errorString());
            return false;
        }
        if (progress)
            progress->bytes += n;
    }
    if (!out.flush()) {
        *error = QStringLiteral("%1: %2").arg(to, out.errorString());
        return false;
    }
    out.close();
    copyMetadata(from, to);
    return true;
}

bool copyTree(const QString &from, const QString &to, RestoreProgress *progress, QString *error) {
    const QFileInfo info(from);
    if (info.isSymLink()) {
        if (QFile::link(info.readSymLink(), to))
            return true;
        *error = QStringLiteral("%1: couldn't create the link").arg(to);
        return false;
    }
    if (!info.isDir())
        return copyFile(from, to, progress, error);
    if (!QDir().mkdir(to)) {
        *error = QStringLiteral("%1: couldn't create the folder").arg(to);
        return false;
    }
    const QDir dir(from);
    for (const QString &name : dir.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
        if (!copyTree(dir.filePath(name), to + '/' + name, progress, error))
            return false;
    copyMetadata(from, to);
    return true;
}

void removeAll(const QString &path) {
    const QFileInfo info(path);
    if (info.isDir() && !info.isSymLink())
        QDir(path).removeRecursively();
    else
        QFile::remove(path);
}

QString newHoldingFolder(const QString &holdingRoot) {
    const QString folder = holdingRoot + '/' + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz") + '-' +
                           QUuid::createUuid().toString(QUuid::Id128).left(6);
    QDir().mkpath(folder);
    return folder;
}

bool move(const QString &from, const QString &to, QString *error) {
    if (QDir().rename(from, to))
        return true;
    *error = QStringLiteral("Couldn't move %1 to %2. Is it on another drive?").arg(from, to);
    return false;
}
} // namespace

bool restoreTo(const QString &source, const QString &target, bool replace, const QString &holdingRoot,
               RestoreProgress *progress, RestoreOperation *operation, QString *error) {
    const QFileInfo sourceInfo(source);
    if (!sourceInfo.exists() && !sourceInfo.isSymLink()) {
        *error = QStringLiteral("%1 isn't in the backup").arg(source);
        return false;
    }
    const QFileInfo targetInfo(target);
    if (!QDir().mkpath(targetInfo.absolutePath())) {
        *error = QStringLiteral("Couldn't create %1").arg(targetInfo.absolutePath());
        return false;
    }
    const bool targetExists = targetInfo.exists() || targetInfo.isSymLink();
    if (targetExists && !replace) {
        *error = QStringLiteral("%1 already exists").arg(target);
        return false;
    }

    const QString temporary = targetInfo.dir().filePath(
        QStringLiteral(".%1.rewind-%2").arg(targetInfo.fileName(), QUuid::createUuid().toString(QUuid::Id128).left(8)));
    if (!copyTree(source, temporary, progress, error)) {
        removeAll(temporary);
        return false;
    }

    *operation = RestoreOperation{target, {}, {}, false};
    if (targetExists) {
        operation->heldOriginal = newHoldingFolder(holdingRoot) + '/' + targetInfo.fileName();
        if (!move(target, operation->heldOriginal, error)) {
            removeAll(temporary);
            return false;
        }
    }
    if (!move(temporary, target, error)) {
        if (targetExists)
            QDir().rename(operation->heldOriginal, target);
        removeAll(temporary);
        return false;
    }
    return true;
}

bool undoRestore(RestoreOperation *operation, const QString &holdingRoot, QString *error) {
    if (operation->undone)
        return true;
    if (operation->heldRestored.isEmpty())
        operation->heldRestored = newHoldingFolder(holdingRoot) + '/' + QFileInfo(operation->target).fileName();
    if (!move(operation->target, operation->heldRestored, error))
        return false;
    if (!operation->heldOriginal.isEmpty() && !move(operation->heldOriginal, operation->target, error)) {
        QDir().rename(operation->heldRestored, operation->target);
        return false;
    }
    operation->undone = true;
    return true;
}

bool redoRestore(RestoreOperation *operation, QString *error) {
    if (!operation->undone)
        return true;
    if (!operation->heldOriginal.isEmpty() && !move(operation->target, operation->heldOriginal, error))
        return false;
    if (!move(operation->heldRestored, operation->target, error)) {
        if (!operation->heldOriginal.isEmpty())
            QDir().rename(operation->heldOriginal, operation->target);
        return false;
    }
    operation->undone = false;
    return true;
}
