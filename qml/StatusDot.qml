import QtQuick
import Afmu

/**
 * 状态小圆点，跟一段文字并排。
 *
 * 对齐的是文字的**视觉中线**（基线上方半个大写字高），不是行框中心：行框上沿要给重音
 * 符号之类的东西留位置，汉字的墨迹因此在行框里偏下，按行框居中会让点看起来往上浮
 * 一两个像素。改成按基线算，字号或字体换了也不用重新调。
 */
Rectangle {
    id: root

    /** 要对齐的那个 Text。 */
    property var label
    property int size: 7

    width: size
    height: size
    radius: size / 2

    FontMetrics {
        id: metrics
        font: root.label.font
    }

    anchors.baseline: root.label.baseline
    baselineOffset: height / 2 + metrics.capitalHeight / 2
}
