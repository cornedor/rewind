#pragma once

#include <QDateTime>
#include <QFutureWatcher>
#include <QObject>
#include <QTimer>
#include <QVariant>
#include <memory>

#include "backupservice.h"
#include "files.h"
#include "models.h"
#include "repository.h"

// Everything the window shows: the folder being browsed, the moment in time
// it is browsed at (a backup, or now), the selected file's versions, and the
// comparison in the side panel. QML only lays it out.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString folder READ folder NOTIFY folderChanged)
    Q_PROPERTY(QVariantList crumbs READ crumbs NOTIFY folderChanged)
    Q_PROPERTY(EntryModel *entries READ entries CONSTANT)
    Q_PROPERTY(QString listingMessage READ listingMessage NOTIFY listingChanged)
    Q_PROPERTY(bool listing READ isListing NOTIFY listingChanged)
    Q_PROPERTY(int selectedRow READ selectedRow WRITE setSelectedRow NOTIFY selectionChanged)
    Q_PROPERTY(QString focusName READ focusName NOTIFY selectionChanged)

    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY filterChanged)
    Q_PROPERTY(bool onlyChanges READ onlyChanges WRITE setOnlyChanges NOTIFY filterChanged)

    Q_PROPERTY(QString repository READ repository CONSTANT)
    Q_PROPERTY(QString repositoryState READ repositoryState NOTIFY repositoryChanged)
    Q_PROPERTY(QString repositoryError READ repositoryError NOTIFY repositoryChanged)
    Q_PROPERTY(QVariantList timeline READ timeline NOTIFY timelineChanged)
    Q_PROPERTY(int snapshotIndex READ snapshotIndex NOTIFY momentChanged)
    Q_PROPERTY(QVariantMap moment READ moment NOTIFY momentChanged)
    Q_PROPERTY(bool folderInBackups READ folderInBackups NOTIFY timelineChanged)

    Q_PROPERTY(int versionCount READ versionCount NOTIFY versionsChanged)
    Q_PROPERTY(int currentVersion READ currentVersion NOTIFY versionsChanged)
    Q_PROPERTY(int nowVersion READ nowVersion NOTIFY versionsChanged)
    Q_PROPERTY(bool versionsLoading READ versionsLoading NOTIFY versionsChanged)

    Q_PROPERTY(QString compareMode READ compareMode WRITE setCompareMode NOTIFY previewChanged)
    Q_PROPERTY(QVariantMap preview READ preview NOTIFY previewChanged)
    Q_PROPERTY(DiffModel *diff READ diff CONSTANT)

    Q_PROPERTY(QVariantList changes READ changes NOTIFY changesChanged)
    Q_PROPERTY(bool changesLoading READ changesLoading NOTIFY changesChanged)
    Q_PROPERTY(QString changesError READ changesError NOTIFY changesChanged)

    Q_PROPERTY(bool restoring READ restoring NOTIFY restoreChanged)
    Q_PROPERTY(double restoreProgress READ restoreProgress NOTIFY restoreChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY restoreChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY restoreChanged)

    Q_PROPERTY(BackupService *backups READ backups CONSTANT)
    Q_PROPERTY(QDateTime clock READ clock NOTIFY clockChanged)

