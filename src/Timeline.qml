import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Every backup, newest at the top under "Now". When a file is selected, the
// rail marks the backups where it changed, so finding the version you want is
// a matter of following the dots.
Rectangle {
    id: timeline
    color: theme.surface

    readonly property bool tracking: backend.focusName.length > 0 && backend.versionCount > 0
    readonly property int railX: 30

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Title, and how many versions the selected file has.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.bottomMargin: 10
            spacing: 4
            Label {
                text: qsTr("Timeline")
                font.pixelSize: 15
                font.weight: Font.DemiBold
                color: theme.foreground
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: theme.muted
                text: backend.repositoryState === "loading" ? qsTr("Reading your backups…")
                      : backend.repositoryState === "failed" ? ""
                      : timeline.tracking ? qsTr("“%1” has %2").arg(backend.focusName).arg(qsTr("%n versions", "", backend.versionCount))
                      : qsTr("%n backups", "", backend.timeline.length)
            }
        }

        // Now, at the top.
        TimelineRow {
            Layout.fillWidth: true
            isNow: true
            selected: !win.past
            clockText: qsTr("Now")
            readonly property int latestVersion: backend.timeline.length > 0 ? backend.timeline[0].version : 0
            detail: !timeline.tracking ? ""
                    : backend.nowVersion > 0 && backend.nowVersion === latestVersion ? qsTr("Backed up")
                    : backend.nowVersion > 0 ? qsTr("Same as version %1").arg(backend.nowVersion)
                    : qsTr("Not backed up")
            mark: !timeline.tracking ? "" : backend.nowVersion > 0 ? "same" : "start"
            first: true
            onClicked: backend.showSnapshot(-1)
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: backend.timeline
            currentIndex: backend.snapshotIndex
            ScrollBar.vertical: ScrollBar {}
            onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
            visible: backend.repositoryState !== "failed"

            delegate: TimelineRow {
                required property var modelData
                required property int index
                width: ListView.view.width
                dayHeading: modelData.newDay ? modelData.day : ""
                clockText: modelData.clock
                detail: !timeline.tracking ? modelData.summary
                        : modelData.mark === "absent" ? qsTr("Not there")
                        : modelData.mark !== "start" ? ""
                        : index === backend.timeline.length - 1 ? qsTr("Oldest")
                        : backend.timeline[index + 1].mark === "absent" ? qsTr("Appeared")
                        : qsTr("Changed")
                mark: modelData.mark
                dimmed: !modelData.covers
                selected: index === backend.snapshotIndex
                last: index === backend.timeline.length - 1
                onClicked: backend.showSnapshot(index)
            }
        }

        // The repository couldn't be read: say why, and offer to try again.
        ColumnLayout {
            visible: backend.repositoryState === "failed"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 16
            spacing: 12
            Label {
                text: ""
                font.family: win.iconFont
                font.pixelSize: 26
                color: theme.removed
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Your backups couldn't be opened.")
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
                color: theme.foreground
            }
            Label {
                Layout.fillWidth: true
                text: backend.repositoryError
                wrapMode: Text.Wrap
                color: theme.muted
                font.pixelSize: 12
            }
            ActionButton {
                text: qsTr("Try again")
                key: "R"
                onClicked: backend.retry()
            }
            Item { Layout.fillHeight: true }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: theme.border }

        // How to move through time.
        GridLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            columns: 4
            columnSpacing: 6
            rowSpacing: 6
            KeyCap { key: "["; tint: theme.muted }
            KeyCap { key: "]"; tint: theme.muted }
            Label { Layout.columnSpan: 2; Layout.fillWidth: true; text: qsTr("older / newer backup"); color: theme.muted; font.pixelSize: 12; elide: Text.ElideRight }
            KeyCap { key: "{"; tint: theme.muted }
            KeyCap { key: "}"; tint: theme.muted }
            Label { Layout.columnSpan: 2; Layout.fillWidth: true; text: qsTr("previous / next version"); color: theme.muted; font.pixelSize: 12; elide: Text.ElideRight }
        }
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: backend.repositoryState === "loading"
        visible: running
    }

    component TimelineRow: Item {
        id: row
        property bool isNow: false
        property string dayHeading
        property string clockText
        property string detail
        property string mark
        property bool selected
        property bool dimmed: false
        property bool first: false
        property bool last: false
        signal clicked()

        implicitHeight: (dayHeading.length > 0 ? 30 : 0) + 34
        opacity: dimmed ? 0.45 : 1

        Label {
            visible: row.dayHeading.length > 0
            x: timeline.railX + 18
            y: 10
            text: row.dayHeading
            font.pixelSize: 12
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 0.6
            color: theme.muted
        }

        // The rail runs through every row.
        Rectangle {
            x: timeline.railX - 1
            y: row.first ? body.y + body.height / 2 : 0
            width: 2
            height: row.last ? body.y + body.height / 2 - y : row.height - y
            color: theme.border
        }

        Rectangle {
            id: body
            y: row.dayHeading.length > 0 ? 30 : 0
            x: 8
            width: row.width - 16
            height: 34
            radius: theme.radius
            color: row.selected ? Qt.alpha(theme.accent, 0.16) : rowMouse.containsMouse ? Qt.alpha(theme.foreground, 0.05) : "transparent"
            border.width: row.selected ? 1 : 0
            border.color: Qt.alpha(theme.accent, 0.55)

            // The dot: a ring where the file changed, a small dot where it
            // stayed the same, a dash where it wasn't there.
            Rectangle {
                readonly property int size: row.selected ? 14 : row.mark === "start" || row.isNow ? 12 : row.mark === "absent" ? 8 : 7
                x: timeline.railX - 8 - size / 2
                anchors.verticalCenter: parent.verticalCenter
                width: size
                height: row.mark === "absent" ? 2 : size
                radius: row.mark === "absent" ? 1 : size / 2
                color: row.selected ? theme.accent
                     : row.mark === "start" ? theme.surface
                     : row.mark === "absent" ? theme.muted
                     : row.isNow ? theme.foreground : theme.muted
                border.width: row.mark === "start" && !row.selected ? 3 : 0
                border.color: theme.accent
            }

            Label {
                x: timeline.railX + 10
                anchors.verticalCenter: parent.verticalCenter
                text: row.clockText
                font.pixelSize: 14
                font.weight: row.selected || row.isNow ? Font.DemiBold : Font.Normal
                font.features: { "tnum": 1 }
                color: row.selected ? theme.foreground : row.mark === "absent" ? theme.muted : theme.foreground
            }
            Label {
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - timeline.railX - 70
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideRight
                text: row.detail
                font.pixelSize: 12
                color: row.mark === "start" ? theme.accent : theme.muted
            }
            MouseArea {
                id: rowMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: row.clicked()
            }
        }
    }
}
