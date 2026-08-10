import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Afmu

ApplicationWindow {
    id: win

    visible: true
    width: 1220
    height: 800
    minimumWidth: 980
    minimumHeight: 640
    title: "FileBridge"

    // 无边框：标题栏、边框、圆角全部自绘
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "transparent"

    readonly property bool isMaximized: visibility === Window.Maximized
                                        || visibility === Window.FullScreen
    readonly property int frameRadius: isMaximized ? 0 : 10
    readonly property int resizeMargin: 6

    property int currentPage: 0

    function toggleMaximize() {
        if (win.isMaximized)
            win.showNormal()
        else
            win.showMaximized()
    }

    // ------------------------------------------------------------ 窗体
    Rectangle {
        id: shell
        anchors.fill: parent
        radius: win.frameRadius
        color: Theme.bg
        border.width: win.isMaximized ? 0 : 1
        border.color: Theme.border

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: win.isMaximized ? 0 : 1
            spacing: 0

            TitleBar {
                id: titleBar
                Layout.fillWidth: true
                targetWindow: win
                cornerRadius: win.isMaximized ? 0 : win.frameRadius - 1
                titleText: "FileBridge"
                subtitleText: App.connected ? App.peerSummary : Tr.t("Android File Manager Utils · 客户端")
                onDoubleClicked: win.toggleMaximize()
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                NavRail {
                    Layout.fillHeight: true
                    Layout.preferredWidth: Theme.navWidth
                    currentIndex: win.currentPage
                    onNavigate: (i) => win.currentPage = i
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: win.currentPage

                    DevicesPage {}
                    BrowsePage { id: browsePage }
                    TransfersPage {}
                    ServerPage {}
                    SettingsPage {}
                }
            }

            // -------------------------------------------------- 状态栏
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.statusBarH
                color: Theme.surface
                bottomLeftRadius: win.isMaximized ? 0 : win.frameRadius - 1
                bottomRightRadius: win.isMaximized ? 0 : win.frameRadius - 1
                topLeftRadius: 0
                topRightRadius: 0

                Rectangle {
                    width: parent.width
                    height: 1
                    color: Theme.borderSoft
                }

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.gapMd
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.gapMd

                    Row {
                        spacing: 6
                        anchors.verticalCenter: parent.verticalCenter
                        StatusDot {
                            label: barLabel
                            size: 6
                            color: App.connected ? Theme.success : Theme.textFaint
                        }
                        Text {
                            id: barLabel
                            text: App.connected ? App.peerSummary : Tr.t("未连接")
                            font.pixelSize: Theme.fsXs
                            color: Theme.textFaint
                        }
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "·"
                        font.pixelSize: Theme.fsXs
                        color: Theme.textFaint
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: App.serverRunning
                              ? Tr.t("服务 ") + (App.localAddresses.length > 0 ? App.localAddresses[0] : "0.0.0.0")
                                + ":" + App.serverPort
                              : Tr.t("服务未开启")
                        font.pixelSize: Theme.fsXs
                        color: Theme.textFaint
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.gapMd
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.gap

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: App.transfers.activeCount > 0
                        text: Tr.t("进行中 ") + App.transfers.activeCount
                        font.pixelSize: Theme.fsXs
                        color: Theme.accent
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: App.transfers.failedCount > 0
                        text: Tr.t("失败 ") + App.transfers.failedCount
                        font.pixelSize: Theme.fsXs
                        color: Theme.danger
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: Tr.t("AFMU 协议 v1")
                        font.pixelSize: Theme.fsXs
                        color: Theme.alpha(Theme.textFaint, 0.7)
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------ 拖拽上传
    DropArea {
        anchors.fill: parent
        onEntered: (drag) => {
            if (drag.hasUrls && App.connected)
                drag.accept(Qt.CopyAction)
            else
                drag.accepted = false
        }
        onDropped: (drop) => {
            if (drop.hasUrls)
                App.uploadUrls(drop.urls)
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: win.frameRadius
            visible: parent.containsDrag
            color: Theme.alpha(Theme.accent, 0.10)
            border.width: 2
            border.color: Theme.accent

            Text {
                anchors.centerIn: parent
                text: Tr.t("松开即上传到 ") + (App.atRoot ? Tr.t("对端收件箱") : App.currentPath)
                font.pixelSize: Theme.fsLg
                color: Theme.text
            }
        }
    }

    // ------------------------------------------------------------ 自绘缩放边
    Item {
        id: resizeHandles
        anchors.fill: parent
        visible: !win.isMaximized
        z: 100

        readonly property int m: win.resizeMargin

        MouseArea {
            width: resizeHandles.m; height: parent.height
            anchors.left: parent.left
            cursorShape: Qt.SizeHorCursor
            onPressed: win.startSystemResize(Qt.LeftEdge)
        }
        MouseArea {
            width: resizeHandles.m; height: parent.height
            anchors.right: parent.right
            cursorShape: Qt.SizeHorCursor
            onPressed: win.startSystemResize(Qt.RightEdge)
        }
        MouseArea {
            height: resizeHandles.m; width: parent.width
            anchors.top: parent.top
            cursorShape: Qt.SizeVerCursor
            onPressed: win.startSystemResize(Qt.TopEdge)
        }
        MouseArea {
            height: resizeHandles.m; width: parent.width
            anchors.bottom: parent.bottom
            cursorShape: Qt.SizeVerCursor
            onPressed: win.startSystemResize(Qt.BottomEdge)
        }
        MouseArea {
            width: resizeHandles.m * 2; height: resizeHandles.m * 2
            anchors.left: parent.left; anchors.top: parent.top
            cursorShape: Qt.SizeFDiagCursor
            onPressed: win.startSystemResize(Qt.LeftEdge | Qt.TopEdge)
        }
        MouseArea {
            width: resizeHandles.m * 2; height: resizeHandles.m * 2
            anchors.right: parent.right; anchors.top: parent.top
            cursorShape: Qt.SizeBDiagCursor
            onPressed: win.startSystemResize(Qt.RightEdge | Qt.TopEdge)
        }
        MouseArea {
            width: resizeHandles.m * 2; height: resizeHandles.m * 2
            anchors.left: parent.left; anchors.bottom: parent.bottom
            cursorShape: Qt.SizeBDiagCursor
            onPressed: win.startSystemResize(Qt.LeftEdge | Qt.BottomEdge)
        }
        MouseArea {
            width: resizeHandles.m * 2; height: resizeHandles.m * 2
            anchors.right: parent.right; anchors.bottom: parent.bottom
            cursorShape: Qt.SizeFDiagCursor
            onPressed: win.startSystemResize(Qt.RightEdge | Qt.BottomEdge)
        }
    }

    // ------------------------------------------------------------ 提示
    Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.statusBarH + Theme.padLg
        z: 200
    }

    Connections {
        target: App
        function onToast() {
            toast.show(App.toastText, App.toastIsError)
        }
    }

    // 来访授权请求可能在任何一页收到，所以挂在窗口这一层，不放某个页面里
    IncomingAuthDialog {}

    Component.onCompleted: {
        // 启动即扫一次，多数情况下用户开着 App 就能直接看到设备
        App.scan()
    }

    Shortcut { sequence: "Ctrl+R"; onActivated: App.refresh() }
    Shortcut { sequence: "F5"; onActivated: App.refresh() }
    Shortcut { sequence: "Ctrl+1"; onActivated: win.currentPage = 0 }
    Shortcut { sequence: "Ctrl+2"; onActivated: win.currentPage = 1 }
    Shortcut { sequence: "Ctrl+3"; onActivated: win.currentPage = 2 }
    Shortcut { sequence: "Ctrl+4"; onActivated: win.currentPage = 3 }
    Shortcut { sequence: "Ctrl+5"; onActivated: win.currentPage = 4 }
}
