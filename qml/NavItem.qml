import QtQuick
import Afmu

Item {
    id: root

    property string iconName: ""
    property string label: ""
    property bool active: false
    property int badge: 0
    property color badgeColor: Theme.accent

    signal clicked()

    implicitHeight: 38
    implicitWidth: 180

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: Theme.gap
        anchors.rightMargin: Theme.gap
        radius: Theme.radiusSm
        color: root.active ? Theme.alpha(Theme.accent, 0.14)
                           : (ma.containsMouse ? Theme.hover : "transparent")
        Behavior on color { ColorAnimation { duration: Theme.anim } }
    }

    // 选中指示条
    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        x: Theme.gap
        width: 2
        height: root.active ? 18 : 0
        radius: 1
        color: Theme.accent
        Behavior on height { NumberAnimation { duration: Theme.anim; easing.type: Easing.OutCubic } }
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: Theme.gap + Theme.gapMd
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.gapMd

        AppIcon {
            anchors.verticalCenter: parent.verticalCenter
            name: root.iconName
            size: 17
            color: root.active ? Theme.accent : (ma.containsMouse ? Theme.text : Theme.textDim)
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.label
            font.pixelSize: Theme.fsMd
            font.bold: root.active
            color: root.active ? Theme.text : (ma.containsMouse ? Theme.text : Theme.textDim)
        }
    }

    Rectangle {
        anchors.right: parent.right
        anchors.rightMargin: Theme.gap + Theme.gapMd
        anchors.verticalCenter: parent.verticalCenter
        visible: root.badge > 0
        width: Math.max(18, badgeText.implicitWidth + 10)
        height: 17
        radius: 8.5
        color: Theme.alpha(root.badgeColor, 0.18)
        border.width: 1
        border.color: Theme.alpha(root.badgeColor, 0.4)

        Text {
            id: badgeText
            anchors.centerIn: parent
            text: root.badge > 99 ? "99+" : root.badge
            font.pixelSize: Theme.fsXs
            color: root.badgeColor
        }
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
