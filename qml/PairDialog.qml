import QtQuick
import QtQuick.Controls.Basic
import Afmu

/**
 * 配对二维码。手机在 App 里点「扫码连接」对着扫，本机地址 + token 一次给全，
 * 用户不用再手抄那 10 位。
 */
Popup {
    id: root

    modal: true
    dim: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    anchors.centerIn: Overlay.overlay
    width: 380
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
            text: Tr.t("扫码连接")
            font.pixelSize: Theme.fsLg
            font.bold: true
            color: Theme.text
        }

        Text {
            width: parent.width
            text: Tr.t("在手机 App 里点「扫码连接」，对准下面的二维码。")
            font.pixelSize: Theme.fsSm
            color: Theme.textDim
            wrapMode: Text.WordWrap
        }

        // 二维码本身必须是浅底深色，扫码器对反色的容忍度很差
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 260
            height: 260
            radius: Theme.radiusSm
            color: "#ffffff"
            visible: App.pairUri.length > 0

            QrView {
                anchors.fill: parent
                anchors.margins: 6
                text: App.pairUri
                foreground: "#101216"
                background: "#ffffff"
            }
        }

        Text {
            width: parent.width
            visible: App.pairUri.length === 0
            text: Tr.t("没有可用的局域网地址，无法生成二维码。")
            font.pixelSize: Theme.fsSm
            color: Theme.warning
            wrapMode: Text.WordWrap
        }

        // 服务没开的话手机扫完码连不上——扫码之后手机会立刻回连一次做校验
        Row {
            width: parent.width
            visible: !App.serverRunning && App.pairUri.length > 0
            spacing: Theme.gap

            Text {
                width: parent.width - startBtn.width - Theme.gap
                anchors.verticalCenter: parent.verticalCenter
                text: Tr.t("本机服务还没启动，手机扫完码会连不上。")
                font.pixelSize: Theme.fsXs
                color: Theme.warning
                wrapMode: Text.WordWrap
            }
            FlatButton {
                id: startBtn
                anchors.verticalCenter: parent.verticalCenter
                text: Tr.t("启动服务")
                implicitHeight: 26
                onClicked: App.startServer()
            }
        }

        Text {
            width: parent.width
            text: Tr.t("二维码里含本机 token，别截图发出去。")
            font.pixelSize: Theme.fsXs
            color: Theme.textFaint
            wrapMode: Text.WordWrap
        }

        Row {
            anchors.right: parent.right
            spacing: Theme.gap

            FlatButton {
                text: Tr.t("复制内容")
                enabled: App.pairUri.length > 0
                onClicked: App.copyToClipboard(App.pairUri)
            }
            FlatButton {
                text: Tr.t("完成")
                variant: FlatButton.Variant.Primary
                onClicked: root.close()
            }
        }
    }
}
