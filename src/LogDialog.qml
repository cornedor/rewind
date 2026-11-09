import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The backup service's recent journal, read-only.
Popup {
    id: dialog
    anchors.centerIn: Overlay.overlay
    width: Math.min(900, Overlay.overlay ? Overlay.overlay.width - 80 : 900)
    height: Math.min(600, Overlay.overlay ? Overlay.overlay.height - 80 : 600)
    modal: true
    focus: true
    padding: 20
    property string text: ""

    onAboutToShow: { text = qsTr("Loading…"); backend.backups.loadLog() }
    Connections {
        target: backend.backups
        function onLogLoaded(log) { dialog.text = log.length ? log : qsTr("The journal has nothing for this service.") }
    }

    background: Rectangle { radius: theme.radius; color: theme.raised; border.width: 1; border.color: theme.border }

    contentItem: ColumnLayout {
        spacing: 12
        Label {
            text: qsTr("Backup log: %1").arg(backend.backups.unit)
            font.pixelSize: 17
            font.weight: Font.DemiBold
            color: theme.foreground
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            TextArea {
                readOnly: true
                text: dialog.text
                font.family: "monospace"
                font.pixelSize: 12
                wrapMode: TextEdit.Wrap
                color: theme.foreground
                background: null
            }
        }
    }
}
