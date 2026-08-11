import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import Afmu

Item {
    id: page

    // 等确认的解除配对，按指纹记 —— 列表可能在弹窗开着的时候变，按行号记会删错设备
    property string pendingUnpair: ""

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight + 2 * Theme.padLg
        clip: true
        ScrollBar.vertical: AppScrollBar {}

        ColumnLayout {
            id: col
            x: Theme.padLg
            y: Theme.padLg
            width: parent.width - 2 * Theme.padLg
            spacing: Theme.gapLg

            ColumnLayout {
                spacing: 2
                Text {
                    text: Tr.t("设置")
                    font.pixelSize: Theme.fsXl
                    font.bold: true
                    color: Theme.text
                }
                Text {
                    text: Tr.t("配置写在 ") + App.config.configFilePath() + Tr.t("（权限 600）")
                    font.pixelSize: Theme.fsSm
                    color: Theme.textFaint
                }
            }

            // ------------------------------------------------------ 本机
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: localCol.implicitHeight + 2 * Theme.pad
                radius: Theme.radius
                color: Theme.surface
                border.width: 1
                border.color: Theme.borderSoft

                ColumnLayout {
                    id: localCol
                    anchors.fill: parent
                    anchors.margins: Theme.pad
                    spacing: Theme.gapMd

                    SectionLabel { text: Tr.t("本机") }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gapMd
                        Text {
                            Layout.preferredWidth: 150
                            text: Tr.t("设备名")
                            font.pixelSize: Theme.fsMd
                            color: Theme.text
                        }
                        AppTextField {
                            Layout.fillWidth: true
                            text: App.config.deviceName
                            placeholderText: Tr.t("显示给对端的名字")
                            onEditingFinished: {
                                App.config.deviceName = text.trim()
                                App.restartServerIfRunning()
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gapMd
                        Text {
                            Layout.preferredWidth: 150
                            text: Tr.t("服务端口")
                            font.pixelSize: Theme.fsMd
                            color: Theme.text
                        }
                        AppTextField {
                            Layout.preferredWidth: 110
                            text: App.config.serverPort
                            validator: IntValidator { bottom: 1; top: 65535 }
                            onEditingFinished: {
                                App.config.serverPort = parseInt(text)
                                App.restartServerIfRunning()
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: Tr.t("被占用时会依次退到 8766 / 8767 / 随机端口，实际端口以发现应答为准")
                            font.pixelSize: Theme.fsXs
                            color: Theme.textFaint
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gapMd
                        Text {
                            Layout.preferredWidth: 150
                            text: Tr.t("收件箱")
                            font.pixelSize: Theme.fsMd
                            color: Theme.text
                        }
                        AppTextField {
                            Layout.fillWidth: true
                            text: App.config.inboxDir
                            onEditingFinished: App.config.inboxDir = text.trim()
                        }
                        FlatButton {
                            text: Tr.t("选择")
                            iconName: "folder"
                            onClicked: inboxDialog.open()
                        }
                        FlatButton {
                            iconName: "external"
                            onClicked: App.openLocalFolder(App.config.inboxDir)
                        }
                    }
                }
            }

            // ------------------------------------------------------ 客户端
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: clientCol.implicitHeight + 2 * Theme.pad
                radius: Theme.radius
                color: Theme.surface
                border.width: 1
                border.color: Theme.borderSoft

                ColumnLayout {
                    id: clientCol
                    anchors.fill: parent
                    anchors.margins: Theme.pad
                    spacing: Theme.gapMd

                    SectionLabel { text: Tr.t("客户端") }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gapMd
                        Text {
                            Layout.preferredWidth: 150
                            text: Tr.t("对端 token")
                            font.pixelSize: Theme.fsMd
                            color: Theme.text
                        }
                        AppTextField {
                            id: peerTokenField
                            Layout.preferredWidth: 200
                            monospace: true
                            text: App.config.peerToken
                            placeholderText: Tr.t("手机 App 首页显示的 token")
                            // 同 DevicesPage：切页 / 点无焦点按钮都不会触发 editingFinished，
                            // 只靠它提交会静默丢掉刚敲进去的 token
                            onTextEdited: peerTokenCommit.restart()
                            onEditingFinished: peerTokenCommit.commit()

                            Timer {
                                id: peerTokenCommit
                                interval: 250
                                onTriggered: commit()
                                function commit() {
                                    stop()
                                    var t = peerTokenField.text.trim()
                                    if (t !== App.config.peerToken)
                                        App.config.peerToken = t
                                }
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gapMd
                        Text {
                            Layout.preferredWidth: 150
                            text: Tr.t("下载目录")
                            font.pixelSize: Theme.fsMd
                            color: Theme.text
                        }
                        AppTextField {
                            Layout.fillWidth: true
                            text: App.config.downloadDir
                            onEditingFinished: App.config.downloadDir = text.trim()
                        }
                        FlatButton {
                            text: Tr.t("选择")
                            iconName: "folder"
                            onClicked: downloadDialog.open()
                        }
                        FlatButton {
                            iconName: "external"
                            onClicked: App.openLocalFolder(App.config.downloadDir)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gapMd
                        Text {
                            Layout.preferredWidth: 150
                            text: Tr.t("发现超时")
                            font.pixelSize: Theme.fsMd
                            color: Theme.text
                        }
                        AppTextField {
                            Layout.preferredWidth: 110
                            text: App.config.discoverTimeoutMs
                            validator: IntValidator { bottom: 300; top: 10000 }
                            onEditingFinished: App.config.discoverTimeoutMs = parseInt(text)
                        }
                        Text {
                            Layout.fillWidth: true
                            text: Tr.t("毫秒。建议 1000–2000，边收边等而不是固定 sleep")
                            font.pixelSize: Theme.fsXs
                            color: Theme.textFaint
                        }
                    }
                }
            }

            // ------------------------------------------------------ 界面
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: uiCol.implicitHeight + 2 * Theme.pad
                radius: Theme.radius
                color: Theme.surface
                border.width: 1
                border.color: Theme.borderSoft

                ColumnLayout {
                    id: uiCol
                    anchors.fill: parent
                    anchors.margins: Theme.pad
                    spacing: Theme.gapMd

                    SectionLabel { text: Tr.t("界面") }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gapMd
                        Text {
                            Layout.preferredWidth: 150
                            text: Tr.t("语言")
                            font.pixelSize: Theme.fsMd
                            color: Theme.text
                        }

                        Repeater {
                            model: [
                                { key: "system", label: Tr.t("跟随系统") },
                                { key: "zh",     label: "中文" },
                                { key: "en",     label: "English" }
                            ]
                            delegate: FlatButton {
                                required property var modelData
                                text: modelData.label
                                variant: Lang.language === modelData.key
                                         ? FlatButton.Variant.Primary : FlatButton.Variant.Subtle
                                onClicked: Lang.language = modelData.key
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: Tr.t("界面语言，切换后立即生效")
                            font.pixelSize: Theme.fsXs
                            color: Theme.textFaint
                        }
                    }
                }
            }

            // ------------------------------------------------------ 已配对设备（v2）
            //
            // 这张表在 v2 里就是访问控制列表：里面有指纹的设备才握得上 TLS。
            // 写入要等 §12 第 3–6 步的握手接上，但**删除入口必须先存在** ——
            // 否则第一次写进去的东西用户就拿不掉了，而那是一道开着的门。
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: peersCol.implicitHeight + 2 * Theme.pad
                radius: Theme.radius
                color: Theme.surface
                border.width: 1
                border.color: Theme.borderSoft

                ColumnLayout {
                    id: peersCol
                    anchors.fill: parent
                    anchors.margins: Theme.pad
                    spacing: Theme.gapMd

                    RowLayout {
                        Layout.fillWidth: true
                        SectionLabel { text: Tr.t("已配对设备") }
                        Item { Layout.fillWidth: true }
                        StatBadge {
                            visible: App.peers.count > 0
                            label: App.peers.count + " " + Tr.t("台")
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: App.peers.count === 0
                        text: Tr.t("还没有配对过的设备。加密连接（协议 v2）启用后，配对成功的设备会出现在这里。")
                        font.pixelSize: Theme.fsSm
                        color: Theme.textFaint
                        wrapMode: Text.WordWrap
                    }

                    Repeater {
                        model: App.peers

                        delegate: Rectangle {
                            required property int index
                            required property string fp
                            required property string fpDisplay
                            required property string name
                            required property string os
                            required property string lastAddress
                            required property string pairedAtText
                            required property bool pinned

                            Layout.fillWidth: true
                            Layout.preferredHeight: peerRow.implicitHeight + Theme.gapMd
                            radius: Theme.radiusSm
                            color: Theme.alpha(Theme.text, 0.03)
                            border.width: 1
                            border.color: Theme.borderSoft

                            RowLayout {
                                id: peerRow
                                anchors.fill: parent
                                anchors.margins: Theme.gap
                                spacing: Theme.gapMd

                                AppIcon {
                                    name: os === "android" ? "phone" : "monitor"
                                    size: 18
                                    color: Theme.textDim
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    RowLayout {
                                        spacing: Theme.gap
                                        Text {
                                            text: name !== "" ? name : Tr.t("未命名设备")
                                            font.pixelSize: Theme.fsMd
                                            color: Theme.text
                                        }
                                        // 只走加密、不允许回退明文（草案 §8.1）
                                        StatBadge {
                                            visible: pinned
                                            label: Tr.t("仅加密")
                                            tone: Theme.success
                                        }
                                    }

                                    // 指纹是这台设备的身份本身。全长显示，不截断 ——
                                    // 用户要拿它跟对端屏幕上的比，少一位就比不出问题。
                                    Text {
                                        Layout.fillWidth: true
                                        text: fpDisplay
                                        font.family: "monospace"
                                        font.pixelSize: Theme.fsXs
                                        color: Theme.textDim
                                        wrapMode: Text.WrapAnywhere
                                    }

                                    Text {
                                        text: [pairedAtText !== "" ? Tr.t("配对于 ") + pairedAtText : "",
                                               lastAddress !== "" ? Tr.t("上次 ") + lastAddress : ""]
                                              .filter(function (s) { return s !== "" }).join("  ·  ")
                                        font.pixelSize: Theme.fsXs
                                        color: Theme.textFaint
                                    }
                                }

                                IconButton {
                                    iconName: "copy"
                                    tip: Tr.t("复制指纹")
                                    onClicked: App.copyToClipboard(fpDisplay)
                                }
                                IconButton {
                                    iconName: "trash"
                                    tip: Tr.t("解除配对")
                                    activeColor: Theme.danger
                                    onClicked: {
                                        page.pendingUnpair = fp
                                        unpairDialog.open2(
                                            Tr.t("解除配对"),
                                            Tr.t("解除后这台设备将无法再连接本机，要用需重新配对。") + "\n\n"
                                                + (name !== "" ? name + "\n" : "") + fpDisplay,
                                            true)
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: App.peers.count > 0
                        text: Tr.t("配对关系不会自动过期 —— 半年没用的设备下次还能直接连。要清理只能在这里手动解除。")
                        font.pixelSize: Theme.fsXs
                        color: Theme.textFaint
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // ------------------------------------------------------ 说明
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: aboutCol.implicitHeight + 2 * Theme.pad
                radius: Theme.radius
                color: Theme.surface
                border.width: 1
                border.color: Theme.borderSoft

                ColumnLayout {
                    id: aboutCol
                    anchors.fill: parent
                    anchors.margins: Theme.pad
                    spacing: Theme.gap

                    SectionLabel { text: Tr.t("关于") }

                    Text {
                        Layout.fillWidth: true
                        text: Tr.t("FileBridge Linux 客户端 · AFMU 协议 v1") + "\n"
                              + Tr.t("发现走 UDP 8766 广播，传输走 HTTP/1.1 明文，端口以发现应答为准。") + "\n"
                              + Tr.t("协议是对称的：本机既能当客户端拉取手机上的文件，也能当服务端接收手机推来的文件。")
                        font.pixelSize: Theme.fsSm
                        lineHeight: 1.5
                        color: Theme.textDim
                        wrapMode: Text.WordWrap
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: warnRow.implicitHeight + Theme.gapMd
                        radius: Theme.radiusSm
                        color: Theme.alpha(Theme.warning, 0.08)
                        border.width: 1
                        border.color: Theme.alpha(Theme.warning, 0.25)

                        RowLayout {
                            id: warnRow
                            anchors.fill: parent
                            anchors.margins: Theme.gap
                            spacing: Theme.gap

                            AppIcon { name: "alert"; size: 16; color: Theme.warning }
                            Text {
                                Layout.fillWidth: true
                                text: Tr.t("token 只防同一局域网内的误连和顺手翻看，不是对抗嗅探的安全边界。")
                                      + Tr.t("不要在不可信网络（公共 Wi-Fi、咖啡厅）上开启服务。")
                                font.pixelSize: Theme.fsSm
                                color: Theme.warning
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }
        }
    }

    ConfirmDialog {
        id: unpairDialog
        confirmText: Tr.t("解除配对")
        onAccepted: {
            App.peers.remove(page.pendingUnpair)
            page.pendingUnpair = ""
        }
    }

    FolderDialog {
        id: downloadDialog
        title: Tr.t("选择下载目录")
        onAccepted: App.config.downloadDir = App.urlToLocalPath(selectedFolder)
    }

    FolderDialog {
        id: inboxDialog
        title: Tr.t("选择收件箱目录")
        onAccepted: App.config.inboxDir = App.urlToLocalPath(selectedFolder)
    }
}
