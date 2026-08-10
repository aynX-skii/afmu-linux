import QtQuick
import Afmu

Column {
    id: root

    property string iconName: "info"
    property string title: ""
    property string subtitle: ""

    spacing: Theme.gapMd
    width: Math.min(parent ? parent.width - 2 * Theme.padLg : 420, 420)

    AppIcon {
        anchors.horizontalCenter: parent.horizontalCenter
        name: root.iconName
        size: 38
        weight: 1.4
        color: Theme.alpha(Theme.textFaint, 0.7)
    }
    Text {
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        text: root.title
        font.pixelSize: Theme.fsLg
        color: Theme.textDim
        wrapMode: Text.WordWrap
    }
    Text {
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        visible: root.subtitle.length > 0
        text: root.subtitle
        font.pixelSize: Theme.fsMd
        lineHeight: 1.35
        color: Theme.textFaint
        wrapMode: Text.WordWrap
    }
}
