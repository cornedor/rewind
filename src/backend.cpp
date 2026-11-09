#include "backend.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QLocale>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent>

namespace {
Signature entrySignature(const Entry &entry) {
    Signature signature;
    signature.exists = true;
    signature.isDir = entry.isDir;
    signature.isLink = entry.isLink;
    signature.size = entry.size;
    signature.modified = entry.modified;
    return signature;
}

bool isImage(const QString &path) {
    static const auto formats = QImageReader::supportedImageFormats();
    return formats.contains(QFileInfo(path).suffix().toLower().toUtf8());
}

// The file's size and time ride along in the URL. Qt's Image keeps showing
// what it loaded for a URL, so a file edited while Rewind is open needs a new
// one to be read again; the query doesn't change which file is read.
QString imageUrl(const QString &path) {
    const Signature signature = signatureOf(path);
    QUrl url = QUrl::fromLocalFile(path);
    url.setQuery(QStringLiteral("v=%1-%2").arg(signature.modified).arg(signature.size));
    return url.toString();
}

QString parentOf(const QString &path) {
    if (path == "/" || path.isEmpty())
        return "/";
    const QString parent = QFileInfo(path).absolutePath();
    return parent.isEmpty() ? "/" : parent;
}
} // namespace

Backend::Backend(Repository *repository, BackupService *backups, const QString &startPath, QObject *parent)
    : QObject(parent), m_repository(repository), m_backups(backups), m_home(QDir::homePath()) {
    const QFileInfo start(startPath.isEmpty() ? m_home : startPath);
    const QString absolute = QDir::cleanPath(start.absoluteFilePath());
    // A file argument opens its folder with the file selected; a path that no
    // longer exists does too, so `rewind ~/gone.txt` finds what was deleted.
    if (start.isDir()) {
        m_folder = absolute;
    } else {
        m_folder = parentOf(absolute);
        m_focusName = QFileInfo(absolute).fileName();
    }

    m_clock = QDateTime::currentDateTime();
    m_clockTimer.setInterval(30000);
    connect(&m_clockTimer, &QTimer::timeout, this, [this] {
        m_clock = QDateTime::currentDateTime();
        emit clockChanged();
    });
    m_clockTimer.start();

    m_focusDebounce.setSingleShot(true);
    m_focusDebounce.setInterval(120);
    connect(&m_focusDebounce, &QTimer::timeout, this, &Backend::loadVersions);

    m_progressTimer.setInterval(100);
    connect(&m_progressTimer, &QTimer::timeout, this, &Backend::restoreChanged);

    connect(m_repository, &Repository::snapshotsChanged, this, &Backend::onRepositoryChanged);
    connect(m_repository, &Repository::mountChanged, this, &Backend::onRepositoryChanged);
    connect(m_backups, &BackupService::finished, this, [this](bool success) {
        if (!success) {
            emit notify(tr("The backup failed. Open the status bar's log to see why."));
            return;
        }
        emit notify(tr("Backup finished."));
        m_repository->reopen();
    });
    connect(m_backups, &BackupService::skipped, this, [this] {
        emit notify(tr("The backup service skipped this run because one of its conditions wasn't met."));
    });
    connect(m_backups, &BackupService::failedToStart, this, [this](const QString &message) {
        emit notify(tr("Couldn't start the backup: %1").arg(message));
    });

    relist();
}

Backend::~Backend() {
    if (m_progress)
        m_progress->cancel = true;
}

// Folders and moments

QVariantList Backend::crumbs() const {
    QVariantList crumbs;
    QString path;
    QString rest = m_folder;
    if (m_folder == m_home || m_folder.startsWith(m_home + '/')) {
        crumbs << QVariantMap{{"name", tr("Home")}, {"path", m_home}, {"home", true}};
        path = m_home;
        rest = m_folder.mid(m_home.size());
    } else {
        crumbs << QVariantMap{{"name", "/"}, {"path", "/"}, {"home", false}};
    }
    for (const QString &part : rest.split('/', Qt::SkipEmptyParts)) {
        path = path == "/" ? '/' + part : path + '/' + part;
        crumbs << QVariantMap{{"name", part}, {"path", path}, {"home", false}};
    }
    return crumbs;
}

QString Backend::repository() const {
    return m_repository->config().repository;
}

