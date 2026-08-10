import QtQuick
import QtQuick.Controls.Basic
import Afmu

Popup {
    id: root

    property string title: ""
    property string placeholder: ""
    property string confirmText: Tr.t("确定")

    signal submitted(string value)

    function open2(t, ph, initial) {
        title = t
        placeholder = ph
        field.text = initial !== undefined ? initial : ""
        open()
        field.forceActiveFocus()
        field.selectAll()
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
        AppTextField {
            id: field
            width: parent.width
            placeholderText: root.placeholder
            onAccepted: root.commit()
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
                variant: FlatButton.Variant.Primary
                enabled: field.text.trim().length > 0
                onClicked: root.commit()
            }
        }
    }

    function commit() {
        const v = field.text.trim()
        if (v.length === 0)
            return
        close()
        submitted(v)
    }
}
