import QtQuick
import QtQuick.Controls

// A unified diff, or a file's plain text, with line numbers.
Rectangle {
    id: view
    property bool showChanges: true
    property bool truncated: false

    radius: theme.radius
    color: theme.surface
    border.width: 1
    border.color: theme.border
    clip: true

    readonly property int gutter: 46

    ListView {
        id: lines
        anchors.fill: parent
        anchors.margins: 1
        model: backend.diff
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        reuseItems: true
        ScrollBar.vertical: ScrollBar {}
        onModelChanged: positionViewAtBeginning()
        Connections {
            target: backend.diff
            function onModelReset() { lines.positionViewAtBeginning() }
        }

        delegate: Rectangle {
            id: line
            required property string kind
            required property int oldLine
            required property int newLine
            required property string text

            width: ListView.view.width
            height: kind === "hunk" ? 28 : Math.max(20, content.implicitHeight + 2)
            color: kind === "added" ? Qt.alpha(theme.added, 0.14)
                 : kind === "removed" ? Qt.alpha(theme.removed, 0.14)
                 : kind === "hunk" ? Qt.alpha(theme.accent, 0.07) : "transparent"

            Row {
                id: numbers
                height: parent.height
                visible: line.kind !== "hunk"
                Label {
                    width: view.showChanges ? view.gutter : 0
                    visible: view.showChanges
                    height: parent.height
                    horizontalAlignment: Text.AlignRight
                    rightPadding: 8
                    topPadding: 1
                    text: line.oldLine > 0 ? line.oldLine : ""
                    font.family: "monospace"
                    font.pixelSize: 12
                    color: Qt.alpha(theme.muted, 0.8)
                }
                Label {
                    width: view.gutter
                    height: parent.height
                    horizontalAlignment: Text.AlignRight
                    rightPadding: 8
                    topPadding: 1
                    text: line.newLine > 0 && line.kind !== "removed" ? line.newLine : ""
                    font.family: "monospace"
                    font.pixelSize: 12
                    color: Qt.alpha(theme.muted, 0.8)
                }
                Label {
                    width: 18
                    visible: view.showChanges
                    height: parent.height
                    topPadding: 1
                    text: line.kind === "added" ? "+" : line.kind === "removed" ? "−" : ""
                    font.family: "monospace"
                    font.pixelSize: 13
                    font.weight: Font.Bold
                    color: line.kind === "added" ? theme.added : theme.removed
                }
            }

            Label {
                id: content
                anchors.left: numbers.right
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.leftMargin: view.showChanges ? 0 : 10
                anchors.verticalCenter: line.kind === "hunk" ? parent.verticalCenter : undefined
                topPadding: 1
                visible: line.kind !== "hunk"
                text: line.text.length ? line.text : " "
                textFormat: Text.PlainText
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                font.family: "monospace"
                font.pixelSize: 13
                color: theme.foreground
            }

            Label {
                visible: line.kind === "hunk"
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.right: parent.right
                elide: Text.ElideRight
                text: qsTr("Line %1").arg(line.newLine) + (line.text.length ? "  ·  " + line.text : "")
                font.family: "monospace"
                font.pixelSize: 12
                color: theme.muted
            }
        }

        footer: Label {
            visible: view.truncated
            width: ListView.view.width
            padding: 12
            text: qsTr("Only the first 20,000 lines are shown. Press O to open the whole file.")
            color: theme.muted
        }
    }
}
