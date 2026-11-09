import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One backup: when it ran and how long it took, how big it is, what it
// changed and which files took the space, and whether the cleanup keeps it.
Rectangle {
    id: pane
    color: theme.background
    signal browse()
    signal showChanges()
    signal openFile(string path)

    readonly property var d: overview.detail
    readonly property bool valid: d.valid === true

    function number(n) { return n === undefined || n < 0 ? "–" : Number(n).toLocaleString(Qt.locale(), "f", 0) }
    function size(n) { return n === undefined || n < 0 ? "–" : backend.formatSize(n) }
    function clockOf(time) {
        translator.language
        if (!time || isNaN(time.getTime())) return "–"
        // The locale's own short time, with seconds: a backup can take seconds.
        const format = Qt.locale().timeFormat(Locale.ShortFormat).replace(/mm(?!:ss)/, "mm:ss")
        return time.toLocaleTimeString(Qt.locale(), format)
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        visible: pane.valid
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: scroll.availableWidth
            spacing: 0

            // When, and the two ways in.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 20
                Layout.bottomMargin: 16
                spacing: 4
                Label {
                    Layout.fillWidth: true
                    text: { backend.clock; translator.language; return pane.valid ? backend.formatMoment(pane.d.time) : "" }
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    color: theme.foreground
                }
                RowLayout {
                    spacing: 6
                    Label {
                        text: { backend.clock; translator.language; return pane.valid ? backend.formatAgo(pane.d.time) : "" }
                        font.pixelSize: 13
                        color: theme.muted
                    }
                    Label { text: "·"; color: theme.muted; font.pixelSize: 13 }
                    // The id, which restic commands take; a click copies all of it.
                    Rectangle {
                        implicitWidth: idRow.implicitWidth + 12
                        implicitHeight: 22
                        radius: Math.min(4, theme.radius)
                        color: idMouse.containsMouse ? theme.raised : "transparent"
                        border.width: 1
                        border.color: idMouse.containsMouse ? theme.border : "transparent"
                        Row {
                            id: idRow
                            anchors.centerIn: parent
                            spacing: 6
                            Label { text: pane.d.shortId || ""; font.family: "monospace"; font.pixelSize: 12; color: theme.muted }
                            Label { text: ""; font.family: win.iconFont; font.pixelSize: 11; color: theme.muted; opacity: idMouse.containsMouse ? 1 : 0.5 }
                        }
                        MouseArea { id: idMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: overview.copyId() }
                        ToolTip.visible: idMouse.containsMouse
                        ToolTip.delay: 500
                        ToolTip.text: qsTr("Copy the full id (Y)")
                    }
                }

                RowLayout {
                    Layout.topMargin: 12
                    spacing: 8
                    ActionButton { text: qsTr("Browse files"); key: "Enter"; primary: true; onClicked: pane.browse() }
                    ActionButton {
                        text: qsTr("What changed")
                        key: "W"
                        enabled: pane.d.isFirst !== true
                        onClicked: pane.showChanges()
                    }
                }
            }

            // Whether the cleanup keeps it. Only said when it matters, or
            // briefly when it's kept.
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.bottomMargin: 16
                visible: pane.d.retentionKnown === true
                implicitHeight: keepRow.implicitHeight + 20
                radius: theme.radius
                readonly property color tint: pane.d.removing ? theme.changedColor : theme.added
                color: Qt.alpha(tint, 0.10)
                border.width: 1
                border.color: Qt.alpha(tint, 0.35)
                RowLayout {
                    id: keepRow
                    anchors.fill: parent
                    anchors.margins: 10
                    anchors.leftMargin: 12
                    spacing: 10
                    Label {
                        Layout.alignment: Qt.AlignTop
                        text: pane.d.removing ? "" : ""
                        font.family: win.iconFont
                        font.pixelSize: 14
                        color: parent.parent.tint
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            color: theme.foreground
                            text: {
                                translator.language
                                if (pane.d.removing) return qsTr("The next cleanup removes this backup")
                                const kept = win.keptText(pane.d.kept || [])
                                return kept.length ? qsTr("Kept as %1").arg(kept) : qsTr("Kept by your cleanup rules")
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: pane.d.removing === true
                            wrapMode: Text.Wrap
                            font.pixelSize: 12
                            color: theme.muted
                            text: {
                                backend.clock; translator.language
                                const next = backend.backups.cleanupNextRun
                                const when = next && !isNaN(next.getTime()) ? backend.formatFuture(next) : ""
                                return when.length
                                    ? qsTr("No rule keeps it once newer backups exist. %1 runs %2.").arg(backend.backups.cleanupUnit).arg(when)
                                    : qsTr("No rule keeps it once newer backups exist.")
                            }
                        }
                    }
                }
            }

            Section { title: qsTr("This backup") }
            GridLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.bottomMargin: 18
                columns: 2
                columnSpacing: 16
                rowSpacing: 7
                visible: pane.d.hasSummary === true

                Key { text: qsTr("Ran") }
                Value {
                    text: { translator.language; return qsTr("%1 to %2").arg(pane.clockOf(pane.d.start)).arg(pane.clockOf(pane.d.end)) }
                    trailing: overview.formatDuration(pane.d.duration)
                }
                Key { text: qsTr("Holds") }
                Value {
                    text: pane.size(pane.d.totalBytes)
                    trailing: qsTr("%1 files").arg(pane.number(pane.d.totalFiles))
                }
                Key { text: qsTr("Since the one before") }
                Value {
                    text: {
                        translator.language
                        if (pane.d.isFirst) return qsTr("First backup")
                        const g = pane.d.growth
                        if (g === undefined) return "–"
                        if (Math.abs(g) < 1024) return qsTr("Same size")
                        return (g > 0 ? "+" : "−") + backend.formatSize(Math.abs(g))
                    }
                }
                Key { text: qsTr("New data") }
                Value {
                    text: pane.size(pane.d.addedRaw)
                    trailing: pane.d.added >= 0 ? qsTr("%1 stored").arg(pane.size(pane.d.added)) : ""
                }
            }
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.bottomMargin: 18
                visible: pane.valid && pane.d.hasSummary !== true
                wrapMode: Text.Wrap
                font.pixelSize: 13
                color: theme.muted
                text: qsTr("This backup was made with a restic older than 0.17, which didn't record its size or how long it took.")
            }

            Section { title: qsTr("Files"); visible: pane.d.hasSummary === true }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.bottomMargin: 18
                spacing: 8
                visible: pane.d.hasSummary === true
                Count { label: qsTr("New"); value: pane.number(pane.d.filesNew); tint: theme.added }
                Count { label: qsTr("Changed"); value: pane.number(pane.d.filesChanged); tint: theme.changedColor }
                Count {
                    label: qsTr("Deleted")
                    value: pane.d.isFirst ? "0" : pane.d.filesRemoved !== undefined ? pane.number(pane.d.filesRemoved) : "…"
                    tint: theme.removed
                }
                Count { label: qsTr("Unchanged"); value: pane.number(pane.d.filesUnmodified); tint: theme.muted }
            }

            // What took the space.
            Section { title: qsTr("Biggest new and changed files") }
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.bottomMargin: 6
                visible: overview.largest.length > 0
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: theme.muted
                text: qsTr("Their full size; restic only stores the parts that changed.")
            }
            Repeater {
                model: overview.largest
                Rectangle {
                    id: fileRow
                    required property var modelData
                    required property int index
                    readonly property real share: overview.largest.length && overview.largest[0].size > 0
                                                  ? modelData.size / overview.largest[0].size : 0
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    Layout.rightMargin: 10
                    implicitHeight: 44
                    radius: Math.min(6, theme.radius)
                    color: fileMouse.containsMouse ? Qt.alpha(theme.selection, 0.3) : "transparent"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 10
                        Label {
                            text: win.glyph(fileRow.modelData.name, false, false)
                            font.family: win.iconFont
                            font.pixelSize: 15
                            color: theme.muted
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                Label {
                                    Layout.fillWidth: true
                                    text: fileRow.modelData.name
                                    elide: Text.ElideMiddle
                                    font.pixelSize: 13
                                    color: theme.foreground
                                }
                                Label {
                                    text: fileRow.modelData.sizeText
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    font.features: { "tnum": 1 }
                                    color: theme.foreground
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Label {
                                    Layout.fillWidth: true
                                    text: fileRow.modelData.folder
                                    elide: Text.ElideMiddle
                                    font.pixelSize: 11
                                    color: theme.muted
                                }
                                Label {
                                    text: fileRow.modelData.isNew ? qsTr("new") : qsTr("changed")
                                    font.pixelSize: 11
                                    color: theme.muted
                                }
                                // How it compares with the biggest one.
                                Rectangle {
                                    Layout.preferredWidth: 60
                                    implicitHeight: 4
                                    radius: 2
                                    color: Qt.alpha(theme.muted, 0.18)
                                    Rectangle {
                                        width: Math.max(2, parent.width * fileRow.share)
                                        height: parent.height
                                        radius: 2
                                        color: Qt.alpha(theme.accent, 0.7)
                                    }
                                }
                            }
                        }
                    }
                    MouseArea {
                        id: fileMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: pane.openFile(fileRow.modelData.path)
                    }
                    ToolTip.visible: fileMouse.containsMouse
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("Show it in this backup")
                }
            }
            RowLayout {
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.bottomMargin: 6
                visible: overview.largestLoading || overview.largestNote.length > 0
                spacing: 10
                BusyIndicator {
                    visible: overview.largestLoading
                    running: visible
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                    padding: 0
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: 12
                    color: theme.muted
                    text: overview.largestNote.length ? overview.largestNote : qsTr("Comparing with the backup before it…")
                }
            }

            // Where it came from.
            Label {
                Layout.fillWidth: true
                Layout.margins: 20
                Layout.topMargin: 18
                wrapMode: Text.Wrap
                font.pixelSize: 11
                color: theme.muted
                text: {
                    if (!pane.valid) return ""
                    let parts = []
                    if (pane.d.program) parts.push(pane.d.program)
                    if (pane.d.hostname) parts.push((pane.d.username ? pane.d.username + "@" : "") + pane.d.hostname)
                    if (pane.d.paths && pane.d.paths.length) parts.push(pane.d.paths.map(p => backend.shortPath(p)).join(", "))
                    if (pane.d.tags && pane.d.tags.length) parts.push(pane.d.tags.join(", "))
                    return parts.join("  ·  ")
                }
            }
        }
    }

    Label {
        anchors.centerIn: parent
        visible: !pane.valid && backend.repositoryState !== "loading"
        text: qsTr("Pick a backup to see it here.")
        color: theme.muted
    }

    component Section: Label {
        property string title
        Layout.fillWidth: true
        Layout.leftMargin: 20
        Layout.rightMargin: 20
        Layout.bottomMargin: 10
        text: title
        font.pixelSize: 12
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.6
        color: theme.muted
    }

    component Key: Label {
        font.pixelSize: 13
        color: theme.muted
    }

    component Value: RowLayout {
        property alias text: valueLabel.text
        property string trailing
        Layout.fillWidth: true
        spacing: 8
        Label {
            id: valueLabel
            font.pixelSize: 13
            font.features: { "tnum": 1 }
            color: theme.foreground
        }
        Label {
            visible: parent.trailing.length > 0
            text: parent.trailing
            font.pixelSize: 13
            font.features: { "tnum": 1 }
            color: theme.muted
        }
        Item { Layout.fillWidth: true }
    }

    // A count with a colored mark beside it; the number stays in text color.
    component Count: Rectangle {
        property string label
        property string value
        property color tint
        Layout.fillWidth: true
        implicitHeight: 54
        radius: Math.min(6, theme.radius)
        color: theme.surface
        border.width: 1
        border.color: theme.border
        Column {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3
            Label {
                text: parent.parent.value
                font.pixelSize: 16
                font.weight: Font.DemiBold
                font.features: { "tnum": 1 }
                color: theme.foreground
            }
            Row {
                spacing: 6
                Rectangle { width: 6; height: 6; radius: 3; anchors.verticalCenter: parent.verticalCenter; color: parent.parent.parent.tint }
                Label { text: parent.parent.parent.label; font.pixelSize: 11; color: theme.muted }
            }
        }
    }
}
