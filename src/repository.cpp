#include "repository.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <csignal>

Repository::Repository(const ResticConfig &config, const QString &runtimeDirectory, QObject *parent)
    : QObject(parent), m_config(config) {
    m_runtimeDirectory = runtimeDirectory.isEmpty()
                             ? QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + "/rewind"
                             : runtimeDirectory;
    m_restic = QStandardPaths::findExecutable("restic");
    m_mountPoint = m_runtimeDirectory + "/mnt-" + QString::number(QCoreApplication::applicationPid());
}

Repository::~Repository() {
    close();
}

void Repository::fail(const QString &message) {
    if (m_error.isEmpty())
        m_error = message;
}

QProcessEnvironment Repository::processEnvironment() const {
    QProcessEnvironment environment = m_config.environment;
    // restic's FUSE library runs `fusermount`, which only FUSE 2 ships. Arch
    // ships FUSE 3, whose fusermount3 speaks the same protocol, so give restic
    // a private PATH entry that points the old name at it.
    if (QStandardPaths::findExecutable("fusermount").isEmpty() && !fusermount().isEmpty()) {
        const QString shim = m_runtimeDirectory + "/bin";
        QDir().mkpath(shim);
        QFile::link(fusermount(), shim + "/fusermount");
        environment.insert("PATH", shim + ':' + environment.value("PATH", qEnvironmentVariable("PATH")));
    }
    return environment;
}

QString Repository::fusermount() const {
    const QString three = QStandardPaths::findExecutable("fusermount3");
    return three.isEmpty() ? QStandardPaths::findExecutable("fusermount") : three;
}

void Repository::open() {
    m_error.clear();
    if (!m_config.error.isEmpty()) {
        fail(m_config.error);
        m_snapshotsState = m_mountState = State::Failed;
        emit snapshotsChanged();
        emit mountChanged();
        return;
    }
    if (m_restic.isEmpty()) {
        fail(tr("restic isn't installed. Install it with: omarchy-pkg-add restic"));
        m_snapshotsState = m_mountState = State::Failed;
        emit snapshotsChanged();
        emit mountChanged();
        return;
    }
    loadSnapshots();
    startMount();
}

void Repository::reopen() {
    stopMount();
    open();
}

void Repository::close() {
    if (m_snapshotsProcess) {
        m_snapshotsProcess->disconnect(this);
        m_snapshotsProcess->kill();
        m_snapshotsProcess->waitForFinished(1000);
        m_snapshotsProcess->deleteLater();
        m_snapshotsProcess = nullptr;
    }
    stopMount();
}

void Repository::loadSnapshots() {
    if (m_snapshotsProcess)
        return;
    m_snapshotsState = State::Loading;
    emit snapshotsChanged();

    auto *process = new QProcess(this);
    m_snapshotsProcess = process;
    process->setProcessEnvironment(processEnvironment());
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        m_snapshotsProcess = nullptr;
        process->deleteLater();
        if (code != 0 || status != QProcess::NormalExit) {
            fail(explainFailure(process->readAllStandardError()));
            m_snapshotsState = State::Failed;
            emit snapshotsChanged();
            return;
        }
        QString error;
        m_snapshots = Restic::parseSnapshots(process->readAllStandardOutput(), QSysInfo::machineHostName(), &error);
        if (!error.isEmpty())
            fail(error);
        m_snapshotsState = error.isEmpty() ? State::Ready : State::Failed;
        emit snapshotsChanged();
        resolveTreeRoots();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_snapshotsProcess = nullptr;
        process->deleteLater();
        fail(tr("Couldn't run restic: %1").arg(process->errorString()));
        m_snapshotsState = State::Failed;
        emit snapshotsChanged();
    });
    process->start(m_restic, {"snapshots", "--json", "--no-lock"});
}

