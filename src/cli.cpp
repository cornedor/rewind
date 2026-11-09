// Commands that need no display, so agents and scripts can use Rewind too.
// The shape follows Hype's src/cli.cpp (MIT, David Heinemeier Hansson).
#include "cli.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <cstdio>

#include "files.h"
#include "repository.h"

namespace {
void print(FILE *stream, const QString &text) {
    std::fputs(qPrintable(text + '\n'), stream);
}

int fail(const QString &message) {
    print(stderr, message);
    return 1;
}

void emitJson(const QJsonValue &value) {
    const QByteArray json = value.isArray() ? QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)
                                            : QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    print(stdout, QString::fromUtf8(json));
}

QString envFile() {
    return QDir::homePath() + "/.config/restic/env";
}

QString absolute(const QString &path) {
    const QString expanded = path.startsWith("~") ? QDir::homePath() + path.mid(1) : path;
    return QDir::cleanPath(QFileInfo(expanded).absoluteFilePath());
}

// Opens the repository and waits until the snapshots, and the mount when
// asked for, are ready or have failed.
bool openRepository(Repository &repository, bool needMount, QString *error) {
    QEventLoop loop;
    auto check = [&] {
        const bool snapshotsDone = repository.snapshotsState() == Repository::State::Ready ||
                                   repository.snapshotsState() == Repository::State::Failed;
        const bool mountDone = !needMount || repository.mountState() == Repository::State::Ready ||
                               repository.mountState() == Repository::State::Failed;
        if (snapshotsDone && mountDone)
            loop.quit();
    };
    QObject::connect(&repository, &Repository::snapshotsChanged, &loop, check);
    QObject::connect(&repository, &Repository::mountChanged, &loop, check);
    repository.open();
    QTimer::singleShot(0, &loop, check);
    loop.exec();
    if (repository.snapshotsState() != Repository::State::Ready ||
        (needMount && repository.mountState() != Repository::State::Ready)) {
        *error = repository.error();
        return false;
    }
    return true;
}

QJsonObject snapshotJson(const Snapshot &snapshot) {
    return {{"id", snapshot.shortId},
            {"time", snapshot.time.toString(Qt::ISODate)},
            {"host", snapshot.hostname},
            {"paths", QJsonArray::fromStringList(snapshot.paths)}};
}

struct Command {
    QCommandLineParser parser;
    Command(const QString &description, bool json = true) {
        parser.setApplicationDescription(description);
        parser.addHelpOption();
        if (json)
            parser.addOption({"json", "Print JSON."});
    }
};

int snapshots(const QStringList &arguments) {
    Command command("List backups, newest first.");
    command.parser.process(arguments);
    Repository repository(Restic::loadConfig(QProcessEnvironment::systemEnvironment(), envFile()));
    QString error;
    if (!openRepository(repository, false, &error))
        return fail(error);
    if (command.parser.isSet("json")) {
        QJsonArray list;
        for (const Snapshot &snapshot : repository.snapshots())
            list << snapshotJson(snapshot);
        emitJson(list);
        return 0;
    }
    for (const Snapshot &snapshot : repository.snapshots())
        print(stdout, snapshot.shortId + "  " + snapshot.time.toString("yyyy-MM-dd HH:mm") + "  " +
                          snapshot.paths.join(", "));
    return 0;
}

int versions(const QStringList &arguments) {
    Command command("List the distinct versions of a file or folder in the backups.");
    command.parser.addPositionalArgument("path", "The file or folder, as it is (or was) on disk.");
    command.parser.process(arguments);
    if (command.parser.positionalArguments().isEmpty())
        return fail("Usage: rewind versions <path> [--json]");
    const QString path = absolute(command.parser.positionalArguments().first());

    Repository repository(Restic::loadConfig(QProcessEnvironment::systemEnvironment(), envFile()));
    QString error;
    if (!openRepository(repository, true, &error))
        return fail(error);
    QVector<Signature> signatures;
    for (const Snapshot &snapshot : repository.snapshots())
        signatures << signatureOf(repository.sourcePath(snapshot, path));
    const Signature now = signatureOf(path);
    const VersionMap map = mapVersions(signatures, now);

    // One row per version, at the newest backup that holds it.
    QJsonArray list;
    int previous = -1;
    for (int i = 0; i < map.version.size(); ++i) {
        const int version = map.version.at(i);
        if (version == 0 || version == previous)
            continue;
        previous = version;
        const Snapshot &snapshot = repository.snapshots().at(i);
        const Signature &signature = signatures.at(i);
        list << QJsonObject{{"version", version},
                            {"snapshot", snapshot.shortId},
                            {"time", snapshot.time.toString(Qt::ISODate)},
                            {"size", signature.size},
                            {"modified", QDateTime::fromSecsSinceEpoch(signature.modified).toString(Qt::ISODate)},
                            {"current", version == map.nowMatches}};
    }
    if (command.parser.isSet("json")) {
        emitJson(QJsonObject{{"path", path}, {"existsNow", now.exists}, {"versions", list}});
        return 0;
    }
    if (list.isEmpty())
        return fail(path + " isn't in any backup.");
    for (const QJsonValue &value : list) {
        const QJsonObject row = value.toObject();
        print(stdout, QStringLiteral("v%1  %2  %3  %4 bytes%5")
                          .arg(row.value("version").toInt())
                          .arg(row.value("snapshot").toString(), row.value("time").toString())
                          .arg(row.value("size").toInteger())
                          .arg(row.value("current").toBool() ? "  (same as now)" : ""));
    }
    return 0;
}

