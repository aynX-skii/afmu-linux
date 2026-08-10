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

        // ---------------------------------------------------------- 页头
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gapMd

            ColumnLayout {
                spacing: 2
                Text {
                    text: Tr.t("浏览文件")
                    font.pixelSize: Theme.fsXl
                    font.bold: true
                    color: Theme.text
                }
                Text {
                    text: App.connected
                          ? App.peerName + (App.peerWritable ? "" : Tr.t(" · 只读"))
                          : Tr.t("未连接设备")
                    font.pixelSize: Theme.fsSm
                    color: Theme.textFaint
                }
            }

            Item { Layout.fillWidth: true }

            FlatButton {
                iconName: "upload"
                text: Tr.t("上传文件")
                variant: FlatButton.Variant.Primary
                enabled: App.connected && App.peerWritable
                onClicked: uploadDialog.open()
            }
            FlatButton {
                iconName: "download"
                text: Tr.t("下载选中 (") + App.files.selectedCount + ")"
                enabled: App.connected && App.files.selectedCount > 0
                onClicked: App.downloadSelected()
            }
        }

        // ---------------------------------------------------------- 工具条
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            radius: Theme.radiusSm
            color: Theme.surface
            border.width: 1
            border.color: Theme.borderSoft

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.gap
                anchors.rightMargin: Theme.gap
                spacing: Theme.gap

                IconButton {
                    iconName: "cornerUpLeft"
                    tip: Tr.t("上一级")
                    enabled: App.connected && !App.atRoot
                    onClicked: App.goParent()
                }
                IconButton {
                    iconName: "home"
                    tip: Tr.t("根目录")
                    enabled: App.connected
                    onClicked: App.goRoot()
                }
                IconButton {
                    iconName: "refresh"
                    tip: Tr.t("刷新 (F5)")
                    enabled: App.connected
                    onClicked: App.refresh()
                }

                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 18; color: Theme.border }

                // 面包屑
                Flickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: crumbs.implicitWidth
                    flickableDirection: Flickable.HorizontalFlick
                    clip: true

                    Row {
                        id: crumbs
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2

                        Repeater {
                            model: App.breadcrumbs

                            delegate: Row {
                                required property int index
                                required property var modelData
                                spacing: 2

                                AppIcon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: index > 0
                                    name: "chevronRight"
                                    size: 13
                                    color: Theme.textFaint
                                }

                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: crumbText.implicitWidth + 2 * Theme.gap
                                    height: 24
                                    radius: Theme.radiusXs
                                    color: crumbMa.containsMouse ? Theme.hover : "transparent"

                                    Text {
                                        id: crumbText
                                        anchors.centerIn: parent
                                        text: modelData.name
                                        font.pixelSize: Theme.fsSm
                                        font.bold: index === App.breadcrumbs.length - 1
                                        color: index === App.breadcrumbs.length - 1
                                               ? Theme.text : Theme.textDim
                                    }

                                    MouseArea {
                                        id: crumbMa
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: App.navigate(modelData.path)
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 18; color: Theme.border }

                IconButton {
                    iconName: "plus"
                    tip: Tr.t("新建目录")
                    enabled: App.connected && App.peerWritable && !App.atRoot
                    onClicked: mkdirDialog.open2(Tr.t("新建目录"), Tr.t("目录名"), "")
                }
                IconButton {
                    iconName: "trash"
                    tip: Tr.t("删除选中")
                    enabled: App.connected && App.peerWritable && App.files.selectedCount > 0
                    baseColor: Theme.textDim
                    activeColor: Theme.danger
                    onClicked: deleteDialog.open2(
                        Tr.t("删除 ") + App.files.selectedCount + Tr.t(" 项？"),
                        Tr.t("删除是不可逆的，对端没有回收站。目录会被递归删除。"), true)
                }
            }
        }

        // ---------------------------------------------------------- 文件列表
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radius
            color: Theme.surface
            border.width: 1
            border.color: Theme.borderSoft
            clip: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // 表头
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    color: Theme.surfaceAlt
                    topLeftRadius: Theme.radius
                    topRightRadius: Theme.radius

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.gapMd
                        anchors.rightMargin: Theme.gapMd
                        spacing: Theme.gapMd

                        AppCheckBox {
                            id: selectAllBox
                            enabled: App.files.count > 0
                            checked: App.files.count > 0 && App.files.selectedCount === App.files.count
                            onClicked: App.files.selectAll(checked)
                        }
                        Text {
                            Layout.fillWidth: true
                            text: Tr.t("名称")
                            font.pixelSize: Theme.fsXs
                            color: Theme.textFaint
                        }
                        Text {
                            Layout.preferredWidth: 96
                            text: Tr.t("大小")
                            horizontalAlignment: Text.AlignRight
                            font.pixelSize: Theme.fsXs
                            color: Theme.textFaint
                        }
                        Text {
                            Layout.preferredWidth: 128
                            text: Tr.t("修改时间")
                            horizontalAlignment: Text.AlignRight
                            font.pixelSize: Theme.fsXs
                            color: Theme.textFaint
                        }
                        Item { Layout.preferredWidth: 64 }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.borderSoft }

                ProgressLine {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 2
                    visible: App.loading
                    indeterminate: true
                }

                ListView {
                    id: fileList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: App.files
                    visible: count > 0
                    ScrollBar.vertical: AppScrollBar {}

                    delegate: Item {
                        required property int index
                        required property string name
                        required property string path
                        required property bool isDir
                        required property double size
                        required property string sizeText
                        required property string mtimeText
                        required property bool selected

                        width: fileList.width
                        height: Theme.rowH

                        Rectangle {
                            anchors.fill: parent
                            anchors.leftMargin: 1
                            anchors.rightMargin: 1
                            color: selected ? Theme.alpha(Theme.accent, 0.10)
                                            : (rowMa.containsMouse ? Theme.hover : "transparent")
                        }

                        MouseArea {
                            id: rowMa
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton
                            cursorShape: isDir ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onDoubleClicked: {
                                if (isDir)
                                    App.navigate(path)
                                else
                                    App.downloadPath(path, name, size)
                            }
                            onClicked: App.files.toggleSelected(index)
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.gapMd
                            anchors.rightMargin: Theme.gapMd
                            spacing: Theme.gapMd

                            AppCheckBox {
                                checked: selected
                                onClicked: App.files.setSelected(index, checked)
                            }

                            AppIcon {
                                name: isDir ? "folder" : "file"
                                size: 16
                                color: isDir ? Theme.accent : Theme.textFaint
                            }

                            Text {
                                Layout.fillWidth: true
                                text: name
                                font.pixelSize: Theme.fsMd
                                color: Theme.text
                                elide: Text.ElideMiddle
                            }

                            Text {
                                Layout.preferredWidth: 96
                                text: sizeText
                                horizontalAlignment: Text.AlignRight
                                font.pixelSize: Theme.fsSm
                                color: Theme.textDim
                            }

                            Text {
                                Layout.preferredWidth: 128
                                text: mtimeText
                                horizontalAlignment: Text.AlignRight
                                font.pixelSize: Theme.fsSm
                                color: Theme.textFaint
                            }

                            Row {
                                Layout.preferredWidth: 64
                                spacing: 2

                                IconButton {
                                    visible: !isDir
                                    iconName: "download"
                                    iconSize: 15
                                    boxSize: 28
                                    tip: Tr.t("下载")
                                    onClicked: App.downloadPath(path, name, size)
                                }
                                IconButton {
                                    visible: isDir
                                    iconName: "chevronRight"
                                    iconSize: 15
                                    boxSize: 28
                                    tip: Tr.t("进入")
                                    onClicked: App.navigate(path)
                                }
                                IconButton {
                                    iconName: "trash"
                                    iconSize: 15
                                    boxSize: 28
                                    enabled: App.peerWritable
                                    activeColor: Theme.danger
                                    tip: Tr.t("删除")
                                    onClicked: {
                                        page.pendingDelete = path
                                        page.pendingRecursive = isDir
                                        singleDeleteDialog.open2(
                                            Tr.t("删除 ") + name + "？",
                                            Tr.t("删除不可逆，对端没有回收站。"), true)
                                    }
                                }
                            }
                        }

                        Rectangle {
                            anchors.bottom: parent.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: Theme.gapMd
                            anchors.rightMargin: Theme.gapMd
                            height: 1
                            color: Theme.alpha(Theme.borderSoft, 0.7)
                            visible: index < fileList.count - 1
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: fileList.count === 0

                    EmptyState {
                        anchors.centerIn: parent
                        iconName: App.connected ? "folderOpen" : "link"
                        title: App.connected ? Tr.t("这个目录是空的") : Tr.t("先连接一台设备")
                        subtitle: App.connected
                                  ? Tr.t("把文件拖进窗口即可上传到当前目录。")
                                  : Tr.t("在「设备」页扫描局域网，或手动输入地址连接。")
                    }
                }
            }
        }
    }

    property string pendingDelete: ""
    property bool pendingRecursive: false

    // ---------------------------------------------------------------- 对话框
    PromptDialog {
        id: mkdirDialog
        confirmText: Tr.t("创建")
        onSubmitted: (v) => App.makeDirectory(v)
    }

    ConfirmDialog {
        id: deleteDialog
        confirmText: Tr.t("删除")
        onAccepted: App.deleteSelected()
    }

    ConfirmDialog {
        id: singleDeleteDialog
        confirmText: Tr.t("删除")
        onAccepted: App.deletePath(page.pendingDelete, page.pendingRecursive)
    }

    FileDialog {
        id: uploadDialog
        title: Tr.t("选择要上传的文件")
        fileMode: FileDialog.OpenFiles
        onAccepted: App.uploadUrls(selectedFiles)
    }
}
