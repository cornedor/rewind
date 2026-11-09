#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>
#include <atomic>
#include <functional>

// How a file in one place relates to the same file in another.
enum class EntryStatus {
    Unknown,     // nothing to compare with
    Same,
    Changed,     // in a backup: differs from the file on disk now
    Deleted,     // in a backup: gone from disk now
    NotBackedUp, // on disk now: not in the latest backup
};

struct Entry {
    QString name;
    QString path;   // the absolute path on the real filesystem
    QString source; // where this version is read from (disk or the mount)
    bool isDir = false;
    bool isLink = false;
    qint64 size = 0;
    qint64 modified = 0; // seconds since the epoch
    EntryStatus status = EntryStatus::Unknown;
};

// What makes two versions of a file the same: restic keeps size and mtime,
// so a file whose size and mtime didn't move is the same file.
struct Signature {
    bool exists = false;
    bool isDir = false;
    bool isLink = false;
    qint64 size = 0;
    qint64 modified = 0;
    bool operator==(const Signature &other) const {
        return exists == other.exists && isDir == other.isDir && isLink == other.isLink &&
               (isDir || (size == other.size && modified == other.modified));
    }
    bool operator!=(const Signature &other) const { return !(*this == other); }
};

Signature signatureOf(const QString &path);
EntryStatus compareBackupWithNow(const Signature &backup, const Signature &now);
EntryStatus compareNowWithBackup(const Signature &now, const Signature &backup);

// Reads a folder, directories first and then by name the way people sort.
// A missing folder gives an empty list and a false return.
bool readFolder(const QString &folder, const QString &absoluteFolder, QVector<Entry> *entries);
void sortEntries(QVector<Entry> *entries);

// Versions of one path across snapshots, newest first like the timeline.
struct VersionMap {
    QVector<int> version;   // per snapshot: 1 for the oldest version, 0 where the path is missing
    QVector<bool> starts;   // per snapshot: a new version begins here (going forward in time)
    int count = 0;
    int nowMatches = 0;     // the version the file on disk is, or 0 when it is none of them
};
VersionMap mapVersions(const QVector<Signature> &newestFirst, const Signature &now);

struct DiffLine {
    enum Kind { Context, Added, Removed, Hunk };
    Kind kind = Context;
    int oldLine = 0;
    int newLine = 0;
    QString text;
};

struct TextComparison {
    enum Kind { Text, Identical, Binary, TooLarge, Missing, Failed };
    Kind kind = Failed;
    QVector<DiffLine> lines;
    int added = 0;
    int removed = 0;
    bool truncated = false;
    QString error;
};

bool looksBinary(const QString &path);
QVector<DiffLine> parseUnifiedDiff(const QByteArray &diff, int *added, int *removed);
// A missing side counts as empty, so a new file reads as all added.
TextComparison compareText(const QString &oldPath, const QString &newPath);
// One file's text, as context lines, for a version with nothing to compare.
TextComparison readText(const QString &path);

// Restoring copies a backed-up version out of the mount, next to the target
// first, then renames it into place, so a half-copied file is never seen.
// A replaced file is moved to the holding folder, never deleted, which is
// what lets Ctrl+Z put it back.
struct RestoreOperation {
    QString target;
    QString heldOriginal; // where the replaced file waits, empty for a copy
    QString heldRestored; // where the restored copy waits after an undo
    bool undone = false;
};

struct RestoreProgress {
    std::atomic<qint64> bytes{0};
    std::atomic<qint64> total{0};
    std::atomic<bool> cancel{false};
};

QString restoredCopyName(const QString &fileName, const QDateTime &when, const QString &label);
QString freePath(const QString &path);
qint64 treeSize(const QString &path);
bool restoreTo(const QString &source, const QString &target, bool replace, const QString &holdingRoot,
               RestoreProgress *progress, RestoreOperation *operation, QString *error);
bool undoRestore(RestoreOperation *operation, const QString &holdingRoot, QString *error);
bool redoRestore(RestoreOperation *operation, QString *error);