QString Backend::repositoryState() const {
    const auto snapshots = m_repository->snapshotsState();
    const auto mount = m_repository->mountState();
    if (snapshots == Repository::State::Failed || mount == Repository::State::Failed)
        return "failed";
    if (snapshots == Repository::State::Ready && mount == Repository::State::Ready)
        return "ready";
    if (snapshots == Repository::State::Ready)
        return "mounting";
    return "loading";
}

const Snapshot *Backend::current() const {
    const auto &snapshots = m_repository->snapshots();
    if (m_snapshotIndex < 0 || m_snapshotIndex >= snapshots.size())
        return nullptr;
    return &snapshots.at(m_snapshotIndex);
}

bool Backend::folderInBackups() const {
    const auto &snapshots = m_repository->snapshots();
    return std::any_of(snapshots.begin(), snapshots.end(), [this](const Snapshot &s) {
        return Restic::covers(s, m_folder) || !Restic::namesLeadingToBackup(s, m_folder).isEmpty();
    });
}

QVariantList Backend::timeline() const {
    QVariantList rows;
    const auto &snapshots = m_repository->snapshots();
    const QString path = focusPath();
    const bool marks = !path.isEmpty() && m_versionsPath == path && m_versions.version.size() == snapshots.size();
    const QDate today = m_clock.date();
    QDate previousDay;
    for (int i = 0; i < snapshots.size(); ++i) {
        const Snapshot &snapshot = snapshots.at(i);
        const QDate day = snapshot.time.date();
        QString summary;
        if (snapshot.filesNew >= 0 && snapshot.filesChanged >= 0) {
            const qint64 changed = snapshot.filesNew + snapshot.filesChanged;
            summary = changed == 0 ? tr("No changes") : tr("%n files changed", "", int(changed));
        }
        QString mark;
        if (marks)
            mark = m_versions.version.at(i) == 0 ? "absent" : m_versions.starts.at(i) ? "start" : "same";
        rows << QVariantMap{
            {"index", i},
            {"id", snapshot.shortId},
            {"time", snapshot.time},
            {"clock", QLocale().toString(snapshot.time.time(), QLocale::ShortFormat)},
            {"day", dayHeading(day, today)},
            {"newDay", day != previousDay},
            {"summary", summary},
            {"mark", mark},
            {"version", marks ? m_versions.version.at(i) : 0},
            {"covers", Restic::covers(snapshot, m_folder) || !Restic::namesLeadingToBackup(snapshot, m_folder).isEmpty()},
        };
        previousDay = day;
    }
    return rows;
}

QVariantMap Backend::moment() const {
    const Snapshot *snapshot = current();
    if (!snapshot)
        return {{"isNow", true}, {"title", tr("Now")}, {"index", -1}};
    return {{"isNow", false},
            {"title", describeMoment(snapshot->time, m_clock)},
            {"ago", describeAgo(snapshot->time, m_clock)},
            {"id", snapshot->shortId},
            {"index", m_snapshotIndex},
            {"total", int(m_repository->snapshots().size())},
            {"hasOlder", m_snapshotIndex + 1 < m_repository->snapshots().size()}};
}

void Backend::onRepositoryChanged() {
    // A reopened repository can hold a new backup at the top, so keep showing
    // the same backup by id rather than by position.
    const auto &snapshots = m_repository->snapshots();
    if (m_snapshotIndex >= 0) {
        int index = -1;
        for (int i = 0; i < snapshots.size(); ++i)
            if (snapshots.at(i).shortId == m_shownId)
                index = i;
        m_snapshotIndex = index;
        if (index < 0)
            m_shownId.clear();
    }

    emit repositoryChanged();
    emit momentChanged();
    emit timelineChanged();
    relist();
    m_focusDebounce.start();
}

void Backend::showSnapshot(int index) {
    const int count = m_repository->snapshots().size();
    index = std::clamp(index, -1, count - 1);
    if (index == m_snapshotIndex)
        return;
    m_snapshotIndex = index;
    m_shownId = index >= 0 ? m_repository->snapshots().at(index).shortId : QString();
    m_changes.clear();
    m_changesError.clear();
    m_changesKey.clear();
    emit changesChanged();
    emit momentChanged();
    emit versionsChanged();
    emit timelineChanged();
    relist();
}

void Backend::older() {
    showSnapshot(m_snapshotIndex + 1);
}

void Backend::newer() {
    showSnapshot(m_snapshotIndex - 1);
}

