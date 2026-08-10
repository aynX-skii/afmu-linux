import QtQuick
import QtQuick.Controls.Basic
import Afmu

ScrollBar {
    id: control

    policy: ScrollBar.AsNeeded
    padding: 2

    contentItem: Rectangle {
        implicitWidth: 6
        implicitHeight: 6
        radius: 3
        color: control.pressed ? Theme.textDim : (control.hovered ? Theme.textFaint : Theme.border)
        opacity: control.policy === ScrollBar.AlwaysOn || control.size < 1.0 ? 1 : 0
        Behavior on color { ColorAnimation { duration: Theme.anim } }
        Behavior on opacity { NumberAnimation { duration: Theme.anim } }
    }

    background: Rectangle {
        color: "transparent"
    }
}
