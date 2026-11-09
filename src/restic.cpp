#include "restic.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>

namespace Restic {

QHash<QString, QString> parseEnvironmentFile(const QByteArray &contents) {
    QHash<QString, QString> values;
    for (const QString &raw : QString::fromUtf8(contents).split('\n')) {
        QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith(';'))
            continue;
        if (line.startsWith("export "))
            line = line.mid(7).trimmed();
        const int equals = line.indexOf('=');
        if (equals <= 0)
            continue;
        const QString key = line.left(equals).trimmed();
        QString value = line.mid(equals + 1).trimmed();
        if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front())
            value = value.mid(1, value.size() - 2);
        values.insert(key, value);
    }
    return values;
}

ResticConfig loadConfig(const QProcessEnvironment &base, const QString &envFile) {
    ResticConfig config;
    config.environment = base;
    const bool shellHasRepository = base.contains("RESTIC_REPOSITORY") || base.contains("RESTIC_REPOSITORY_FILE");
    if (!shellHasRepository) {
        QFile file(envFile);
        if (file.open(QIODevice::ReadOnly)) {
            const auto values = parseEnvironmentFile(file.readAll());
            for (auto it = values.begin(); it != values.end(); ++it)
                config.environment.insert(it.key(), it.value());
            config.source = envFile;
        }
    }

    QString repository = config.environment.value("RESTIC_REPOSITORY");
    if (repository.isEmpty() && config.environment.contains("RESTIC_REPOSITORY_FILE")) {
        QFile file(config.environment.value("RESTIC_REPOSITORY_FILE"));
        if (file.open(QIODevice::ReadOnly))
            repository = QString::fromUtf8(file.readLine()).trimmed();
    }
    if (repository.isEmpty())
        config.error = QCoreApplication::translate("Restic", "No restic repository is set up. Put RESTIC_REPOSITORY "
                                                             "and RESTIC_PASSWORD_COMMAND in %1.")
                           .arg(envFile);
    config.repository = describeRepository(repository);
    return config;
}

QString describeRepository(const QString &repository) {
    QString shown = repository;
    static const QRegularExpression credentials("//[^/@]+@");
    shown.replace(credentials, "//");
    return shown;
}

QVector<Snapshot> parseSnapshots(const QByteArray &json, const QString &hostname, QString *error) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (!document.isArray()) {
        if (error)
            *error = QCoreApplication::translate("Restic", "restic returned something other than a snapshot list: %1")
                         .arg(parseError.errorString());
        return {};
    }

    QVector<Snapshot> all;
    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        Snapshot snapshot;
        snapshot.id = object.value("id").toString();
        snapshot.shortId = object.value("short_id").toString(snapshot.id.left(8));
        snapshot.time = QDateTime::fromString(object.value("time").toString(), Qt::ISODateWithMs).toLocalTime();
        for (const QJsonValue &path : object.value("paths").toArray())
            snapshot.paths << QDir::cleanPath(path.toString());
        snapshot.hostname = object.value("hostname").toString();
        snapshot.parent = object.value("parent").toString();
        snapshot.username = object.value("username").toString();
        snapshot.programVersion = object.value("program_version").toString();
        for (const QJsonValue &tag : object.value("tags").toArray())
            snapshot.tags << tag.toString();
        const QJsonObject summary = object.value("summary").toObject();
        if (!summary.isEmpty()) {
            snapshot.backupStart = QDateTime::fromString(summary.value("backup_start").toString(), Qt::ISODateWithMs).toLocalTime();
            snapshot.backupEnd = QDateTime::fromString(summary.value("backup_end").toString(), Qt::ISODateWithMs).toLocalTime();
            snapshot.filesNew = summary.value("files_new").toInteger(-1);
            snapshot.filesChanged = summary.value("files_changed").toInteger(-1);
            snapshot.filesUnmodified = summary.value("files_unmodified").toInteger(-1);
            snapshot.dataAdded = summary.value("data_added").toInteger(-1);
            snapshot.dataAddedPacked = summary.value("data_added_packed").toInteger(-1);
            snapshot.totalFiles = summary.value("total_files_processed").toInteger(-1);
            snapshot.totalBytes = summary.value("total_bytes_processed").toInteger(-1);
        }
        if (!snapshot.id.isEmpty() && snapshot.time.isValid())
            all << snapshot;
    }

    const bool hasOwn = std::any_of(all.begin(), all.end(), [&](const Snapshot &s) { return s.hostname == hostname; });
    QVector<Snapshot> snapshots;
    for (const Snapshot &snapshot : all)
        if (!hasOwn || snapshot.hostname == hostname)
            snapshots << snapshot;
    std::stable_sort(snapshots.begin(), snapshots.end(),
                     [](const Snapshot &a, const Snapshot &b) { return a.time > b.time; });
    return snapshots;
}

namespace {
bool isWithin(const QString &path, const QString &root) {
    return path == root || root == "/" || path.startsWith(root + '/');
}

QString treeRootOf(const Snapshot &snapshot, int i) {
    return i < snapshot.treeRoots.size() ? snapshot.treeRoots.at(i) : snapshot.paths.at(i);
}
} // namespace