int Backend::currentVersion() const {
    if (m_snapshotIndex < 0)
        return m_versions.nowMatches;
    return m_snapshotIndex < m_versions.version.size() ? m_versions.version.at(m_snapshotIndex) : 0;
}

int Backend::previousVersionIndex(int from) const {
    const int v = from < 0 ? m_versions.nowMatches : (from < m_versions.version.size() ? m_versions.version.at(from) : 0);
    for (int j = from + 1; j < m_versions.version.size(); ++j)
        if (m_versions.version.at(j) != 0 && m_versions.version.at(j) != v)
            return j;
    return -2;
}

int Backend::nextVersionIndex(int from) const {
    if (from < 0)
        return -2;
    const int v = from < m_versions.version.size() ? m_versions.version.at(from) : 0;
    for (int j = from - 1; j >= 0; --j) {
        const int w = m_versions.version.at(j);
        if (w == 0 || w == v)
            continue;
        // Land on the newest backup that still has this version.
        while (j > 0 && m_versions.version.at(j - 1) == w)
            --j;
        return j;
    }
    // Nothing newer in the backups, but the file on disk is newer still.
    return v != m_versions.nowMatches ? -1 : -2;
}

void Backend::previousVersion() {
    if (m_versionsPath != focusPath())
        return;
    const int index = previousVersionIndex(m_snapshotIndex);
    if (index == -2) {
        emit notify(tr("This is the oldest version in your backups."));
        return;
    }
    showSnapshot(index);
}

void Backend::nextVersion() {
    if (m_versionsPath != focusPath())
        return;
    const int index = nextVersionIndex(m_snapshotIndex);
    if (index == -2) {
        emit notify(tr("This is the newest version."));
        return;
    }
    showSnapshot(index);
}

// Listing

void Backend::goTo(const QString &path, const QString &select) {
    const QString folder = QDir::cleanPath(path.startsWith("~") ? m_home + path.mid(1) : path);
    m_folder = folder.isEmpty() ? "/" : folder;
    m_focusName = select;
    m_selectedRow = -1;
    m_filter.clear();
    emit filterChanged();
    emit folderChanged();
    emit timelineChanged();
    relist();
}

void Backend::goUp() {
    if (m_folder == "/")
        return;
    goTo(parentOf(m_folder), QFileInfo(m_folder).fileName());
}

void Backend::openRow(int row) {
    if (row < 0 || row >= m_entries.count())
        return;
    const Entry &entry = m_entries.at(row);
    if (entry.isDir)
        goTo(entry.path);
    else
        openExternally();
}

void Backend::relist() {
    const int generation = ++m_listingGeneration;
    m_listing = true;
    emit listingChanged();

    const QString folder = m_folder;
    const Snapshot *snapshot = current();
    const bool mountReady = m_repository->mountState() == Repository::State::Ready;
    const bool failed = repositoryState() == "failed";
    const bool atSnapshot = snapshot != nullptr;
    const QString source = snapshot ? m_repository->sourcePath(*snapshot, folder) : QString();
    const QStringList leading = snapshot ? Restic::namesLeadingToBackup(*snapshot, folder) : QStringList();
    const auto &snapshots = m_repository->snapshots();
    const QString latestSource =
        !snapshot && !snapshots.isEmpty() ? m_repository->sourcePath(snapshots.first(), folder) : QString();

    auto *watcher = new QFutureWatcher<ListingResult>(this);
    connect(watcher, &QFutureWatcher<ListingResult>::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        if (generation == m_listingGeneration)
            applyListing(watcher->result());
    });
    watcher->setFuture(QtConcurrent::run([=]() {
        ListingResult result;
        if (atSnapshot) {
            if (!mountReady) {
                result.message = failed ? tr("Your backups couldn't be opened.") : tr("Opening your backups…");
            } else if (!source.isEmpty()) {
                if (readFolder(source, folder, &result.entries)) {
                    for (Entry &entry : result.entries)
                        entry.status = compareBackupWithNow(entrySignature(entry), ::signatureOf(entry.path));
                } else {
                    result.message = tr("This folder isn't in this backup.");
                }
            } else if (!leading.isEmpty()) {
                for (const QString &name : leading) {
                    Entry entry;
                    entry.name = name;
                    entry.path = folder == "/" ? '/' + name : folder + '/' + name;
                    entry.isDir = true;
                    entry.status = EntryStatus::Same;
                    result.entries << entry;
                }
                sortEntries(&result.entries);
            } else {
                result.message = tr("This folder isn't in this backup.");
            }
        } else if (!readFolder(folder, folder, &result.entries)) {
            result.message = tr("This folder doesn't exist anymore. Press [ to find it in your backups.");
        } else if (!latestSource.isEmpty()) {
            for (Entry &entry : result.entries)
                entry.status = compareNowWithBackup(entrySignature(entry), ::signatureOf(latestSource + '/' + entry.name));
        }
        return result;
    }));
}

