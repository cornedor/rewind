import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// What each backup added to the repository, oldest on the left. One bar per
// backup; the picked one is solid. A bar taller than the scale (the first
// backup, usually) is cut with a notch and labelled, so it doesn't flatten the
// rest. Hover says when and how much; a click picks the backup.
Item {
    id: chart
    implicitHeight: 132

    readonly property var points: overview.chart
    // The scale rounded up to 1, 2 or 5 times a power of ten (of the size
    // unit), so the grid lines read as round numbers.
    readonly property real scale: niceScale(overview.chartScale)
    readonly property int axisWidth: 64
    readonly property int gap: barSlot >= 5 ? 2 : barSlot >= 3 ? 1 : 0
    // When there are more backups than fit, the newest that fit are shown.
    readonly property int fitting: Math.max(1, Math.floor(plot.width / 3))
    readonly property int first: Math.max(0, points.length - fitting)
    readonly property var shown: points.slice(first)
    readonly property real barSlot: shown.length > 0 ? Math.min(48, plot.width / shown.length) : 0
    property int hovered: -1

    function niceLabel(bytes) { return backend.formatSize(Math.round(bytes)) }
    function niceScale(value) {
        if (!(value > 0)) return 0
        const unit = Math.pow(1024, Math.floor(Math.log(value) / Math.log(1024)))
        const inUnit = value / unit
        const power = Math.pow(10, Math.floor(Math.log10(inUnit)))
        const step = [1, 2, 5, 10].find(f => f * power >= inUnit)
        return step * power * unit
    }

    // Grid lines at the top of the scale and halfway, with their values.
    Repeater {
        model: chart.scale > 0 ? [1, 0.5] : []
        Item {
            required property real modelData
            x: 0
            y: plot.y + plot.height * (1 - modelData)
            width: chart.width
            Rectangle { x: chart.axisWidth; width: plot.width; height: 1; color: Qt.alpha(theme.border, 0.8) }
            Label {
                width: chart.axisWidth - 10
                y: -height / 2
                horizontalAlignment: Text.AlignRight
                text: chart.niceLabel(chart.scale * modelData)
                font.pixelSize: 11
                font.features: { "tnum": 1 }
                color: theme.muted
            }
        }
    }
    Rectangle { x: plot.x; y: plot.y + plot.height; width: plot.width; height: 1; color: theme.border }

    Item {
        id: plot
        x: chart.axisWidth
        y: 18
        width: chart.width - chart.axisWidth
        height: chart.height - 18 - 22

        // Barred to the right edge, so the newest backup is always in the
        // same place.
        Row {
            id: bars
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: parent.height
            spacing: 0
            layoutDirection: Qt.LeftToRight
            Repeater {
                model: chart.shown
                Item {
                    id: slot
                    required property var modelData
                    required property int index
                    readonly property bool picked: modelData.snapshot === overview.selected
                    readonly property bool over: chart.scale > 0 && modelData.value > chart.scale * 1.001
                    readonly property real ratio: chart.scale > 0 ? Math.min(1, modelData.value / chart.scale) : 0
                    width: chart.barSlot
                    height: bars.height

                    Rectangle {
                        id: bar
                        anchors.bottom: parent.bottom
                        x: chart.gap / 2
                        width: Math.max(1, parent.width - chart.gap)
                        // A backup that added nothing still gets a sliver, so
                        // every backup is visible on the axis.
                        height: !slot.modelData.known ? 0 : Math.max(2, Math.round(parent.height * slot.ratio))
                        radius: Math.min(3, width / 2, theme.radius)
                        color: slot.picked ? theme.accent
                             : chart.hovered === slot.index ? Qt.alpha(theme.accent, 0.75)
                             : Qt.alpha(theme.accent, 0.42)
                        Behavior on height { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                        // Square at the axis: only the data end is rounded.
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: Math.min(parent.height, parent.radius)
                            color: parent.color
                        }
                        // The cut in a bar taller than the scale.
                        Rectangle {
                            visible: slot.over
                            y: 7
                            width: parent.width
                            height: 3
                            color: theme.surface
                        }
                    }
                    // A cut bar says how big it really is.
                    Label {
                        visible: slot.over && chart.barSlot >= 6
                        anchors.horizontalCenter: bar.horizontalCenter
                        anchors.bottom: bar.top
                        anchors.bottomMargin: 2
                        text: chart.niceLabel(slot.modelData.value)
                        font.pixelSize: 10
                        font.features: { "tnum": 1 }
                        color: theme.muted
                    }
                    // Past the end of the bar too: thin bars are hard to hit.
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onEntered: chart.hovered = slot.index
                        onExited: if (chart.hovered === slot.index) chart.hovered = -1
                        onClicked: overview.select(slot.modelData.snapshot)
                    }
                }
            }
        }

        // Where the time axis starts and ends.
        Label {
            visible: chart.shown.length > 1 && bars.width > 320
            anchors.top: parent.bottom
            anchors.topMargin: 5
            x: parent.width - bars.width
            text: { backend.clock; translator.language; return chart.shown.length ? backend.formatMoment(chart.shown[0].time) : "" }
            font.pixelSize: 11
            color: theme.muted
        }
        Label {
            visible: chart.shown.length > 0
            anchors.top: parent.bottom
            anchors.topMargin: 5
            anchors.right: parent.right
            text: { backend.clock; translator.language; return chart.shown.length ? backend.formatMoment(chart.shown[chart.shown.length - 1].time) : "" }
            font.pixelSize: 11
            color: theme.muted
        }

        // The hover card, kept inside the chart.
        Rectangle {
            id: tip
            readonly property var point: chart.hovered >= 0 && chart.hovered < chart.shown.length ? chart.shown[chart.hovered] : null
            visible: point !== null
            readonly property real anchorX: bars.x + (chart.hovered + 0.5) * chart.barSlot
            x: Math.max(0, Math.min(plot.width - width, anchorX - width / 2))
            y: -14
            width: tipColumn.implicitWidth + 20
            height: tipColumn.implicitHeight + 12
            radius: Math.min(6, theme.radius)
            color: theme.raised
            border.width: 1
            border.color: theme.border
            z: 5
            Column {
                id: tipColumn
                anchors.centerIn: parent
                spacing: 1
                Label {
                    text: { backend.clock; translator.language; return tip.point ? backend.formatMoment(tip.point.time) : "" }
                    font.pixelSize: 12
                    color: theme.muted
                }
                Label {
                    text: !tip.point ? "" : tip.point.known ? qsTr("%1 added").arg(chart.niceLabel(tip.point.value))
                                                            : qsTr("Size not recorded")
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    font.features: { "tnum": 1 }
                    color: theme.foreground
                }
            }
        }
    }

    Label {
        visible: chart.first > 0
        anchors.right: parent.right
        anchors.top: parent.top
        text: qsTr("The newest %1 of %2 backups").arg(chart.shown.length).arg(chart.points.length)
        font.pixelSize: 11
        color: theme.muted
    }
}
