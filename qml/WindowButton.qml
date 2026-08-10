import QtQuick
import QtQuick.Controls.Basic
import Afmu

// 标题栏右上角的窗口按钮，自绘
Item {
    id: root

    property string iconName: "minus"
    property bool danger: false
    signal clicked()

    implicitWidth: 46
    implicitHeight: Theme.titleBarH

    Rectangle {
        anchors.fill: parent
        color: {
            if (!ma.containsMouse)
                return "transparent"
            if (root.danger)
                return ma.pressed ? Qt.darker(Theme.danger, 1.2) : Theme.danger
            return ma.pressed ? Theme.pressed : Theme.hover
        }
        Behavior on color { ColorAnimation { duration: 90 } }
    }

    AppIcon {
        anchors.centerIn: parent
        name: root.iconName
        size: 15
        weight: 1.9
        color: ma.containsMouse ? (root.danger ? "#ffffff" : Theme.text) : Theme.textDim
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.clicked()
    }
}