void Backend::applyListing(const ListingResult &result) {
    m_allEntries = result.entries;
    m_listingMessage = result.message;
    m_listing = false;
    emit listingChanged();
    applyFilter();
}

void Backend::applyFilter() {
    QVector<Entry> visible;
    for (const Entry &entry : std::as_const(m_allEntries)) {
        if (!m_showHidden && entry.name.startsWith('.') && entry.name != m_focusName)
            continue;
        if (m_onlyChanges && entry.status != EntryStatus::Changed && entry.status != EntryStatus::Deleted &&
            entry.status != EntryStatus::NotBackedUp)
            continue;
        if (!m_filter.isEmpty() && !entry.name.contains(m_filter, Qt::CaseInsensitive))
            continue;
        visible << entry;
    }
    m_entries.reset(visible);

    const QString previousPath = focusPath();
    int row = m_focusName.isEmpty() ? -1 : m_entries.find(m_focusName);
    // With nothing chosen yet, or the chosen file filtered away, start at
    // the top. A file that only isn't in this backup stays chosen, so its
    // versions stay on the timeline while moving through time.
    const bool focusFiltered = !m_focusName.isEmpty() && row < 0 &&
                               std::any_of(m_allEntries.begin(), m_allEntries.end(),
                                           [this](const Entry &e) { return e.name == m_focusName; });
    if (row < 0 && (m_focusName.isEmpty() || focusFiltered) && m_entries.count() > 0) {
        row = 0;
        m_focusName = m_entries.at(0).name;
    }
    m_selectedRow = row;
    emit selectionChanged();
    loadPreview();
    if (focusPath() != previousPath || m_versionsPath != focusPath())
        m_focusDebounce.start();
}

void Backend::setSelectedRow(int row) {
    if (row < 0 || row >= m_entries.count() || row == m_selectedRow)
        return;
    m_selectedRow = row;
    m_focusName = m_entries.at(row).name;
    emit selectionChanged();
    focusChanged();
}

void Backend::moveSelection(int delta) {
    if (m_entries.count() == 0)
        return;
    const int row = m_selectedRow < 0 ? 0 : std::clamp(m_selectedRow + delta, 0, m_entries.count() - 1);
    if (row == m_selectedRow)
        return;
    setSelectedRow(row);
}

void Backend::focusChanged() {
    loadPreview();
    m_focusDebounce.start();
    emit timelineChanged();
}

void Backend::setFilter(const QString &filter) {
    if (filter == m_filter)
        return;
    m_filter = filter;
    emit filterChanged();
    applyFilter();
}

void Backend::setShowHidden(bool show) {
    if (show == m_showHidden)
        return;
    m_showHidden = show;
    emit filterChanged();
    applyFilter();
}

void Backend::setOnlyChanges(bool only) {
    if (only == m_onlyChanges)
        return;
    m_onlyChanges = only;
    emit filterChanged();
    applyFilter();
}

QString Backend::focusPath() const {
    if (m_focusName.isEmpty())
        return {};
    return m_folder == "/" ? '/' + m_focusName : m_folder + '/' + m_focusName;
}

Entry Backend::focusEntry(bool *found) const {
    *found = false;
    for (const Entry &entry : m_allEntries)
        if (entry.name == m_focusName) {
            *found = true;
            return entry;
        }
    return {};
}

// Versions

QString Backend::versionSource(int snapshot) const {
    const auto &snapshots = m_repository->snapshots();
    if (snapshot < 0 || snapshot >= snapshots.size())
        return {};
    return m_repository->sourcePath(snapshots.at(snapshot), focusPath());
}

