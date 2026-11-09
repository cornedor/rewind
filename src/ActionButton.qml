import QtQuick
import QtQuick.Controls

// A labelled button with its keyboard key beside the label.
Rectangle {
    id: button
    property string text
    property string key
    property bool primary: false
    property bool small: false
    signal clicked()

    implicitHeight: small ? 30 : 36
    implicitWidth: row.implicitWidth + (small ? 18 : 24)
    radius: Math.min(6, theme.radius)
    opacity: enabled ? 1 : 0.4
    color: primary ? (mouse.containsMouse ? Qt.lighter(theme.accent, 1.08) : theme.accent)
                   : (mouse.containsMouse ? theme.raised : theme.surface)
    border.width: primary ? 0 : 1
    border.color: theme.border

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 8
        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: button.text
            font.pixelSize: button.small ? 12 : 14
            font.weight: Font.DemiBold
            color: button.primary ? theme.accentForeground : theme.foreground
        }
        KeyCap {
            anchors.verticalCenter: parent.verticalCenter
            visible: button.key.length > 0
            key: button.key
            tint: button.primary ? theme.accentForeground : theme.muted
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: if (button.enabled) button.clicked()
    }
}
