import QtQuick
import Afmu

Rectangle {
    id: root

    property string label: ""
    property color tone: Theme.textDim
    property bool dot: true

    implicitHeight: 22
    implicitWidth: row.implicitWidth + 2 * Theme.gap
    radius: Theme.radiusXs
    color: Theme.alpha(tone, 0.12)
    border.width: 1
    border.color: Theme.alpha(tone, 0.28)

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6

        StatusDot {
            label: badgeLabel
            visible: root.dot
            size: 6
            color: root.tone
        }
        Text {
            id: badgeLabel
            text: root.label
            font.pixelSize: Theme.fsXs
            color: root.tone
        }
    }
}