void Backend::loadVersions() {
    const int generation = ++m_versionsGeneration;
    const QString path = focusPath();
    const auto &snapshots = m_repository->snapshots();
    if (path.isEmpty() || snapshots.isEmpty() || m_repository->mountState() != Repository::State::Ready) {
        m_versions = {};
        m_versionsPath.clear();
        m_versionsLoading = false;
        emit versionsChanged();
        emit timelineChanged();
        return;
    }
    QStringList sources;
    for (const Snapshot &snapshot : snapshots)
        sources << m_repository->sourcePath(snapshot, path);
    m_versionsLoading = true;
    emit versionsChanged();

    auto *watcher = new QFutureWatcher<VersionMap>(this);
    connect(watcher, &QFutureWatcher<VersionMap>::finished, this, [this, watcher, generation, path] {
        watcher->deleteLater();
        if (generation != m_versionsGeneration)
            return;
        m_versions = watcher->result();
        m_versionsPath = path;
        m_versionsLoading = false;
        emit versionsChanged();
        emit timelineChanged();
        if (m_compareMode == "previous" || m_snapshotIndex < 0)
            loadPreview();
    });
    watcher->setFuture(QtConcurrent::run([sources, path] {
        QVector<Signature> signatures;
        for (const QString &source : sources)
            signatures << ::signatureOf(source);
        return mapVersions(signatures, ::signatureOf(path));
    }));
}

// Preview

void Backend::setCompareMode(const QString &mode) {
    if (mode == m_compareMode || (mode != "now" && mode != "previous"))
        return;
    m_compareMode = mode;
    loadPreview();
}

void Backend::toggleCompare() {
    setCompareMode(m_compareMode == "now" ? "previous" : "now");
}

