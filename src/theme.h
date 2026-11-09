#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QTimer>

// Follows the current Omarchy theme live: the accent, the whole palette (so a
// light theme gives a light window), and Hyprland's window rounding.
class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(QString background READ background NOTIFY changed)
    Q_PROPERTY(QString surface READ surface NOTIFY changed)
    Q_PROPERTY(QString raised READ raised NOTIFY changed)
    Q_PROPERTY(QString border READ border NOTIFY changed)
    Q_PROPERTY(QString foreground READ foreground NOTIFY changed)
    Q_PROPERTY(QString muted READ muted NOTIFY changed)
    Q_PROPERTY(QString selection READ selection NOTIFY changed)
    Q_PROPERTY(QString selectionForeground READ selectionForeground NOTIFY changed)
    Q_PROPERTY(QString added READ added NOTIFY changed)
    Q_PROPERTY(QString removed READ removed NOTIFY changed)
    Q_PROPERTY(QString changedColor READ changedColor NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)

public:
    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr, const QString &hyprctl = {});
    ~Theme() override;

    bool dark() const { return m_dark; }
    QString accent() const { return name("accent"); }
    QString accentForeground() const;
    QString background() const { return name("background"); }
    QString surface() const { return name("surface"); }
    QString raised() const { return name("raised"); }
    QString border() const { return name("border"); }
    QString foreground() const { return name("foreground"); }
    QString muted() const { return name("muted"); }
    QString selection() const { return name("selection"); }
    QString selectionForeground() const { return name("selection_foreground"); }
    QString added() const { return name("green"); }
    QString removed() const { return name("red"); }
    QString changedColor() const { return name("yellow"); }
    int radius() const { return m_radius; }

    // Reads every "key = \"#rrggbb\"" pair and the mode from colors.toml.
    static QHash<QString, QString> readColors(const QString &path, QString *mode = nullptr);
    static QString readAccent(const QString &path);
    // Fills in what a theme leaves out, so every color the window uses exists.
    static QHash<QString, QColor> resolvePalette(const QHash<QString, QString> &colors, const QString &mode,
                                                 bool *dark = nullptr);

signals:
    void changed();

private:
    void reload();
    void refreshRounding();
    QString name(const char *key) const { return m_palette.value(key).name(); }

    QString m_directory;
    QHash<QString, QColor> m_palette;
    bool m_dark = true;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_hyprctl;
    int m_radius = 0;
    QProcess m_roundingProcess;
    QTimer m_roundingPoll, m_roundingTimeout;
};
