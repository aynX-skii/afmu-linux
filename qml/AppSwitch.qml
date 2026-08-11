import QtQuick
import QtQuick.Controls.Basic
import Afmu

Switch {
    id: control

    implicitHeight: 26
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    spacing: Theme.gapMd

    indicator: Rectangle {
        implicitWidth: 40
        implicitHeight: 22
        x: control.leftPadding
        y: parent.height / 2 - height / 2
        radius: height / 2
        // 置灰要连滑块一起：只把文字变淡、滑块还亮着，看起来像是能点的
        opacity: control.enabled ? 1.0 : 0.45
        color: control.checked ? Theme.accent : Theme.elevated
        border.width: 1
        border.color: control.checked ? Theme.accent
                                      : (control.hovered ? Theme.border : Theme.borderSoft)
        Behavior on color { ColorAnimation { duration: Theme.anim } }

        Rectangle {
            x: control.checked ? parent.width - width - 3 : 3
            y: 3
            width: 16
            height: 16
            radius: 8
            color: control.checked ? Theme.textOnAccent : Theme.textDim
            Behavior on x { NumberAnimation { duration: Theme.anim; easing.type: Easing.OutCubic } }
            Behavior on color { ColorAnimation { duration: Theme.anim } }
        }
    }

    contentItem: Text {
        text: control.text
        visible: control.text.length > 0
        font.pixelSize: Theme.fsMd
        color: control.enabled ? Theme.text : Theme.textFaint
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator.width + control.spacing
    }

    background: null
}