void Backend::loadPreview() {
    const int generation = ++m_previewGeneration;
    bool found = false;
    const Entry entry = focusEntry(&found);
    const Snapshot *snapshot = current();
    const auto &snapshots = m_repository->snapshots();

    QVariantMap preview{{"name", m_focusName}, {"path", focusPath()}, {"atSnapshot", snapshot != nullptr}};
    if (!found) {
        preview["kind"] = m_focusName.isEmpty() ? "empty" : "missing";
        preview["note"] = m_focusName.isEmpty() ? QString()
                          : snapshot           ? tr("“%1” isn't in this backup.").arg(m_focusName)
                                               : tr("“%1” isn't on disk now.").arg(m_focusName);
        m_preview = preview;
        m_diff.reset({});
        emit previewChanged();
        return;
    }

    preview["isDir"] = entry.isDir;
    preview["status"] = EntryModel::statusName(entry.status);
    preview["canRestore"] = snapshot != nullptr && !entry.source.isEmpty();

    // Pick the two versions to compare: older on the left, newer on the right.
    QString left, right, leftLabel, rightLabel, title, note, noteKind;
    const bool versionsKnown = m_versionsPath == focusPath() && m_versions.version.size() == snapshots.size();
    if (snapshot) {
        const QString here = entry.source;
        if (m_compareMode == "previous") {
            const int previous = versionsKnown ? previousVersionIndex(m_snapshotIndex) : -2;
            title = tr("What changed in this version");
            rightLabel = describeMoment(snapshot->time, m_clock);
            right = here;
            if (previous >= 0) {
                left = versionSource(previous);
                leftLabel = describeMoment(snapshots.at(previous).time, m_clock);
            } else {
                note = versionsKnown ? tr("This is the oldest version in your backups.") : tr("Looking for older versions…");
                noteKind = "info";
            }
        } else {
            title = tr("Changes since this backup");
            left = here;
            leftLabel = describeMoment(snapshot->time, m_clock);
            if (entry.status == EntryStatus::Deleted) {
                note = tr("Deleted since this backup. Press Space to restore it.");
                noteKind = "deleted";
            } else {
                right = entry.path;
                rightLabel = tr("Now");
                if (entry.status == EntryStatus::Same) {
                    note = entry.isDir ? QString() : tr("Same as the file on disk now.");
                    noteKind = "same";
                }
            }
        }
    } else {
        title = tr("Changes since the last backup");
        right = entry.path;
        rightLabel = tr("Now");
        if (!snapshots.isEmpty())
            left = m_repository->sourcePath(snapshots.first(), entry.path);
        if (entry.status == EntryStatus::NotBackedUp) {
            left.clear();
            note = tr("Not in the last backup yet.");
            noteKind = "new";
        } else if (entry.status == EntryStatus::Same) {
            note = entry.isDir ? QString() : tr("Backed up, no changes since the last backup.");
            noteKind = "same";
        } else if (!left.isEmpty()) {
            leftLabel = describeMoment(snapshots.first().time, m_clock);
        }
    }
    preview["title"] = title;
    preview["leftLabel"] = leftLabel;
    preview["rightLabel"] = rightLabel;
    preview["note"] = note;
    preview["noteKind"] = noteKind;
    preview["canCompare"] = snapshot != nullptr && !entry.isDir;

    // Nothing differs, or only one side exists: show that one version.
    const bool single = noteKind == "same" || noteKind == "deleted" || noteKind == "new" || left.isEmpty() ||
                        right.isEmpty();
    const QString only = noteKind == "deleted" || right.isEmpty() ? left : right;
    const bool dir = entry.isDir;
    const bool image = !dir && isImage(entry.name);

    auto *watcher = new QFutureWatcher<PreviewResult>(this);
    connect(watcher, &QFutureWatcher<PreviewResult>::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        if (generation != m_previewGeneration)
            return;
        const PreviewResult result = watcher->result();
        m_preview = result.preview;
        m_diff.reset(result.comparison.lines);
        emit previewChanged();
    });
    watcher->setFuture(QtConcurrent::run([=]() mutable {
        PreviewResult result;
        auto describeSide = [&](const QString &prefix, const QString &path) {
            const Signature signature = ::signatureOf(path);
            preview[prefix + "Exists"] = signature.exists;
            preview[prefix + "Size"] = signature.size;
            preview[prefix + "Modified"] = signature.exists ? QDateTime::fromSecsSinceEpoch(signature.modified) : QDateTime();
        };
        describeSide("left", single ? QString() : left);
        describeSide("right", single ? only : right);
        if (dir) {
            preview["kind"] = "folder";
            preview["items"] = int(QDir(only.isEmpty() ? right : only)
                                       .entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)
                                       .size());
        } else if (image) {
            preview["kind"] = "image";
            preview["leftUrl"] = single || left.isEmpty() ? QString() : imageUrl(left);
            preview["rightUrl"] = imageUrl(single ? only : right);
        } else {
            result.comparison = single ? readText(only) : compareText(left, right);
            switch (result.comparison.kind) {
            case TextComparison::Text:
                preview["kind"] = "diff";
                break;
            case TextComparison::Identical:
                if (!single) {
                    // Same text, different timestamp: say so and show it.
                    result.comparison = readText(right);
                    preview["note"] = tr("Only the date changed; the contents are the same.");
                    preview["noteKind"] = "same";
                }
                preview["kind"] = "text";
                break;
            case TextComparison::Binary:
            case TextComparison::TooLarge:
                preview["kind"] = "binary";
                preview["tooLarge"] = result.comparison.kind == TextComparison::TooLarge;
                break;
            case TextComparison::Missing:
                preview["kind"] = "missing";
                break;
            case TextComparison::Failed:
                preview["kind"] = "error";
                preview["note"] = result.comparison.error;
                break;
            }
            preview["added"] = result.comparison.added;
            preview["removed"] = result.comparison.removed;
            preview["truncated"] = result.comparison.truncated;
        }
        result.preview = preview;
        return result;
    }));
}

// Restoring

QString Backend::holdingRoot() const {
    return QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + "/rewind/holding";
}

QVariantMap Backend::restorePlan() const {
    const Snapshot *snapshot = current();
    if (!snapshot)
        return {{"ok", false}, {"reason", tr("Go back to a backup first: press [ or pick one on the timeline.")}};
    bool found = false;
    const Entry entry = focusEntry(&found);
    if (!found || entry.source.isEmpty())
        return {{"ok", false}, {"reason", tr("Pick a file or folder to restore.")}};
    const QFileInfo now(entry.path);
    const bool existsNow = now.exists() || now.isSymLink();
    const QString copy = freePath(QDir(parentOf(entry.path)).filePath(
        restoredCopyName(entry.name, snapshot->time, tr("restored"))));
    return {{"ok", true},
            {"name", entry.name},
            {"isDir", entry.isDir},
            {"when", describeMoment(snapshot->time, m_clock)},
            {"existsNow", existsNow},
            {"same", entry.status == EntryStatus::Same},
            {"original", shortPath(entry.path)},
            {"copy", shortPath(copy)},
            {"copyName", QFileInfo(copy).fileName()}};
}

