import QtQuick
import QtQuick.Controls.Basic
import Afmu

CheckBox {
    id: control

    implicitHeight: 22
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    padding: 0
    spacing: Theme.gap

    indicator: Rectangle {
        implicitWidth: 17
        implicitHeight: 17
        x: control.leftPadding
        y: parent.height / 2 - height / 2
        radius: Theme.radiusXs
        color: control.checked ? Theme.accent : "transparent"
        border.width: 1
        border.color: control.checked ? Theme.accent
                                      : (control.hovered ? Theme.textDim : Theme.border)
        Behavior on color { ColorAnimation { duration: 90 } }

        AppIcon {
            anchors.centerIn: parent
            name: "check"
            size: 13
            weight: 3
            color: Theme.textOnAccent
            opacity: control.checked ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 90 } }
        }
    }

    contentItem: Text {
        text: control.text
        visible: control.text.length > 0
        font.pixelSize: Theme.fsMd
        color: Theme.text
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator.width + control.spacing
    }

    background: null
}
