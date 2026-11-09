#include "backupservice.h"

#include <QStandardPaths>

#include "restic.h"

BackupService::BackupService(const QString &unit, QObject *parent)
    : QObject(parent),
      m_unit(!unit.isEmpty() ? unit : qEnvironmentVariable("REWIND_BACKUP_UNIT", "restic-backup")),
      m_cleanupUnit(qEnvironmentVariable("REWIND_PRUNE_UNIT", "restic-prune")) {
    connect(&m_poll, &QTimer::timeout, this, &BackupService::refresh);
    m_poll.start(30000);
    refresh();
}

BackupService::~BackupService() {
    if (m_show) {
        m_show->disconnect(this);
        m_show->kill();
        m_show->waitForFinished(1000);
    }
}

QHash<QString, QString> BackupService::parseShow(const QByteArray &output) {
    QHash<QString, QString> values;
    for (const QByteArray &line : output.split('\n')) {
        const int equals = line.indexOf('=');
        if (equals <= 0)
            continue;
        // A unit with two ExecStart= lines lists the property twice; keep both.
        const QString key = QString::fromUtf8(line.left(equals));
        const QString value = QString::fromUtf8(line.mid(equals + 1)).trimmed();
        values.insert(key, values.contains(key) ? values.value(key) + '\n' + value : value);
    }
    return values;
}

QList<QHash<QString, QString>> BackupService::parseShowBlocks(const QByteArray &output) {
    QList<QHash<QString, QString>> blocks;
    QByteArray current;
    for (const QByteArray &line : output.split('\n')) {
        if (line.trimmed().isEmpty()) {
            blocks << parseShow(current);
            current.clear();
            continue;
        }
        current += line + '\n';
    }
    blocks << parseShow(current);
    return blocks;
}

QDateTime BackupService::parseTimestamp(const QString &value) {
    // --timestamp=unix prints "@1794223051"; an unset time is empty or "n/a".
    if (!value.startsWith('@'))
        return {};
    bool ok = false;
    const qint64 seconds = value.mid(1).toLongLong(&ok);
    return ok && seconds > 0 ? QDateTime::fromSecsSinceEpoch(seconds) : QDateTime();
}

void BackupService::refresh() {
    if (m_show || QStandardPaths::findExecutable("systemctl").isEmpty())
        return;
    m_show = new QProcess(this);
    connect(m_show, &QProcess::finished, this, [this] {
        const QByteArray output = m_show->readAllStandardOutput();
        m_show->deleteLater();
        m_show = nullptr;
        // Each unit comes back as a block, separated by a blank line.
        const auto blocks = parseShowBlocks(output);
        applyCleanup(blocks.value(2), blocks.value(3));
        apply(blocks.value(0), blocks.value(1));
    });
    m_show->start("systemctl", {"--user", "show", "--timestamp=unix", m_unit + ".service", m_unit + ".timer",
                                m_cleanupUnit + ".service", m_cleanupUnit + ".timer",
                                "-p", "LoadState,ActiveState,Result,ExecMainStartTimestamp,ExecMainExitTimestamp,"
                                      "ConditionResult,ConditionTimestamp,NextElapseUSecRealtime,ExecStart"});
}

void BackupService::apply(const QHash<QString, QString> &service, const QHash<QString, QString> &timer) {
    const bool wasRunning = m_running;
    const QDateTime previousFinish = m_lastFinished;
    const bool available = service.value("LoadState") == "loaded";
    const QString active = service.value("ActiveState");
    const bool running = active == "activating" || active == "active" || active == "reloading";
    const bool failed = active == "failed" || (!service.value("Result").isEmpty() && service.value("Result") != "success");
    const QDateTime last = parseTimestamp(service.value("ExecMainExitTimestamp"));
    const QDateTime next = timer.value("LoadState") == "loaded" ? parseTimestamp(timer.value("NextElapseUSecRealtime"))
                                                                : QDateTime();
    const QDateTime started = running ? parseTimestamp(service.value("ExecMainStartTimestamp")) : QDateTime();
    // A run whose conditions failed (on battery, offline) is newer than the
    // last one that ran.
    const QDateTime checked = parseTimestamp(service.value("ConditionTimestamp"));
    const bool conditionFailed = service.value("ConditionResult") == "no" && checked.isValid() &&
                         (!last.isValid() || checked > last);
    if (available == m_available && running == m_running && failed == m_lastFailed && last == m_lastFinished &&
        next == m_nextRun && started == m_started && conditionFailed == m_lastSkipped)
        return;
    m_started = started;
    m_lastSkipped = conditionFailed;
    m_available = available;
    m_running = running;
    m_lastFailed = failed;
    m_lastFinished = last;
    m_nextRun = next;
    // Watch closely while a backup runs, so the timeline updates as it ends.
    m_poll.setInterval(running ? 2000 : 30000);
    emit changed();
    // A unit whose conditions fail (on battery, offline) ends without running,
    // which leaves its last exit time where it was.
    if (wasRunning && !running)
        last != previousFinish ? emit finished(!failed) : emit skipped();
}

void BackupService::applyCleanup(const QHash<QString, QString> &service, const QHash<QString, QString> &timer) {
    const bool available = service.value("LoadState") == "loaded";
    const QDateTime next = available && timer.value("LoadState") == "loaded"
                               ? parseTimestamp(timer.value("NextElapseUSecRealtime")) : QDateTime();
    const QDateTime last = available ? parseTimestamp(service.value("ExecMainExitTimestamp")) : QDateTime();
    QStringList arguments;
    for (const QStringList &argv : Restic::parseExecStart(service.value("ExecStart"))) {
        arguments = Restic::retentionArguments(argv);
        if (!arguments.isEmpty())
            break;
    }
    const bool rulesChanged = arguments != m_retentionArguments;
    if (available == m_cleanupAvailable && next == m_cleanupNextRun && last == m_cleanupLastRun && !rulesChanged)
        return;
    m_cleanupAvailable = available;
    m_cleanupNextRun = next;
    m_cleanupLastRun = last;
    m_retentionArguments = arguments;
    emit changed();
    if (rulesChanged)
        emit retentionChanged();
}

void BackupService::start() {
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process](int code) {
        process->deleteLater();
        if (code != 0)
            emit failedToStart(QString::fromUtf8(process->readAllStandardError()).trimmed());
        refresh();
    });
    process->start("systemctl", {"--user", "start", "--no-block", m_unit + ".service"});
    m_running = true;
    m_poll.setInterval(2000);
    emit changed();
}

void BackupService::loadLog() {
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process] {
        process->deleteLater();
        emit logLoaded(QString::fromUtf8(process->readAllStandardOutput()).trimmed());
    });
    process->start("journalctl", {"--user", "-u", m_unit + ".service", "-n", "60", "-o", "short-iso", "--no-pager"});
}