void Backend::restore(bool replace) {
    const Snapshot *snapshot = current();
    bool found = false;
    const Entry entry = focusEntry(&found);
    if (m_restoring || !snapshot || !found || entry.source.isEmpty())
        return;
    const QFileInfo now(entry.path);
    const bool existsNow = now.exists() || now.isSymLink();
    const QString target = !existsNow ? entry.path
                           : replace  ? entry.path
                                      : freePath(QDir(parentOf(entry.path))
                                                     .filePath(restoredCopyName(entry.name, snapshot->time, tr("restored"))));
    const bool replacing = existsNow && replace;
    const QString source = entry.source;
    const QString holding = holdingRoot();

    m_restoring = true;
    m_progress = std::make_shared<RestoreProgress>();
    m_progressTimer.start();
    emit restoreChanged();

    struct Outcome {
        bool ok = false;
        RestoreOperation operation;
        QString error;
    };
    auto progress = m_progress;
    auto *watcher = new QFutureWatcher<Outcome>(this);
    connect(watcher, &QFutureWatcher<Outcome>::finished, this, [this, watcher, replacing, target] {
        watcher->deleteLater();
        const Outcome outcome = watcher->result();
        m_restoring = false;
        m_progressTimer.stop();
        if (outcome.ok) {
            m_done << outcome.operation;
            m_undone.clear();
            const QString name = QFileInfo(target).fileName();
            emit notify(replacing ? tr("Restored “%1”. The version it replaced is kept; Ctrl+Z puts it back.").arg(name)
                                  : tr("Restored “%1”. Ctrl+Z undoes it.").arg(name));
        } else if (outcome.error == "cancelled") {
            emit notify(tr("Restore cancelled."));
        } else {
            emit notify(tr("Couldn't restore: %1").arg(outcome.error));
        }
        emit restoreChanged();
        relist();
        m_focusDebounce.start();
    });
    watcher->setFuture(QtConcurrent::run([source, target, replacing, holding, progress] {
        Outcome outcome;
        progress->total = treeSize(source);
        outcome.ok = restoreTo(source, target, replacing, holding, progress.get(), &outcome.operation, &outcome.error);
        return outcome;
    }));
}

double Backend::restoreProgress() const {
    if (!m_progress || m_progress->total <= 0)
        return 0;
    return std::min(1.0, double(m_progress->bytes) / double(m_progress->total));
}

void Backend::cancelRestore() {
    if (m_progress)
        m_progress->cancel = true;
}

void Backend::undo() {
    if (!canUndo())
        return;
    RestoreOperation operation = m_done.takeLast();
    QString error;
    if (!undoRestore(&operation, holdingRoot(), &error)) {
        m_done << operation;
        emit notify(tr("Couldn't undo: %1").arg(error));
        return;
    }
    m_undone << operation;
    emit notify(tr("Undid restoring “%1”.").arg(QFileInfo(operation.target).fileName()));
    emit restoreChanged();
    relist();
    m_focusDebounce.start();
}

void Backend::redo() {
    if (!canRedo())
        return;
    RestoreOperation operation = m_undone.takeLast();
    QString error;
    if (!redoRestore(&operation, &error)) {
        m_undone << operation;
        emit notify(tr("Couldn't redo: %1").arg(error));
        return;
    }
    m_done << operation;
    emit notify(tr("Restored “%1” again.").arg(QFileInfo(operation.target).fileName()));
    emit restoreChanged();
    relist();
    m_focusDebounce.start();
}

void Backend::openExternally() {
    bool found = false;
    const Entry entry = focusEntry(&found);
    if (!found || entry.source.isEmpty())
        return;
    const Snapshot *snapshot = current();
    if (!snapshot || entry.isDir) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(entry.source));
        return;
    }
    // Apps get a read-only copy outside the mount, so they can't save over a
    // backup and keep working after Rewind closes.
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + "/rewind/open/" +
                           snapshot->shortId;
    QDir().mkpath(folder);
    const QString copy = folder + '/' + entry.name;
    if (QFile::exists(copy)) {
        QFile::setPermissions(copy, QFile::ReadOwner | QFile::WriteOwner);
        QFile::remove(copy);
    }
    if (!QFile::copy(entry.source, copy)) {
        emit notify(tr("Couldn't open “%1” from the backup.").arg(entry.name));
        return;
    }
    QFile::setPermissions(copy, QFile::ReadOwner | QFile::ReadGroup);
    QDesktopServices::openUrl(QUrl::fromLocalFile(copy));
}

