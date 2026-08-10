pragma Singleton

import QtQuick

QtObject {
    // ---- 底色（暗黑扁平，无渐变无阴影，只靠层级色差和 1px 描边分区）
    readonly property color bg:          "#0e1014"
    readonly property color surface:     "#14171d"
    readonly property color surfaceAlt:  "#181c23"
    readonly property color elevated:    "#1e232c"
    readonly property color hover:       "#232936"
    readonly property color pressed:     "#2b3341"
    readonly property color border:      "#252b35"
    readonly property color borderSoft:  "#1c212a"
    readonly property color borderFocus: "#3d5fa8"

    // ---- 文字
    readonly property color text:        "#e9edf5"
    readonly property color textDim:     "#98a1b3"
    readonly property color textFaint:   "#646d7e"
    readonly property color textOnAccent:"#ffffff"

    // ---- 语义色
    readonly property color accent:      "#4c8dff"
    readonly property color accentHover: "#6199ff"
    readonly property color accentDim:   "#2f5db3"
    readonly property color success:     "#37d399"
    readonly property color warning:     "#f0b429"
    readonly property color danger:      "#f2555a"
    readonly property color dangerDim:   "#b23a3e"

    // ---- 尺寸
    readonly property int radius:     8
    readonly property int radiusSm:   6
    readonly property int radiusXs:   4
    readonly property int titleBarH:  40
    readonly property int statusBarH: 26
    readonly property int navWidth:   196
    readonly property int rowH:       38
    readonly property int controlH:   34

    readonly property int gap:      8
    readonly property int gapMd:    12
    readonly property int gapLg:    18
    readonly property int pad:      16
    readonly property int padLg:    22

    // ---- 字号
    readonly property int fsXs:  11
    readonly property int fsSm:  12
    readonly property int fsMd:  13
    readonly property int fsLg:  15
    readonly property int fsXl:  19
    readonly property string mono: "monospace"

    readonly property int anim: 130

    function alpha(c, a) {
        return Qt.rgba(c.r, c.g, c.b, a)
    }
}
