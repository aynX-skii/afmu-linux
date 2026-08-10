import QtQuick
import Afmu

Rectangle {
    id: root

    property real value: 0          // 0..1
    property color tone: Theme.accent
    property bool indeterminate: false

    implicitHeight: 4
    radius: height / 2
    color: Theme.elevated
    clip: true

    Rectangle {
        id: bar
        height: parent.height
        radius: parent.radius
        color: root.tone
        width: root.indeterminate ? parent.width * 0.32
                                  : parent.width * Math.max(0, Math.min(1, root.value))
        Behavior on width {
            enabled: !root.indeterminate
            NumberAnimation { duration: 120 }
        }
    }

    SequentialAnimation {
        running: root.indeterminate && root.visible
        loops: Animation.Infinite
        NumberAnimation { target: bar; property: "x"; from: -bar.width; to: root.width
                          duration: 950; easing.type: Easing.InOutQuad }
    }
}
