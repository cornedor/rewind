import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Every shortcut, on ? .
Rectangle {
    id: overlay
    color: Qt.alpha(theme.background, 0.92)

    MouseArea { anchors.fill: parent; onClicked: overlay.visible = false }

    readonly property var groups: [
        { title: qsTr("Time"), keys: [
            ["[", qsTr("Older backup")], ["]", qsTr("Newer backup")],
            ["{", qsTr("Previous version of this file")], ["}", qsTr("Next version of this file")],
            ["Esc", qsTr("Back to now")], ["W", qsTr("What changed in this backup")] ] },
        { title: qsTr("Files"), keys: [
            ["↑ ↓", qsTr("Select")], ["Enter  →", qsTr("Open folder or file")], ["←  Backspace", qsTr("Up a folder")],
            ["~", qsTr("Home folder")], ["/", qsTr("Filter this folder")], ["Ctrl+H", qsTr("Show hidden files")],
            ["F", qsTr("Only changed or deleted")] ] },
        { title: qsTr("Versions"), keys: [
            ["Space", qsTr("Restore…")], ["O", qsTr("Open this version")], ["C", qsTr("Compare with now or with the previous version")],
            ["Ctrl+Z", qsTr("Undo restore")], ["Ctrl+Shift+Z", qsTr("Redo restore")] ] },
        { title: qsTr("Backups"), keys: [
            ["Tab", qsTr("Files or backups")], ["Enter", qsTr("Browse the picked backup")], ["S", qsTr("Newest or biggest first")],
            ["Y", qsTr("Copy the backup's id")], ["L", qsTr("Backup log")],
            ["B", qsTr("Back up now")], ["R", qsTr("Try again after an error")], ["?", qsTr("This help")], ["Q", qsTr("Quit")] ] }
    ]

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 80, grid.implicitWidth + 64)
        height: grid.implicitHeight + 64
        radius: theme.radius
        color: theme.raised
        border.width: 1
        border.color: theme.border

        GridLayout {
            id: grid
            anchors.centerIn: parent
            columns: 2
            columnSpacing: 48
            rowSpacing: 28
            Repeater {
                model: overlay.groups
                ColumnLayout {
                    required property var modelData
                    Layout.alignment: Qt.AlignTop
                    spacing: 10
                    Label { text: modelData.title; font.pixelSize: 13; font.weight: Font.DemiBold; font.capitalization: Font.AllUppercase; color: theme.accent }
                    Repeater {
                        model: modelData.keys
                        RowLayout {
                            required property var modelData
                            spacing: 12
                            Item {
                                Layout.preferredWidth: 120
                                Layout.preferredHeight: 22
                                Row {
                                    spacing: 4
                                    Repeater {
                                        model: modelData[0].split("  ")
                                        KeyCap { required property string modelData; key: modelData; tint: theme.foreground }
                                    }
                                }
                            }
                            Label { text: modelData[1]; color: theme.foreground; font.pixelSize: 13 }
                        }
                    }
                }
            }
        }
    }
}