void Repository::startMount() {
    if (m_mount)
        return;
    if (fusermount().isEmpty() || !QFileInfo::exists("/dev/fuse")) {
        fail(tr("FUSE isn't available, so backups can't be opened. Install it with: omarchy-pkg-add fuse3"));
        m_mountState = State::Failed;
        emit mountChanged();
        return;
    }

    cleanStaleMounts();
    QDir().mkpath(m_mountPoint);
    m_serving = false;
    m_mountState = State::Loading;
    m_mountErrors.clear();
    emit mountChanged();

    m_mount = new QProcess(this);
    m_mount->setProcessEnvironment(processEnvironment());
    connect(m_mount, &QProcess::readyReadStandardOutput, this, [this] {
        const QByteArray output = m_mount->readAllStandardOutput();
        if (m_mountState == State::Loading && output.contains("Now serving")) {
            m_serving = true;
            resolveTreeRoots();
        }
    });
    connect(m_mount, &QProcess::readyReadStandardError, this, [this] {
        m_mountErrors += m_mount->readAllStandardError();
        m_mountErrors = m_mountErrors.right(8192);
    });
    connect(m_mount, &QProcess::finished, this, [this] {
        m_mountErrors += m_mount->readAllStandardError();
        m_mount->deleteLater();
        m_mount = nullptr;
        fail(explainFailure(m_mountErrors));
        m_mountState = State::Failed;
        emit mountChanged();
    });
    connect(m_mount, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        fail(tr("Couldn't run restic: %1").arg(m_mount->errorString()));
        m_mount->deleteLater();
        m_mount = nullptr;
        m_mountState = State::Failed;
        emit mountChanged();
    });
    // The mount takes restic's shared lock, like a backup does, so a prune
    // waits until Rewind closes instead of removing data it is showing.
    m_mount->start(m_restic, {"mount", m_mountPoint});
}

void Repository::stopMount() {
    if (m_mount) {
        m_mount->disconnect(this);
        // restic unmounts itself on SIGTERM.
        m_mount->terminate();
        if (!m_mount->waitForFinished(3000)) {
            if (!fusermount().isEmpty())
                QProcess::execute(fusermount(), {"-u", "-z", m_mountPoint});
            if (!m_mount->waitForFinished(2000))
                m_mount->kill();
            m_mount->waitForFinished(1000);
        }
        delete m_mount;
        m_mount = nullptr;
    }
    m_serving = false;
    if (isMounted(m_mountPoint) && !fusermount().isEmpty())
        QProcess::execute(fusermount(), {"-u", "-z", m_mountPoint});
    QDir().rmdir(m_mountPoint);
    if (m_mountState != State::Idle) {
        m_mountState = State::Idle;
        emit mountChanged();
    }
}

bool Repository::isMounted(const QString &path) const {
    QFile mounts("/proc/self/mountinfo");
    if (!mounts.open(QIODevice::ReadOnly))
        return false;
    const QByteArray needle = ' ' + QFile::encodeName(path).replace(" ", "\\040") + ' ';
    return mounts.readAll().contains(needle);
}

