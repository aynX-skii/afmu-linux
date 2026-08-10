import QtQuick
import QtQuick.Window
import Afmu

// 完全自绘的标题栏：拖动用 startSystemMove，双击最大化
Item {
    id: root

    property Window targetWindow: null
    property int cornerRadius: 0
    property string titleText: "FileBridge"
    property string subtitleText: ""

    implicitHeight: Theme.titleBarH

    Rectangle {
        anchors.fill: parent
        color: Theme.surface
        topLeftRadius: root.cornerRadius
        topRightRadius: root.cornerRadius
        bottomLeftRadius: 0
        bottomRightRadius: 0
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.borderSoft
    }

    // 拖动区域（覆盖除按钮外的整条）
    MouseArea {
        id: dragArea
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: buttons.left
        acceptedButtons: Qt.LeftButton

        onPressed: {
            if (root.targetWindow)
                root.targetWindow.startSystemMove()
        }
        onDoubleClicked: root.doubleClicked()
    }

    signal doubleClicked()

    Row {
        anchors.left: parent.left
        anchors.leftMargin: Theme.gapMd
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.gapMd

        // 应用标记
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 20
            height: 20
            radius: 5
            color: Theme.accent

            AppIcon {
                anchors.centerIn: parent
                name: "repeat"
                size: 13
                weight: 2.4
                color: Theme.textOnAccent
            }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.titleText
            font.pixelSize: Theme.fsMd
            font.bold: true
            color: Theme.text
        }

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 1
            height: 14
            color: Theme.border
            visible: root.subtitleText.length > 0
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.subtitleText.length > 0
            text: root.subtitleText
            font.pixelSize: Theme.fsSm
            color: Theme.textFaint
            elide: Text.ElideRight
        }
    }

    Row {
        id: buttons
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        spacing: 0

        WindowButton {
            iconName: "minus"
            onClicked: if (root.targetWindow) root.targetWindow.showMinimized()
        }
        WindowButton {
            iconName: root.targetWindow && root.targetWindow.visibility === Window.Maximized
                      ? "restore" : "square"
            onClicked: root.doubleClicked()
        }
        WindowButton {
            iconName: "x"
            danger: true
            onClicked: if (root.targetWindow) root.targetWindow.close()
        }
    }
}
