import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Afmu

Item {
    id: page

    // 等用户确认的那一次「忘记」。见列表里的垃圾桶按钮。
    property string pendingForgetHost: ""
    property int pendingForgetPort: 0
    property string pendingForgetName: ""

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
                    // 一旦 delegate 里出现 required property，QML 就**只**注入声明过的
                    // 那几个角色，其余角色不再作为上下文属性可见。漏声明的后果不是
                    // 报个错就算了：绑定抛 ReferenceError 之后属性停在默认值上，于是
                    // 「已配对就藏起来」的按钮一直显示，锁图标也一直亮着 —— 界面对
                    // "这台设备配过没有"这个问题给出的答案是恒定的，而且是错的。
                    required property bool paired
                    // 这次扫描真的听到它应答了吗。假 = 这一行是从配对表里列出来的，
                    // 地址是上次见到它的那个。**必须显示出来**：本机分不清「设备关着」
                    // 和「广播被吃掉了」，两种都表现为没应答，所以只能如实说「没应答」，
                    // 不能让「列表里有它」被读成「它开着」。
                    required property bool heard

                    width: list.width - (list.ScrollBar.vertical.visible ? 10 : 0)
                    height: 62
                    radius: Theme.radiusSm
                    opacity: heard ? 1 : 0.62
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
                                text: heard
                                      ? host + ":" + port + "  ·  " + os
                                      : host + ":" + port + "  ·  " + Tr.t("上次见到它的地址")
                                font.pixelSize: Theme.fsXs
                                color: Theme.textFaint
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }

                        // 没应答的照样能点：广播走不通不等于 TCP 走不通 —— 恰恰是
                        // 最常见的一种情况（Windows 防火墙默认拦入站 UDP，AP 隔离，
                        // 手机息屏）。所以这里说的是"它没应答"，不是"它不可用"。
                        StatBadge {
                            visible: !heard
                            label: Tr.t("未应答")
                            tone: Theme.textFaint
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
                            // 已经在配对表里的就别再问一遍了 —— 除非对端已经不认得
                            // 本机（配对只剩单边），那时候这颗按钮正是唯一的出路。
                            visible: App.tlsReady
                                     && (!paired || App.forgotUsPeer === host + ":" + port)
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

                        IconButton {
                            iconName: "trash"
                            tip: Tr.t("忘记这台设备")
                            activeColor: Theme.danger
                            onClicked: {
                                // 没配对过的那一行只是这一轮扫描的观察结果，删掉什么
                                // 都不损失 —— 设备真在网上，下次扫描它自己会回来。
                                // 为这种事弹个窗问一句，纯属挡路。
                                if (!paired) {
                                    App.forgetDevice(host, port)
                                    return
                                }
                                // 配对过的完全是另一回事：配对表在 v2 里就是访问控制
                                // 列表，删掉这条等于关掉一道门，而要再打开得两台设备
                                // 都在手边重新配一次。这种事必须先问。
                                page.pendingForgetHost = host
                                page.pendingForgetPort = port
                                page.pendingForgetName = name
                                forgetDialog.open2(
                                    Tr.t("忘记这台设备"),
                                    Tr.t("这台设备已经配对过。忘记之后它无法再连接本机，")
                                        + Tr.t("要用得两台设备都在手边重新配对一次。") + "\n\n"
                                        + (name !== "" ? name + "\n" : "") + host + ":" + port,
                                    true)
                            }
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

    ConfirmDialog {
        id: forgetDialog
        confirmText: Tr.t("忘记")
        onAccepted: {
            App.forgetDevice(page.pendingForgetHost, page.pendingForgetPort)
            page.pendingForgetHost = ""
            page.pendingForgetPort = 0
            page.pendingForgetName = ""
        }
    }
    AuthWaitDialog {}
}
