import QtQuick
import QtQuick.Controls

// A keyboard key, as in "Space" or "[", shown next to what it does.
Rectangle {
    id: cap
    property string key
    property color tint: theme.foreground
    implicitWidth: Math.max(22, label.implicitWidth + 12)
    implicitHeight: 22
    radius: Math.min(4, theme.radius)
    color: "transparent"
    border.width: 1
    border.color: Qt.alpha(tint, 0.45)
    Label {
        id: label
        anchors.centerIn: parent
        text: cap.key
        font.pixelSize: 11
        font.weight: Font.DemiBold
        color: cap.tint
    }
}
