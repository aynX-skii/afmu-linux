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

            FlatButton {
                iconName: "power"
                text: App.serverRunning ? Tr.t("停止服务") : Tr.t("启动服务")
                variant: App.serverRunning ? FlatButton.Variant.Danger : FlatButton.Variant.Primary
                onClicked: App.serverRunning ? App.stopServer() : App.startServer()
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
