#pragma once

#include "QrCode.h"

#include <QColor>
#include <QImage>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

/**
 * QML 里的二维码：<QrView text="afmu://pair?…" />
 *
 * 用 QQuickPaintedItem 而不是 Repeater 铺 Rectangle —— 版本 5 就有 37x37 = 1369 个模块，
 * 做成 item 树光是创建就要几十毫秒，而这里只需要一张静态图。
 */
class QrView : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    Q_PROPERTY(QColor foreground READ foreground WRITE setForeground NOTIFY colorsChanged)
    Q_PROPERTY(QColor background READ background WRITE setBackground NOTIFY colorsChanged)
    /** 静区，单位是模块数。规范要求 4，展示在深色卡片里时 2 也够扫。 */
    Q_PROPERTY(int quietZone READ quietZone WRITE setQuietZone NOTIFY quietZoneChanged)
    Q_PROPERTY(bool valid READ isValid NOTIFY textChanged)

public:
    explicit QrView(QQuickItem *parent = nullptr);

    QString text() const { return m_text; }
    void setText(const QString &v);

    QColor foreground() const { return m_fg; }
    void setForeground(const QColor &v);

    QColor background() const { return m_bg; }
    void setBackground(const QColor &v);

    int quietZone() const { return m_quiet; }
    void setQuietZone(int v);

    bool isValid() const { return m_code.isValid(); }

    void paint(QPainter *painter) override;

signals:
    void textChanged();
    void colorsChanged();
    void quietZoneChanged();

private:
    /** 把模块矩阵烘成一张「1 模块 = 1 像素」的小图，paint() 只负责整数倍放大。 */
    void rebuildImage();

    QString m_text;
    QColor m_fg = Qt::black;
    QColor m_bg = Qt::white;
    int m_quiet = 3;
    afmu::QrCode m_code;
    QImage m_image;
};
