import QtQuick
import Afmu

Item {
    id: root

    property int currentIndex: 0
    signal navigate(int index)

    implicitWidth: Theme.navWidth

    Rectangle {
        anchors.fill: parent
        color: Theme.surface
    }

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.borderSoft
    }

    Column {
        id: nav
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: Theme.gapMd
        spacing: 2

        SectionLabel {
            x: Theme.gap + Theme.gapMd
            text: Tr.t("连接")
            bottomPadding: 6
        }

        NavItem {
            width: parent.width
            iconName: "radar"
            label: Tr.t("设备")
            active: root.currentIndex === 0
            onClicked: root.navigate(0)
        }
        NavItem {
            width: parent.width
            iconName: "folder"
            label: Tr.t("浏览文件")
            active: root.currentIndex === 1
            onClicked: root.navigate(1)
        }
        NavItem {
            width: parent.width
            iconName: "repeat"
            label: Tr.t("传输")
            active: root.currentIndex === 2
            badge: App.transfers.activeCount
            onClicked: root.navigate(2)
        }

        Item { width: 1; height: Theme.gapMd }

        SectionLabel {
            x: Theme.gap + Theme.gapMd
            text: Tr.t("本机")
            bottomPadding: 6
        }

        NavItem {
            width: parent.width
            iconName: "drive"
            label: Tr.t("接收服务")
            active: root.currentIndex === 3
            badge: App.serverRunning ? 0 : 0
            onClicked: root.navigate(3)
        }
        NavItem {
            width: parent.width
            iconName: "sliders"
            label: Tr.t("设置")
            active: root.currentIndex === 4
            onClicked: root.navigate(4)
        }
    }

    // 底部连接状态卡片
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.gapMd
        anchors.rightMargin: Theme.gapMd + 1
        height: statusCol.implicitHeight + 2 * Theme.gapMd
        radius: Theme.radiusSm
        color: Theme.surfaceAlt
        border.width: 1
        border.color: Theme.borderSoft

        Column {
            id: statusCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: Theme.gapMd
            spacing: 6

            Row {
                spacing: 6
                StatusDot {
                    label: peerLabel
                    color: App.connected ? Theme.success : Theme.textFaint
                }
                Text {
                    id: peerLabel
                    text: App.connected ? App.peerName : Tr.t("未连接设备")
                    font.pixelSize: Theme.fsSm
                    font.bold: true
                    color: App.connected ? Theme.text : Theme.textFaint
                    elide: Text.ElideRight
                    width: statusCol.width - 16
                }
            }
            Text {
                width: statusCol.width
                text: App.connected ? App.peerHost + ":" + App.peerPort : Tr.t("在「设备」页扫描或手动连接")
                font.pixelSize: Theme.fsXs
                color: Theme.textFaint
                elide: Text.ElideRight
            }
            Row {
                spacing: 6
                StatusDot {
                    label: serveLabel
                    color: App.serverRunning ? Theme.success : Theme.textFaint
                }
                Text {
                    id: serveLabel
                    text: App.serverRunning ? "接收服务 :" + App.serverPort : Tr.t("接收服务未开启")
                    font.pixelSize: Theme.fsXs
                    color: App.serverRunning ? Theme.textDim : Theme.textFaint
                }
            }
        }
    }
}
