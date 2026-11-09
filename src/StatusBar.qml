import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Where the backups live, when the last one ran and the next one will, and a
// button to make one now through the user's own backup service.
Rectangle {
    id: bar
    signal showLog()
    signal showBackups()
    implicitHeight: 38
    color: theme.surface

    readonly property var backups: backend.backups

    Rectangle { width: parent.width; height: 1; color: theme.border }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 10
        spacing: 12

        Label { text: ""; font.family: win.iconFont; color: theme.muted }
        Label {
            Layout.maximumWidth: 280
            text: backend.repository
            elide: Text.ElideMiddle
            color: theme.muted
            font.pixelSize: 12
        }

        Rectangle { width: 1; height: 16; color: theme.border }

        // Restoring has the bar while it runs.
        RowLayout {
            visible: backend.restoring
            spacing: 10
            Label { text: qsTr("Restoring…"); font.pixelSize: 12; color: theme.foreground }
            ProgressBar { Layout.preferredWidth: 160; value: backend.restoreProgress }
            Label {
                text: qsTr("Cancel")
                color: theme.accent
                font.pixelSize: 12
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: backend.cancelRestore() }
            }
        }

        RowLayout {
            visible: !backend.restoring
            spacing: 8
            BusyIndicator {
                visible: bar.backups.running
                running: visible
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                padding: 0
            }
            Label {
                font.pixelSize: 12
                font.underline: statusMouse.containsMouse
                color: bar.backups.lastFailed && !bar.backups.running ? theme.removed : theme.muted
                // The whole story is in the Backups view.
                MouseArea {
                    id: statusMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: bar.showBackups()
                }
                text: {
                    backend.clock; translator.language
                    if (!bar.backups.available)
                        return qsTr("No backup service named %1 was found").arg(bar.backups.unit)
                    if (bar.backups.running)
                        return qsTr("Backing up…")
                    let parts = []
                    if (bar.backups.lastFinished.getTime && !isNaN(bar.backups.lastFinished.getTime()))
                        parts.push(bar.backups.lastFailed ? qsTr("Last backup failed %1").arg(backend.formatAgo(bar.backups.lastFinished))
                                                         : qsTr("Last backup %1").arg(backend.formatAgo(bar.backups.lastFinished)))
                    if (bar.backups.nextRun.getTime && !isNaN(bar.backups.nextRun.getTime()))
                        parts.push(qsTr("next %1").arg(backend.formatFuture(bar.backups.nextRun)))
                    return parts.join("  ·  ")
                }
            }
            Label {
                visible: bar.backups.available
                text: qsTr("Log")
                color: theme.accent
                font.pixelSize: 12
                font.underline: logMouse.containsMouse
                MouseArea { id: logMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: bar.showLog() }
            }
        }

        Item { Layout.fillWidth: true }

        ActionButton {
            text: bar.backups.running ? qsTr("Backing up…") : qsTr("Back up now")
            key: "B"
            small: true
            visible: bar.backups.available
            enabled: !bar.backups.running
            onClicked: bar.backups.start()
        }
    }
}
