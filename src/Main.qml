import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1360
    height: 840
    minimumWidth: 980
    minimumHeight: 600
    visible: true
    title: past ? qsTr("Rewind: %1").arg(backend.moment.title) : qsTr("Rewind")
    color: theme.background

    Material.theme: theme.dark ? Material.Dark : Material.Light
    Material.accent: theme.accent
    Material.primary: theme.accent
    Material.background: theme.background
    Material.foreground: theme.foreground
    Material.roundedScale: theme.radius

    readonly property bool past: !backend.moment.isNow
    // "files" browses a moment in time; "backups" looks at the backups themselves.
    property string view: "files"
    readonly property bool browsing: view === "files" && !overlayOpen
    // The Backups list's columns, shared by its titles and rows.
    readonly property int whenWidth: 210
    readonly property int tookWidth: 84
    readonly property int filesWidth: 190
    readonly property int cleanupWidth: 190
    readonly property bool overlayOpen: restoreDialog.opened || changesPanel.opened || logDialog.opened
                                        || quitDialog.opened || help.visible
    readonly property string iconFont: "monospace"
    readonly property int small: Math.min(4, theme.radius)

    function focusList() { view === "backups" ? backupsView.forceActiveFocus() : fileList.forceActiveFocus() }

    function showView(name) {
        if (name === view) return
        if (name === "backups" && backend.snapshotIndex >= 0)
            overview.select(backend.snapshotIndex)
        if (topBar.filterActive) topBar.closeFilter()
        view = name
        focusList()
    }

    // From the Backups view into the files, at a backup.
    function browseBackup(snapshot) {
        backend.showSnapshot(snapshot)
        showView("files")
    }

    function healthColor(state) {
        switch (state) {
        case "ok": return theme.added
        case "failed": case "unreachable": return theme.removed
        case "skipped": case "late": return theme.changedColor
        case "running": case "loading": return theme.accent
        }
        return theme.muted
    }

    function healthGlyph(state) {
        switch (state) {
        case "ok": return "\uf00c"
        case "failed": case "unreachable": return "\uf00d"
        case "skipped": case "late": return "\uf071"
        }
        return "\uf1da"
    }

    // The cleanup rules that keep a backup: "hourly, daily".
    function keptText(rules) {
        if (!rules || rules.length === 0) return ""
        const names = {
            "last": qsTr("latest"), "hourly": qsTr("hourly"), "daily": qsTr("daily"), "weekly": qsTr("weekly"),
            "monthly": qsTr("monthly"), "yearly": qsTr("yearly")
        }
        return rules.map(r => names[r] !== undefined ? names[r] : r).join(", ")
    }

    function statusLabel(status) {
        switch (status) {
        case "deleted": return qsTr("Deleted")
        case "changed": return qsTr("Changed")
        case "new": return qsTr("Not backed up")
        }
        return ""
    }

    function statusColor(status) {
        switch (status) {
        case "deleted": return theme.removed
        case "changed": return theme.changedColor
        case "new": return theme.muted
        }
        return theme.muted
    }

    function glyph(name, isDir, isLink) {
        if (isLink) return ""
        if (isDir) return ""
        const dot = name.lastIndexOf(".")
        const ext = dot > 0 ? name.substring(dot + 1).toLowerCase() : ""
        if (["png", "jpg", "jpeg", "gif", "webp", "svg", "bmp", "avif", "heic"].includes(ext)) return ""
        if (["pdf"].includes(ext)) return ""
        if (["zip", "gz", "xz", "zst", "tar", "7z", "rar", "bz2"].includes(ext)) return ""
        if (["mp4", "mkv", "mov", "webm", "avi"].includes(ext)) return ""
        if (["mp3", "flac", "wav", "ogg", "m4a", "opus"].includes(ext)) return ""
        if (["js", "ts", "php", "py", "cpp", "h", "c", "rs", "go", "qml", "sh", "rb", "java", "kt", "lua",
             "json", "toml", "yaml", "yml", "xml", "html", "css", "scss", "conf", "ini"].includes(ext)) return ""
        if (["txt", "md", "csv", "log", "rst", "org"].includes(ext)) return ""
        return ""
    }

    function restore() {
        if (backend.restoring) return
        const plan = backend.restorePlan()
        if (!plan.ok) {
            toast.show(plan.reason)
            return
        }
        restoreDialog.plan = plan
        restoreDialog.open()
    }

    function handleEscape() {
        if (view === "backups") {
            showView("files")
            return
        }
        if (topBar.filterActive) {
            topBar.closeFilter()
        } else if (past) {
            backend.showSnapshot(-1)
        }
        focusList()
    }

    // Moving through time and between files.
    Shortcut { sequence: "["; enabled: win.browsing; context: Qt.ApplicationShortcut; onActivated: backend.older() }
    Shortcut { sequence: "]"; enabled: win.browsing; context: Qt.ApplicationShortcut; onActivated: backend.newer() }
    Shortcut { sequence: "{"; enabled: win.browsing; context: Qt.ApplicationShortcut; onActivated: backend.previousVersion() }
    Shortcut { sequence: "}"; enabled: win.browsing; context: Qt.ApplicationShortcut; onActivated: backend.nextVersion() }
    Shortcut {
        sequence: "Esc"
        enabled: !restoreDialog.opened && !changesPanel.opened && !logDialog.opened && !quitDialog.opened
        context: Qt.ApplicationShortcut
        onActivated: help.visible ? help.visible = false : win.handleEscape()
    }
    Shortcut { sequence: "C"; enabled: win.browsing && win.past; context: Qt.ApplicationShortcut; onActivated: backend.toggleCompare() }
    Shortcut { sequence: "W"; enabled: win.browsing && win.past; context: Qt.ApplicationShortcut; onActivated: changesPanel.open() }
    Shortcut { sequence: "O"; enabled: win.browsing; context: Qt.ApplicationShortcut; onActivated: backend.openExternally() }
    Shortcut { sequence: "Space"; enabled: win.browsing; context: Qt.ApplicationShortcut; onActivated: win.restore() }
    Shortcut { sequence: "/"; enabled: win.browsing; context: Qt.ApplicationShortcut; onActivated: topBar.openFilter() }
    Shortcut { sequence: "Ctrl+F"; enabled: win.browsing; context: Qt.ApplicationShortcut; onActivated: topBar.openFilter() }
    Shortcut { sequence: "Ctrl+H"; enabled: win.browsing; context: Qt.ApplicationShortcut; onActivated: backend.showHidden = !backend.showHidden }
    Shortcut { sequence: "."; enabled: win.browsing; context: Qt.ApplicationShortcut; onActivated: backend.showHidden = !backend.showHidden }
    Shortcut { sequence: "F"; enabled: win.browsing; context: Qt.ApplicationShortcut; onActivated: backend.onlyChanges = !backend.onlyChanges }
    Shortcut { sequence: "B"; enabled: !win.overlayOpen && backend.backups.available && !backend.backups.running; context: Qt.ApplicationShortcut; onActivated: backend.backups.start() }
    Shortcut { sequence: "R"; enabled: !win.overlayOpen && backend.repositoryState === "failed"; context: Qt.ApplicationShortcut; onActivated: backend.retry() }
    Shortcut { sequence: "~"; enabled: win.browsing; context: Qt.ApplicationShortcut; onActivated: backend.goTo("~") }
    Shortcut { sequences: [StandardKey.Undo]; enabled: win.browsing; context: Qt.ApplicationShortcut; onActivated: backend.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Shift+Z"]; enabled: win.browsing; context: Qt.ApplicationShortcut; onActivated: backend.redo() }
    Shortcut {
        sequence: "Tab"
        enabled: !win.overlayOpen && !topBar.filterActive
        context: Qt.ApplicationShortcut
        onActivated: win.showView(win.view === "files" ? "backups" : "files")
    }
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: help.visible = !help.visible }
    Shortcut {
        sequence: "Q"
        enabled: !restoreDialog.opened && !quitDialog.opened
        context: Qt.ApplicationShortcut
        onActivated: backend.restoring ? quitDialog.open() : Qt.quit()
    }

    Connections {
        target: backend
        function onNotify(message) { toast.show(message) }
    }
    Connections {
        target: overview
        function onCopied(message) { toast.show(message) }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            TopBar {
                id: topBar
                Layout.fillWidth: true
                onDone: win.focusList()
            }

            PastBanner {
                Layout.fillWidth: true
                visible: win.view === "files"
                onShowChanges: changesPanel.open()
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: win.view === "files" ? 0 : 1

                RowLayout {
                    spacing: 0

                    FileList {
                        id: fileList
                        Layout.fillHeight: true
                        Layout.preferredWidth: Math.round(win.width * 0.36)
                        Layout.minimumWidth: 380
                        focus: true
                    }

                    Rectangle { Layout.fillHeight: true; width: 1; color: theme.border }

                    PreviewPane {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        onRestoreRequested: win.restore()
                    }

                    Rectangle { Layout.fillHeight: true; width: 1; color: theme.border }

                    Timeline {
                        Layout.fillHeight: true
                        Layout.preferredWidth: 270
                    }
                }

                BackupsView {
                    id: backupsView
                    onBrowse: (snapshot) => win.browseBackup(snapshot)
                    onShowChanges: (snapshot) => { win.browseBackup(snapshot); changesPanel.open() }
                    onOpenFile: (snapshot, path) => {
                        win.browseBackup(snapshot)
                        const slash = path.lastIndexOf("/")
                        backend.goTo(slash > 0 ? path.substring(0, slash) : "/", path.substring(slash + 1))
                    }
                    onShowLog: logDialog.open()
                }
            }

            StatusBar {
                Layout.fillWidth: true
                onShowLog: logDialog.open()
                onShowBackups: win.showView("backups")
            }
        }

        // A frame in the accent around everything says "this is the past"
        // without reading a word.
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.width: win.past && win.view === "files" ? 3 : 0
            border.color: theme.accent
            Behavior on border.width { NumberAnimation { duration: 140 } }
        }

        HelpOverlay { id: help; anchors.fill: parent; visible: false }
        Toast { id: toast }
    }

    RestoreDialog {
        id: restoreDialog
        onClosed: win.focusList()
    }

    ChangesPanel {
        id: changesPanel
        onClosed: win.focusList()
    }

    LogDialog {
        id: logDialog
        onClosed: win.focusList()
    }

    Dialog {
        id: quitDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("A restore is still running")
        Label { text: qsTr("Quitting now stops it, and nothing it started is left behind.") }
        footer: DialogButtonBox {
            Button { text: qsTr("Keep restoring"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Button {
                text: qsTr("Stop and quit")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
        onAccepted: {
            backend.cancelRestore()
            Qt.quit()
        }
        onClosed: win.focusList()
    }
}
