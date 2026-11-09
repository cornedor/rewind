import QtQuick
import QtQuick.Controls

Rectangle {
    id: pill
    property string status
    readonly property string text: win.statusLabel(status)
    readonly property color tint: win.statusColor(status)
    implicitWidth: label.implicitWidth + 14
    implicitHeight: 20
    radius: Math.min(4, theme.radius)
    color: Qt.alpha(tint, 0.14)
    border.width: 1
    border.color: Qt.alpha(tint, 0.5)
    Label {
        id: label
        anchors.centerIn: parent
        text: pill.text
        font.pixelSize: 11
        font.weight: Font.DemiBold
        color: pill.tint
    }
}
