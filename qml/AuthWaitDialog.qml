import QtQuick
import QtQuick.Controls.Basic
import Afmu

/**
 * 等对方在手机上点「允许」。
 *
 * 确认码是给用户比对用的：同一个局域网里谁都能让手机弹窗，只有屏幕上这四位和手机上
 * 显示的一致，才说明弹的是自己刚点的那次连接。
 */
Popup {
    id: root

    // 结果通过 toast 反馈，所以只在等待期间显示
    visible: App.authPending

    modal: true
    dim: true
    closePolicy: Popup.NoAutoClose
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
            text: App.authIsPairing ? Tr.t("等待对方确认配对") : Tr.t("等待对方授权")
            font.pixelSize: Theme.fsLg
            font.bold: true
            color: Theme.text
        }

        Text {
            width: parent.width
            text: App.authStatus === "sending"
                  ? Tr.t("正在发送请求…")
                  : (App.authIsPairing
                     ? Tr.t("已在 %1 上弹出配对确认，请核对下面的码再点「允许」。").arg(App.authTarget)
                     : Tr.t("已在 %1 上弹出通知，请点「允许」。").arg(App.authTarget))
            font.pixelSize: Theme.fsMd
            color: Theme.textDim
            wrapMode: Text.WordWrap
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
                    text: App.authIsPairing ? Tr.t("比对码") : Tr.t("确认码")
                    font.pixelSize: Theme.fsXs
                    color: Theme.textFaint
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: App.authIsPairing ? App.authSas : App.authCode
                    font.pixelSize: App.authIsPairing ? 24 : 30
                    font.family: Theme.mono
                    font.letterSpacing: App.authIsPairing ? 3 : 6
                    color: Theme.accent
                }
            }
        }

        Text {
            width: parent.width
            text: App.authIsPairing
                  ? Tr.t("这个码是本机自己算出来的，不是对方发来的 —— 所以它和对方屏幕上的一致，")
                    + Tr.t("就说明中间没有人在转发。不一致就直接取消。")
                  : Tr.t("对方屏幕上显示的确认码必须和这里一致，否则不要同意。")
            font.pixelSize: Theme.fsXs
            color: Theme.textFaint
            wrapMode: Text.WordWrap
        }

        ProgressLine {
            width: parent.width
            height: 3
            indeterminate: true
        }

        Item {
            width: parent.width
            height: cancelBtn.height

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: App.authRemaining > 0
                      ? Tr.t("剩余 ") + App.authRemaining + Tr.t(" 秒") : ""
                font.pixelSize: Theme.fsXs
                color: Theme.textFaint
            }
            FlatButton {
                id: cancelBtn
                anchors.right: parent.right
                text: Tr.t("取消")
                onClicked: App.cancelAuthorization()
            }
        }
    }
}
