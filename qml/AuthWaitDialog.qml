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

    // 上一次失败了，而且用户还没点「知道了」。
    readonly property bool failed: App.authError.length > 0
    readonly property bool pairing: root.failed ? App.authErrorIsPairing : App.authIsPairing
    // 配对的比对码要等 commit + reveal 两个来回才算得出来
    readonly property bool waitingForCode: root.pairing && App.authSas.length === 0

    // 等待期间显示；**失败之后继续显示**，直到用户自己关掉。
    //
    // 原来这里只绑 authPending，成败都靠底部那条 5 秒的提示条交代。问题是失败可以
    // 快到 86 毫秒（对端只提供明文时 TLS 握手当场就崩）—— 弹窗一闪而过，用户的
    // 视线还在屏幕中间，看到的是"点了没反应，手机上也没弹"。而失败在这条流程里
    // 是常态：对端没开加密、服务没起、对方拒绝、超时，每一种都得说清楚下一步做什么。
    visible: App.authPending || root.failed

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
            text: root.failed
                  ? (root.pairing ? Tr.t("配对没能完成") : Tr.t("授权没能完成"))
                  : (root.pairing ? Tr.t("等待对方确认配对") : Tr.t("等待对方授权"))
            font.pixelSize: Theme.fsLg
            font.bold: true
            color: root.failed ? Theme.danger : Theme.text
        }

        Text {
            width: parent.width
            // 失败原因就写在这儿，用户视线本来就在这个框上。
            text: root.failed
                  ? App.authError
                  : (App.authStatus === "sending"
                     ? Tr.t("正在发送请求…")
                     : (root.pairing
                        ? Tr.t("已在 %1 上弹出配对确认，请核对下面的码再点「允许」。").arg(App.authTarget)
                        : Tr.t("已在 %1 上弹出通知，请点「允许」。").arg(App.authTarget)))
            font.pixelSize: Theme.fsMd
            color: Theme.textDim
            wrapMode: Text.WordWrap
        }

        Rectangle {
            visible: !root.failed
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
                    text: root.pairing ? Tr.t("比对码") : Tr.t("确认码")
                    font.pixelSize: Theme.fsXs
                    color: Theme.textFaint
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: !root.waitingForCode
                    text: root.pairing ? App.authSas : App.authCode
                    font.pixelSize: root.pairing ? 24 : 30
                    font.family: Theme.mono
                    font.letterSpacing: root.pairing ? 3 : 6
                    color: Theme.accent
                }
                // 第一步还没回来时比对码算不出来。与其留一片空白（看着像卡死了），
                // 不如说清楚现在在等什么 —— 对端息屏时这一步要等十几秒是常事。
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: root.waitingForCode
                    text: Tr.t("等对端应答…")
                    font.pixelSize: Theme.fsMd
                    color: Theme.textFaint
                }
            }
        }

        Text {
            visible: !root.failed
            width: parent.width
            text: root.pairing
                  ? Tr.t("这个码是本机自己算出来的，不是对方发来的 —— 所以它和对方屏幕上的一致，")
                    + Tr.t("就说明中间没有人在转发。不一致就直接取消。")
                  : Tr.t("对方屏幕上显示的确认码必须和这里一致，否则不要同意。")
            font.pixelSize: Theme.fsXs
            color: Theme.textFaint
            wrapMode: Text.WordWrap
        }

        ProgressLine {
            visible: !root.failed
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
                // 失败之后不再显示倒计时：那时它已经归零，留着只会让人以为还在等
                text: !root.failed && App.authRemaining > 0
                      ? Tr.t("剩余 ") + App.authRemaining + Tr.t(" 秒") : ""
                font.pixelSize: Theme.fsXs
                color: Theme.textFaint
            }
            FlatButton {
                id: cancelBtn
                anchors.right: parent.right
                text: root.failed ? Tr.t("知道了") : Tr.t("取消")
                variant: root.failed ? FlatButton.Variant.Primary : FlatButton.Variant.Ghost
                onClicked: root.failed ? App.dismissAuthError() : App.cancelAuthorization()
            }
        }
    }
}
