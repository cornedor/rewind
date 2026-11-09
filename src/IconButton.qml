import QtQuick
import QtQuick.Controls

// A square, flat button with a Nerd Font glyph and a tooltip.
Rectangle {
    id: button
    property string glyph
    property string tip
    property bool checked: false
    property int size: 34
    signal clicked()

    implicitWidth: size
    implicitHeight: size
    radius: win.small
    opacity: enabled ? 1 : 0.35
    color: checked ? Qt.alpha(theme.accent, 0.18) : mouse.containsMouse ? theme.raised : "transparent"
    border.width: checked ? 1 : 0
    border.color: Qt.alpha(theme.accent, 0.6)

    Label {
        anchors.centerIn: parent
        text: button.glyph
        font.family: win.iconFont
        font.pixelSize: 15
        color: button.checked ? theme.accent : theme.foreground
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: button.clicked()
    }

    ToolTip.visible: mouse.containsMouse && tip.length > 0
    ToolTip.delay: 500
    ToolTip.text: tip
}
