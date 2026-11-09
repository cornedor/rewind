#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <sys/stat.h>

#include "backend.h"
#include "backupservice.h"
#include "files.h"
#include "overview.h"
#include "repository.h"
#include "restic.h"
#include "theme.h"
#include "translator.h"

namespace {
void write(const QString &path, const QByteArray &contents) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(contents);
}

QByteArray read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

Snapshot snapshotOf(const QStringList &paths, const QStringList &roots = {}) {
    Snapshot snapshot;
    snapshot.id = snapshot.shortId = "abcd1234";
    snapshot.paths = paths;
    snapshot.treeRoots = roots;
    return snapshot;
}

Signature sig(qint64 size, qint64 modified) {
    Signature s;
    s.exists = true;
    s.size = size;
    s.modified = modified;
    return s;
}
} // namespace

class RewindTests : public QObject {
    Q_OBJECT

private slots:
    // restic configuration

    void parsesSystemdEnvironmentFiles()
    {
        const auto values = Restic::parseEnvironmentFile(
            "# comment\n; also\nRESTIC_REPOSITORY=rclone:box:backups\n"
            "RESTIC_PASSWORD_COMMAND=\"secret-tool lookup restic home\"\nexport RESTIC_PACK_SIZE='64'\nbroken\n");
        QCOMPARE(values.value("RESTIC_REPOSITORY"), QString("rclone:box:backups"));
        QCOMPARE(values.value("RESTIC_PASSWORD_COMMAND"), QString("secret-tool lookup restic home"));
        QCOMPARE(values.value("RESTIC_PACK_SIZE"), QString("64"));
        QCOMPARE(values.size(), 3);
    }

    void environmentFileFillsInWhenTheShellHasNoRepository()
    {
        QTemporaryDir dir;
        write(dir.filePath("env"), "RESTIC_REPOSITORY=/backups\n");
        QProcessEnvironment shell;
        const ResticConfig fromFile = Restic::loadConfig(shell, dir.filePath("env"));
        QCOMPARE(fromFile.environment.value("RESTIC_REPOSITORY"), QString("/backups"));
        QCOMPARE(fromFile.source, dir.filePath("env"));
        QVERIFY(fromFile.error.isEmpty());

        shell.insert("RESTIC_REPOSITORY", "/elsewhere");
        const ResticConfig fromShell = Restic::loadConfig(shell, dir.filePath("env"));
        QCOMPARE(fromShell.environment.value("RESTIC_REPOSITORY"), QString("/elsewhere"));
        QVERIFY(fromShell.source.isEmpty());

        QVERIFY(!Restic::loadConfig({}, dir.filePath("missing")).error.isEmpty());
    }

    void hidesCredentialsInTheRepositoryName()
    {
        QCOMPARE(Restic::describeRepository("rest:https://sam:hunter2@backup.example/sam"),
                 QString("rest:https://backup.example/sam"));
        QCOMPARE(Restic::describeRepository("rclone:box:restic"), QString("rclone:box:restic"));
    }

