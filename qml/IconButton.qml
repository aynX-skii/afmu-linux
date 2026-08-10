import QtQuick
import QtQuick.Controls.Basic
import Afmu

Button {
    id: control

    property string iconName: ""
    property real iconSize: 16
    property color baseColor: Theme.textDim
    property color activeColor: Theme.text
    property string tip: ""
    property real boxSize: 30

    implicitWidth: boxSize
    implicitHeight: boxSize
    hoverEnabled: true
    focusPolicy: Qt.NoFocus

    contentItem: AppIcon {
        name: control.iconName
        size: control.iconSize
        anchors.centerIn: parent
        color: !control.enabled ? Theme.textFaint
                                : (control.hovered ? control.activeColor : control.baseColor)
    }

    background: Rectangle {
        radius: Theme.radiusXs
        color: control.down ? Theme.pressed : (control.hovered ? Theme.hover : "transparent")
        Behavior on color { ColorAnimation { duration: Theme.anim } }
    }

    ToolTip.visible: tip.length > 0 && hovered
    ToolTip.delay: 500
    ToolTip.text: tip
}
