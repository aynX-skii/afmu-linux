import QtQuick
import QtQuick.Controls.Basic
import Afmu

Popup {
    id: root

    property string title: ""
    property string body: ""
    property string confirmText: Tr.t("确定")
    property bool destructive: false

    signal accepted()

    function open2(t, b, danger) {
        title = t
        body = b
        destructive = danger === true
        open()
    }

    modal: true
    dim: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    anchors.centerIn: Overlay.overlay
    width: 400
    padding: Theme.padLg

    background: Rectangle {
        color: Theme.surface
        radius: Theme.radius
        border.width: 1
        border.color: Theme.border
    }

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.55)
    }

    contentItem: Column {
        spacing: Theme.gapMd

        Text {
            width: parent.width
            text: root.title
            font.pixelSize: Theme.fsLg
            font.bold: true
            color: Theme.text
            wrapMode: Text.WordWrap
        }
        Text {
            width: parent.width
            visible: root.body.length > 0
            text: root.body
            font.pixelSize: Theme.fsMd
            lineHeight: 1.4
            color: Theme.textDim
            wrapMode: Text.WordWrap
        }
        Item { width: 1; height: Theme.gap }
        Row {
            anchors.right: parent.right
            spacing: Theme.gap

            FlatButton {
                text: Tr.t("取消")
                onClicked: root.close()
            }
            FlatButton {
                text: root.confirmText
                variant: root.destructive ? FlatButton.Variant.Danger : FlatButton.Variant.Primary
                onClicked: {
                    root.close()
                    root.accepted()
                }
            }
        }
    }
}
