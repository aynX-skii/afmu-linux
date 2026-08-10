import QtQuick
import QtQuick.Controls.Basic
import Afmu

TextField {
    id: control

    property bool monospace: false

    implicitHeight: Theme.controlH
    implicitWidth: 200
    leftPadding: Theme.gapMd
    rightPadding: Theme.gapMd
    selectByMouse: true
    color: Theme.text
    placeholderTextColor: Theme.textFaint
    selectionColor: Theme.alpha(Theme.accent, 0.35)
    selectedTextColor: Theme.text
    font.pixelSize: Theme.fsMd
    font.family: monospace ? Theme.mono : font.family

    background: Rectangle {
        radius: Theme.radiusSm
        color: control.enabled ? Theme.surfaceAlt : Theme.surface
        border.width: 1
        border.color: control.activeFocus ? Theme.accent
                                          : (control.hovered ? Theme.border : Theme.borderSoft)
        Behavior on border.color { ColorAnimation { duration: Theme.anim } }
    }
}
