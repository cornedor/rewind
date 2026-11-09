import QtQuick
import QtQuick.Controls

// A short message at the bottom that goes away on its own.
Rectangle {
    id: toast
    function show(message) {
        label.text = message
        opacity = 1
        hide.restart()
    }

    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: 56
    width: Math.min(parent.width - 80, label.implicitWidth + 40)
    height: label.implicitHeight + 22
    radius: theme.radius
    color: theme.foreground
    opacity: 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 180 } }

    Label {
        id: label
        anchors.centerIn: parent
        width: Math.min(implicitWidth, toast.parent.width - 120)
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        color: theme.background
        font.pixelSize: 14
    }
    Timer { id: hide; interval: 4500; onTriggered: toast.opacity = 0 }
    MouseArea { anchors.fill: parent; onClicked: toast.opacity = 0 }
}
