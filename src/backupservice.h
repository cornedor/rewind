#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QTimer>

// The systemd user service that makes the backups. Rewind never runs restic
// backup itself: it starts the service the user already set up, so a manual
// backup uses the same paths, excludes and hooks as the hourly one.
class BackupService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(bool lastFailed READ lastFailed NOTIFY changed)
    Q_PROPERTY(QDateTime lastFinished READ lastFinished NOTIFY changed)
    Q_PROPERTY(QDateTime nextRun READ nextRun NOTIFY changed)
    Q_PROPERTY(QString unit READ unit CONSTANT)
    Q_PROPERTY(QDateTime started READ started NOTIFY changed)
    Q_PROPERTY(bool lastSkipped READ lastSkipped NOTIFY changed)
    Q_PROPERTY(bool cleanupAvailable READ cleanupAvailable NOTIFY changed)
    Q_PROPERTY(QDateTime cleanupNextRun READ cleanupNextRun NOTIFY changed)
    Q_PROPERTY(QDateTime cleanupLastRun READ cleanupLastRun NOTIFY changed)
    Q_PROPERTY(QString cleanupUnit READ cleanupUnit CONSTANT)

public:
    explicit BackupService(const QString &unit = {}, QObject *parent = nullptr);
    ~BackupService() override;

    bool available() const { return m_available; }
    bool running() const { return m_running; }
    bool lastFailed() const { return m_lastFailed; }
    QDateTime lastFinished() const { return m_lastFinished; }
    QDateTime nextRun() const { return m_nextRun; }
    QString unit() const { return m_unit; }
    QDateTime started() const { return m_started; }
    bool lastSkipped() const { return m_lastSkipped; }
    // The service that forgets and prunes old backups (REWIND_PRUNE_UNIT,
    // restic-prune by default), and the rules its `restic forget` uses.
    bool cleanupAvailable() const { return m_cleanupAvailable; }
    QDateTime cleanupNextRun() const { return m_cleanupNextRun; }
    QDateTime cleanupLastRun() const { return m_cleanupLastRun; }
    QString cleanupUnit() const { return m_cleanupUnit; }
    QStringList retentionArguments() const { return m_retentionArguments; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void loadLog();

    // Parses `systemctl show --timestamp=unix` output into key/value pairs.
    static QHash<QString, QString> parseShow(const QByteArray &output);
    static QDateTime parseTimestamp(const QString &value);
    // Splits `systemctl show` output for several units into one block each.
    static QList<QHash<QString, QString>> parseShowBlocks(const QByteArray &output);

signals:
    void changed();
    void finished(bool success);
    void skipped();
    void logLoaded(const QString &text);
    void failedToStart(const QString &message);
    void retentionChanged();

private:
    void apply(const QHash<QString, QString> &service, const QHash<QString, QString> &timer);
    void applyCleanup(const QHash<QString, QString> &service, const QHash<QString, QString> &timer);

    QString m_unit;
    QString m_cleanupUnit;
    QDateTime m_started;
    bool m_lastSkipped = false;
    bool m_cleanupAvailable = false;
    QDateTime m_cleanupNextRun;
    QDateTime m_cleanupLastRun;
    QStringList m_retentionArguments;
    bool m_available = false;
    bool m_running = false;
    bool m_lastFailed = false;
    QDateTime m_lastFinished;
    QDateTime m_nextRun;
    QTimer m_poll;
    QProcess *m_show = nullptr;
};
