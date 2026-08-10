pragma Singleton

import QtQuick

/**
 * 界面文案入口：QML 里一律写 Tr.t("中文原文")。
 *
 * 为什么要在 QML 里再包一层，而不是直接调 C++ 的 Lang.t()：
 * QML 的绑定依赖是在求值过程中动态捕获的，只有**在 QML 里读到的属性**才会被记录。
 * 直接调 Lang.t(...) 是一次普通的方法调用，语言变了绑定不会重新求值，界面不会刷新。
 * 这里的 t() 读了 lang 这个 QML 属性，于是每个用到 Tr.t() 的绑定都自动依赖它，
 * 切换语言时整个界面即时刷新，不需要重启，也不需要手动去 retranslate。
 */
QtObject {
    readonly property string lang: Lang.effective

    function t(zh) {
        if (lang === "zh")
            return zh
        return Lang.t(zh)
    }
}
