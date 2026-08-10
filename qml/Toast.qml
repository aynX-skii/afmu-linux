import QtQuick
import Afmu

Rectangle {
    id: root

    property string message: ""
    property bool isError: false

    function show(text, error) {
        message = text
        isError = error === true
        opacity = 1
        hideTimer.restart()
    }

    width: Math.min(row.implicitWidth + 2 * Theme.pad, parent ? parent.width - 2 * Theme.padLg : 520)
    height: Math.max(40, row.implicitHeight + Theme.gapMd)
    radius: Theme.radiusSm
    color: Theme.elevated
    border.width: 1
    border.color: isError ? Theme.alpha(Theme.danger, 0.5) : Theme.border
    opacity: 0
    visible: opacity > 0

    Behavior on opacity { NumberAnimation { duration: 180 } }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.gap

        AppIcon {
            anchors.verticalCenter: parent.verticalCenter
            name: root.isError ? "alert" : "checkCircle"
            size: 16
            color: root.isError ? Theme.danger : Theme.success
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, 480)
            text: root.message
            font.pixelSize: Theme.fsMd
            color: Theme.text
            wrapMode: Text.WordWrap
        }
    }

    Timer {
        id: hideTimer
        interval: root.isError ? 5200 : 2600
        onTriggered: root.opacity = 0
    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.opacity = 0
    }
}
