#pragma once

#include <QObject>
#include <QProcess>
#include <QVector>
#include <functional>

#include "restic.h"

struct Change {
    QChar modifier; // '+', '-', 'M', 'T' (type), 'U' (metadata)
    QString path;   // absolute, on the real filesystem
};

// One restic repository, opened two ways at once: `restic snapshots` for the
// timeline, and `restic mount` so every backed-up file can be read like a
// normal file. Reading through the mount is what makes browsing, diffing and
// previews fast; restic only loads its index once.
class Repository : public QObject {
    Q_OBJECT

public:
    enum class State { Idle, Loading, Ready, Failed };

    explicit Repository(const ResticConfig &config, const QString &runtimeDirectory = {}, QObject *parent = nullptr);
    ~Repository() override;

    void open();
    void reopen();
    void close();

    State snapshotsState() const { return m_snapshotsState; }
    State mountState() const { return m_mountState; }
    QString error() const { return m_error; }
    const ResticConfig &config() const { return m_config; }
    const QVector<Snapshot> &snapshots() const { return m_snapshots; }

    // Where a snapshot's version of an absolute path lives in the mount;
    // empty when the mount isn't ready or the snapshot doesn't hold the path.
    QString sourcePath(const Snapshot &snapshot, const QString &absolutePath) const;
    QString snapshotRoot(const Snapshot &snapshot) const;
    QString mountPoint() const { return m_mountPoint; }

    // What a backup changed compared to the one before it, from `restic diff`.
    void loadChanges(const Snapshot &older, const Snapshot &newer,
                     std::function<void(const QVector<Change> &, const QString &error)> done);

    // How much the repository takes, from `restic stats --mode raw-data`.
    void loadStats(std::function<void(const RepositoryStats &, const QString &error)> done);
    // Which backups `restic forget` with these rules keeps; a dry run, so
    // nothing is removed.
    void loadRetention(const QStringList &rules, std::function<void(const Retention &, const QString &error)> done);

    static QVector<Change> parseChanges(const QByteArray &jsonLines, const Snapshot &newer, const Snapshot &older);
    // The useful line of restic's stderr: its Fatal: message, or the last line.
    static QString explainFailure(const QByteArray &stderrOutput);

signals:
    void snapshotsChanged();
    void mountChanged();

private:
    void loadSnapshots();
    // Runs a read-only restic command and hands back its output or what went wrong.
    void run(const QStringList &arguments, std::function<void(const QByteArray &output, const QString &error)> done);
    void startMount();
    void stopMount();
    void cleanStaleMounts();
    bool isMounted(const QString &path) const;
    QString fusermount() const;
    QProcessEnvironment processEnvironment() const;
    void fail(const QString &message);
    void resolveTreeRoots();

    ResticConfig m_config;
    QString m_restic;
    QString m_runtimeDirectory;
    QString m_mountPoint;
    QProcess *m_mount = nullptr;
    QByteArray m_mountErrors;
    QProcess *m_snapshotsProcess = nullptr;
    QVector<Snapshot> m_snapshots;
    State m_snapshotsState = State::Idle;
    State m_mountState = State::Idle;
    bool m_serving = false;
    bool m_resolving = false;
    QString m_error;
};
