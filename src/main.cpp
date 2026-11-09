// rewind — browse restic backups like a time machine, compare versions, and
// restore files without leaving the desktop.

#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>
#include <csignal>

#include "backend.h"
#include "backupservice.h"
#include "cli.h"
#include "overview.h"
#include "repository.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    QStringList arguments;
    for (int i = 0; i < argc; ++i)
        arguments << QString::fromLocal8Bit(argv[i]);
    const bool cli = arguments.size() > 1 && isCliCommand(arguments.at(1));
    if (cli)
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QGuiApplication app(argc, argv);
    app.setApplicationName("rewind");
    app.setApplicationVersion("0.1.0");

    // Ctrl+C in a terminal or a session logout must still unmount the backups.
    for (int signal : {SIGINT, SIGTERM, SIGHUP})
        std::signal(signal, [](int) { QCoreApplication::quit(); });

    if (cli)
        return runCli(arguments);

    // Matches the window to rewind.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("rewind");
    app.setWindowIcon(QIcon::fromTheme("rewind"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    QString start = arguments.value(1);
    if (start == "open")
        start = arguments.value(2);
    if (start.startsWith("file://"))
        start = QUrl(start).toLocalFile();

    Theme theme;
    Repository repository(Restic::loadConfig(QProcessEnvironment::systemEnvironment(),
                                             QDir::homePath() + "/.config/restic/env"));
    BackupService backups;
    Backend backend(&repository, &backups, start);
    Overview overview(&repository, &backups);

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("overview", &overview);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    repository.open();
    const int code = app.exec();
    repository.close();
    return code;
}
