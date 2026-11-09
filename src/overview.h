#pragma once

#include <QAbstractListModel>
#include <QDate>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariant>

#include "backupservice.h"
#include "repository.h"

// Every backup as a row: when it ran, how long it took, what it added, and
// whether the cleanup rules keep it. Sorted newest first or biggest first.
class BackupModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        IndexRole = Qt::UserRole + 1, IdRole, ShortIdRole, TimeRole, ClockRole, DayRole, DurationRole,
        FilesNewRole, FilesChangedRole, AddedRole, KeptRole, RemovingRole, LatestRole, HasSummaryRole
    };

    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_order.size(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_order.size(); }
    // Snapshot index of a row, and the row showing a snapshot.
    int snapshotAt(int row) const { return row >= 0 && row < m_order.size() ? m_order.at(row) : -1; }
    int rowOf(int snapshot) const { return m_order.indexOf(snapshot); }

    void reset(const QVector<Snapshot> &snapshots, bool bySize, const QDate &today);
    void setRetention(const Retention &retention, bool known);
    void setToday(const QDate &today);

signals:
    void countChanged();

private:
    QVector<Snapshot> m_snapshots;
    QVector<int> m_order;
    bool m_bySize = false;
    QDate m_today;
    Retention m_retention;
    bool m_retentionKnown = false;
};

// The Backups view: how the backups are doing as a whole, every backup, and
// the one picked. It only reads; restic is run with --no-lock and forget only
// as a dry run.
class Overview : public QObject {
    Q_OBJECT
    Q_PROPERTY(BackupModel *backups READ backups CONSTANT)
    Q_PROPERTY(QString sort READ sort WRITE setSort NOTIFY sortChanged)
    Q_PROPERTY(int selected READ selected WRITE select NOTIFY selectedChanged)
    Q_PROPERTY(int selectedRow READ selectedRow NOTIFY selectedChanged)
    Q_PROPERTY(QVariantMap detail READ detail NOTIFY detailChanged)
    Q_PROPERTY(QVariantList chart READ chart NOTIFY chartChanged)
    Q_PROPERTY(double chartScale READ chartScale NOTIFY chartChanged)
    Q_PROPERTY(QVariantMap summary READ summary NOTIFY summaryChanged)
    Q_PROPERTY(QString health READ health NOTIFY summaryChanged)
    Q_PROPERTY(QVariantList largest READ largest NOTIFY largestChanged)
    Q_PROPERTY(bool largestLoading READ largestLoading NOTIFY largestChanged)
    Q_PROPERTY(QString largestNote READ largestNote NOTIFY largestChanged)
    Q_PROPERTY(int typicalDuration READ typicalDuration NOTIFY summaryChanged)

public:
    Overview(Repository *repository, BackupService *service, QObject *parent = nullptr);

    BackupModel *backups() { return &m_model; }
    QString sort() const { return m_bySize ? "size" : "time"; }
    void setSort(const QString &sort);
    int selected() const { return m_selected; }
    void select(int snapshot);
    int selectedRow() const { return m_model.rowOf(m_selected); }
    QVariantMap detail() const;
    QVariantList chart() const { return m_chart; }
    double chartScale() const { return m_chartScale; }
    QVariantMap summary() const;
    QString health() const;
    QVariantList largest() const { return m_largest; }
    bool largestLoading() const { return m_largestLoading; }
    QString largestNote() const { return m_largestNote; }
    int typicalDuration() const;

    Q_INVOKABLE void move(int delta);
    Q_INVOKABLE void selectRow(int row);
    Q_INVOKABLE void toggleSort() { setSort(m_bySize ? "time" : "size"); }
    Q_INVOKABLE void copyId();

    // "16 s", "1 min 17 s", "2 h 5 min"
    Q_INVOKABLE static QString formatDuration(qint64 seconds);
    // Rounded for "about every …": "45 s", "20 min", "1 h", "1.5 h", "2 days"
    Q_INVOKABLE static QString formatApproximate(qint64 seconds);
    // "24 hourly, 7 daily and 12 monthly"
    static QString describeRules(const QStringList &arguments);
    // Where the y axis of the "added per backup" chart ends: a single huge
    // backup (the first one, usually) would flatten all the others, so it is
    // cut off above three times the 90th percentile and labelled instead.
    static double chartScaleFor(const QVector<double> &values);
    // The median gap between backups, in seconds; 0 below three backups.
    static qint64 typicalGap(const QVector<Snapshot> &snapshots);

signals:
    void sortChanged();
    void selectedChanged();
    void detailChanged();
    void chartChanged();
    void summaryChanged();
    void largestChanged();
    void copied(const QString &message);

private:
    void onSnapshotsChanged();
    void rebuild();
    void loadStats();
    void loadRetention();
    void loadLargest();
    const Snapshot *selectedSnapshot() const;

    Repository *m_repository;
    BackupService *m_service;
    BackupModel m_model;
    bool m_bySize = false;
    int m_selected = -1;
    QString m_selectedId;
    QVariantList m_chart;
    double m_chartScale = 0;
    QDate m_today;
    QTimer m_minute;

    RepositoryStats m_stats;
    bool m_statsLoading = false;
    QString m_statsError;
    int m_statsGeneration = 0;

    Retention m_retention;
    bool m_retentionLoading = false;
    bool m_retentionKnown = false;
    QString m_retentionError;
    int m_retentionGeneration = 0;

    QVariantList m_largest;
    bool m_largestLoading = false;
    QString m_largestNote;
    QString m_largestKey;
    QHash<QString, QVariantMap> m_largestCache;
    QTimer m_largestDebounce;
};
