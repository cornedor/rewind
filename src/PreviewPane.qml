import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The selected file: which version this is, what changed, and how to get it back.
Rectangle {
    id: pane
    color: theme.background
    signal restoreRequested()

    readonly property var p: backend.preview
    readonly property bool hasFile: p.kind !== undefined && p.kind !== "empty"

    function sideDate(value) {
        backend.clock; translator.language
        return value ? backend.formatMoment(value) : ""
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14
        visible: pane.hasFile

        // Name, and what can be done with it.
        RowLayout {
            Layout.fillWidth: true
            spacing: 14
            Rectangle {
                width: 46
                height: 46
                radius: theme.radius
                color: Qt.alpha(p.status === "deleted" ? theme.removed : theme.accent, 0.14)
                Label {
                    anchors.centerIn: parent
                    text: win.glyph(p.name || "", p.isDir === true, false)
                    font.family: win.iconFont
                    font.pixelSize: 22
                    color: p.status === "deleted" ? theme.removed : theme.accent
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    Layout.fillWidth: true
                    text: p.name || ""
                    font.pixelSize: 19
                    font.weight: Font.DemiBold
                    elide: Text.ElideMiddle
                    color: theme.foreground
                }
                Label {
                    Layout.fillWidth: true
                    text: {
                        translator.language
                        if (backend.versionsLoading) return qsTr("Looking through your backups…")
                        if (backend.versionCount === 0) return backend.repositoryState === "ready" ? qsTr("Not in any backup") : ""
                        if (backend.currentVersion > 0)
                            return qsTr("Version %1 of %2").arg(backend.currentVersion).arg(backend.versionCount)
                        return qsTr("%n versions in your backups", "", backend.versionCount)
                    }
                    color: theme.muted
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
            }
            ActionButton {
                text: qsTr("Open")
                key: "O"
                visible: p.kind !== "missing"
                onClicked: backend.openExternally()
            }
            ActionButton {
                text: p.isDir ? qsTr("Restore folder…") : qsTr("Restore…")
                key: qsTr("Space")
                primary: true
                visible: p.atSnapshot === true
                enabled: p.canRestore === true && !backend.restoring
                onClicked: pane.restoreRequested()
            }
        }

        // Move between versions, and choose what to compare against.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: p.kind !== "missing" || backend.versionCount > 0

            ActionButton {
                text: qsTr("Previous version")
                key: "{"
                small: true
                enabled: backend.versionCount > 0
                onClicked: backend.previousVersion()
            }
            ActionButton {
                text: qsTr("Next version")
                key: "}"
                small: true
                enabled: backend.versionCount > 0 && win.past
                onClicked: backend.nextVersion()
            }
            Item { Layout.fillWidth: true }

            // Two ways to look at a backed-up version.
            Row {
                visible: p.canCompare === true
                spacing: 0
                Segment {
                    text: qsTr("Since this backup")
                    checked: backend.compareMode === "now"
                    first: true
                    onClicked: backend.compareMode = "now"
                }
                Segment {
                    text: qsTr("In this version")
                    checked: backend.compareMode === "previous"
                    onClicked: backend.compareMode = "previous"
                }
            }
            KeyCap { visible: p.canCompare === true; key: "C"; tint: theme.muted }
        }

        // What the two sides are.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            visible: p.kind === "diff" || p.kind === "image" || p.kind === "binary" || p.kind === "text"
            radius: theme.radius
            color: theme.surface
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 10
                Label {
                    Layout.fillWidth: true
                    text: p.title || ""
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    color: theme.foreground
                }
                SideLabel { visible: (p.leftLabel || "").length > 0 && p.kind !== "text"; sign: "−"; tint: theme.removed; text: p.leftLabel || "" }
                Label { visible: (p.leftLabel || "").length > 0 && p.kind !== "text"; text: ""; font.family: win.iconFont; color: theme.muted }
                SideLabel { visible: (p.rightLabel || "").length > 0 && p.kind !== "text"; sign: "+"; tint: theme.added; text: p.rightLabel || "" }
                Label {
                    visible: p.kind === "diff"
                    textFormat: Text.StyledText
                    text: "<font color='" + theme.added + "'>+" + (p.added || 0) + "</font>  <font color='"
                          + theme.removed + "'>−" + (p.removed || 0) + "</font>"
                    font.family: "monospace"
                    font.pixelSize: 13
                }
            }
        }

        // A sentence about this version, when there is one to say.
        Rectangle {
            Layout.fillWidth: true
            visible: (p.note || "").length > 0
            implicitHeight: noteLabel.implicitHeight + 20
            radius: theme.radius
            readonly property color tint: p.noteKind === "deleted" ? theme.removed
                                          : p.noteKind === "new" ? theme.changedColor
                                          : p.noteKind === "same" ? theme.added : theme.muted
            color: Qt.alpha(tint, 0.12)
            border.width: 1
            border.color: Qt.alpha(tint, 0.4)
            Label {
                id: noteLabel
                anchors.fill: parent
                anchors.margins: 10
                anchors.leftMargin: 14
                text: p.note || ""
                wrapMode: Text.Wrap
                color: theme.foreground
                verticalAlignment: Text.AlignVCenter
            }
        }

        // The content itself.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            DiffView {
                anchors.fill: parent
                visible: p.kind === "diff" || p.kind === "text"
                showChanges: p.kind === "diff"
                truncated: p.truncated === true
            }

            RowLayout {
                anchors.fill: parent
                visible: p.kind === "image"
                spacing: 16
                ImageSide { visible: (p.leftUrl || "").length > 0; source: p.leftUrl || ""; label: p.leftLabel || ""; tint: theme.removed }
                ImageSide { source: p.rightUrl || ""; label: (p.leftUrl || "").length > 0 ? (p.rightLabel || "") : ""; tint: theme.added }
            }

            // Files that can't be shown as text: compare what restic knows.
            GridLayout {
                visible: p.kind === "binary"
                anchors.top: parent.top
                anchors.left: parent.left
                columns: 3
                columnSpacing: 28
                rowSpacing: 10
                Label { text: "" }
                Label { text: p.leftExists ? (p.leftLabel || "") : ""; color: theme.removed; font.weight: Font.DemiBold }
                Label { text: p.rightExists ? (p.leftExists ? (p.rightLabel || "") : qsTr("This version")) : ""; color: theme.added; font.weight: Font.DemiBold }
                Label { text: qsTr("Size"); color: theme.muted }
                Label { text: p.leftExists ? backend.formatSize(p.leftSize) : "" }
                Label { text: p.rightExists ? backend.formatSize(p.rightSize) : "" }
                Label { text: qsTr("Modified"); color: theme.muted }
                Label { text: p.leftExists ? pane.sideDate(p.leftModified) : "" }
                Label { text: p.rightExists ? pane.sideDate(p.rightModified) : "" }
                Label {
                    Layout.columnSpan: 3
                    Layout.topMargin: 10
                    text: p.tooLarge ? qsTr("Too large to show as text. Press O to open it.")
                                     : qsTr("This isn't a text file. Press O to open it in its app.")
                    color: theme.muted
                }
            }

            Column {
                visible: p.kind === "folder"
                anchors.centerIn: parent
                spacing: 10
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: ""
                    font.family: win.iconFont
                    font.pixelSize: 64
                    color: Qt.alpha(theme.accent, 0.7)
                }
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("%n items", "", p.items || 0)
                    color: theme.muted
                }
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: p.atSnapshot ? qsTr("Enter opens it. Space restores the whole folder.") : qsTr("Enter opens it.")
                    color: theme.muted
                }
            }

            Column {
                visible: p.kind === "missing" || p.kind === "error"
                anchors.centerIn: parent
                width: parent.width - 40
                spacing: 12
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: ""
                    font.family: win.iconFont
                    font.pixelSize: 40
                    color: theme.muted
                }
                Label {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: theme.muted
                    text: backend.versionCount > 0
                          ? qsTr("Press { to go to the backup that has the previous version.")
                          : ""
                }
            }
        }
    }

    // Nothing selected.
    Column {
        anchors.centerIn: parent
        visible: !pane.hasFile
        spacing: 12
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: ""
            font.family: win.iconFont
            font.pixelSize: 48
            color: Qt.alpha(theme.accent, 0.6)
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Pick a file to see its history.")
            color: theme.muted
            font.pixelSize: 15
        }
    }

    component SideLabel: Row {
        property string sign
        property color tint
        property string text
        spacing: 6
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 18; height: 18; radius: Math.min(4, theme.radius)
            color: Qt.alpha(parent.tint, 0.18)
            Label { anchors.centerIn: parent; text: parent.parent.sign; color: parent.parent.tint; font.weight: Font.Bold }
        }
        Label { anchors.verticalCenter: parent.verticalCenter; text: parent.text; color: theme.foreground; font.pixelSize: 13 }
    }

    component Segment: Rectangle {
        id: segment
        property string text
        property bool checked
        property bool first: false
        signal clicked()
        implicitWidth: segmentLabel.implicitWidth + 24
        implicitHeight: 30
        radius: Math.min(4, theme.radius)
        color: checked ? theme.accent : (segmentMouse.containsMouse ? theme.raised : theme.surface)
        border.width: 1
        border.color: checked ? theme.accent : theme.border
        Label {
            id: segmentLabel
            anchors.centerIn: parent
            text: segment.text
            font.pixelSize: 13
            font.weight: segment.checked ? Font.DemiBold : Font.Normal
            color: segment.checked ? theme.accentForeground : theme.foreground
        }
        MouseArea { id: segmentMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: segment.clicked() }
    }

    component ImageSide: ColumnLayout {
        property alias source: image.source
        property string label
        property color tint
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredWidth: 1
        spacing: 8
        Label { visible: parent.label.length > 0; text: parent.label; color: parent.tint; font.weight: Font.DemiBold }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: theme.radius
            color: theme.surface
            border.width: 1
            border.color: theme.border
            Image {
                id: image
                anchors.fill: parent
                anchors.margins: 10
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: false
                sourceSize.width: 1600
                sourceSize.height: 1600
            }
        }
    }
}
