import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
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
                    text: Tr.t("传输")
                    font.pixelSize: Theme.fsXl
                    font.bold: true
                    color: Theme.text
                }
                Text {
                    text: Tr.t("下载落到 ") + App.config.downloadDir
                    font.pixelSize: Theme.fsSm
                    color: Theme.textFaint
                }
            }

            Item { Layout.fillWidth: true }

            FlatButton {
                iconName: "folderOpen"
                text: Tr.t("打开下载目录")
                onClicked: App.openLocalFolder(App.config.downloadDir)
            }
            FlatButton {
                iconName: "x"
                text: Tr.t("全部取消")
                enabled: App.transfers.activeCount > 0
                onClicked: App.transfers.cancelAll()
            }
            FlatButton {
                iconName: "trash"
                text: Tr.t("清除已结束")
                enabled: App.transfers.count > App.transfers.activeCount
                onClicked: App.transfers.clearFinished()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radius
            color: Theme.surface
            border.width: 1
            border.color: Theme.borderSoft
            clip: true

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: Theme.gap
                spacing: Theme.gap
                model: App.transfers
                visible: count > 0
                clip: true
                ScrollBar.vertical: AppScrollBar {}

                delegate: Rectangle {
                    id: row
                    required property int index
                    required property var tid
                    required property string name
                    required property int kind
                    required property string kindText
                    required property int state
                    required property string stateText
                    required property real progress
                    required property string detail
                    required property string localPath

                    // 0 Download 1 Upload 2 ServerIncoming 3 ServerOutgoing
                    readonly property bool inbound: kind === 0 || kind === 2
                    readonly property color tone: state === 3 ? Theme.danger
                                                 : state === 2 ? Theme.success
                                                 : state === 4 ? Theme.textFaint
                                                 : (inbound ? Theme.accent : Theme.success)

                    width: list.width - (list.ScrollBar.vertical.visible ? 10 : 0)
                    height: 72
                    radius: Theme.radiusSm
                    color: Theme.surfaceAlt
                    border.width: 1
                    border.color: state === 3 ? Theme.alpha(Theme.danger, 0.35) : Theme.borderSoft

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.gapMd
                        spacing: 7

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.gap

                            Rectangle {
                                Layout.preferredWidth: 26
                                Layout.preferredHeight: 26
                                radius: Theme.radiusXs
                                color: Theme.alpha(row.tone, 0.14)

                                AppIcon {
                                    anchors.centerIn: parent
                                    name: row.inbound ? "download" : "upload"
                                    size: 14
                                    color: row.tone
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: name
                                font.pixelSize: Theme.fsMd
                                color: Theme.text
                                elide: Text.ElideMiddle
                            }

                            StatBadge {
                                label: kindText
                                tone: Theme.textFaint
                                dot: false
                            }
                            StatBadge {
                                label: stateText
                                tone: row.tone
                            }

                            IconButton {
                                visible: state === 0 || state === 1
                                iconName: "x"
                                iconSize: 14
                                boxSize: 26
                                tip: Tr.t("取消")
                                activeColor: Theme.danger
                                onClicked: App.transfers.cancel(tid)
                            }
                            IconButton {
                                visible: state === 3 || state === 4
                                iconName: "rotate"
                                iconSize: 14
                                boxSize: 26
                                tip: Tr.t("重试")
                                onClicked: App.transfers.retry(tid)
                            }
                            IconButton {
                                visible: state === 2 && row.inbound
                                iconName: "folderOpen"
                                iconSize: 14
                                boxSize: 26
                                tip: Tr.t("在文件管理器中显示")
                                onClicked: App.transfers.revealInFileManager(tid)
                            }
                            IconButton {
                                visible: state === 2 || state === 3 || state === 4
                                iconName: "trash"
                                iconSize: 14
                                boxSize: 26
                                tip: Tr.t("从列表移除")
                                onClicked: App.transfers.remove(tid)
                            }
                        }

                        ProgressLine {
                            Layout.fillWidth: true
                            value: progress
                            tone: row.tone
                            indeterminate: state === 1 && progress <= 0
                        }

                        Text {
                            Layout.fillWidth: true
                            text: detail
                            font.pixelSize: Theme.fsXs
                            color: state === 3 ? Theme.danger : Theme.textFaint
                            elide: Text.ElideMiddle
                        }
                    }
                }
            }

            EmptyState {
                anchors.centerIn: parent
                visible: list.count === 0
                iconName: "repeat"
                title: Tr.t("还没有传输任务")
                subtitle: Tr.t("在「浏览文件」里下载，或把文件拖进窗口上传。") + "\n"
                          + Tr.t("手机推过来的文件也会出现在这里。")
            }
        }
    }
}