QStringList candidateTreeRoots(const QString &path) {
    QStringList candidates;
    const QStringList parts = QDir::cleanPath(path).split('/', Qt::SkipEmptyParts);
    for (int i = 0; i < parts.size(); ++i)
        candidates << '/' + parts.mid(i).join('/');
    if (candidates.isEmpty())
        candidates << "/";
    return candidates;
}

// Joins a root ("/" or "/a/b") and a rest ("" or "/c") into a path.
static QString joinRoot(const QString &root, const QString &rest) {
    const QString joined = (root == "/" ? QString() : root) + rest;
    return joined.isEmpty() ? QStringLiteral("/") : joined;
}

QString treePath(const Snapshot &snapshot, const QString &absolutePath) {
    const QString path = QDir::cleanPath(absolutePath);
    for (int i = 0; i < snapshot.paths.size(); ++i) {
        const QString &root = snapshot.paths.at(i);
        if (!isWithin(path, root))
            continue;
        const QString rest = root == "/" ? (path == "/" ? QString() : path) : path.mid(root.size());
        return joinRoot(treeRootOf(snapshot, i), rest);
    }
    return {};
}

QString absolutePath(const Snapshot &snapshot, const QString &tree) {
    const QString path = QDir::cleanPath(tree.startsWith('/') ? tree : '/' + tree);
    for (int i = 0; i < snapshot.paths.size(); ++i) {
        const QString prefix = treeRootOf(snapshot, i);
        if (!isWithin(path, prefix))
            continue;
        const QString rest = prefix == "/" ? (path == "/" ? QString() : path) : path.mid(prefix.size());
        return joinRoot(snapshot.paths.at(i), rest);
    }
    return {};
}

QStringList namesLeadingToBackup(const Snapshot &snapshot, const QString &absoluteFolder) {
    const QString folder = QDir::cleanPath(absoluteFolder);
    QStringList names;
    for (const QString &root : snapshot.paths) {
        if (root == folder || !isWithin(root, folder))
            continue;
        const QString rest = folder == "/" ? root.mid(1) : root.mid(folder.size() + 1);
        const QString name = rest.section('/', 0, 0);
        if (!name.isEmpty() && !names.contains(name))
            names << name;
    }
    return names;
}

bool covers(const Snapshot &snapshot, const QString &absolutePath) {
    return !treePath(snapshot, absolutePath).isEmpty();
}

RepositoryStats parseStats(const QByteArray &json) {
    const QJsonObject object = QJsonDocument::fromJson(json).object();
    RepositoryStats stats;
    stats.storedBytes = object.value("total_size").toInteger(-1);
    stats.uncompressedBytes = object.value("total_uncompressed_size").toInteger(-1);
    return stats;
}

QStringList retentionArguments(const QStringList &argv) {
    if (!argv.contains("forget"))
        return {};
    static const QStringList withValue{"--group-by", "--host", "--tag", "--path", "--keep-tag"};
    QStringList kept;
    for (int i = 0; i < argv.size(); ++i) {
        const QString &argument = argv.at(i);
        const QString name = argument.section('=', 0, 0);
        if (!name.startsWith("--keep-") && !withValue.contains(name))
            continue;
        kept << argument;
        // `--keep-daily 7` carries its value in the next argument.
        if (!argument.contains('=') && i + 1 < argv.size()) {
            kept << argv.at(i + 1);
            ++i;
        }
    }
    return kept;
}

QList<QStringList> parseExecStart(const QString &value) {
    // { path=/usr/bin/restic ; argv[]=/usr/bin/restic forget --prune ; ignore_errors=no ; … }
    QList<QStringList> commands;
    static const QRegularExpression argv("argv\\[\\]=([^;]*);");
    auto it = argv.globalMatch(value);
    while (it.hasNext())
        commands << it.next().captured(1).trimmed().split(' ', Qt::SkipEmptyParts);
    return commands;
}

Retention parseForget(const QByteArray &json) {
    Retention retention;
    for (const QJsonValue &group : QJsonDocument::fromJson(json).array()) {
        const QJsonObject object = group.toObject();
        for (const QJsonValue &reason : object.value("reasons").toArray()) {
            const QJsonObject r = reason.toObject();
            const QString id = r.value("snapshot").toObject().value("id").toString();
            QStringList rules;
            for (const QJsonValue &match : r.value("matches").toArray()) {
                // "hourly snapshot", "last snapshot", "within 2d", "tags [x]"
                QString words = match.toString();
                if (words.endsWith(" snapshot"))
                    words.chop(9);
                // "oldest daily": kept by the daily rule while there are
                // fewer days of backups than it keeps.
                if (words.startsWith("oldest "))
                    words = words.mid(7);
                if (!rules.contains(words))
                    rules << words;
            }
            if (!id.isEmpty())
                retention.keptBecause.insert(id, rules);
        }
        for (const QJsonValue &snapshot : object.value("keep").toArray()) {
            const QString id = snapshot.toObject().value("id").toString();
            if (!retention.keptBecause.contains(id))
                retention.keptBecause.insert(id, {});
        }
        for (const QJsonValue &snapshot : object.value("remove").toArray())
            retention.removed << snapshot.toObject().value("id").toString();
    }
    return retention;
}

} // namespace Restic