    void parsesSnapshotsNewestFirstForThisMachine()
    {
        const QByteArray json = R"([
            {"time":"2026-11-08T10:00:00+01:00","paths":["/home/sam"],"hostname":"laptop","id":"aaaa1111ffff","short_id":"aaaa1111",
             "summary":{"files_new":2,"files_changed":3,"data_added":100}},
            {"time":"2026-11-09T10:00:00+01:00","paths":["/home/sam"],"hostname":"laptop","id":"bbbb2222ffff","short_id":"bbbb2222"},
            {"time":"2026-11-10T10:00:00+01:00","paths":["/home/alex"],"hostname":"desktop","id":"cccc3333ffff","short_id":"cccc3333"}
        ])";
        const auto snapshots = Restic::parseSnapshots(json, "laptop");
        QCOMPARE(snapshots.size(), 2);
        QCOMPARE(snapshots.at(0).shortId, QString("bbbb2222"));
        QCOMPARE(snapshots.at(1).filesChanged, 3);
        QCOMPARE(snapshots.at(0).filesNew, -1);
        QCOMPARE(Restic::parseSnapshots(json, "other-machine").size(), 3);

        QString error;
        QVERIFY(Restic::parseSnapshots("Fatal: nope", "laptop", &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    // Paths inside snapshots

    void mapsPathsBackedUpWithAbsolutePaths()
    {
        const Snapshot snapshot = snapshotOf({"/home/sam"});
        QCOMPARE(Restic::treePath(snapshot, "/home/sam/Documents/a.txt"), QString("/home/sam/Documents/a.txt"));
        QCOMPARE(Restic::treePath(snapshot, "/home/sam"), QString("/home/sam"));
        QVERIFY(Restic::treePath(snapshot, "/home/samantha").isEmpty());
        QVERIFY(Restic::treePath(snapshot, "/etc").isEmpty());
        QCOMPARE(Restic::absolutePath(snapshot, "/home/sam/x"), QString("/home/sam/x"));
    }

    void mapsPathsBackedUpWithRelativePaths()
    {
        // `cd /srv && restic backup home/sam` lists /srv/home/sam in the
        // snapshot's paths, but stores it at /home/sam in the tree.
        const Snapshot snapshot = snapshotOf({"/srv/home/sam"}, {"/home/sam"});
        QCOMPARE(Restic::treePath(snapshot, "/srv/home/sam/Notes/ideas.md"), QString("/home/sam/Notes/ideas.md"));
        QCOMPARE(Restic::absolutePath(snapshot, "/home/sam/Notes/ideas.md"), QString("/srv/home/sam/Notes/ideas.md"));
        QVERIFY(Restic::absolutePath(snapshot, "/other").isEmpty());
    }

    void mapsABackupOfTheRoot()
    {
        const Snapshot snapshot = snapshotOf({"/"}, {"/"});
        QCOMPARE(Restic::treePath(snapshot, "/etc/hosts"), QString("/etc/hosts"));
        QCOMPARE(Restic::treePath(snapshot, "/"), QString("/"));
        QCOMPARE(Restic::absolutePath(snapshot, "/etc/hosts"), QString("/etc/hosts"));
    }

    void leadsFromParentFoldersToTheBackup()
    {
        const Snapshot snapshot = snapshotOf({"/home/sam", "/etc/nginx"});
        QCOMPARE(Restic::namesLeadingToBackup(snapshot, "/home"), QStringList{"sam"});
        QCOMPARE(Restic::namesLeadingToBackup(snapshot, "/"), (QStringList{"home", "etc"}));
        QVERIFY(Restic::namesLeadingToBackup(snapshot, "/home/sam").isEmpty());
        QVERIFY(Restic::namesLeadingToBackup(snapshot, "/var").isEmpty());
    }

    void listsCandidateTreeRootsLongestFirst()
    {
        QCOMPARE(Restic::candidateTreeRoots("/a/b/c"), (QStringList{"/a/b/c", "/b/c", "/c"}));
        QCOMPARE(Restic::candidateTreeRoots("/"), QStringList{"/"});
    }

    void readsWhatABackupChanged()
    {
        const Snapshot newer = snapshotOf({"/home/sam"});
        const QByteArray json = "{\"message_type\":\"change\",\"path\":\"/home/sam/Notes/\",\"modifier\":\"M\"}\n"
                                "{\"message_type\":\"change\",\"path\":\"/home/sam/Notes/ideas.md\",\"modifier\":\"M\"}\n"
                                "{\"message_type\":\"change\",\"path\":\"/home/sam/old.txt\",\"modifier\":\"-\"}\n"
                                "{\"message_type\":\"statistics\"}\n";
        const auto changes = Repository::parseChanges(json, newer, newer);
        QCOMPARE(changes.size(), 2);
        QCOMPARE(changes.at(0).path, QString("/home/sam/Notes/ideas.md"));
        QCOMPARE(changes.at(1).modifier, QChar('-'));
    }

    void explainsResticFailures()
    {
        QCOMPARE(Repository::explainFailure("Loading snapshots...\nFatal: wrong password or no key found\n"),
                 QString("wrong password or no key found"));
        QCOMPARE(Repository::explainFailure("something broke\n"), QString("something broke"));
    }

    // Versions

    void groupsSnapshotsIntoVersions()
    {
        // Newest first: v3, v3, missing, v2, v1, v1
        const QVector<Signature> signatures{sig(30, 3), sig(30, 3), {}, sig(20, 2), sig(10, 1), sig(10, 1)};
        const VersionMap map = mapVersions(signatures, sig(30, 3));
        QCOMPARE(map.count, 3);
        QCOMPARE(map.version, (QVector<int>{3, 3, 0, 2, 1, 1}));
        QCOMPARE(map.starts, (QVector<bool>{false, true, false, true, false, true}));
        QCOMPARE(map.nowMatches, 3);
        QCOMPARE(mapVersions(signatures, sig(99, 9)).nowMatches, 0);
    }

    void aFileThatComesBackIsANewVersionEvenWhenItIsTheSame()
    {
        const QVector<Signature> signatures{sig(10, 1), {}, sig(10, 1)};
        const VersionMap map = mapVersions(signatures, {});
        QCOMPARE(map.version, (QVector<int>{2, 0, 1}));
        QCOMPARE(map.nowMatches, 0);
    }

    void comparesBackupsWithNow()
    {
        QCOMPARE(compareBackupWithNow(sig(1, 1), {}), EntryStatus::Deleted);
        QCOMPARE(compareBackupWithNow(sig(1, 1), sig(1, 1)), EntryStatus::Same);
        QCOMPARE(compareBackupWithNow(sig(1, 1), sig(1, 2)), EntryStatus::Changed);
        QCOMPARE(compareNowWithBackup(sig(1, 1), {}), EntryStatus::NotBackedUp);
        Signature folderThen = sig(0, 1), folderNow = sig(0, 5);
        folderThen.isDir = folderNow.isDir = true;
        QCOMPARE(compareBackupWithNow(folderThen, folderNow), EntryStatus::Same);
    }

    // Diffs

    void parsesUnifiedDiffs()
    {
        int added = 0, removed = 0;
        const auto lines = parseUnifiedDiff("--- a\n+++ b\n@@ -1,3 +1,3 @@ heading\n one\n-two\n+TWO\n three\n", &added, &removed);
        QCOMPARE(lines.size(), 5);
        QCOMPARE(lines.at(0).kind, DiffLine::Hunk);
        QCOMPARE(lines.at(0).text, QString("heading"));
        QCOMPARE(lines.at(2).kind, DiffLine::Removed);
        QCOMPARE(lines.at(2).oldLine, 2);
        QCOMPARE(lines.at(3).kind, DiffLine::Added);
        QCOMPARE(lines.at(3).newLine, 2);
        QCOMPARE(lines.at(4).oldLine, 3);
        QCOMPARE(added, 1);
        QCOMPARE(removed, 1);
    }

    void comparesTextFiles()
    {
        QTemporaryDir dir;
        write(dir.filePath("old.txt"), "a\nb\n");
        write(dir.filePath("new.txt"), "a\nc\n");
        write(dir.filePath("same.txt"), "a\nb\n");
        write(dir.filePath("binary"), QByteArray("x\0y", 3));

        const TextComparison changed = compareText(dir.filePath("old.txt"), dir.filePath("new.txt"));
        QCOMPARE(changed.kind, TextComparison::Text);
        QCOMPARE(changed.added, 1);
        QCOMPARE(changed.removed, 1);
        QCOMPARE(compareText(dir.filePath("old.txt"), dir.filePath("same.txt")).kind, TextComparison::Identical);
        QCOMPARE(compareText(dir.filePath("old.txt"), dir.filePath("binary")).kind, TextComparison::Binary);
        QCOMPARE(compareText(QString(), dir.filePath("new.txt")).added, 2);
        QCOMPARE(readText(dir.filePath("new.txt")).lines.size(), 2);
    }

    // Restoring

    void namesRestoredCopies()
    {
        const QDateTime when(QDate(2026, 11, 8), QTime(14, 5));
        QCOMPARE(restoredCopyName("report.pdf", when, "restored"), QString("report (restored 2026-11-08 14.05).pdf"));
        QCOMPARE(restoredCopyName("site.tar.gz", when, "restored"), QString("site (restored 2026-11-08 14.05).tar.gz"));
        QCOMPARE(restoredCopyName(".bashrc", when, "restored"), QString(".bashrc (restored 2026-11-08 14.05)"));
        QCOMPARE(restoredCopyName("Photos", when, "restored"), QString("Photos (restored 2026-11-08 14.05)"));
    }

    void restoresADeletedFileWithItsModeAndTime()
    {
        QTemporaryDir dir;
        write(dir.filePath("backup/a.txt"), "then");
        ::chmod(QFile::encodeName(dir.filePath("backup/a.txt")).constData(), 0640);
        {
            QFile source(dir.filePath("backup/a.txt"));
            QVERIFY(source.open(QIODevice::ReadWrite));
            QVERIFY(source.setFileTime(QDateTime(QDate(2025, 1, 2), QTime(3, 4, 5)), QFileDevice::FileModificationTime));
        }

        RestoreOperation operation;
        RestoreProgress progress;
        QString error;
        QVERIFY2(restoreTo(dir.filePath("backup/a.txt"), dir.filePath("home/a.txt"), false, dir.filePath("holding"),
                           &progress, &operation, &error), qPrintable(error));
        QCOMPARE(read(dir.filePath("home/a.txt")), QByteArray("then"));
        struct stat st;
        QCOMPARE(::stat(QFile::encodeName(dir.filePath("home/a.txt")).constData(), &st), 0);
        QCOMPARE(int(st.st_mode & 0777), 0640);
        QCOMPARE(QFileInfo(dir.filePath("home/a.txt")).lastModified(), QDateTime(QDate(2025, 1, 2), QTime(3, 4, 5)));
        QCOMPARE(progress.bytes.load(), qint64(4));
        // Nothing half-written is left beside it.
        QCOMPARE(QDir(dir.filePath("home")).entryList(QDir::Files | QDir::Hidden), QStringList{"a.txt"});
    }

    void neverOverwritesWithoutBeingAskedTo()
    {
        QTemporaryDir dir;
        write(dir.filePath("backup/a.txt"), "then");
        write(dir.filePath("home/a.txt"), "now");
        RestoreOperation operation;
        QString error;
        QVERIFY(!restoreTo(dir.filePath("backup/a.txt"), dir.filePath("home/a.txt"), false, dir.filePath("holding"),
                           nullptr, &operation, &error));
        QCOMPARE(read(dir.filePath("home/a.txt")), QByteArray("now"));
    }

    void replacingKeepsTheOriginalAndUndoPutsItBack()
    {
        QTemporaryDir dir;
        write(dir.filePath("backup/a.txt"), "then");
        write(dir.filePath("home/a.txt"), "now");
        RestoreOperation operation;
        QString error;
        QVERIFY(restoreTo(dir.filePath("backup/a.txt"), dir.filePath("home/a.txt"), true, dir.filePath("holding"),
                          nullptr, &operation, &error));
        QCOMPARE(read(dir.filePath("home/a.txt")), QByteArray("then"));
        QCOMPARE(read(operation.heldOriginal), QByteArray("now"));

        QVERIFY(undoRestore(&operation, dir.filePath("holding"), &error));
        QCOMPARE(read(dir.filePath("home/a.txt")), QByteArray("now"));
        QVERIFY(redoRestore(&operation, &error));
        QCOMPARE(read(dir.filePath("home/a.txt")), QByteArray("then"));
        QVERIFY(undoRestore(&operation, dir.filePath("holding"), &error));
        QCOMPARE(read(dir.filePath("home/a.txt")), QByteArray("now"));
    }

    void restoresFoldersWithLinks()
    {
        QTemporaryDir dir;
        write(dir.filePath("backup/Photos/2026/sun.png"), "png");
        QVERIFY(QFile::link("2026/sun.png", dir.filePath("backup/Photos/latest")));
        RestoreOperation operation;
        QString error;
        QVERIFY2(restoreTo(dir.filePath("backup/Photos"), dir.filePath("home/Photos"), false, dir.filePath("holding"),
                           nullptr, &operation, &error), qPrintable(error));
        QCOMPARE(read(dir.filePath("home/Photos/2026/sun.png")), QByteArray("png"));
        QCOMPARE(QFileInfo(dir.filePath("home/Photos/latest")).symLinkTarget(), dir.filePath("home/Photos/2026/sun.png"));

        // Undoing a restored copy takes it away again, into the holding folder.
        QVERIFY(undoRestore(&operation, dir.filePath("holding"), &error));
        QVERIFY(!QFileInfo::exists(dir.filePath("home/Photos")));
    }

    void cancellingLeavesNothingBehind()
    {
        QTemporaryDir dir;
        write(dir.filePath("backup/big.bin"), QByteArray(4 << 20, 'x'));
        RestoreProgress progress;
        progress.cancel = true;
        RestoreOperation operation;
        QString error;
        QVERIFY(!restoreTo(dir.filePath("backup/big.bin"), dir.filePath("home/big.bin"), false, dir.filePath("holding"),
                           &progress, &operation, &error));
        QCOMPARE(QDir(dir.filePath("home")).entryList(QDir::Files | QDir::Hidden), QStringList{});
    }

    // The backup service

    void readsTheBackupServiceState()
    {
        const auto values = BackupService::parseShow("LoadState=loaded\nActiveState=inactive\nExecMainExitTimestamp=@1794223051\n");
        QCOMPARE(values.value("ActiveState"), QString("inactive"));
        QCOMPARE(BackupService::parseTimestamp(values.value("ExecMainExitTimestamp")).toSecsSinceEpoch(), 1794223051);
        QVERIFY(!BackupService::parseTimestamp("n/a").isValid());
        QVERIFY(!BackupService::parseTimestamp("").isValid());
    }

    void keepsEveryExecStartOfAUnit()
    {
        // restic-prune runs forget, then check: systemctl lists ExecStart twice.
        const QByteArray output =
            "LoadState=loaded\nActiveState=inactive\n\n"
            "LoadState=loaded\nNextElapseUSecRealtime=@1794223051\n\n"
            "LoadState=loaded\n"
            "ExecStart={ path=/usr/bin/restic ; argv[]=/usr/bin/restic forget --prune --retry-lock 1h --keep-hourly 24 "
            "--keep-daily=7 --group-by host ; ignore_errors=no ; start_time=[n/a] ; stop_time=[n/a] ; pid=0 ; code=(null) ; status=0/0 }\n"
            "ExecStart={ path=/usr/bin/restic ; argv[]=/usr/bin/restic check --read-data-subset=2% ; ignore_errors=no ; "
            "start_time=[n/a] ; stop_time=[n/a] ; pid=0 ; code=(null) ; status=0/0 }\n\n"
            "LoadState=not-found\n";
        const auto blocks = BackupService::parseShowBlocks(output);
        QVERIFY(blocks.size() >= 4);
        QCOMPARE(blocks.at(1).value("NextElapseUSecRealtime"), QString("@1794223051"));
        QCOMPARE(blocks.at(3).value("LoadState"), QString("not-found"));
        const auto commands = Restic::parseExecStart(blocks.at(2).value("ExecStart"));
        QCOMPARE(commands.size(), 2);
        QCOMPARE(Restic::retentionArguments(commands.at(0)),
                 (QStringList{"--keep-hourly", "24", "--keep-daily=7", "--group-by", "host"}));
        QVERIFY(Restic::retentionArguments(commands.at(1)).isEmpty());
    }

    // The Backups view

    void parsesWhatEachBackupRecorded()
    {
        const QByteArray json = R"([{"id":"aaaa1111","short_id":"aaaa1111","time":"2026-11-09T13:05:06.37+01:00",
            "paths":["/home/me"],"hostname":"pc","username":"me","program_version":"restic 0.19.1",
            "summary":{"backup_start":"2026-11-09T13:05:06.378+01:00","backup_end":"2026-11-09T13:05:22.167+01:00",
            "files_new":27,"files_changed":60,"files_unmodified":95284,"data_added":23842462,
            "data_added_packed":5448577,"total_files_processed":95371,"total_bytes_processed":5199571877}},
            {"id":"bbbb2222","time":"2026-11-08T13:05:06+01:00","paths":["/home/me"],"hostname":"pc"}])";
        const auto snapshots = Restic::parseSnapshots(json, "pc");
        QCOMPARE(snapshots.size(), 2);
        const Snapshot &s = snapshots.at(0);
        QVERIFY(s.hasSummary());
        QCOMPARE(s.backupStart.secsTo(s.backupEnd), 15);
        QCOMPARE(s.dataAddedPacked, qint64(5448577));
        QCOMPARE(s.totalBytes, qint64(5199571877));
        QCOMPARE(s.totalFiles, qint64(95371));
        QCOMPARE(s.programVersion, QString("restic 0.19.1"));
        // Made by a restic before 0.17: nothing recorded, rather than zeros.
        QVERIFY(!snapshots.at(1).hasSummary());
        QCOMPARE(snapshots.at(1).dataAddedPacked, qint64(-1));
    }

    void readsWhichBackupsTheCleanupKeeps()
    {
        const QByteArray json = R"([{"host":"pc","paths":["/home/me"],
            "keep":[{"id":"newest"},{"id":"older"}],"remove":[{"id":"gone"}],
            "reasons":[{"snapshot":{"id":"newest"},"matches":["hourly snapshot","daily snapshot"]},
                       {"snapshot":{"id":"older"},"matches":["hourly snapshot","oldest daily snapshot","within 2d"]}]}])";
        const Retention retention = Restic::parseForget(json);
        QCOMPARE(retention.keptBecause.value("newest"), (QStringList{"hourly", "daily"}));
        QCOMPARE(retention.keptBecause.value("older"), (QStringList{"hourly", "daily", "within 2d"}));
        QCOMPARE(retention.removed, QStringList{"gone"});
        QVERIFY(!retention.keptBecause.contains("gone"));
    }

    void readsRepositoryStats()
    {
        const auto stats = Restic::parseStats(R"({"total_size":2593823520,"total_uncompressed_size":4505700000,"snapshots_count":5})");
        QCOMPARE(stats.storedBytes, qint64(2593823520));
        QCOMPARE(stats.uncompressedBytes, qint64(4505700000));
    }

    void describesCleanupRules()
    {
        QCOMPARE(Overview::describeRules({"--keep-hourly", "24", "--keep-daily=7", "--keep-weekly", "4", "--keep-monthly", "12"}),
                 QString("24 hourly, 7 daily, 4 weekly and 12 monthly"));
        QCOMPARE(Overview::describeRules({"--keep-within", "30d"}), QString("everything from the last 30d"));
        QCOMPARE(Overview::describeRules({"--group-by", "host"}), QString());
    }

    void describesDurations()
    {
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom));
        QCOMPARE(Overview::formatDuration(16), QString("16 s"));
        QCOMPARE(Overview::formatDuration(77), QString("1 min 17 s"));
        QCOMPARE(Overview::formatDuration(120), QString("2 min"));
        QCOMPARE(Overview::formatDuration(3900), QString("1 h 5 min"));
        QCOMPARE(Overview::formatApproximate(3569), QString("59 min"));
        QCOMPARE(Overview::formatApproximate(3600), QString("1 h"));
        QCOMPARE(Overview::formatApproximate(5400), QString("1.5 h"));
        QCOMPARE(Overview::formatApproximate(2 * 86400), QString("2 days"));
    }

    void chartCutsOffOneHugeBackup()
    {
        // The first backup took 2.2 GB, the hourly ones a few MB: the scale
        // follows the hourly ones.
        const QVector<double> values{2.2e9, 5e6, 48e6, 161e6, 17e6, 6e6, 9e6, 12e6, 3e6, 4e6};
        const double scale = Overview::chartScaleFor(values);
        QVERIFY(scale < 2.2e9);
        QVERIFY(scale >= 161e6);
        // Even backups need no cut.
        QCOMPARE(Overview::chartScaleFor({5e6, 6e6, 7e6, 8e6, 9e6}), 9e6);
        QCOMPARE(Overview::chartScaleFor({}), 0.0);
    }

    void findsTheUsualGapBetweenBackups()
    {
        QVector<Snapshot> snapshots;
        const QDateTime start(QDate(2026, 11, 9), QTime(14, 0));
        for (int hours : {0, 1, 2, 3, 30}) {
            Snapshot s;
            s.time = start.addSecs(-hours * 3600);
            snapshots << s;
        }
        QCOMPARE(Overview::typicalGap(snapshots), qint64(3600));
        QCOMPARE(Overview::typicalGap(snapshots.mid(0, 2)), qint64(0));
    }

    // Words

    void describesMomentsInTime()
    {
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom));
        const QDateTime now(QDate(2026, 11, 9), QTime(12, 0));
        QCOMPARE(Backend::describeMoment(QDateTime(QDate(2026, 11, 9), QTime(9, 5)), now), QString("Today 09:05"));
        QCOMPARE(Backend::describeMoment(QDateTime(QDate(2026, 11, 8), QTime(9, 5)), now), QString("Yesterday 09:05"));
        QCOMPARE(Backend::describeMoment(QDateTime(QDate(2026, 11, 5), QTime(9, 5)), now), QString("Thursday 09:05"));
        QCOMPARE(Backend::describeMoment(QDateTime(QDate(2026, 3, 5), QTime(9, 5)), now), QString("Thu 5 Mar 09:05"));
        QCOMPARE(Backend::describeMoment(QDateTime(QDate(2025, 3, 5), QTime(9, 5)), now), QString("5 Mar 2025 09:05"));
        QCOMPARE(Backend::describeAgo(now.addSecs(-30), now), QString("Just now"));
        QCOMPARE(Backend::describeAgo(now.addSecs(-3 * 3600), now), QString("3 hours ago"));
        QCOMPARE(Backend::describeFuture(now.addSecs(25 * 60), now), QString("in 25 minutes"));
        QLocale::setDefault(QLocale::system());
    }

    // End to end: a real repository, mounted.

    void opensARealRepositoryThroughTheMount()
    {
        if (QStandardPaths::findExecutable("restic").isEmpty() || !QFileInfo::exists("/dev/fuse") ||
            QStandardPaths::findExecutable("fusermount3").isEmpty())
            QSKIP("Needs restic and FUSE");
        QTemporaryDir dir;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert("RESTIC_REPOSITORY", dir.filePath("repo"));
        environment.insert("RESTIC_PASSWORD", "test");
        environment.insert("RESTIC_CACHE_DIR", dir.filePath("cache"));
        auto restic = [&](const QStringList &arguments, const QString &workingDirectory = {}) {
            QProcess process;
            process.setProcessEnvironment(environment);
            process.setWorkingDirectory(workingDirectory.isEmpty() ? dir.path() : workingDirectory);
            process.start("restic", arguments);
            return process.waitForFinished(60000) && process.exitCode() == 0;
        };
        QVERIFY(restic({"init", "-q"}));
        write(dir.filePath("home/sam/notes.txt"), "one\n");
        // One backup typed with an absolute path, one with a relative path,
        // the two ways restic lays out its trees.
        QVERIFY(restic({"backup", "-q", "--time", "2026-11-01 10:00:00", dir.filePath("home/sam")}));
        write(dir.filePath("home/sam/notes.txt"), "one\ntwo\n");
        QVERIFY(restic({"backup", "-q", "--time", "2026-11-02 10:00:00", "sam"}, dir.filePath("home")));

        ResticConfig config;
        config.environment = environment;
        config.repository = dir.filePath("repo");
        Repository repository(config, dir.filePath("runtime"));
        QSignalSpy mounted(&repository, &Repository::mountChanged);
        repository.open();
        QTRY_VERIFY_WITH_TIMEOUT(repository.mountState() == Repository::State::Ready ||
                                     repository.mountState() == Repository::State::Failed, 60000);
        QVERIFY2(repository.mountState() == Repository::State::Ready, qPrintable(repository.error()));
        QCOMPARE(repository.snapshots().size(), 2);

        const QString notes = dir.filePath("home/sam/notes.txt");
        QCOMPARE(read(repository.sourcePath(repository.snapshots().at(0), notes)), QByteArray("one\ntwo\n"));
        QCOMPARE(read(repository.sourcePath(repository.snapshots().at(1), notes)), QByteArray("one\n"));

        QVector<Signature> signatures;
        for (const Snapshot &snapshot : repository.snapshots())
            signatures << signatureOf(repository.sourcePath(snapshot, notes));
        QCOMPARE(mapVersions(signatures, signatureOf(notes)).count, 2);

        const QString mountPoint = repository.mountPoint();
        repository.close();
        QVERIFY(!QFileInfo::exists(mountPoint));
    }

    // Omarchy

    void themeFallsBackToADarkWindow()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#ffd60a"));
        QCOMPARE(theme.accentForeground(), QString("black"));
        QVERIFY(theme.dark());
    }

    void themeFollowsALightPalette()
    {
        QTemporaryDir dir;
        write(dir.filePath("theme/colors.toml"),
              "mode = \"light\"\naccent = \"#3760bf\"\nbackground = \"#e9ecf5\"\nforeground = \"#080808\"\n"
              "red = \"#ad3459\"\ngreen = \"#456a27\"\n");
        Theme theme(dir.path());
        QVERIFY(!theme.dark());
        QCOMPARE(theme.accent(), QString("#3760bf"));
        QCOMPARE(theme.accentForeground(), QString("white"));
        QCOMPARE(theme.background(), QString("#e9ecf5"));
        QCOMPARE(theme.removed(), QString("#ad3459"));
        QCOMPARE(theme.added(), QString("#456a27"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        write(response, "{\"int\":0}");

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        write(response, "{\"int\":6}");
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void shippedCatalogsCoverEveryString()
    {
        // The Dutch catalog is compiled from src/i18n; every key must be a
        // string or plural object, and plural strings need English forms too.
        const QString root = QFINDTESTDATA("../src/i18n");
        const QJsonObject en = QJsonDocument::fromJson(read(root + "/en.json")).object();
        const QJsonObject nl = QJsonDocument::fromJson(read(root + "/nl.json")).object();
        QVERIFY(!nl.isEmpty());
        for (auto it = nl.begin(); it != nl.end(); ++it) {
            QVERIFY2(it.value().isString() || it.value().isObject(), qPrintable(it.key()));
            if (it.key().contains("%n"))
                QVERIFY2(en.contains(it.key()), qPrintable(it.key()));
        }
    }
};

QTEST_MAIN(RewindTests)
#include "rewind_tests.moc"