int restore(const QStringList &arguments) {
    Command command("Restore a file or folder from a backup. Keeps the current one unless --replace is given.");
    command.parser.addPositionalArgument("snapshot", "The backup's id, or latest.");
    command.parser.addPositionalArgument("path", "The file or folder, as it is (or was) on disk.");
    command.parser.addOption({"replace", "Replace the current version. It is kept in ~/.local/state/rewind/holding."});
    command.parser.process(arguments);
    const QStringList positional = command.parser.positionalArguments();
    if (positional.size() != 2)
        return fail("Usage: rewind restore <snapshot> <path> [--replace] [--json]");
    const QString path = absolute(positional.at(1));

    Repository repository(Restic::loadConfig(QProcessEnvironment::systemEnvironment(), envFile()));
    QString error;
    if (!openRepository(repository, true, &error))
        return fail(error);
    const Snapshot *snapshot = nullptr;
    for (const Snapshot &candidate : repository.snapshots())
        if (positional.first() == "latest" ? &candidate == &repository.snapshots().first()
                                           : candidate.id.startsWith(positional.first()))
            snapshot = &candidate;
    if (!snapshot)
        return fail("No backup " + positional.first());
    const QString source = repository.sourcePath(*snapshot, path);
    if (source.isEmpty() || (!QFileInfo::exists(source) && !QFileInfo(source).isSymLink()))
        return fail(path + " isn't in backup " + snapshot->shortId);

    const bool existsNow = QFileInfo::exists(path) || QFileInfo(path).isSymLink();
    const bool replace = existsNow && command.parser.isSet("replace");
    const QString target = !existsNow || replace
                               ? path
                               : freePath(QFileInfo(path).dir().filePath(
                                     restoredCopyName(QFileInfo(path).fileName(), snapshot->time, "restored")));
    RestoreOperation operation;
    RestoreProgress progress;
    const QString holding = QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + "/rewind/holding";
    if (!restoreTo(source, target, replace, holding, &progress, &operation, &error))
        return fail(error);
    if (command.parser.isSet("json")) {
        emitJson(QJsonObject{{"restored", target}, {"snapshot", snapshot->shortId}, {"replacedKeptAt", operation.heldOriginal}});
        return 0;
    }
    print(stdout, "Restored " + target);
    if (!operation.heldOriginal.isEmpty())
        print(stdout, "The replaced version is kept at " + operation.heldOriginal);
    return 0;
}

// As the hey and basecamp tools do: one shared copy, linked into Claude Code,
// found directly by Codex.
int skill(const QStringList &arguments) {
    Command command("Print the agent skill, or install it for your coding agents with: skill install.", false);
    command.parser.addPositionalArgument("install", "Copy to ~/.agents/skills/rewind and link into ~/.claude/skills",
                                         "[install]");
    command.parser.process(arguments);
    QFile bundled(":/skill.md");
    if (!bundled.open(QIODevice::ReadOnly))
        return 1;
    const QByteArray text = bundled.readAll();
    if (command.parser.positionalArguments().isEmpty()) {
        std::fputs(text.constData(), stdout);
        return 0;
    }
    if (command.parser.positionalArguments().first() != "install")
        return fail("Use rewind skill to print the skill, or rewind skill install.");
    const QDir home = QDir::home();
    const QString directory = home.filePath(".agents/skills/rewind");
    QSaveFile file(directory + "/SKILL.md");
    if (!QDir().mkpath(directory) || !file.open(QIODevice::WriteOnly) || file.write(text) < 0 || !file.commit())
        return fail("Could not write " + file.fileName());
    print(stdout, "Installed " + file.fileName());
    if (!home.exists(".claude"))
        return 0;
    const QString link = home.filePath(".claude/skills/rewind");
    const QFileInfo existing(link);
    if (existing.isSymLink())
        QFile::remove(link);
    else if (existing.exists())
        return fail(link + " exists and is not a link; leaving it alone.");
    if (!home.mkpath(".claude/skills") || !QFile::link("../../.agents/skills/rewind", link))
        return fail("Could not link " + link);
    print(stdout, "Linked " + link);
    return 0;
}

int help() {
    print(stdout, "Rewind: browse your restic backups like a time machine.\n\n"
                  "Usage:\n"
                  "  rewind [path]                     Open the window at a folder or file\n"
                  "  rewind open [path]                The same\n"
                  "  rewind snapshots [--json]         List backups, newest first\n"
                  "  rewind versions <path> [--json]   List the versions of a file in the backups\n"
                  "  rewind restore <snapshot> <path> [--replace] [--json]\n"
                  "                                    Restore beside the current file, or replace it\n"
                  "  rewind skill [install]            Print the skill for coding agents, or install it\n\n"
                  "The repository comes from ~/.config/restic/env unless RESTIC_REPOSITORY is set.");
    return 0;
}
} // namespace

bool isCliCommand(const QString &word) {
    return QStringList{"snapshots", "versions", "restore", "skill", "help", "--help", "-h"}.contains(word);
}

int runCli(const QStringList &arguments) {
    const QString command = arguments.value(1);
    return command == "snapshots" ? snapshots(arguments.mid(1))
           : command == "versions" ? versions(arguments.mid(1))
           : command == "restore"  ? restore(arguments.mid(1))
           : command == "skill"    ? skill(arguments.mid(1))
                                   : help();
}
