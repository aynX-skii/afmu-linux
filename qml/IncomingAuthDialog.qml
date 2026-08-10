import QtQuick
import QtQuick.Controls.Basic
import Afmu

/**
 * 反方向的授权弹窗：别的设备（另一台 PC，或者手机）想连本机，等用户点「允许」。
 *
 * 确认码必须和对方屏幕上显示的一致才能点允许 —— 同一个局域网里谁都能让这个弹窗跳出来，
 * 这四位是用户唯一能分辨「是不是自己刚发起的那一次」的东西。
 */
Popup {
    id: root

    visible: App.incomingAuthPending

    modal: true
    dim: true
    closePolicy: Popup.NoAutoClose
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

        Row {
            spacing: Theme.gapMd

            Rectangle {
                width: 36
                height: 36
                radius: Theme.radiusSm
                color: Theme.elevated

                AppIcon {
                    anchors.centerIn: parent
                    name: App.incomingAuthOs === "android"
                          ? "phone" : (App.incomingAuthOs === "linux" ? "monitor" : "drive")
                    size: 18
                    color: Theme.accent
                }
            }

            Column {
                spacing: 2

                Text {
                    text: Tr.t("有设备想连接本机")
                    font.pixelSize: Theme.fsLg
                    font.bold: true
                    color: Theme.text
                }
                Text {
                    text: App.incomingAuthName + "  ·  " + App.incomingAuthHost
                    font.pixelSize: Theme.fsSm
                    color: Theme.textDim
                }
            }
        }

        Rectangle {
            width: parent.width
            height: 74
            radius: Theme.radiusSm
            color: Theme.surfaceAlt
            border.width: 1
            border.color: Theme.borderSoft

            Column {
                anchors.centerIn: parent
                spacing: 4

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: Tr.t("确认码")
                    font.pixelSize: Theme.fsXs
                    color: Theme.textFaint
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: App.incomingAuthCode
                    font.pixelSize: 30
                    font.family: Theme.mono
                    font.letterSpacing: 6
                    color: Theme.accent
                }
            }
        }

        Text {
            width: parent.width
            text: Tr.t("只有对方屏幕上显示的确认码与此相同时才点「允许」。")
                  + Tr.t("允许之后本机的 token 会交给它，它就能浏览、上传和拉取本机共享的目录。")
            font.pixelSize: Theme.fsXs
            color: Theme.textFaint
            wrapMode: Text.WordWrap
        }

        Item {
            width: parent.width
            height: allowBtn.height

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: App.incomingAuthRemaining > 0
                      ? Tr.t("剩余 ") + App.incomingAuthRemaining + Tr.t(" 秒") : ""
                font.pixelSize: Theme.fsXs
                color: Theme.textFaint
            }

            Row {
                anchors.right: parent.right
                spacing: Theme.gap

                FlatButton {
                    text: Tr.t("拒绝")
                    onClicked: App.denyIncomingAuth()
                }
                FlatButton {
                    id: allowBtn
                    text: Tr.t("允许")
                    iconName: "lock"
                    variant: FlatButton.Variant.Primary
                    onClicked: App.approveIncomingAuth()
                }
            }
        }
    }
}
