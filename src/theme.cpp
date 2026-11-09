// Theme watching follows Omacut (MIT, David Heinemeier Hansson).
#include "theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <cmath>

namespace {
double luminance(const QColor &c) {
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    return .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
}

QColor mix(const QColor &a, const QColor &b, double amount) {
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * amount, a.greenF() + (b.greenF() - a.greenF()) * amount,
                            a.blueF() + (b.blueF() - a.blueF()) * amount);
}
} // namespace

Theme::Theme(const QString &directory, QObject *parent, const QString &hyprctl)
    : QObject(parent),
      m_directory(directory.isEmpty() ? QDir::homePath() + "/.local/state/omarchy/current" : directory) {
    m_palette = resolvePalette({}, {}, &m_dark);
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);

    // Corners follow Hyprland's effective rounding, which themes and personal
    // overrides can both set, so ask hyprctl rather than reading its config.
    // Without Hyprland the corners stay sharp, like Omarchy's default.
    m_hyprctl = !hyprctl.isEmpty() ? hyprctl
              : directory.isEmpty() && !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")
                  ? QStandardPaths::findExecutable("hyprctl") : QString();
    m_roundingTimeout.setSingleShot(true);
    m_roundingTimeout.setInterval(500);
    connect(&m_roundingTimeout, &QTimer::timeout, &m_roundingProcess, &QProcess::kill);
    connect(&m_roundingProcess, &QProcess::errorOccurred, this, [this] { m_roundingTimeout.stop(); });
    connect(&m_roundingProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_roundingTimeout.stop();
        const auto value = QJsonDocument::fromJson(m_roundingProcess.readAllStandardOutput()).object().value("int");
        if (code != 0 || status != QProcess::NormalExit || !value.isDouble() || value.toInt(-1) < 0)
            return;
        if (value.toInt() != m_radius) {
            m_radius = value.toInt();
            emit changed();
        }
    });
    m_roundingPoll.setInterval(1000);
    connect(&m_roundingPoll, &QTimer::timeout, this, &Theme::refreshRounding);
    if (!m_hyprctl.isEmpty())
        m_roundingPoll.start();
    reload();
}

Theme::~Theme() {
    if (m_roundingProcess.state() != QProcess::NotRunning) {
        m_roundingProcess.kill();
        m_roundingProcess.waitForFinished(1000);
    }
}

void Theme::refreshRounding() {
    if (m_hyprctl.isEmpty() || m_roundingProcess.state() != QProcess::NotRunning)
        return;
    m_roundingProcess.start(m_hyprctl, {"-j", "getoption", "decoration:rounding"});
    m_roundingTimeout.start();
}

QHash<QString, QString> Theme::readColors(const QString &path, QString *mode) {
    QHash<QString, QString> colors;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return colors;
    const QString text = QString::fromUtf8(file.readAll());
    static const QRegularExpression pair(R"re(^\s*([A-Za-z0-9_]+)\s*=\s*["'](#[0-9a-fA-F]{6})["'])re",
                                         QRegularExpression::MultilineOption);
    for (auto it = pair.globalMatch(text); it.hasNext();) {
        const auto match = it.next();
        colors.insert(match.captured(1).toLower(), match.captured(2));
    }
    static const QRegularExpression modeLine(R"re(^\s*mode\s*=\s*["'](\w+)["'])re", QRegularExpression::MultilineOption);
    if (mode) {
        const auto match = modeLine.match(text);
        *mode = match.hasMatch() ? match.captured(1).toLower() : QString();
    }
    return colors;
}

QString Theme::readAccent(const QString &path) {
    return readColors(path).value("accent", "#FFD60A");
}

QHash<QString, QColor> Theme::resolvePalette(const QHash<QString, QString> &colors, const QString &mode, bool *dark) {
    auto pick = [&](std::initializer_list<const char *> keys, const char *fallback) {
        for (const char *key : keys)
            if (colors.contains(key))
                return QColor(colors.value(key));
        return QColor(fallback);
    };

    const QColor background = pick({"background"}, "#0e0e10");
    // Older themes have no mode, so a light background decides.
    const bool isDark = mode == "light" ? false : mode == "dark" ? true : luminance(background) < .4;
    const QColor foreground = pick({"foreground"}, isDark ? "#e6e6e6" : "#1a1a1a");

    QHash<QString, QColor> palette;
    palette["accent"] = pick({"accent"}, "#FFD60A");
    palette["background"] = background;
    palette["foreground"] = foreground;
    palette["muted"] = colors.contains("muted") ? QColor(colors.value("muted")) : mix(foreground, background, .45);
    // Side panels sit a step below the window color and cards a step above,
    // using the theme's own shades when it has them.
    palette["surface"] = colors.contains("dark_background") ? QColor(colors.value("dark_background"))
                                                             : mix(background, foreground, .04);
    palette["raised"] = colors.contains("lighter_background") ? QColor(colors.value("lighter_background"))
                                                               : mix(background, foreground, .09);
    palette["border"] = mix(background, foreground, .14);
    palette["selection"] = pick({"selection"}, isDark ? "#2c3550" : "#c4d2f0");
    palette["selection_foreground"] = pick({"selection_foreground"}, isDark ? "#ffffff" : "#000000");
    palette["green"] = pick({"green", "color2"}, isDark ? "#9ece6a" : "#456a27");
    palette["red"] = pick({"red", "color1"}, isDark ? "#f7768e" : "#ad3459");
    palette["yellow"] = pick({"yellow", "color3"}, isDark ? "#e0af68" : "#80601c");
    if (dark)
        *dark = isDark;
    return palette;
}

// Black or white, whichever reads on the accent (WCAG relative luminance).
QString Theme::accentForeground() const {
    return luminance(m_palette.value("accent")) > .179 ? "black" : "white";
}

void Theme::reload() {
    refreshRounding();
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);

    // A theme switch replaces symlinks and files, so watch the parents too and
    // re-arm after every change. Watching the nearest existing ancestor means
    // installing Omarchy later works without restarting the app.
    QString ancestor = m_directory;
    while (!QFileInfo::exists(ancestor) && ancestor != "/")
        ancestor = QFileInfo(ancestor).absolutePath();
    QStringList candidates{ancestor, QFileInfo(ancestor).absolutePath(), m_directory,
                           m_directory + "/theme", m_directory + "/theme/colors.toml"};
    candidates.removeDuplicates();
    for (const auto &path : candidates)
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);

    QString mode;
    const auto colors = readColors(m_directory + "/theme/colors.toml", &mode);
    bool isDark = true;
    const auto palette = resolvePalette(colors, mode, &isDark);
    if (palette != m_palette || isDark != m_dark) {
        m_palette = palette;
        m_dark = isDark;
        emit changed();
    }
}
