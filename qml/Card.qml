import QtQuick
import Afmu

Rectangle {
    default property alias content: inner.data
    property alias padding: inner.anchors.margins

    color: Theme.surface
    radius: Theme.radius
    border.width: 1
    border.color: Theme.borderSoft

    implicitWidth: inner.implicitWidth + 2 * inner.anchors.margins
    implicitHeight: inner.childrenRect.height + 2 * inner.anchors.margins

    Item {
        id: inner
        anchors.fill: parent
        anchors.margins: Theme.pad
    }
}