// What a backup changed

void Backend::loadChanges() {
    const Snapshot *snapshot = current();
    const auto &snapshots = m_repository->snapshots();
    m_changesError.clear();
    if (!snapshot) {
        m_changes.clear();
        m_changesError = tr("Pick a backup on the timeline to see what it changed.");
        emit changesChanged();
        return;
    }
    if (m_snapshotIndex + 1 >= snapshots.size()) {
        m_changes.clear();
        m_changesError = tr("This is your first backup, so everything in it was new.");
        emit changesChanged();
        return;
    }
    const Snapshot older = snapshots.at(m_snapshotIndex + 1);
    const Snapshot newer = *snapshot;
    const QString key = older.id + newer.id;
    m_changesKey = key;
    if (m_changesCache.contains(key)) {
        m_changes = m_changesCache.value(key);
        m_changesLoading = false;
        emit changesChanged();
        return;
    }
    m_changes.clear();
    m_changesLoading = true;
    emit changesChanged();
    m_repository->loadChanges(older, newer, [this, key](const QVector<Change> &changes, const QString &error) {
        QVariantList rows;
        for (const Change &change : changes) {
            if (rows.size() >= 5000)
                break;
            rows << QVariantMap{{"modifier", QString(change.modifier)},
                                {"path", change.path},
                                {"name", QFileInfo(change.path).fileName()},
                                {"folder", shortPath(parentOf(change.path))}};
        }
        if (error.isEmpty())
            m_changesCache.insert(key, rows);
        if (key != m_changesKey)
            return;
        m_changes = rows;
        m_changesError = error;
        m_changesLoading = false;
        emit changesChanged();
    });
}

void Backend::retry() {
    m_repository->reopen();
}

// Text

QString Backend::formatSize(qint64 bytes) const {
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

QString Backend::shortPath(const QString &path) const {
    if (path == m_home)
        return "~";
    if (path.startsWith(m_home + '/'))
        return '~' + path.mid(m_home.size());
    return path;
}

QString Backend::describeMoment(const QDateTime &time, const QDateTime &now) {
    const QLocale locale;
    const QString clock = locale.toString(time.time(), QLocale::ShortFormat);
    const qint64 days = time.date().daysTo(now.date());
    if (days == 0)
        return tr("Today %1").arg(clock);
    if (days == 1)
        return tr("Yesterday %1").arg(clock);
    if (days > 1 && days < 7)
        return locale.toString(time.date(), "dddd") + ' ' + clock;
    if (time.date().year() == now.date().year())
        return locale.toString(time.date(), "ddd d MMM") + ' ' + clock;
    return locale.toString(time.date(), "d MMM yyyy") + ' ' + clock;
}

QString Backend::describeAgo(const QDateTime &time, const QDateTime &now) {
    const qint64 seconds = time.secsTo(now);
    if (seconds < 60)
        return tr("Just now");
    if (seconds < 3600)
        return tr("%n minutes ago", "", int(seconds / 60));
    if (seconds < 86400)
        return tr("%n hours ago", "", int(seconds / 3600));
    if (seconds < 86400 * 31)
        return tr("%n days ago", "", int(seconds / 86400));
    if (seconds < 86400 * 365)
        return tr("%n months ago", "", int(seconds / (86400 * 30)));
    return tr("%n years ago", "", int(seconds / (86400 * 365)));
}

QString Backend::describeFuture(const QDateTime &time, const QDateTime &now) {
    const qint64 seconds = now.secsTo(time);
    if (seconds < 60)
        return tr("any moment");
    if (seconds < 3600)
        return tr("in %n minutes", "", int((seconds + 59) / 60));
    if (seconds < 86400)
        return tr("in %n hours", "", int(seconds / 3600));
    return tr("in %n days", "", int(seconds / 86400));
}

QString Backend::dayHeading(const QDate &day, const QDate &today) {
    const QLocale locale;
    const qint64 days = day.daysTo(today);
    if (days == 0)
        return tr("Today");
    if (days == 1)
        return tr("Yesterday");
    if (days > 1 && days < 7)
        return locale.toString(day, "dddd");
    if (day.year() == today.year())
        return locale.toString(day, "d MMMM");
    return locale.toString(day, "d MMMM yyyy");
}
