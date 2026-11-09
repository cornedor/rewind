#include "overview.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QLocale>
#include <QtConcurrent>
#include <algorithm>
#include <numeric>

#include "backend.h"

namespace {
qint64 durationOf(const Snapshot &snapshot) {
    if (!snapshot.backupStart.isValid() || !snapshot.backupEnd.isValid())
        return -1;
    return std::max<qint64>(0, (snapshot.backupStart.msecsTo(snapshot.backupEnd) + 500) / 1000);
}

QString sizeText(qint64 bytes) {
    return bytes < 0 ? QString() : QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

// The folder a path sits in, with the home folder as ~.
QString folderOf(const QString &path) {
    const QString home = QDir::homePath();
    QString folder = QFileInfo(path).absolutePath();
    if (folder == home)
        return "~";
    if (folder.startsWith(home + '/'))
        return '~' + folder.mid(home.size());
    return folder;
}

const int kLargestShown = 8;
const int kLargestChecked = 4000;
} // namespace

// The rows

QVariant BackupModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_order.size())
        return {};
    const int i = m_order.at(index.row());
    const Snapshot &s = m_snapshots.at(i);
    switch (role) {
    case IndexRole: return i;
    case IdRole: return s.id;
    case ShortIdRole: return s.shortId;
    case TimeRole: return s.time;
    case ClockRole: return QLocale().toString(s.time.time(), QLocale::ShortFormat);
    case DayRole: return Backend::dayHeading(s.time.date(), m_today);
    case DurationRole: return durationOf(s);
    case FilesNewRole: return s.filesNew;
    case FilesChangedRole: return s.filesChanged;
    case AddedRole: return s.dataAddedPacked;
    case KeptRole: return m_retention.keptBecause.value(s.id);
    case RemovingRole: return m_retentionKnown && m_retention.removed.contains(s.id);
    case LatestRole: return i == 0;
    case HasSummaryRole: return s.hasSummary();
    }
    return {};
}

QHash<int, QByteArray> BackupModel::roleNames() const {
    return {{IndexRole, "snapshot"},       {IdRole, "id"},
            {ShortIdRole, "shortId"},      {TimeRole, "time"},
            {ClockRole, "clock"},          {DayRole, "day"},
            {DurationRole, "duration"},
            {FilesNewRole, "filesNew"},    {FilesChangedRole, "filesChanged"},
            {AddedRole, "added"},
            {KeptRole, "kept"},            {RemovingRole, "removing"},
            {LatestRole, "latest"},        {HasSummaryRole, "hasSummary"}};
}

void BackupModel::reset(const QVector<Snapshot> &snapshots, bool bySize, const QDate &today) {
    beginResetModel();
    m_snapshots = snapshots;
    m_bySize = bySize;
    m_today = today;
    m_order.resize(snapshots.size());
    std::iota(m_order.begin(), m_order.end(), 0);
    if (bySize)
        std::stable_sort(m_order.begin(), m_order.end(), [&](int a, int b) {
            return snapshots.at(a).dataAddedPacked > snapshots.at(b).dataAddedPacked;
        });
    endResetModel();
    emit countChanged();
}

void BackupModel::setRetention(const Retention &retention, bool known) {
    m_retention = retention;
    m_retentionKnown = known;
    if (!m_order.isEmpty())
        emit dataChanged(index(0), index(m_order.size() - 1), {KeptRole, RemovingRole});
}

void BackupModel::setToday(const QDate &today) {
    if (today == m_today)
        return;
    m_today = today;
    if (!m_order.isEmpty())
        emit dataChanged(index(0), index(m_order.size() - 1), {DayRole});
}

// The view

Overview::Overview(Repository *repository, BackupService *service, QObject *parent)
    : QObject(parent), m_repository(repository), m_service(service), m_today(QDate::currentDate()) {
    connect(m_repository, &Repository::snapshotsChanged, this, &Overview::onSnapshotsChanged);
    // The biggest files are read through the mount, which opens after the list.
    connect(m_repository, &Repository::mountChanged, this, [this] {
        if (m_repository->mountState() == Repository::State::Ready && m_largest.isEmpty())
            m_largestDebounce.start();
    });
    connect(m_service, &BackupService::changed, this, &Overview::summaryChanged);
    connect(m_service, &BackupService::retentionChanged, this, &Overview::loadRetention);

    // "Late" and day headings move with the clock.
    m_minute.setInterval(60000);
    connect(&m_minute, &QTimer::timeout, this, [this] {
        m_today = QDate::currentDate();
        m_model.setToday(m_today);
        emit summaryChanged();
    });
    m_minute.start();

    // Stepping through the list quickly shouldn't start a restic diff per row.
    m_largestDebounce.setSingleShot(true);
    m_largestDebounce.setInterval(250);
    connect(&m_largestDebounce, &QTimer::timeout, this, &Overview::loadLargest);
}

