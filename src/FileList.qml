import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The folder as it was at the chosen moment, with what has happened to each
// entry since.
FocusScope {
    id: root

    Keys.onPressed: (event) => {
        switch (event.key) {
        case Qt.Key_Up: backend.moveSelection(-1); break
        case Qt.Key_Down: backend.moveSelection(1); break
        case Qt.Key_PageUp: backend.moveSelection(-Math.max(1, Math.floor(list.height / 34) - 1)); break
        case Qt.Key_PageDown: backend.moveSelection(Math.max(1, Math.floor(list.height / 34) - 1)); break
        case Qt.Key_Home: backend.moveSelection(-100000); break
        case Qt.Key_End: backend.moveSelection(100000); break
        case Qt.Key_Return:
        case Qt.Key_Enter:
        case Qt.Key_Right: backend.openRow(backend.selectedRow); break
        case Qt.Key_Left:
        case Qt.Key_Backspace: backend.goUp(); break
        default: return
        }
        event.accepted = true
    }

    Rectangle { anchors.fill: parent; color: theme.background }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Column titles.
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 10
            Label { Layout.fillWidth: true; text: qsTr("Name"); color: theme.muted; font.pixelSize: 12 }
            Label { Layout.preferredWidth: 72; horizontalAlignment: Text.AlignRight; text: qsTr("Size"); color: theme.muted; font.pixelSize: 12 }
            Label { Layout.preferredWidth: 118; text: qsTr("Modified"); color: theme.muted; font.pixelSize: 12 }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: theme.border }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: list
                anchors.fill: parent
                clip: true
                model: backend.entries
                currentIndex: backend.selectedRow
                highlightMoveDuration: 0
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)

                delegate: Rectangle {
                    id: row
                    required property int index
                    required property string name
                    required property bool isDir
                    required property bool isLink
                    required property var size
                    required property var modified
                    required property string status

                    readonly property bool selected: index === backend.selectedRow
                    readonly property color textColor: selected ? theme.selectionForeground : theme.foreground

                    width: ListView.view.width
                    height: 34
                    color: selected ? (root.activeFocus ? theme.selection : Qt.alpha(theme.selection, 0.6))
                                    : rowMouse.containsMouse ? Qt.alpha(theme.selection, 0.25) : "transparent"

                    // Entries that are gone now stand out on the left edge.
                    Rectangle {
                        visible: row.status === "deleted" || row.status === "changed"
                        width: 3
                        height: parent.height
                        color: win.statusColor(row.status)
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 10

                        Label {
                            Layout.preferredWidth: 18
                            text: win.glyph(row.name, row.isDir, row.isLink)
                            font.family: win.iconFont
                            font.pixelSize: 15
                            color: row.status === "deleted" ? theme.removed : row.isDir ? theme.accent : theme.muted
                        }
                        Label {
                            Layout.fillWidth: true
                            text: row.name
                            elide: Text.ElideMiddle
                            font.pixelSize: 14
                            color: row.textColor
                            opacity: row.name.startsWith(".") ? 0.7 : 1
                        }
                        StatusPill {
                            status: row.status
                            visible: text.length > 0
                        }
                        Label {
                            Layout.preferredWidth: 72
                            horizontalAlignment: Text.AlignRight
                            text: row.isDir ? "" : backend.formatSize(row.size)
                            color: row.selected ? row.textColor : theme.muted
                            font.pixelSize: 12
                        }
                        Label {
                            Layout.preferredWidth: 118
                            text: { backend.clock; translator.language; return row.modified ? backend.formatMoment(row.modified) : "" }
                            elide: Text.ElideRight
                            color: row.selected ? row.textColor : theme.muted
                            font.pixelSize: 12
                        }
                    }

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: { backend.selectedRow = row.index; root.forceActiveFocus() }
                        onDoubleClicked: backend.openRow(row.index)
                    }
                }
            }

            // Why the list is empty.
            Column {
                anchors.centerIn: parent
                width: parent.width - 64
                spacing: 10
                visible: list.count === 0 && !backend.listing
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: backend.listingMessage.length > 0 ? "" : ""
                    font.family: win.iconFont
                    font.pixelSize: 34
                    color: theme.muted
                }
                Label {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: theme.muted
                    text: backend.listingMessage.length > 0 ? backend.listingMessage
                          : backend.filter.length > 0 ? qsTr("Nothing here matches “%1”.").arg(backend.filter)
                          : backend.onlyChanges ? (win.past ? qsTr("Nothing in this folder changed since this backup.")
                                                            : qsTr("Everything in this folder is backed up."))
                          : qsTr("This folder is empty.")
                }
            }

            BusyIndicator {
                anchors.centerIn: parent
                running: backend.listing && list.count === 0
                visible: running
            }
        }
    }
}
