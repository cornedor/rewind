import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The backups themselves: are they working, how much do they take, and every
// backup with what it added and whether the cleanup keeps it.
FocusScope {
    id: view
    signal browse(int snapshot)
    signal showChanges(int snapshot)
    signal openFile(int snapshot, string path)
    signal showLog()

    readonly property var s: overview.summary
    readonly property var service: backend.backups
    readonly property string health: overview.health
    // Ticks each second while a backup runs, for its elapsed time.
    property date now: new Date()

    function valid(time) { return time && time.getTime && !isNaN(time.getTime()) }
    function pageRows() { return Math.max(1, Math.floor(list.height / 44) - 1) }

    Timer { interval: 1000; repeat: true; running: view.service.running && view.visible; onTriggered: view.now = new Date() }

    Keys.onPressed: (event) => {
        if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier)) {
            if (event.key === Qt.Key_C && event.modifiers === Qt.ControlModifier) { overview.copyId(); event.accepted = true }
            return
        }
        switch (event.key) {
        case Qt.Key_Up: case Qt.Key_K: overview.move(-1); break
        case Qt.Key_Down: case Qt.Key_J: overview.move(1); break
        case Qt.Key_PageUp: overview.move(-view.pageRows()); break
        case Qt.Key_PageDown: overview.move(view.pageRows()); break
        case Qt.Key_Home: overview.move(-100000); break
        case Qt.Key_End: overview.move(100000); break
        case Qt.Key_BracketRight: overview.move(-1); break
        case Qt.Key_BracketLeft: overview.move(1); break
        case Qt.Key_Return: case Qt.Key_Enter: if (overview.selected >= 0) view.browse(overview.selected); break
        case Qt.Key_W: if (overview.detail.isFirst === false) view.showChanges(overview.selected); break
        case Qt.Key_S: overview.toggleSort(); break
        case Qt.Key_Y: overview.copyId(); break
        case Qt.Key_L: if (view.service.available) view.showLog(); break
        default: return
        }
        event.accepted = true
    }

    Rectangle { anchors.fill: parent; color: theme.background }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // How things stand, in one sentence, and the numbers behind it.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: header.implicitHeight + 36
            color: theme.surface

            ColumnLayout {
                id: header
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 18
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 16

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14

                    // The state: an icon, a color and words, never color alone.
                    Rectangle {
                        readonly property color tint: win.healthColor(view.health)
                        width: 44
                        height: 44
                        radius: 22
                        color: Qt.alpha(tint, 0.15)
                        border.width: 1
                        border.color: Qt.alpha(tint, 0.45)
                        Label {
                            anchors.centerIn: parent
                            visible: view.health !== "running" && view.health !== "loading"
                            text: win.healthGlyph(view.health)
                            font.family: win.iconFont
                            font.pixelSize: 19
                            color: parent.tint
                        }
                        BusyIndicator {
                            anchors.centerIn: parent
                            visible: view.health === "running" || view.health === "loading"
                            running: visible
                            width: 30
                            height: 30
                            padding: 0
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            font.pixelSize: 19
                            font.weight: Font.DemiBold
                            color: theme.foreground
                            text: {
                                backend.clock; translator.language
                                switch (view.health) {
                                case "loading": return qsTr("Reading your backups…")
                                case "unreachable": return qsTr("Your backups couldn't be opened")
                                case "running": return qsTr("Backing up now")
                                case "none": return qsTr("No backups yet")
                                case "failed": return qsTr("The last backup failed")
                                case "skipped": return qsTr("The last backup was skipped")
                                case "late": return qsTr("Backups are later than usual")
                                }
                                return qsTr("Backed up %1").arg(backend.formatAgo(view.s.latest))
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            font.pixelSize: 13
                            color: theme.muted
                            text: {
                                backend.clock; translator.language; view.now
                                const latest = view.valid(view.s.latest) ? backend.formatAgo(view.s.latest) : ""
                                const next = view.valid(view.service.nextRun) ? backend.formatFuture(view.service.nextRun) : ""
                                switch (view.health) {
                                case "loading": return backend.repository
                                case "unreachable": return backend.repositoryError
                                case "running": {
                                    const started = view.valid(view.service.started) ? view.service.started : null
                                    const elapsed = started ? Math.max(0, Math.round((view.now - started) / 1000)) : -1
                                    let parts = []
                                    if (elapsed >= 0) parts.push(qsTr("Running for %1").arg(overview.formatDuration(elapsed)))
                                    if (overview.typicalDuration > 0) parts.push(qsTr("usually takes %1").arg(overview.formatDuration(overview.typicalDuration)))
                                    return parts.join("  ·  ")
                                }
                                case "none":
                                    return view.service.available ? qsTr("Back up now to make the first one.")
                                                                  : qsTr("No backup service named %1 was found").arg(view.service.unit)
                                case "failed": {
                                    const when = view.valid(view.service.lastFinished) ? backend.formatAgo(view.service.lastFinished) : ""
                                    return qsTr("It stopped %1; the log says why. The latest good backup is from %2.").arg(when).arg(latest)
                                }
                                case "skipped":
                                    return qsTr("One of %1's conditions wasn't met. The latest backup is from %2.").arg(view.service.unit).arg(latest)
                                case "late":
                                    return qsTr("The latest is from %1, while they usually run about every %2.")
                                               .arg(latest).arg(overview.formatApproximate(view.s.gap))
                                }
                                if (next.length) return qsTr("Next backup %1").arg(next)
                                return view.service.available ? qsTr("%1 has no timer, so backups only run when started.").arg(view.service.unit) : ""
                            }
                        }
                    }

                    ActionButton {
                        text: qsTr("Log")
                        key: "L"
                        visible: view.service.available
                        onClicked: view.showLog()
                    }
                    ActionButton {
                        visible: view.service.available
                        text: view.service.running ? qsTr("Backing up…") : qsTr("Back up now")
                        key: "B"
                        primary: view.health === "none" || view.health === "failed" || view.health === "late" || view.health === "skipped" || view.health === "ok"
                        enabled: !view.service.running
                        onClicked: view.service.start()
                    }
                    ActionButton {
                        visible: view.health === "unreachable"
                        text: qsTr("Try again")
                        key: "R"
                        primary: true
                        onClicked: backend.retry()
                    }
                }

                // Four numbers that matter.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    visible: view.health !== "unreachable"

                    Tile {
                        glyph: ""
                        title: qsTr("Stored")
                        loading: view.s.statsLoading === true && !(view.s.stored > 0)
                        value: view.s.stored > 0 ? backend.formatSize(view.s.stored) : view.s.statsError ? "–" : ""
                        detail: {
                            translator.language
                            if (view.s.statsError) return view.s.statsError
                            if (view.s.saved !== undefined && view.s.uncompressed > 0)
                                return qsTr("%1 before compression, %2% saved").arg(backend.formatSize(view.s.uncompressed)).arg(Math.round(view.s.saved))
                            return qsTr("In %1").arg(backend.repository)
                        }
                        tip: backend.repository
                    }
                    Tile {
                        glyph: ""
                        title: qsTr("Latest backup holds")
                        loading: view.health === "loading"
                        value: view.s.totalBytes > 0 ? backend.formatSize(view.s.totalBytes) : view.s.count > 0 ? "–" : ""
                        detail: view.s.totalFiles > 0 ? qsTr("%1 files").arg(Number(view.s.totalFiles).toLocaleString(Qt.locale(), "f", 0)) : ""
                    }
                    Tile {
                        glyph: ""
                        title: qsTr("History")
                        loading: view.health === "loading"
                        value: view.s.count > 0 ? qsTr("%n backups", "", view.s.count) : ""
                        detail: {
                            backend.clock; translator.language
                            if (!view.valid(view.s.oldest)) return ""
                            const since = qsTr("Oldest: %1").arg(backend.formatMoment(view.s.oldest))
                            return view.s.gap > 0 ? since + "  ·  " + qsTr("about every %1").arg(overview.formatApproximate(view.s.gap)) : since
                        }
                    }
                    Tile {
                        glyph: ""
                        title: qsTr("Cleanup")
                        loading: view.s.retentionLoading === true
                        value: {
                            backend.clock; translator.language
                            if (!view.service.cleanupAvailable) return qsTr("Not found")
                            if (view.valid(view.service.cleanupNextRun)) return backend.formatFuture(view.service.cleanupNextRun)
                            return qsTr("No timer")
                        }
                        detail: {
                            translator.language
                            if (!view.service.cleanupAvailable)
                                return qsTr("No %1 service, so Rewind can't tell which backups stay.").arg(view.service.cleanupUnit)
                            if (view.s.retentionError) return view.s.retentionError
                            return view.s.rules ? qsTr("Keeps %1").arg(view.s.rules) : qsTr("No keep rules found")
                        }
                        badge: view.s.removing > 0 ? qsTr("%n backups go", "", view.s.removing) : ""
                    }
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.border }
        }

        // Every backup and the one picked.
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // Added per backup, over time.
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 20
                    Layout.bottomMargin: 6
                    spacing: 6
                    visible: overview.chart.length > 1
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("Added per backup")
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            color: theme.foreground
                        }
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("New data each backup stored, after compression")
                            elide: Text.ElideRight
                            font.pixelSize: 12
                            color: theme.muted
                        }
                    }
                    BackupChart { Layout.fillWidth: true }
                }

                // Column titles; the first one picks the order.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 42
                    Layout.leftMargin: 20
                    Layout.rightMargin: 16
                    spacing: 12
                    Row {
                        Layout.preferredWidth: win.whenWidth
                        spacing: 6
                        Segment { text: qsTr("Newest"); checked: overview.sort === "time"; onClicked: overview.sort = "time" }
                        Segment { text: qsTr("Biggest"); checked: overview.sort === "size"; onClicked: overview.sort = "size" }
                        KeyCap { anchors.verticalCenter: parent.verticalCenter; key: "S"; tint: theme.muted }
                    }
                    Label { Layout.preferredWidth: win.tookWidth; horizontalAlignment: Text.AlignRight; text: qsTr("Took"); color: theme.muted; font.pixelSize: 12 }
                    Label { Layout.preferredWidth: win.filesWidth; horizontalAlignment: Text.AlignRight; text: qsTr("Files"); color: theme.muted; font.pixelSize: 12 }
                    Label { Layout.fillWidth: true; horizontalAlignment: Text.AlignRight; text: qsTr("Added"); color: theme.muted; font.pixelSize: 12 }
                    Label { Layout.preferredWidth: win.cleanupWidth; leftPadding: 8; text: qsTr("Cleanup"); color: theme.muted; font.pixelSize: 12 }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: theme.border }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ListView {
                        id: list
                        anchors.fill: parent
                        clip: true
                        model: overview.backups
                        currentIndex: overview.selectedRow
                        highlightMoveDuration: 0
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
                        visible: overview.backups.count > 0

                        // A backup being made shows where it will land.
                        header: Item {
                            width: ListView.view.width
                            height: view.service.running && overview.sort === "time" ? 52 : 0
                            visible: height > 0
                            clip: true
                            Behavior on height { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 6
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                radius: theme.radius
                                color: Qt.alpha(theme.accent, 0.08)
                                border.width: 1
                                border.color: Qt.alpha(theme.accent, 0.35)
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    spacing: 12
                                    BusyIndicator { running: parent.visible; Layout.preferredWidth: 22; Layout.preferredHeight: 22; padding: 0 }
                                    Label { text: qsTr("Backing up now"); font.pixelSize: 14; font.weight: Font.DemiBold; color: theme.foreground }
                                    Label {
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        font.pixelSize: 12
                                        color: theme.muted
                                        text: {
                                            view.now; translator.language
                                            const started = view.valid(view.service.started) ? view.service.started : null
                                            if (!started) return ""
                                            return qsTr("Running for %1").arg(overview.formatDuration(Math.max(0, Math.round((view.now - started) / 1000))))
                                        }
                                    }
                                    // Only an estimate: restic doesn't report progress
                                    // to the service, so this follows the usual time.
                                    ProgressBar {
                                        Layout.preferredWidth: 160
                                        visible: overview.typicalDuration > 0 && view.valid(view.service.started)
                                        indeterminate: !visible
                                        value: visible ? Math.min(0.95, (view.now - view.service.started) / 1000 / overview.typicalDuration) : 0
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Based on how long backups usually take")
                                    }
                                }
                            }
                        }

                        section.property: overview.sort === "time" ? "day" : ""
                        section.criteria: ViewSection.FullString
                        section.delegate: Item {
                            required property string section
                            width: ListView.view.width
                            height: 32
                            Label {
                                x: 20
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: 6
                                text: parent.section
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                                font.capitalization: Font.AllUppercase
                                font.letterSpacing: 0.6
                                color: theme.muted
                            }
                        }

                        delegate: Rectangle {
                            id: row
                            required property int index
                            required property int snapshot
                            required property string clock
                            required property string day
                            required property var duration
                            required property var filesNew
                            required property var filesChanged
                            required property var added
                            required property var kept
                            required property bool removing
                            required property bool latest
                            required property bool hasSummary

                            readonly property bool picked: index === overview.selectedRow
                            readonly property bool browsing: snapshot === backend.snapshotIndex
                            readonly property color textColor: picked && view.activeFocus ? theme.selectionForeground : theme.foreground
                            readonly property real ratio: overview.chartScale > 0 && added > 0 ? Math.min(1, added / overview.chartScale) : 0

                            width: ListView.view.width
                            height: 44
                            color: picked ? (view.activeFocus ? theme.selection : Qt.alpha(theme.selection, 0.6))
                                          : rowMouse.containsMouse ? Qt.alpha(theme.selection, 0.25) : "transparent"

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 20
                                anchors.rightMargin: 16
                                spacing: 12

                                // When: the time, and in "Biggest" order the day too.
                                RowLayout {
                                    Layout.preferredWidth: win.whenWidth
                                    Layout.maximumWidth: win.whenWidth
                                    Layout.fillWidth: false
                                    spacing: 8
                                    Label {
                                        text: overview.sort === "size" ? row.day + "  " + row.clock : row.clock
                                        font.pixelSize: 14
                                        font.weight: row.latest ? Font.DemiBold : Font.Normal
                                        font.features: { "tnum": 1 }
                                        elide: Text.ElideRight
                                        Layout.maximumWidth: win.whenWidth - (latestChip.visible ? latestChip.width + 8 : 0) - (browsingChip.visible ? browsingChip.width + 8 : 0)
                                        color: row.textColor
                                    }
                                    Chip { id: latestChip; visible: row.latest; text: qsTr("Latest") }
                                    Chip { id: browsingChip; visible: row.browsing; text: qsTr("Browsing"); accent: true }
                                    Item { Layout.fillWidth: true }
                                }
                                Label {
                                    Layout.preferredWidth: win.tookWidth
                                    horizontalAlignment: Text.AlignRight
                                    elide: Text.ElideLeft
                                    text: row.duration >= 0 ? overview.formatDuration(row.duration) : "–"
                                    font.pixelSize: 13
                                    font.features: { "tnum": 1 }
                                    color: row.picked && view.activeFocus ? row.textColor : theme.muted
                                }
                                // New and changed files.
                                Label {
                                    Layout.preferredWidth: win.filesWidth
                                    horizontalAlignment: Text.AlignRight
                                    elide: Text.ElideLeft
                                    font.pixelSize: 13
                                    font.features: { "tnum": 1 }
                                    color: row.picked && view.activeFocus ? row.textColor : theme.muted
                                    text: {
                                        translator.language
                                        if (!row.hasSummary) return "–"
                                        if (row.filesNew + row.filesChanged === 0) return qsTr("No changes")
                                        return qsTr("%1 new, %2 changed").arg(Number(row.filesNew).toLocaleString(Qt.locale(), "f", 0))
                                                                        .arg(Number(row.filesChanged).toLocaleString(Qt.locale(), "f", 0))
                                    }
                                }
                                // What it added, as a number and a bar on the chart's scale.
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    Item { Layout.fillWidth: true }
                                    Rectangle {
                                        Layout.preferredWidth: Math.max(40, Math.min(140, parent.width - 90))
                                        implicitHeight: 6
                                        radius: 3
                                        color: Qt.alpha(theme.muted, 0.15)
                                        visible: row.added >= 0
                                        Rectangle {
                                            width: row.added > 0 ? Math.max(3, parent.width * row.ratio) : 0
                                            height: parent.height
                                            radius: 3
                                            color: row.picked ? theme.accent : Qt.alpha(theme.accent, 0.6)
                                        }
                                    }
                                    Label {
                                        Layout.preferredWidth: 68
                                        horizontalAlignment: Text.AlignRight
                                        text: row.added >= 0 ? backend.formatSize(row.added) : "–"
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        font.features: { "tnum": 1 }
                                        color: row.textColor
                                    }
                                }
                                // Kept, and by which rule; or going.
                                RowLayout {
                                    Layout.preferredWidth: win.cleanupWidth
                                    Layout.maximumWidth: win.cleanupWidth
                                    Layout.leftMargin: 8
                                    spacing: 6
                                    Label {
                                        visible: row.removing
                                        text: ""
                                        font.family: win.iconFont
                                        font.pixelSize: 12
                                        color: theme.changedColor
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        font.pixelSize: 12
                                        color: row.removing ? theme.changedColor : row.picked && view.activeFocus ? row.textColor : theme.muted
                                        text: { translator.language; return row.removing ? qsTr("Goes at next cleanup") : win.keptText(row.kept) }
                                    }
                                }
                            }

                            MouseArea {
                                id: rowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: { view.forceActiveFocus(); overview.selectRow(row.index) }
                                onDoubleClicked: view.browse(row.snapshot)
                            }
                        }
                    }

                    // Nothing to list: say why, and what to do.
                    ColumnLayout {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 40, 420)
                        visible: overview.backups.count === 0 && view.health !== "loading"
                        spacing: 10
                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: view.health === "unreachable" ? "" : ""
                            font.family: win.iconFont
                            font.pixelSize: 34
                            color: view.health === "unreachable" ? theme.removed : theme.muted
                        }
                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            text: view.health === "unreachable" ? qsTr("Your backups couldn't be opened.") : qsTr("There are no backups of this machine yet.")
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            color: theme.foreground
                        }
                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            text: view.health === "unreachable" ? backend.repositoryError
                                  : view.service.available ? qsTr("Press B to make the first one with %1.").arg(view.service.unit) : ""
                            font.pixelSize: 13
                            color: theme.muted
                        }
                    }
                    BusyIndicator {
                        anchors.centerIn: parent
                        running: view.health === "loading" && overview.backups.count === 0
                        visible: running
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: theme.border }
                // How to get around, quietly.
                Row {
                    Layout.margins: 10
                    Layout.leftMargin: 20
                    spacing: 16
                    Hint { keys: ["↑", "↓"]; label: qsTr("pick") }
                    Hint { keys: ["Enter"]; label: qsTr("browse its files") }
                    Hint { keys: ["W"]; label: qsTr("what changed") }
                    Hint { keys: ["S"]; label: qsTr("newest or biggest first") }
                    Hint { keys: ["Tab"]; label: qsTr("back to files") }
                }
            }

            Rectangle { Layout.fillHeight: true; width: 1; color: theme.border }

            BackupDetail {
                Layout.fillHeight: true
                Layout.preferredWidth: Math.max(380, Math.min(460, Math.round(view.width * 0.34)))
                onBrowse: view.browse(overview.selected)
                onShowChanges: view.showChanges(overview.selected)
                onOpenFile: (path) => view.openFile(overview.selected, path)
            }
        }
    }

    component Tile: Rectangle {
        id: tile
        property string glyph
        property string title
        property string value
        property string detail
        property string tip
        property bool loading: false
        // A short warning beside the value, like "2 backups go".
        property string badge
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        implicitHeight: 76
        radius: theme.radius
        color: theme.background
        border.width: 1
        border.color: theme.border
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            anchors.leftMargin: 14
            spacing: 3
            RowLayout {
                spacing: 7
                Label { text: tile.glyph; font.family: win.iconFont; font.pixelSize: 11; color: theme.muted }
                Label { text: tile.title; font.pixelSize: 11; font.weight: Font.DemiBold; font.capitalization: Font.AllUppercase; font.letterSpacing: 0.5; color: theme.muted }
            }
            // A placeholder the size of the number while it loads.
            Item {
                Layout.fillWidth: true
                implicitHeight: 24
                Rectangle {
                    visible: tile.badge.length > 0
                    x: Math.min(valueLabel.contentWidth + 10, parent.width - width)
                    anchors.verticalCenter: parent.verticalCenter
                    implicitWidth: badgeLabel.implicitWidth + 12
                    implicitHeight: 20
                    radius: Math.min(4, theme.radius)
                    color: Qt.alpha(theme.changedColor, 0.14)
                    border.width: 1
                    border.color: Qt.alpha(theme.changedColor, 0.45)
                    Row {
                        anchors.centerIn: parent
                        spacing: 5
                        Label { text: "\uf1b8"; font.family: win.iconFont; font.pixelSize: 10; color: theme.changedColor }
                        Label { id: badgeLabel; text: tile.badge; font.pixelSize: 11; font.weight: Font.DemiBold; color: theme.foreground }
                    }
                }
                Label {
                    id: valueLabel
                    visible: !tile.loading || tile.value.length > 0
                    width: parent.width
                    text: tile.value
                    elide: Text.ElideRight
                    font.pixelSize: 19
                    font.weight: Font.DemiBold
                    font.features: { "tnum": 1 }
                    color: theme.foreground
                }
                Rectangle {
                    visible: tile.loading && tile.value.length === 0
                    anchors.verticalCenter: parent.verticalCenter
                    width: 80
                    height: 14
                    radius: 4
                    color: Qt.alpha(theme.muted, 0.18)
                    SequentialAnimation on opacity {
                        running: parent.visible
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.4; duration: 600 }
                        NumberAnimation { to: 1; duration: 600 }
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                text: tile.detail
                elide: Text.ElideRight
                font.pixelSize: 12
                color: theme.muted
            }
        }
        MouseArea { id: tileMouse; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
        ToolTip.visible: tileMouse.containsMouse && (tip.length > 0 || detail.length > 30)
        ToolTip.delay: 500
        ToolTip.text: tip.length > 0 && tip !== detail ? detail + "\n" + tip : detail
    }

    component Chip: Rectangle {
        property string text
        property bool accent: false
        implicitWidth: chipLabel.implicitWidth + 12
        implicitHeight: 18
        radius: Math.min(4, theme.radius)
        color: accent ? theme.accent : Qt.alpha(theme.muted, 0.16)
        Label {
            id: chipLabel
            anchors.centerIn: parent
            text: parent.text
            font.pixelSize: 10
            font.weight: Font.DemiBold
            color: parent.accent ? theme.accentForeground : theme.muted
        }
    }

    component Segment: Rectangle {
        id: segment
        property string text
        property bool checked
        signal clicked()
        implicitWidth: segmentLabel.implicitWidth + 20
        implicitHeight: 26
        radius: Math.min(4, theme.radius)
        color: checked ? theme.accent : (segmentMouse.containsMouse ? theme.raised : theme.surface)
        border.width: 1
        border.color: checked ? theme.accent : theme.border
        Label {
            id: segmentLabel
            anchors.centerIn: parent
            text: segment.text
            font.pixelSize: 12
            font.weight: segment.checked ? Font.DemiBold : Font.Normal
            color: segment.checked ? theme.accentForeground : theme.foreground
        }
        MouseArea { id: segmentMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: segment.clicked() }
    }

    component Hint: Row {
        property var keys: []
        property string label
        spacing: 4
        Repeater { model: parent.keys; KeyCap { required property string modelData; key: modelData; tint: theme.muted } }
        Label { anchors.verticalCenter: parent.verticalCenter; leftPadding: 2; text: parent.label; font.pixelSize: 12; color: theme.muted }
    }
}