void Overview::onSnapshotsChanged() {
    if (m_repository->snapshotsState() != Repository::State::Ready) {
        emit summaryChanged();
        return;
    }
    // The tree roots resolving also lands here; only a new list needs reloading.
    const auto &snapshots = m_repository->snapshots();
    const bool sameList = m_model.count() == snapshots.size() &&
                          (snapshots.isEmpty() || m_model.data(m_model.index(m_model.rowOf(0)), BackupModel::IdRole) ==
                                                      snapshots.first().id);
    if (sameList && m_model.count() > 0)
        return;
    rebuild();
    loadStats();
    loadRetention();
}

void Overview::rebuild() {
    const auto &snapshots = m_repository->snapshots();
    m_model.reset(snapshots, m_bySize, m_today);

    // Keep the same backup picked across a reload, by id; else the newest.
    int selected = -1;
    for (int i = 0; i < snapshots.size(); ++i)
        if (snapshots.at(i).id == m_selectedId)
            selected = i;
    if (selected < 0 && !snapshots.isEmpty())
        selected = 0;
    m_selected = selected;
    m_selectedId = selected >= 0 ? snapshots.at(selected).id : QString();

    // Oldest on the left, like any chart over time.
    m_chart.clear();
    QVector<double> values;
    for (int i = snapshots.size() - 1; i >= 0; --i) {
        const Snapshot &s = snapshots.at(i);
        m_chart << QVariantMap{{"snapshot", i}, {"value", double(std::max<qint64>(0, s.dataAddedPacked))},
                               {"known", s.dataAddedPacked >= 0}, {"time", s.time}};
        if (s.dataAddedPacked >= 0)
            values << double(s.dataAddedPacked);
    }
    m_chartScale = chartScaleFor(values);

    emit chartChanged();
    emit selectedChanged();
    emit detailChanged();
    emit summaryChanged();
    m_largestDebounce.start();
}

void Overview::setSort(const QString &sort) {
    const bool bySize = sort == "size";
    if (bySize == m_bySize)
        return;
    m_bySize = bySize;
    m_model.reset(m_repository->snapshots(), m_bySize, m_today);
    m_model.setRetention(m_retention, m_retentionKnown);
    emit sortChanged();
    emit selectedChanged();
}

void Overview::select(int snapshot) {
    const auto &snapshots = m_repository->snapshots();
    if (snapshot < 0 || snapshot >= snapshots.size() || snapshot == m_selected)
        return;
    m_selected = snapshot;
    m_selectedId = snapshots.at(snapshot).id;
    emit selectedChanged();
    emit detailChanged();
    m_largest.clear();
    m_largestNote.clear();
    m_largestLoading = true;
    emit largestChanged();
    m_largestDebounce.start();
}

void Overview::selectRow(int row) {
    select(m_model.snapshotAt(row));
}

void Overview::move(int delta) {
    if (m_model.count() == 0)
        return;
    const int row = std::clamp(selectedRow() + delta, 0, m_model.count() - 1);
    selectRow(row);
}

void Overview::copyId() {
    const Snapshot *snapshot = selectedSnapshot();
    if (!snapshot)
        return;
    QGuiApplication::clipboard()->setText(snapshot->id);
    emit copied(tr("Copied the backup's id, %1.").arg(snapshot->shortId));
}

const Snapshot *Overview::selectedSnapshot() const {
    const auto &snapshots = m_repository->snapshots();
    return m_selected >= 0 && m_selected < snapshots.size() ? &snapshots.at(m_selected) : nullptr;
}

