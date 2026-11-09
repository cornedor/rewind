import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: bar
    implicitHeight: 52
    color: theme.surface

    signal done()
    readonly property bool filterActive: filterField.activeFocus || filterField.text.length > 0

    function openFilter() { filterField.visible = true; filterField.forceActiveFocus(); filterField.selectAll() }
    function closeFilter() { backend.filter = ""; filterField.text = ""; bar.done() }

    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.border }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 12
        spacing: 6

        // Files at a moment in time, or the backups themselves.
        Rectangle {
            implicitWidth: switchRow.implicitWidth + 6
            implicitHeight: 34
            radius: win.small + 2
            color: theme.background
            border.width: 1
            border.color: theme.border
            Row {
                id: switchRow
                anchors.centerIn: parent
                spacing: 2
                ViewTab { name: "files"; glyph: ""; text: qsTr("Files") }
                ViewTab { name: "backups"; glyph: ""; text: qsTr("Backups") }
            }
        }
        KeyCap { key: "Tab"; tint: theme.muted; Layout.rightMargin: 6 }

        IconButton {
            visible: win.view === "files"
            glyph: ""
            tip: qsTr("Up a folder (Backspace)")
            enabled: backend.folder !== "/"
            onClicked: backend.goUp()
        }

        // The path, one button per folder. It scrolls to keep the current
        // folder in view when the path is long.
        Flickable {
            id: crumbFlick
            visible: win.view === "files"
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: crumbRow.width
            clip: true
            interactive: contentWidth > width
            boundsBehavior: Flickable.StopAtBounds
            onContentWidthChanged: contentX = Math.max(0, contentWidth - width)
            onWidthChanged: contentX = Math.max(0, contentWidth - width)

            Row {
                id: crumbRow
                height: parent.height
                spacing: 0
                Repeater {
                    model: backend.crumbs
                    delegate: Row {
                        required property var modelData
                        required property int index
                        height: crumbRow.height
                        Label {
                            visible: index > 0
                            anchors.verticalCenter: parent.verticalCenter
                            text: ""
                            font.family: win.iconFont
                            color: theme.muted
                            leftPadding: 4
                            rightPadding: 4
                        }
                        Rectangle {
                            readonly property bool last: index === backend.crumbs.length - 1
                            anchors.verticalCenter: parent.verticalCenter
                            height: 32
                            width: crumbLabel.implicitWidth + 16
                            radius: win.small
                            color: crumbMouse.containsMouse ? theme.raised : "transparent"
                            Label {
                                id: crumbLabel
                                anchors.centerIn: parent
                                text: (modelData.home ? "  " : "") + modelData.name
                                font.pixelSize: 14
                                font.weight: parent.last ? Font.DemiBold : Font.Normal
                                color: parent.last ? theme.foreground : theme.muted
                            }
                            MouseArea {
                                id: crumbMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: { backend.goTo(modelData.path); bar.done() }
                            }
                        }
                    }
                }
            }
        }

        // In the Backups view: where they live.
        Label {
            visible: win.view === "backups"
            Layout.fillWidth: true
            leftPadding: 6
            text: backend.repository
            elide: Text.ElideMiddle
            font.pixelSize: 13
            color: theme.muted
        }

        TextField {
            id: filterField
            Layout.preferredWidth: 240
            Layout.alignment: Qt.AlignVCenter
            visible: bar.filterActive
            placeholderText: qsTr("Filter this folder")
            Material.containerStyle: Material.Filled
            Material.roundedScale: theme.radius
            topPadding: 6
            bottomPadding: 6
            text: backend.filter
            onTextEdited: backend.filter = text
            Keys.onReturnPressed: bar.done()
            Keys.onEnterPressed: bar.done()
            Keys.onDownPressed: bar.done()
        }

        IconButton {
            glyph: ""
            tip: qsTr("Filter (/)")
            visible: !bar.filterActive && win.view === "files"
            onClicked: bar.openFilter()
        }
        IconButton {
            glyph: backend.showHidden ? "" : ""
            visible: win.view === "files"
            tip: backend.showHidden ? qsTr("Hide hidden files (Ctrl+H)") : qsTr("Show hidden files (Ctrl+H)")
            checked: backend.showHidden
            onClicked: backend.showHidden = !backend.showHidden
        }
        IconButton {
            glyph: ""
            visible: win.view === "files"
            tip: win.past ? qsTr("Only what changed or was deleted since (F)") : qsTr("Only what isn't backed up yet (F)")
            checked: backend.onlyChanges
            onClicked: backend.onlyChanges = !backend.onlyChanges
        }
        IconButton {
            glyph: ""
            tip: qsTr("Keyboard shortcuts (?)")
            onClicked: help.visible = true
        }
    }

    component ViewTab: Rectangle {
        id: tab
        property string name
        property string glyph
        property string text
        readonly property bool active: win.view === name
        implicitWidth: tabRow.implicitWidth + 22
        implicitHeight: 28
        radius: win.small
        color: active ? theme.accent : tabMouse.containsMouse ? theme.raised : "transparent"
        Behavior on color { ColorAnimation { duration: 120 } }
        Row {
            id: tabRow
            anchors.centerIn: parent
            spacing: 7
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: tab.glyph
                font.family: win.iconFont
                font.pixelSize: 13
                color: tab.active ? theme.accentForeground : theme.muted
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: tab.text
                font.pixelSize: 13
                font.weight: tab.active ? Font.DemiBold : Font.Normal
                color: tab.active ? theme.accentForeground : theme.foreground
            }
        }
        MouseArea {
            id: tabMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: win.showView(tab.name)
        }
    }
}