public:
    Backend(Repository *repository, BackupService *backups, const QString &startPath, QObject *parent = nullptr);
    ~Backend() override;

    QString folder() const { return m_folder; }
    QVariantList crumbs() const;
    EntryModel *entries() { return &m_entries; }
    QString listingMessage() const { return m_listingMessage; }
    bool isListing() const { return m_listing; }
    int selectedRow() const { return m_selectedRow; }
    void setSelectedRow(int row);
    QString focusName() const { return m_focusName; }

    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    bool showHidden() const { return m_showHidden; }
    void setShowHidden(bool show);
    bool onlyChanges() const { return m_onlyChanges; }
    void setOnlyChanges(bool only);

    QString repository() const;
    QString repositoryState() const;
    QString repositoryError() const { return m_repository->error(); }
    QVariantList timeline() const;
    int snapshotIndex() const { return m_snapshotIndex; }
    QVariantMap moment() const;
    bool folderInBackups() const;

    int versionCount() const { return m_versions.count; }
    int currentVersion() const;
    int nowVersion() const { return m_versionsPath == focusPath() ? m_versions.nowMatches : 0; }
    bool versionsLoading() const { return m_versionsLoading; }

    QString compareMode() const { return m_compareMode; }
    void setCompareMode(const QString &mode);
    QVariantMap preview() const { return m_preview; }
    DiffModel *diff() { return &m_diff; }

    QVariantList changes() const { return m_changes; }
    bool changesLoading() const { return m_changesLoading; }
    QString changesError() const { return m_changesError; }

    bool restoring() const { return m_restoring; }
    double restoreProgress() const;
    bool canUndo() const { return !m_done.isEmpty() && !m_restoring; }
    bool canRedo() const { return !m_undone.isEmpty() && !m_restoring; }

    BackupService *backups() { return m_backups; }
    QDateTime clock() const { return m_clock; }

    Q_INVOKABLE void openRow(int row);
    Q_INVOKABLE void goUp();
    Q_INVOKABLE void goTo(const QString &path, const QString &select = {});
    Q_INVOKABLE void moveSelection(int delta);
    Q_INVOKABLE void showSnapshot(int index);
    Q_INVOKABLE void older();
    Q_INVOKABLE void newer();
    Q_INVOKABLE void previousVersion();
    Q_INVOKABLE void nextVersion();
    Q_INVOKABLE void toggleCompare();

    Q_INVOKABLE QVariantMap restorePlan() const;
    Q_INVOKABLE void restore(bool replace);
    Q_INVOKABLE void cancelRestore();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void openExternally();
    Q_INVOKABLE void loadChanges();
    Q_INVOKABLE void retry();

    Q_INVOKABLE QString formatSize(qint64 bytes) const;
    Q_INVOKABLE QString formatMoment(const QDateTime &time) const { return describeMoment(time, m_clock); }
    Q_INVOKABLE QString formatAgo(const QDateTime &time) const { return describeAgo(time, m_clock); }
    Q_INVOKABLE QString formatFuture(const QDateTime &time) const { return describeFuture(time, m_clock); }
    Q_INVOKABLE QString shortPath(const QString &path) const;
    Q_INVOKABLE bool isRunning() const { return m_restoring; }

    // "Today 14:05", "Yesterday 09:00", "Mon 3 Nov 14:05", "3 Nov 2025 14:05"
    static QString describeMoment(const QDateTime &time, const QDateTime &now);
    // "Just now", "5 minutes ago", "3 hours ago", "2 days ago"
    static QString describeAgo(const QDateTime &time, const QDateTime &now);
    // "in 5 minutes", "in 2 hours"
    static QString describeFuture(const QDateTime &time, const QDateTime &now);
    static QString dayHeading(const QDate &day, const QDate &today);

signals:
    void folderChanged();
    void listingChanged();
    void selectionChanged();
    void filterChanged();
    void repositoryChanged();
    void timelineChanged();
    void momentChanged();
    void versionsChanged();
    void previewChanged();
    void changesChanged();
    void restoreChanged();
    void clockChanged();
    void notify(const QString &message);

private:
    struct ListingResult {
        QVector<Entry> entries;
        QString message;
    };
    struct PreviewResult {
        TextComparison comparison;
        QVariantMap preview;
    };

    void relist();
    void applyListing(const ListingResult &result);
    void applyFilter();
    void focusChanged();
    void loadVersions();
    void loadPreview();
    void onRepositoryChanged();
    const Snapshot *current() const;
    QString versionSource(int snapshot) const;
    int previousVersionIndex(int from) const;
    int nextVersionIndex(int from) const;
    QString focusPath() const;
    Entry focusEntry(bool *found) const;
    QString holdingRoot() const;

    Repository *m_repository;
    BackupService *m_backups;
    QString m_folder;
    QString m_home;
    EntryModel m_entries;
    QVector<Entry> m_allEntries;
    QString m_listingMessage;
    bool m_listing = false;
    int m_listingGeneration = 0;
    int m_selectedRow = -1;
    QString m_focusName;
    QString m_filter;
    bool m_showHidden = false;
    bool m_onlyChanges = false;

    int m_snapshotIndex = -1;
    QString m_shownId;
    VersionMap m_versions;
    QString m_versionsPath;
    bool m_versionsLoading = false;
    int m_versionsGeneration = 0;
    QTimer m_focusDebounce;

    QString m_compareMode = "now";
    QVariantMap m_preview;
    DiffModel m_diff;
    int m_previewGeneration = 0;

    QVariantList m_changes;
    bool m_changesLoading = false;
    QString m_changesError;
    QString m_changesKey;
    QHash<QString, QVariantList> m_changesCache;

    bool m_restoring = false;
    std::shared_ptr<RestoreProgress> m_progress;
    QTimer m_progressTimer;
    QVector<RestoreOperation> m_done;
    QVector<RestoreOperation> m_undone;

    QDateTime m_clock;
    QTimer m_clockTimer;
};
