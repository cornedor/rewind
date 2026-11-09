#pragma once

#include <QDateTime>
#include <QHash>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QVector>

// How restic is reached: the environment restic runs with, and where it came
// from. Rewind reads the same file the user's backup service uses, so the app
// sees exactly the repository the backups go to.
struct ResticConfig {
    QProcessEnvironment environment;
    QString source;     // the env file used, or empty when the shell's environment was used
    QString repository; // with any credentials removed, for display
    QString error;      // set when there is no repository to open
};

struct Snapshot {
    QString id;
    QString shortId;
    QDateTime time;
    QStringList paths;
    // Where each path sits in the snapshot's tree. restic keeps the path as
    // it was typed: `restic backup /home/me` stores /home/me, while
    // `restic backup me` run in /home stores /me. Defaults to the full path.
    QStringList treeRoots;
    QString hostname;
    QString parent;
    QString username;
    QString programVersion;
    QStringList tags;
    // From the summary restic 0.17 and later store; -1 or invalid when the
    // backup was made by an older restic.
    QDateTime backupStart;
    QDateTime backupEnd;
    qint64 filesNew = -1;
    qint64 filesChanged = -1;
    qint64 filesUnmodified = -1;
    qint64 dataAdded = -1;       // new data, before compression
    qint64 dataAddedPacked = -1; // what it took in the repository
    qint64 totalFiles = -1;
    qint64 totalBytes = -1;

    bool hasSummary() const { return totalBytes >= 0; }
};

// How much the whole repository takes, from `restic stats --mode raw-data`.
struct RepositoryStats {
    qint64 storedBytes = -1;
    qint64 uncompressedBytes = -1;
};

// Which backups the user's cleanup rules keep, and why, from a dry run of
// `restic forget` with the rules their prune service uses.
struct Retention {
    QHash<QString, QStringList> keptBecause; // snapshot id -> rules: "hourly", "daily", "last"
    QStringList removed;                     // snapshot ids
};

namespace Restic {

// Parses a systemd EnvironmentFile: KEY=VALUE lines, # and ; comments,
// optional single or double quotes around the value.
QHash<QString, QString> parseEnvironmentFile(const QByteArray &contents);

// The shell's environment wins when it already names a repository, so
// `RESTIC_REPOSITORY=… rewind` works; otherwise the env file fills it in.
ResticConfig loadConfig(const QProcessEnvironment &base, const QString &envFile);

// The repository without user:password@, which REST and S3 URLs can carry.
QString describeRepository(const QString &repository);

// Newest first. Only snapshots of this machine when it has any, since a
// shared repository holds other machines' homes under the same paths.
QVector<Snapshot> parseSnapshots(const QByteArray &json, const QString &hostname, QString *error = nullptr);

// Map between a snapshot's tree (see Snapshot::treeRoots) and the real
// filesystem.
QString treePath(const Snapshot &snapshot, const QString &absolutePath);
QString absolutePath(const Snapshot &snapshot, const QString &treePath);

// For a folder above the backed-up paths (/home when /home/me is backed up),
// the child names that lead to them; empty when the folder is not above any.
QStringList namesLeadingToBackup(const Snapshot &snapshot, const QString &absoluteFolder);

bool covers(const Snapshot &snapshot, const QString &absolutePath);

RepositoryStats parseStats(const QByteArray &json);

// The arguments of a `restic forget` command line that pick which snapshots
// stay (--keep-*, --group-by, and the --host/--tag/--path filters), so a dry
// run sees what the real one would. Empty when the line isn't a forget.
QStringList retentionArguments(const QStringList &argv);
// The argv of each ExecStart= in `systemctl show -p ExecStart` output.
QList<QStringList> parseExecStart(const QString &value);
Retention parseForget(const QByteArray &json);

// The ways a path could sit in a tree, longest first: /a/b/c, /b/c, /c.
QStringList candidateTreeRoots(const QString &path);

} // namespace Restic