QVariantMap Overview::detail() const {
    const Snapshot *s = selectedSnapshot();
    if (!s)
        return {{"valid", false}};
    const auto &snapshots = m_repository->snapshots();
    const Snapshot *previous = m_selected + 1 < snapshots.size() ? &snapshots.at(m_selected + 1) : nullptr;
    QVariantMap map{
        {"valid", true},
        {"id", s->id},
        {"shortId", s->shortId},
        {"time", s->time},
        {"start", s->backupStart},
        {"end", s->backupEnd},
        {"duration", durationOf(*s)},
        {"hostname", s->hostname},
        {"username", s->username},
        {"program", s->programVersion},
        {"paths", s->paths},
        {"tags", s->tags},
        {"hasSummary", s->hasSummary()},
        {"filesNew", s->filesNew},
        {"filesChanged", s->filesChanged},
        {"filesUnmodified", s->filesUnmodified},
        {"added", s->dataAddedPacked},
        {"addedRaw", s->dataAdded},
        {"totalBytes", s->totalBytes},
        {"totalFiles", s->totalFiles},
        {"isFirst", previous == nullptr},
        {"retentionKnown", m_retentionKnown},
        {"kept", m_retention.keptBecause.value(s->id)},
        {"removing", m_retentionKnown && m_retention.removed.contains(s->id)},
    };
    // How the backed-up size moved since the backup before.
    if (previous && previous->hasSummary() && s->hasSummary())
        map.insert("growth", s->totalBytes - previous->totalBytes);
    const QVariantMap diff = m_largestCache.value(previous ? previous->id + s->id : QString());
    if (diff.contains("removed"))
        map.insert("filesRemoved", diff.value("removed"));
    return map;
}

QVariantMap Overview::summary() const {
    const auto &snapshots = m_repository->snapshots();
    QVariantMap map{
        {"count", int(snapshots.size())},
        {"statsLoading", m_statsLoading},
        {"statsError", m_statsError},
        {"stored", m_stats.storedBytes},
        {"uncompressed", m_stats.uncompressedBytes},
        {"retentionLoading", m_retentionLoading},
        {"retentionError", m_retentionError},
        {"rules", describeRules(m_service->retentionArguments())},
        {"removing", int(m_retention.removed.size())},
    };
    if (m_stats.storedBytes > 0 && m_stats.uncompressedBytes > 0)
        map.insert("saved", 100.0 * (1.0 - double(m_stats.storedBytes) / double(m_stats.uncompressedBytes)));
    if (!snapshots.isEmpty()) {
        const Snapshot &latest = snapshots.first();
        map.insert("latest", latest.time);
        map.insert("oldest", snapshots.last().time);
        map.insert("totalBytes", latest.totalBytes);
        map.insert("totalFiles", latest.totalFiles);
        map.insert("gap", typicalGap(snapshots));
    }
    return map;
}

QString Overview::health() const {
    const auto state = m_repository->snapshotsState();
    if (state == Repository::State::Failed)
        return "unreachable";
    if (m_service->running())
        return "running";
    // A reload after each backup keeps the list it had, so only the first
    // read shows as loading.
    const auto &snapshots = m_repository->snapshots();
    if (state != Repository::State::Ready && snapshots.isEmpty())
        return "loading";
    if (snapshots.isEmpty())
        return "none";
    if (m_service->available() && m_service->lastFailed())
        return "failed";
    if (m_service->available() && m_service->lastSkipped())
        return "skipped";
    // Later than usual: more than three typical gaps, and at least two hours.
    const qint64 gap = typicalGap(snapshots);
    const qint64 age = snapshots.first().time.secsTo(QDateTime::currentDateTime());
    if (gap > 0 && age > std::max<qint64>(3 * gap, 2 * 3600))
        return "late";
    return "ok";
}

int Overview::typicalDuration() const {
    QVector<qint64> durations;
    for (const Snapshot &s : m_repository->snapshots()) {
        if (durations.size() >= 20)
            break;
        // The first backup reads everything and says nothing about the next.
        if (!s.parent.isEmpty() && durationOf(s) >= 0)
            durations << durationOf(s);
    }
    if (durations.isEmpty())
        return -1;
    std::sort(durations.begin(), durations.end());
    return int(durations.at(durations.size() / 2));
}

// Loading

void Overview::loadStats() {
    const int generation = ++m_statsGeneration;
    m_statsLoading = true;
    m_statsError.clear();
    emit summaryChanged();
    m_repository->loadStats([this, generation](const RepositoryStats &stats, const QString &error) {
        if (generation != m_statsGeneration)
            return;
        m_statsLoading = false;
        m_statsError = error;
        if (error.isEmpty())
            m_stats = stats;
        emit summaryChanged();
    });
}

