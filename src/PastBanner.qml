import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The band that says which moment the window shows when it isn't now.
Rectangle {
    id: banner
    signal showChanges()

    implicitHeight: win.past ? 46 : 0
    clip: true
    color: theme.accent
    Behavior on implicitHeight { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 12
        spacing: 10

        Label {
            text: ""
            font.family: win.iconFont
            font.pixelSize: 18
            color: theme.accentForeground
        }
        Label {
            Layout.fillWidth: true
            elide: Text.ElideRight
            textFormat: Text.StyledText
            color: theme.accentForeground
            font.pixelSize: 15
            text: win.past
                  ? qsTr("Backup of <b>%1</b>").arg(backend.moment.title) + "  ·  " + backend.moment.ago
                    + "  ·  " + qsTr("%1 of %2").arg(backend.moment.index + 1).arg(backend.moment.total)
                  : ""
        }

        BannerButton {
            text: qsTr("Older")
            key: "["
            enabled: backend.moment.hasOlder === true
            onClicked: backend.older()
        }
        BannerButton {
            text: qsTr("Newer")
            key: "]"
            onClicked: backend.newer()
        }
        BannerButton {
            text: qsTr("What changed")
            key: "W"
            onClicked: banner.showChanges()
        }
        BannerButton {
            text: qsTr("Back to now")
            key: "Esc"
            strong: true
            onClicked: backend.showSnapshot(-1)
        }
    }

    component BannerButton: Rectangle {
        id: button
        property string text
        property string key
        property bool strong: false
        signal clicked()
        implicitHeight: 32
        implicitWidth: row.implicitWidth + 20
        radius: win.small
        opacity: enabled ? 1 : 0.45
        color: strong ? theme.accentForeground : (mouse.containsMouse ? Qt.alpha(theme.accentForeground, 0.14) : "transparent")
        border.width: strong ? 0 : 1
        border.color: Qt.alpha(theme.accentForeground, 0.4)
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 8
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: button.text
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: button.strong ? theme.accent : theme.accentForeground
            }
            KeyCap {
                anchors.verticalCenter: parent.verticalCenter
                key: button.key
                tint: button.strong ? theme.accent : theme.accentForeground
            }
        }
        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: button.clicked()
        }
    }
}
