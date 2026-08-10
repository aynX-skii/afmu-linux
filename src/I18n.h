#pragma once

#include <QHash>
#include <QObject>
#include <QString>

class Config;

/**
 * 界面语言。源语言是中文，英文放在内置翻译表里。
 *
 * 没走 Qt Linguist 的 .ts/.qm 流程，是为了不给构建加 qt6-l10n-tools 依赖——
 * 这个项目的取向一直是单二进制、apt 只装 qt6-base-dev / qt6-declarative-dev 就能编。
 * 代价是翻译不能交给外部翻译工具，只有两种语言时这个代价可以接受。
 * 以后语言变多了再迁到 .ts 也不难：把 t("中文") 换成 tr("中文") 即可，调用点形状是一样的。
 */
class I18n : public QObject
{
    Q_OBJECT
    // "system" / "zh" / "en"，持久化在 config.json 里
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    // 解析之后的实际语言，只会是 "zh" 或 "en"
    Q_PROPERTY(QString effective READ effective NOTIFY languageChanged)

public:
    explicit I18n(Config *config, QObject *parent = nullptr);

    static I18n *instance();

    QString language() const { return m_language; }
    void setLanguage(const QString &v);

    QString effective() const { return m_effective; }

    /** 跟随系统时系统语言是什么（用于在设置里显示"跟随系统（中文）"）。 */
    Q_INVOKABLE static QString systemLanguage();

    /** 中文原文 → 当前语言。查不到就原样返回，漏翻的地方会显示中文而不是空白。 */
    Q_INVOKABLE QString t(const QString &zh) const;

signals:
    void languageChanged();

private:
    void resolve();

    Config *m_config = nullptr;
    QString m_language;
    QString m_effective;
    QHash<QString, QString> m_en;
};

/**
 * C++ 侧的简写：T(QStringLiteral("中文"))。
 * I18n 还没构造出来时原样返回，静态初始化阶段用也不会炸。
 */
inline QString T(const QString &zh)
{
    I18n *i = I18n::instance();
    return i ? i->t(zh) : zh;
}
