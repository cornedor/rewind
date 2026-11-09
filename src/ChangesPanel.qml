import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Everything one backup changed compared with the one before it.
Popup {
    id: panel
    x: parent ? parent.width - width : 0
    y: 0
    width: Math.min(520, parent ? parent.width * 0.5 : 520)
    height: parent ? parent.height : 600
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    onAboutToShow: { backend.loadChanges(); list.currentIndex = 0 }

    function activate(index) {
        const change = backend.changes[index]
        if (!change) return
        const slash = change.path.lastIndexOf("/")
        backend.goTo(slash > 0 ? change.path.substring(0, slash) : "/", change.name)
        close()
    }

    enter: Transition { NumberAnimation { property: "x"; from: panel.parent.width; to: panel.parent.width - panel.width; duration: 160; easing.type: Easing.OutCubic } }

    background: Rectangle {
        color: theme.raised
        Rectangle { width: 1; height: parent.height; color: theme.border }
    }

    contentItem: ColumnLayout {
        spacing: 0
        Keys.onUpPressed: list.decrementCurrentIndex()
        Keys.onDownPressed: list.incrementCurrentIndex()
        Keys.onReturnPressed: panel.activate(list.currentIndex)
        Keys.onEnterPressed: panel.activate(list.currentIndex)

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 20
            spacing: 4
            Label {
                text: qsTr("What changed in this backup")
                font.pixelSize: 18
                font.weight: Font.DemiBold
                color: theme.foreground
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: theme.muted
                text: backend.changesLoading ? qsTr("Comparing with the backup before it…")
                      : backend.changesError.length > 0 ? backend.changesError
                      : qsTr("%n changes since the backup before it. Enter jumps to one.", "", backend.changes.length)
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: theme.border }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            focus: true
            model: backend.changes
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            highlightMoveDuration: 0
            delegate: Rectangle {
                required property var modelData
                required property int index
                readonly property color tint: modelData.modifier === "+" ? theme.added
                                            : modelData.modifier === "-" ? theme.removed : theme.changedColor
                width: ListView.view.width
                height: 46
                color: index === list.currentIndex ? theme.selection : changeMouse.containsMouse ? Qt.alpha(theme.selection, 0.3) : "transparent"
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    spacing: 12
                    Rectangle {
                        width: 22; height: 22; radius: Math.min(4, theme.radius)
                        color: Qt.alpha(parent.parent.tint, 0.16)
                        Label {
                            anchors.centerIn: parent
                            text: modelData.modifier === "+" ? "+" : modelData.modifier === "-" ? "−" : "~"
                            font.weight: Font.Bold
                            color: parent.parent.parent.tint
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Label { Layout.fillWidth: true; text: modelData.name; elide: Text.ElideMiddle; color: index === list.currentIndex ? theme.selectionForeground : theme.foreground }
                        Label { Layout.fillWidth: true; text: modelData.folder; elide: Text.ElideMiddle; font.pixelSize: 12; color: theme.muted }
                    }
                }
                MouseArea {
                    id: changeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: panel.activate(index)
                }
            }
            BusyIndicator { anchors.centerIn: parent; running: backend.changesLoading; visible: running }
        }
    }
}
