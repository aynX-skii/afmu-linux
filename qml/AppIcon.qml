import QtQuick
import QtQuick.Shapes
import Afmu

// 线性图标：24x24 viewBox，纯描边，无外部资源
Item {
    id: root

    property string name: ""
    property color color: Theme.textDim
    property real size: 18
    property real weight: 2.0
    property bool filled: false

    implicitWidth: size
    implicitHeight: size

    readonly property var catalog: ({
        "x":            "M18 6L6 18M6 6l12 12",
        "minus":        "M5 12h14",
        "square":       "M3.5 3.5h17v17h-17z",
        "restore":      "M8 8V4h12v12h-4M4 8h12v12H4z",
        "search":       "M11 19a8 8 0 1 0 0-16 8 8 0 0 0 0 16zM21 21l-4.35-4.35",
        "refresh":      "M23 4v6h-6M1 20v-6h6M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15",
        "rotate":       "M1 4v6h6M3.51 15a9 9 0 1 0 2.13-9.36L1 10",
        "folder":       "M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z",
        "file":         "M13 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V9zM13 2v7h7",
        "download":     "M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4M7 10l5 5 5-5M12 15V3",
        "upload":       "M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4M17 8l-5-5-5 5M12 3v12",
        "repeat":       "M17 1l4 4-4 4M3 11V9a4 4 0 0 1 4-4h14M7 23l-4-4 4-4M21 13v2a4 4 0 0 1-4 4H3",
        "sliders":      "M4 21v-7M4 10V3M12 21v-9M12 8V3M20 21v-5M20 12V3M1 14h6M9 8h6M17 16h6",
        "drive":        "M22 12H2M5.45 5.11L2 12v6a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2v-6l-3.45-6.89A2 2 0 0 0 16.76 4H7.24a2 2 0 0 0-1.79 1.11zM6 16h.01M10 16h.01",
        "phone":        "M5 2h14a2 2 0 0 1 2 2v16a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2zM12 18h.01",
        "monitor":      "M2.5 3.5h19v13h-19zM8 21h8M12 17v4",
        "plus":         "M12 5v14M5 12h14",
        "trash":        "M3 6h18M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2M10 11v6M14 11v6",
        "cornerUpLeft": "M9 14L4 9l5-5M20 20v-7a4 4 0 0 0-4-4H4",
        "chevronRight": "M9 18l6-6-6-6",
        "chevronDown":  "M6 9l6 6 6-6",
        "check":        "M20 6L9 17l-5-5",
        "alert":        "M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0zM12 9v4M12 17h.01",
        "lock":         "M5 11h14a2 2 0 0 1 2 2v7a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-7a2 2 0 0 1 2-2zM7 11V7a5 5 0 0 1 10 0v4",
        "link":         "M10 13a5 5 0 0 0 7.54.54l3-3a5 5 0 0 0-7.07-7.07l-1.72 1.71M14 11a5 5 0 0 0-7.54-.54l-3 3a5 5 0 0 0 7.07 7.07l1.71-1.71",
        "wifi":         "M5 12.55a11 11 0 0 1 14.08 0M1.42 9a16 16 0 0 1 21.16 0M8.53 16.11a6 6 0 0 1 6.95 0M12 20h.01",
        "home":         "M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2zM9 22V12h6v10",
        "power":        "M18.36 6.64a9 9 0 1 1-12.73 0M12 2v10",
        "copy":         "M9.5 9.5h12v12h-12zM5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1",
        "external":     "M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6M15 3h6v6M10 14L21 3",
        "info":         "M12 22a10 10 0 1 0 0-20 10 10 0 0 0 0 20zM12 16v-4M12 8h.01",
        "xCircle":      "M12 22a10 10 0 1 0 0-20 10 10 0 0 0 0 20zM15 9l-6 6M9 9l6 6",
        "checkCircle":  "M22 11.08V12a10 10 0 1 1-5.93-9.14M22 4L12 14.01l-3-3",
        "clock":        "M12 22a10 10 0 1 0 0-20 10 10 0 0 0 0 20zM12 6v6l4 2",
        "folderOpen":   "M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2zM2 11h20",
        "radar":        "M12 22a10 10 0 1 0 0-20 10 10 0 0 0 0 20zM12 18a6 6 0 1 0 0-12 6 6 0 0 0 0 12zM12 14a2 2 0 1 0 0-4 2 2 0 0 0 0 4z",
        "qr":           "M3.5 3.5h7v7h-7zM13.5 3.5h7v7h-7zM3.5 13.5h7v7h-7zM13.5 13.5h3v3h-3zM20.5 13.5h-1M20.5 17.5v3h-3M13.5 20.5h1"
    })

    readonly property string pathData: catalog[name] !== undefined ? catalog[name] : ""

    Shape {
        anchors.centerIn: parent
        width: 24
        height: 24
        scale: root.size / 24
        antialiasing: true
        preferredRendererType: Shape.CurveRenderer
        visible: root.pathData.length > 0

        ShapePath {
            strokeColor: root.color
            strokeWidth: root.weight
            fillColor: root.filled ? root.color : "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: root.pathData }
        }
    }
}
