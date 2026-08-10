import QtQuick
import QtQuick.Controls.Basic
import Afmu

Button {
    id: control

    enum Variant { Primary, Ghost, Danger, Subtle }

    property string iconName: ""
    property int variant: FlatButton.Variant.Ghost
    property color accentColor: variant === FlatButton.Variant.Danger ? Theme.danger : Theme.accent

    readonly property bool solid: variant === FlatButton.Variant.Primary
                                  || variant === FlatButton.Variant.Danger
    readonly property color fg: !control.enabled
                                ? Theme.textFaint
                                : (control.solid ? Theme.textOnAccent
                                                 : (control.hovered ? Theme.text : Theme.textDim))

    implicitHeight: Theme.controlH
    leftPadding: Theme.gapMd
    rightPadding: Theme.gapMd
    hoverEnabled: true
    focusPolicy: Qt.NoFocus

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight

        Row {
            id: row
            anchors.centerIn: parent
            spacing: control.text.length > 0 && control.iconName.length > 0 ? Theme.gap : 0

            AppIcon {
                anchors.verticalCenter: parent.verticalCenter
                visible: control.iconName.length > 0
                name: control.iconName
                size: 16
                color: control.fg
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: control.text.length > 0
                text: control.text
                font.pixelSize: Theme.fsMd
                color: control.fg
            }
        }
    }

    background: Rectangle {
        radius: Theme.radiusSm
        color: {
            if (!control.enabled)
                return control.solid ? Theme.alpha(control.accentColor, 0.22) : "transparent"
            if (control.solid)
                return control.down ? Qt.darker(control.accentColor, 1.25)
                                    : (control.hovered ? Qt.lighter(control.accentColor, 1.12)
                                                       : control.accentColor)
            if (control.variant === FlatButton.Variant.Subtle)
                return control.down ? Theme.pressed : (control.hovered ? Theme.hover : Theme.elevated)
            return control.down ? Theme.pressed : (control.hovered ? Theme.hover : "transparent")
        }
        border.width: control.solid ? 0 : 1
        border.color: control.hovered || control.variant === FlatButton.Variant.Subtle
                      ? Theme.border : Theme.borderSoft
        Behavior on color { ColorAnimation { duration: Theme.anim } }
    }
}
