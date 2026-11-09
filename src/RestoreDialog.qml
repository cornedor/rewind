import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Says exactly where the restored version will land before anything happens.
Popup {
    id: dialog
    property var plan: ({})
    property bool replace: false

    anchors.centerIn: Overlay.overlay
    width: 560
    modal: true
    focus: true
    padding: 24
    Material.roundedScale: theme.radius
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    onAboutToShow: replace = false

    function confirm() {
        backend.restore(plan.existsNow && replace)
        close()
    }

    background: Rectangle {
        radius: theme.radius
        color: theme.raised
        border.width: 1
        border.color: theme.border
    }

    contentItem: FocusScope {
        implicitHeight: content.implicitHeight
        focus: true
        Keys.onReturnPressed: dialog.confirm()
        Keys.onEnterPressed: dialog.confirm()
        Keys.onUpPressed: dialog.replace = false
        Keys.onDownPressed: if (dialog.plan.existsNow) dialog.replace = true
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_1) { dialog.replace = false; event.accepted = true }
            else if (event.key === Qt.Key_2 && dialog.plan.existsNow) { dialog.replace = true; event.accepted = true }
        }

        ColumnLayout {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 16

            ColumnLayout {
                spacing: 4
                Label {
                    Layout.fillWidth: true
                    text: dialog.plan.isDir ? qsTr("Restore the folder “%1”").arg(dialog.plan.name || "")
                                            : qsTr("Restore “%1”").arg(dialog.plan.name || "")
                    font.pixelSize: 19
                    font.weight: Font.DemiBold
                    elide: Text.ElideMiddle
                    color: theme.foreground
                }
                Label {
                    text: qsTr("As it was on %1").arg(dialog.plan.when || "")
                    color: theme.muted
                }
            }

            // Deleted since: it simply goes back where it was.
            Choice {
                visible: !dialog.plan.existsNow
                checked: true
                number: ""
                title: qsTr("Put it back where it was")
                detail: dialog.plan.original || ""
                mono: true
            }

            Choice {
                visible: dialog.plan.existsNow === true
                checked: !dialog.replace
                number: "1"
                title: qsTr("Keep both")
                detail: qsTr("Saved next to the current one as “%1”.").arg(dialog.plan.copyName || "")
                onPicked: dialog.replace = false
            }
            Choice {
                visible: dialog.plan.existsNow === true
                checked: dialog.replace
                number: "2"
                title: dialog.plan.isDir ? qsTr("Replace the current folder") : qsTr("Replace the current file")
                detail: qsTr("The current version is moved aside, not deleted. Ctrl+Z puts it back.")
                onPicked: dialog.replace = true
            }

            Label {
                visible: dialog.plan.same === true && dialog.plan.existsNow === true
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("The file on disk is the same as this backup.")
                color: theme.muted
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 10
                Item { Layout.fillWidth: true }
                ActionButton { text: qsTr("Cancel"); key: "Esc"; onClicked: dialog.close() }
                ActionButton { text: qsTr("Restore"); key: "Enter"; primary: true; onClicked: dialog.confirm() }
            }
        }
    }

    component Choice: Rectangle {
        id: choice
        property bool checked
        property string number
        property string title
        property string detail
        property bool mono: false
        signal picked()
        Layout.fillWidth: true
        implicitHeight: choiceColumn.implicitHeight + 24
        radius: theme.radius
        color: checked ? Qt.alpha(theme.accent, 0.12) : (choiceMouse.containsMouse ? theme.surface : "transparent")
        border.width: checked ? 2 : 1
        border.color: checked ? theme.accent : theme.border

        RowLayout {
            anchors.fill: parent
            anchors.margins: 12
            anchors.leftMargin: 16
            spacing: 14
            Rectangle {
                width: 18; height: 18; radius: 9
                color: "transparent"
                border.width: 2
                border.color: choice.checked ? theme.accent : theme.muted
                Rectangle { anchors.centerIn: parent; width: 8; height: 8; radius: 4; color: theme.accent; visible: choice.checked }
            }
            ColumnLayout {
                id: choiceColumn
                Layout.fillWidth: true
                spacing: 3
                Label { text: choice.title; font.weight: Font.DemiBold; color: theme.foreground }
                Label {
                    Layout.fillWidth: true
                    text: choice.detail
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    color: theme.muted
                    font.pixelSize: 13
                    font.family: choice.mono ? "monospace" : Qt.application.font.family
                }
            }
            KeyCap { visible: choice.number.length > 0; key: choice.number; tint: theme.muted }
        }
        MouseArea { id: choiceMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: choice.picked() }
    }
}