void Overview::loadRetention() {
    const QStringList rules = m_service->retentionArguments();
    const int generation = ++m_retentionGeneration;
    m_retentionError.clear();
    // Without rules, restic forget would keep everything; there is nothing to
    // know, rather than "everything stays".
    if (rules.isEmpty() || m_repository->snapshotsState() != Repository::State::Ready) {
        m_retention = {};
        m_retentionKnown = false;
        m_retentionLoading = false;
        m_model.setRetention(m_retention, false);
        emit summaryChanged();
        emit detailChanged();
        return;
    }
    m_retentionLoading = true;
    emit summaryChanged();
    m_repository->loadRetention(rules, [this, generation](const Retention &retention, const QString &error) {
        if (generation != m_retentionGeneration)
            return;
        m_retentionLoading = false;
        m_retentionError = error;
        m_retention = error.isEmpty() ? retention : Retention();
        m_retentionKnown = error.isEmpty();
        m_model.setRetention(m_retention, m_retentionKnown);
        emit summaryChanged();
        emit detailChanged();
    });
}

// The biggest files a backup added or changed: what restic diff lists,
// measured through the mount. It answers "why was this backup so big?".
void Overview::loadLargest() {
    const Snapshot *snapshot = selectedSnapshot();
    const auto &snapshots = m_repository->snapshots();
    auto finish = [this](const QVariantList &files, const QString &note) {
        m_largest = files;
        m_largestNote = note;
        m_largestLoading = false;
        emit largestChanged();
    };
    if (!snapshot) {
        finish({}, {});
        return;
    }
    if (m_selected + 1 >= snapshots.size()) {
        finish({}, tr("Your first backup: everything in it was new."));
        return;
    }
    if (m_repository->mountState() != Repository::State::Ready) {
        m_largestLoading = true;
        m_largestNote = tr("Opening your backups…");
        emit largestChanged();
        return;
    }
    const Snapshot older = snapshots.at(m_selected + 1);
    const Snapshot newer = *snapshot;
    const QString key = older.id + newer.id;
    m_largestKey = key;
    if (m_largestCache.contains(key)) {
        const QVariantMap cached = m_largestCache.value(key);
        finish(cached.value("files").toList(), cached.value("note").toString());
        return;
    }
    m_largestLoading = true;
    m_largestNote.clear();
    emit largestChanged();

    m_repository->loadChanges(older, newer, [this, key, newer](const QVector<Change> &changes, const QString &error) {
        if (!error.isEmpty()) {
            if (key == m_largestKey) {
                m_largest.clear();
                m_largestNote = error;
                m_largestLoading = false;
                emit largestChanged();
            }
            return;
        }
        struct Candidate { QChar modifier; QString path; QString source; };
        QVector<Candidate> candidates;
        int removed = 0;
        for (const Change &change : changes) {
            if (change.modifier == '-')
                ++removed;
            else if ((change.modifier == '+' || change.modifier == 'M') && candidates.size() < kLargestChecked)
                candidates << Candidate{change.modifier, change.path, m_repository->sourcePath(newer, change.path)};
        }
        const bool capped = candidates.size() >= kLargestChecked;

        using Measured = QVector<QPair<qint64, int>>;
        auto *watcher = new QFutureWatcher<Measured>(this);
        connect(watcher, &QFutureWatcher<Measured>::finished, this, [this, watcher, candidates, key, removed, capped] {
            watcher->deleteLater();
            Measured measured = watcher->result();
            std::sort(measured.begin(), measured.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
            QVariantList files;
            for (const auto &[size, i] : measured) {
                if (files.size() >= kLargestShown)
                    break;
                const Candidate &c = candidates.at(i);
                files << QVariantMap{{"path", c.path},
                                     {"name", QFileInfo(c.path).fileName()},
                                     {"folder", folderOf(c.path)},
                                     {"size", size},
                                     {"sizeText", sizeText(size)},
                                     {"isNew", c.modifier == '+'}};
            }
            const QString note = files.isEmpty() ? tr("No files were added or changed, only folders or settings.")
                                 : capped ? tr("Measured the first %1 changed files.").arg(QLocale().toString(kLargestChecked))
                                          : QString();
            m_largestCache.insert(key, {{"files", files}, {"note", note}, {"removed", removed}});
            emit detailChanged();
            if (key != m_largestKey)
                return;
            m_largest = files;
            m_largestNote = note;
            m_largestLoading = false;
            emit largestChanged();
        });
        watcher->setFuture(QtConcurrent::run([candidates] {
            Measured measured;
            for (int i = 0; i < candidates.size(); ++i) {
                const QFileInfo info(candidates.at(i).source);
                if (info.isFile() && !info.isSymLink())
                    measured << qMakePair(info.size(), i);
            }
            return measured;
        }));
    });
}

// Words and numbers

QString Overview::formatDuration(qint64 seconds) {
    if (seconds < 0)
        return {};
    if (seconds < 60)
        return tr("%1 s").arg(seconds);
    if (seconds < 3600)
        return seconds % 60 == 0 ? tr("%1 min").arg(seconds / 60)
                                 : tr("%1 min %2 s").arg(seconds / 60).arg(seconds % 60);
    return tr("%1 h %2 min").arg(seconds / 3600).arg((seconds % 3600) / 60);
}

QString Overview::formatApproximate(qint64 seconds) {
    if (seconds < 0)
        return {};
    if (seconds < 60)
        return tr("%1 s").arg(seconds);
    if (seconds < 3600)
        return tr("%1 min").arg((seconds + 30) / 60);
    if (seconds < 86400) {
        const qint64 halves = (seconds + 900) / 1800;
        return halves % 2 == 0 ? tr("%1 h").arg(halves / 2) : tr("%1 h").arg(QLocale().toString(halves / 2.0, 'f', 1));
    }
    return tr("%n days", "", int((seconds + 43200) / 86400));
}

QString Overview::describeRules(const QStringList &arguments) {
    // In the order restic applies them, most recent first.
    static const QList<QPair<QString, const char *>> counts{
        {"--keep-last", QT_TR_NOOP("the last %1")},       {"--keep-hourly", QT_TR_NOOP("%1 hourly")},
        {"--keep-daily", QT_TR_NOOP("%1 daily")},         {"--keep-weekly", QT_TR_NOOP("%1 weekly")},
        {"--keep-monthly", QT_TR_NOOP("%1 monthly")},     {"--keep-yearly", QT_TR_NOOP("%1 yearly")},
        {"--keep-within", QT_TR_NOOP("everything from the last %1")},
        {"--keep-within-hourly", QT_TR_NOOP("hourly for %1")},
        {"--keep-within-daily", QT_TR_NOOP("daily for %1")},
        {"--keep-within-weekly", QT_TR_NOOP("weekly for %1")},
        {"--keep-within-monthly", QT_TR_NOOP("monthly for %1")},
        {"--keep-within-yearly", QT_TR_NOOP("yearly for %1")},
        {"--keep-tag", QT_TR_NOOP("those tagged %1")}};
    QHash<QString, QString> values;
    for (int i = 0; i < arguments.size(); ++i) {
        const QString &argument = arguments.at(i);
        if (argument.contains('='))
            values.insert(argument.section('=', 0, 0), argument.section('=', 1));
        else if (i + 1 < arguments.size())
            values.insert(argument, arguments.at(++i));
    }
    QStringList parts;
    for (const auto &[flag, text] : counts)
        if (values.contains(flag))
            parts << tr(text).arg(values.value(flag) == "-1" ? tr("all") : values.value(flag));
    if (parts.isEmpty())
        return {};
    if (parts.size() == 1)
        return parts.first();
    const QString last = parts.takeLast();
    return tr("%1 and %2").arg(parts.join(", "), last);
}

double Overview::chartScaleFor(const QVector<double> &values) {
    if (values.isEmpty())
        return 0;
    QVector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    const double max = sorted.last();
    const double p90 = sorted.at(int((sorted.size() - 1) * 0.9));
    if (sorted.size() < 4 || p90 <= 0)
        return max;
    return std::min(max, p90 * 3);
}

qint64 Overview::typicalGap(const QVector<Snapshot> &snapshots) {
    if (snapshots.size() < 3)
        return 0;
    QVector<qint64> gaps;
    for (int i = 0; i + 1 < snapshots.size() && gaps.size() < 30; ++i)
        gaps << snapshots.at(i + 1).time.secsTo(snapshots.at(i).time);
    std::sort(gaps.begin(), gaps.end());
    return gaps.at(gaps.size() / 2);
}