// A crashed Rewind leaves its mount behind, which then fails every access
// with "Transport endpoint is not connected". Clear those up on the next start.
void Repository::cleanStaleMounts() {
    const QDir runtime(m_runtimeDirectory);
    for (const QString &name : runtime.entryList({"mnt-*"}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        const qint64 pid = name.mid(4).toLongLong();
        if (pid == QCoreApplication::applicationPid() || (pid > 0 && ::kill(pid_t(pid), 0) == 0))
            continue;
        const QString path = runtime.filePath(name);
        if (isMounted(path))
            QProcess::execute(fusermount(), {"-u", "-z", path});
        runtime.rmdir(name);
    }
}

// The mount is ready once Rewind knows where each backed-up path sits in the
// snapshot trees, which only looking inside the mount can tell: the same
// folder backed up with an absolute and a relative path lands in different
// places. Most snapshots match on the first, full-path try.
void Repository::resolveTreeRoots() {
    if (!m_serving || m_resolving || m_snapshotsState != State::Ready || m_mountState != State::Loading)
        return;
    const QVector<Snapshot> snapshots = m_snapshots;
    const QString ids = m_mountPoint + "/ids/";
    m_resolving = true;
    auto *watcher = new QFutureWatcher<QHash<QString, QStringList>>(this);
    connect(watcher, &QFutureWatcher<QHash<QString, QStringList>>::finished, this, [this, watcher] {
        watcher->deleteLater();
        m_resolving = false;
        if (!m_serving)
            return;
        const auto roots = watcher->result();
        for (Snapshot &snapshot : m_snapshots)
            snapshot.treeRoots = roots.value(snapshot.id, snapshot.paths);
        m_mountState = State::Ready;
        emit snapshotsChanged();
        emit mountChanged();
    });
    watcher->setFuture(QtConcurrent::run([snapshots, ids] {
        QHash<QString, QStringList> roots;
        for (const Snapshot &snapshot : snapshots) {
            QStringList found;
            for (const QString &path : snapshot.paths) {
                QString root = path;
                for (const QString &candidate : Restic::candidateTreeRoots(path)) {
                    const QFileInfo info(ids + snapshot.shortId + candidate);
                    if (info.exists() || info.isSymLink()) {
                        root = candidate;
                        break;
                    }
                }
                found << root;
            }
            roots.insert(snapshot.id, found);
        }
        return roots;
    }));
}

QString Repository::snapshotRoot(const Snapshot &snapshot) const {
    if (m_mountState != State::Ready)
        return {};
    return m_mountPoint + "/ids/" + snapshot.shortId;
}

QString Repository::sourcePath(const Snapshot &snapshot, const QString &absolutePath) const {
    const QString root = snapshotRoot(snapshot);
    const QString tree = Restic::treePath(snapshot, absolutePath);
    if (root.isEmpty() || tree.isEmpty())
        return {};
    return tree == "/" ? root : root + tree;
}

void Repository::run(const QStringList &arguments,
                     std::function<void(const QByteArray &, const QString &)> done) {
    if (m_restic.isEmpty()) {
        done({}, tr("restic isn't installed."));
        return;
    }
    auto *process = new QProcess(this);
    process->setProcessEnvironment(processEnvironment());
    connect(process, &QProcess::finished, this, [process, done](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        if (code != 0 || status != QProcess::NormalExit) {
            done({}, explainFailure(process->readAllStandardError()));
            return;
        }
        done(process->readAllStandardOutput(), {});
    });
    connect(process, &QProcess::errorOccurred, this, [process, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        process->deleteLater();
        done({}, process->errorString());
    });
    process->start(m_restic, arguments);
}

void Repository::loadChanges(const Snapshot &older, const Snapshot &newer,
                             std::function<void(const QVector<Change> &, const QString &)> done) {
    run({"diff", "--json", "--no-lock", older.id, newer.id}, [older, newer, done](const QByteArray &output, const QString &error) {
        if (!error.isEmpty())
            done({}, error);
        else
            done(parseChanges(output, newer, older), {});
    });
}

void Repository::loadStats(std::function<void(const RepositoryStats &, const QString &)> done) {
    run({"stats", "--mode", "raw-data", "--json", "--no-lock"}, [done](const QByteArray &output, const QString &error) {
        done(error.isEmpty() ? Restic::parseStats(output) : RepositoryStats(), error);
    });
}

void Repository::loadRetention(const QStringList &rules,
                               std::function<void(const Retention &, const QString &)> done) {
    // --dry-run only reports; --no-lock keeps it from waiting on the mount's lock.
    run(QStringList{"forget", "--dry-run", "--no-lock", "--json"} + rules,
        [done](const QByteArray &output, const QString &error) {
            done(error.isEmpty() ? Restic::parseForget(output) : Retention(), error);
        });
}

QVector<Change> Repository::parseChanges(const QByteArray &jsonLines, const Snapshot &newer, const Snapshot &older) {
    QVector<Change> changes;
    for (const QByteArray &line : jsonLines.split('\n')) {
        const QJsonObject object = QJsonDocument::fromJson(line).object();
        if (object.value("message_type").toString() != "change")
            continue;
        const QString tree = object.value("path").toString();
        QString absolute = Restic::absolutePath(newer, tree);
        if (absolute.isEmpty())
            absolute = Restic::absolutePath(older, tree);
        if (absolute.isEmpty())
            continue;
        const QString modifier = object.value("modifier").toString();
        // A directory "changes" whenever anything in it does; the files say
        // what happened, so leave the folders out.
        if (tree.endsWith('/') && modifier == "M")
            continue;
        if (absolute.endsWith('/') && absolute.size() > 1)
            absolute.chop(1);
        changes << Change{modifier.isEmpty() ? QChar('?') : modifier.at(0), absolute};
    }
    return changes;
}

QString Repository::explainFailure(const QByteArray &stderrOutput) {
    const QStringList lines = QString::fromUtf8(stderrOutput).split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines)
        if (line.startsWith("Fatal:"))
            return line.mid(6).trimmed();
    for (auto it = lines.rbegin(); it != lines.rend(); ++it)
        if (!it->trimmed().isEmpty() && !it->startsWith("Loading snapshots"))
            return it->trimmed();
    return QCoreApplication::translate("Repository", "restic stopped without saying why.");
}
