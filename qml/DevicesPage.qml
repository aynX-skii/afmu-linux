import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Afmu

Item {
    id: page

    // 把输入框里的 token 落进配置。
    //
    // 光靠 onEditingFinished 是不够的：本页的按钮全是 focusPolicy: Qt.NoFocus，
    // 设备列表是纯 MouseArea，点它们都不会让输入框失焦。用户敲完 token 直接双击设备时，
    // token 还停在控件里没进配置 —— 于是要么报“还没有填对端 token”，要么拿旧 token 撞 401。
    // 所以每个发起连接的入口都先显式提交一次。
    function commitToken() {
        var t = tokenField.text.trim()
        if (t !== App.config.peerToken)
            App.config.peerToken = t
    }

    function connectTo(host, port, name, os) {
        commitToken()
        App.connectToDevice(host, port, name, os)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.padLg
        spacing: Theme.gapLg

        // ---------------------------------------------------------- 页头
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gapMd

            ColumnLayout {
                spacing: 2
                Text {
                    text: Tr.t("设备")
                    font.pixelSize: Theme.fsXl
                    font.bold: true
                    color: Theme.text
                }
                Text {
                    text: Tr.t("向局域网广播 AFMU-DISCOVER 探测包，UDP ") + 8766
                    font.pixelSize: Theme.fsSm
                    color: Theme.textFaint
                }
            }

            Item { Layout.fillWidth: true }

            FlatButton {
                text: Tr.t("显示二维码")
                iconName: "qr"
                onClicked: pairDialog.open()
            }

            FlatButton {
                text: App.scanning ? Tr.t("扫描中…") : Tr.t("扫描局域网")
                iconName: "radar"
                variant: FlatButton.Variant.Primary
                enabled: !App.scanning
                onClicked: App.scan()
            }
        }

        // ---------------------------------------------------------- token
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: tokenRow.implicitHeight + 2 * Theme.pad
            radius: Theme.radius
            color: Theme.surface
            border.width: 1
            border.color: App.config.peerToken.length === 0
                          ? Theme.alpha(Theme.warning, 0.45) : Theme.borderSoft

            RowLayout {
                id: tokenRow
                anchors.fill: parent
                anchors.margins: Theme.pad
                spacing: Theme.gapMd

                AppIcon {
                    name: "lock"
                    size: 18
                    color: App.config.peerToken.length === 0 ? Theme.warning : Theme.textDim
                }

                ColumnLayout {
                    spacing: 2
                    Layout.preferredWidth: 220
                    Text {
                        text: Tr.t("对端 token")
                        font.pixelSize: Theme.fsMd
                        color: Theme.text
                    }
                    Text {
                        text: Tr.t("留空也行：点「请求授权」让对方在它自己屏幕上确认")
                        font.pixelSize: Theme.fsXs
                        color: Theme.textFaint
                    }
                }

                AppTextField {
                    id: tokenField
                    Layout.preferredWidth: 190
                    monospace: true
                    placeholderText: Tr.t("例如 a7k2m9x4qp")
                    text: App.config.peerToken
                    // 边敲边存（节流），配置和输入框里看到的内容不会再不一致
                    onTextEdited: tokenCommit.restart()
                    onEditingFinished: page.commitToken()

                    Timer {
                        id: tokenCommit
                        interval: 250
                        onTriggered: page.commitToken()
                    }
                }

                Item { Layout.fillWidth: true }

                Text {
                    visible: App.config.peerToken.length > 0
                    text: Tr.t("明文 HTTP，token 只防误连，不要在公共 Wi-Fi 上使用")
                    font.pixelSize: Theme.fsXs
                    color: Theme.textFaint
                }
            }
        }

        // ---------------------------------------------------------- 结果列表
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radius
            color: Theme.surface
            border.width: 1
            border.color: Theme.borderSoft
            clip: true

            ProgressLine {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: 1
                height: 3
                visible: App.scanning
                indeterminate: true
            }

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: Theme.gap
                anchors.topMargin: Theme.gap + 3
                spacing: Theme.gap
                model: App.devices
                clip: true
                visible: count > 0
                ScrollBar.vertical: AppScrollBar {}

                delegate: Rectangle {
                    required property int index
                    required property string name
                    required property string os
                    required property string host
                    required property int port

                    width: list.width - (list.ScrollBar.vertical.visible ? 10 : 0)
                    height: 62
                    radius: Theme.radiusSm
                    color: itemMa.containsMouse ? Theme.hover : Theme.surfaceAlt
                    border.width: 1
                    border.color: App.connected && App.peerHost === host && App.peerPort === port
                                  ? Theme.accent : Theme.borderSoft
                    Behavior on color { ColorAnimation { duration: Theme.anim } }

                    MouseArea {
                        id: itemMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        // 点列表时把焦点从输入框收回来，符合桌面端的一般预期
                        onPressed: page.forceActiveFocus()
                        onDoubleClicked: page.connectTo(host, port, name, os)
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.gapMd
                        spacing: Theme.gapMd

                        Rectangle {
                            Layout.preferredWidth: 36
                            Layout.preferredHeight: 36
                            radius: Theme.radiusSm
                            color: Theme.elevated

                            AppIcon {
                                anchors.centerIn: parent
                                name: os === "android" ? "phone" : (os === "linux" ? "monitor" : "drive")
                                size: 18
                                color: Theme.accent
                            }
                        }

                        ColumnLayout {
                            spacing: 3
                            Layout.fillWidth: true

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                Text {
                                    text: name
                                    font.pixelSize: Theme.fsMd
                                    font.bold: true
                                    color: Theme.text
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                // 这个列表里混着两种设备：刚应答广播的，和配对表里的。
                                // 是哪一种决定了流量加不加密，不该让用户去猜。
                                // （paired 由发现协议的滚动 rid 认出来，见 v2 §6.1）
                                AppIcon {
                                    visible: paired
                                    name: "lock"
                                    size: 13
                                    color: Theme.success
                                }
                            }
                            Text {
                                text: host + ":" + port + "  ·  " + os
                                font.pixelSize: Theme.fsXs
                                color: Theme.textFaint
                            }
                        }

                        StatBadge {
                            visible: App.connected && App.peerHost === host && App.peerPort === port
                            label: Tr.t("已连接")
                            tone: Theme.success
                        }

                        // 加密配对排在前面，也是唯一给 Primary 的按钮：这是推荐路径。
                        // 「请求授权」走的还是 v1 明文那套，留着是为了还没升级的对端。
                        FlatButton {
                            text: Tr.t("加密配对")
                            iconName: "lock"
                            variant: FlatButton.Variant.Subtle
                            // 已经在配对表里的就别再问一遍了
                            visible: App.tlsReady && !paired
                            enabled: !App.authPending
                            onClicked: App.requestPairing(host, port, name, os)
                        }

                        FlatButton {
                            text: Tr.t("请求授权")
                            iconName: "link"
                            variant: FlatButton.Variant.Subtle
                            enabled: !App.authPending
                            onClicked: App.requestAuthorization(host, port, name, os)
                        }

                        FlatButton {
                            text: Tr.t("连接")
                            iconName: "link"
                            variant: FlatButton.Variant.Subtle
                            onClicked: page.connectTo(host, port, name, os)
                        }
                    }
                }
            }

            EmptyState {
                anchors.centerIn: parent
                visible: list.count === 0
                iconName: "radar"
                title: App.scanning ? Tr.t("正在扫描局域网…") : Tr.t("还没有发现设备")
                subtitle: App.scanning
                          ? ""
                          : Tr.t("确认对方设备与本机在同一 Wi-Fi，并且它的接收服务已经打开。") + "\n"
                            + Tr.t("路由器开启 AP 隔离时广播会被吃掉，这种情况下用下面的手动连接。")
            }
        }

        // ---------------------------------------------------------- 手动连接
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: manualRow.implicitHeight + 2 * Theme.pad
            radius: Theme.radius
            color: Theme.surface
            border.width: 1
            border.color: Theme.borderSoft

            RowLayout {
                id: manualRow
                anchors.fill: parent
                anchors.margins: Theme.pad
                spacing: Theme.gapMd

                AppIcon { name: "link"; size: 18 }

                Text {
                    text: Tr.t("手动连接")
                    font.pixelSize: Theme.fsMd
                    color: Theme.text
                }

                AppTextField {
                    id: hostField
                    Layout.fillWidth: true
                    placeholderText: Tr.t("192.168.1.30:8765  或  127.0.0.1:18765（adb forward）")
                    text: App.config.lastHost.length > 0
                          ? App.config.lastHost + ":" + App.config.lastPort : ""
                    onAccepted: App.connectManual(text, tokenField.text)
                }

                FlatButton {
                    text: Tr.t("连接")
                    variant: FlatButton.Variant.Primary
                    enabled: hostField.text.trim().length > 0
                    onClicked: App.connectManual(hostField.text, tokenField.text)
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: Tr.t("没有 Wi-Fi 时：adb forward tcp:18765 tcp:8765，然后连 127.0.0.1:18765；")
                  + Tr.t("反向让手机推到本机用 adb reverse tcp:8765 tcp:8765。")
            font.pixelSize: Theme.fsXs
            color: Theme.alpha(Theme.textFaint, 0.85)
            wrapMode: Text.WordWrap
        }
    }

    PairDialog { id: pairDialog }
    AuthWaitDialog {}
}
