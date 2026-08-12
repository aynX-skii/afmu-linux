import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import Afmu

Item {
    id: page

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.padLg
        spacing: Theme.gapMd

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gapMd

            ColumnLayout {
                spacing: 2
                Text {
                    text: Tr.t("接收服务")
                    font.pixelSize: Theme.fsXl
                    font.bold: true
                    color: Theme.text
                }
                Text {
                    text: Tr.t("在本机开一个同协议的服务端，让手机把文件推过来")
                    font.pixelSize: Theme.fsSm
                    color: Theme.textFaint
                }
            }

            Item { Layout.fillWidth: true }

            StatBadge {
                label: App.serverRunning ? Tr.t("运行中 · 端口 ") + App.serverPort : Tr.t("已停止")
                tone: App.serverRunning ? Theme.success : Theme.textFaint
            }

            // 常态下发现应答不含设备名（§1.5）。要让别人在列表里看到「icelab」，
            // 得在这里主动点一下 —— 于是「陌生人能看到设备名」的窗口
            // 从「永远」缩短成「用户主动开启的那一分钟」。
            FlatButton {
                iconName: "radar"
                visible: App.serverRunning
                text: App.pairingMode
                      ? Tr.t("可被发现 · ") + App.pairingRemaining + "s"
                      : Tr.t("允许被发现")
                variant: App.pairingMode ? FlatButton.Variant.Danger : FlatButton.Variant.Ghost
                onClicked: App.pairingMode ? App.stopPairingMode() : App.startPairingMode()
            }

            FlatButton {
                iconName: "power"
                text: App.serverRunning ? Tr.t("停止服务") : Tr.t("启动服务")
                variant: App.serverRunning ? FlatButton.Variant.Danger : FlatButton.Variant.Primary
                onClicked: App.serverRunning ? App.stopServer() : App.startServer()
            }
        }

        // ------------------------------------------------- 传输状态提示（常驻）
        //
        // 服务开着就一直显示，且不可关闭：服务没停这件事就没变，而三周前点掉的一个
        // 提示不算今天的知情同意。
        //
        // **但它必须说实话。** 这条横幅原来是无条件的「未加密」—— v1 时代那是对的，
        // 现在零信任模式下明文根本进不来，那句话就成了假的。一条恒真的警告等于
        // 训练用户忽略它，下次它真的该响的时候没人会看。所以它跟着实际生效的状态走，
        // 而「实际生效」和设置页上的开关不是一回事：零信任会压掉另外两个。
        Rectangle {
            id: transportNote
            Layout.fillWidth: true
            visible: App.serverRunning
            Layout.preferredHeight: warnRow.implicitHeight + Theme.pad
            radius: Theme.radius

            // 和 AppController::applyServerContext 里的判断保持一致
            readonly property bool plaintextOn: App.config.allowLegacyPlaintext
                                                && !App.config.zeroTrustMode
            readonly property bool guestOn: App.config.guestMode && !App.config.zeroTrustMode

            // 加密起不来、明文又被拒 = 这个端口现在谁也进不来。这是个安全属性，
            // 不只是文案：开关显示着「开」却在服务明文，正是这些开关反对的事。
            readonly property bool deadEnd: !App.tlsReady && !plaintextOn

            readonly property color tone: (deadEnd || !App.tlsReady || plaintextOn || guestOn)
                                        ? Theme.warning : Theme.success

            readonly property string title:
                  deadEnd       ? Tr.t("谁也连不上")
                : !App.tlsReady ? Tr.t("未加密")
                : plaintextOn   ? Tr.t("明文连接仍然开着")
                : guestOn       ? Tr.t("只接受加密连接（含访客）")
                                : Tr.t("只接受已配对设备的加密连接")

            readonly property string detail:
                  deadEnd       ? Tr.t("要求只走加密，但加密没能就绪，于是所有连接一律拒绝 —— 不会悄悄退回明文。原因见下面的日志。")
                : !App.tlsReady ? Tr.t("流量是明文 HTTP。同一网络里的任何人都能看到文件名和文件内容。请只在信任的网络里使用。")
                : plaintextOn   ? Tr.t("已配对的设备走加密，其余连接仍是明文 —— 那些文件名和内容同一网络里的人都看得到。")
                : guestOn       ? Tr.t("流量已加密，挡得住偷听。但访客模式开着：没配对过的设备凭访问密码也能进，它们的身份没有经过验证。")
                                : Tr.t("只有配对表里的设备连得上，认证靠双方的密钥。这是 v2 完整的那道防线。")

            color: Qt.rgba(tone.r, tone.g, tone.b, 0.12)
            border.width: 1
            border.color: Qt.rgba(tone.r, tone.g, tone.b, 0.35)

            RowLayout {
                id: warnRow
                anchors.fill: parent
                anchors.margins: Theme.pad / 2
                spacing: Theme.pad / 2

                AppIcon {
                    name: transportNote.tone === Theme.success ? "lock" : "alert"
                    color: transportNote.tone
                    Layout.alignment: Qt.AlignTop
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        text: transportNote.title
                        font.pixelSize: Theme.fsSm
                        font.bold: true
                        color: transportNote.tone
                    }
                    Text {
                        Layout.fillWidth: true
                        text: transportNote.detail
                        font.pixelSize: Theme.fsSm
                        color: Theme.textFaint
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        // ---------------------------------------------------------- 连接信息
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: infoGrid.implicitHeight + 2 * Theme.pad
            radius: Theme.radius
            color: Theme.surface
            border.width: 1
            border.color: Theme.borderSoft

            GridLayout {
                id: infoGrid
                anchors.fill: parent
                anchors.margins: Theme.pad
                columns: 3
                columnSpacing: Theme.padLg
                rowSpacing: Theme.gapMd

                // 本机 token
                ColumnLayout {
                    spacing: 4
                    SectionLabel { text: Tr.t("本机 TOKEN") }
                    RowLayout {
                        spacing: Theme.gap
                        Text {
                            text: App.config.localToken
                            font.pixelSize: Theme.fsLg
                            font.family: Theme.mono
                            font.letterSpacing: 1.5
                            color: Theme.accent
                        }
                        IconButton {
                            iconName: "copy"
                            iconSize: 14
                            boxSize: 24
                            tip: Tr.t("复制")
                            onClicked: App.copyToClipboard(App.config.localToken)
                        }
                        IconButton {
                            iconName: "refresh"
                            iconSize: 14
                            boxSize: 24
                            tip: Tr.t("重新生成")
                            onClicked: regenDialog.open2(Tr.t("重新生成本机 token？"),
                                Tr.t("已经填了旧 token 的设备将无法再连接本机，需要重新抄一次。"), false)
                        }
                    }
                    Text {
                        text: Tr.t("在手机 App 的「PC token」里填这一串")
                        font.pixelSize: Theme.fsXs
                        color: Theme.textFaint
                    }
                    FlatButton {
                        text: Tr.t("显示配对二维码")
                        iconName: "qr"
                        implicitHeight: 28
                        onClicked: pairDialog.open()
                    }
                }

                // 地址
                ColumnLayout {
                    spacing: 4
                    SectionLabel { text: Tr.t("本机地址") }
                    Repeater {
                        model: App.localAddresses
                        delegate: RowLayout {
                            required property var modelData
                            spacing: Theme.gap
                            Text {
                                text: modelData + ":" + (App.serverRunning ? App.serverPort
                                                                           : App.config.serverPort)
                                font.pixelSize: Theme.fsMd
                                font.family: Theme.mono
                                color: Theme.text
                            }
                            IconButton {
                                iconName: "copy"
                                iconSize: 13
                                boxSize: 22
                                onClicked: App.copyToClipboard(
                                    modelData + ":" + (App.serverRunning ? App.serverPort
                                                                        : App.config.serverPort))
                            }
                        }
                    }
                    Text {
                        visible: App.localAddresses.length === 0
                        text: Tr.t("没有可用的局域网地址")
                        font.pixelSize: Theme.fsSm
                        color: Theme.textFaint
                    }
                }

                // 开关
                ColumnLayout {
                    spacing: Theme.gap
                    SectionLabel { text: Tr.t("行为") }
                    AppSwitch {
                        text: Tr.t("可被发现（应答 UDP 探测）")
                        checked: App.config.discoverable
                        onToggled: {
                            App.config.discoverable = checked
                            App.restartServerIfRunning()
                        }
                    }
                    AppSwitch {
                        text: Tr.t("只读（拒绝上传 / 删除 / 建目录）")
                        checked: App.config.readOnly
                        onToggled: App.config.readOnly = checked
                    }
                    AppSwitch {
                        text: Tr.t("启动应用时自动开启服务")
                        checked: App.config.autoStartServer
                        onToggled: App.config.autoStartServer = checked
                    }
                    AppSwitch {
                        text: Tr.t("允许连接请求（没有 token 的设备可以来敲门）")
                        checked: App.config.allowAuthRequests
                        onToggled: App.config.allowAuthRequests = checked
                    }
                }
            }
        }

        // ---------------------------------------------------------- 共享目录
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 168
            radius: Theme.radius
            color: Theme.surface
            border.width: 1
            border.color: Theme.borderSoft
            clip: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.pad
                spacing: Theme.gap

                RowLayout {
                    Layout.fillWidth: true
                    SectionLabel { text: Tr.t("共享目录（对端只能访问这些目录及其子目录）") }
                    Item { Layout.fillWidth: true }
                    FlatButton {
                        text: Tr.t("添加目录")
                        iconName: "plus"
                        implicitHeight: 28
                        onClicked: rootDialog.open()
                    }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    model: App.config.serveRoots
                    ScrollBar.vertical: AppScrollBar {}

                    delegate: Rectangle {
                        required property var modelData
                        width: ListView.view.width
                        height: 30
                        radius: Theme.radiusXs
                        color: Theme.surfaceAlt

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.gap
                            anchors.rightMargin: 4
                            spacing: Theme.gap

                            AppIcon { name: "folder"; size: 14 }
                            Text {
                                Layout.fillWidth: true
                                text: modelData
                                font.pixelSize: Theme.fsSm
                                font.family: Theme.mono
                                color: Theme.textDim
                                elide: Text.ElideMiddle
                            }
                            Text {
                                visible: modelData === App.config.inboxDir
                                text: Tr.t("收件箱")
                                font.pixelSize: Theme.fsXs
                                color: Theme.success
                            }
                            IconButton {
                                iconName: "x"
                                iconSize: 13
                                boxSize: 24
                                enabled: App.config.serveRoots.length > 1
                                activeColor: Theme.danger
                                onClicked: App.config.removeServeRoot(modelData)
                            }
                        }
                    }
                }
            }
        }

        // ---------------------------------------------------------- 日志
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 110
            radius: Theme.radius
            color: Theme.surface
            border.width: 1
            border.color: Theme.borderSoft
            clip: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.pad
                spacing: Theme.gap

                RowLayout {
                    Layout.fillWidth: true
                    SectionLabel { text: Tr.t("活动日志") }
                    Item { Layout.fillWidth: true }
                    IconButton {
                        iconName: "trash"
                        iconSize: 14
                        boxSize: 24
                        tip: Tr.t("清空")
                        onClicked: App.clearLog()
                    }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: App.serverLog
                    ScrollBar.vertical: AppScrollBar {}

                    delegate: Text {
                        required property var modelData
                        width: ListView.view.width
                        text: modelData
                        font.pixelSize: Theme.fsXs
                        font.family: Theme.mono
                        color: Theme.textFaint
                        elide: Text.ElideRight
                        bottomPadding: 3
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: App.serverLog.length === 0
                    text: Tr.t("暂无活动")
                    font.pixelSize: Theme.fsSm
                    color: Theme.textFaint
                }
            }
        }
    }

    ConfirmDialog {
        id: regenDialog
        confirmText: Tr.t("重新生成")
        onAccepted: App.config.regenerateLocalToken()
    }

    PairDialog { id: pairDialog }

    FolderDialog {
        id: rootDialog
        title: Tr.t("选择要共享的目录")
        onAccepted: App.config.addServeRoot(App.urlToLocalPath(selectedFolder))
    }
}
