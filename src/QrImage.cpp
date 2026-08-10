#include "QrImage.h"

#include <QPainter>

QrView::QrView(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    // 二维码是纯黑白方块，抗锯齿反而会把模块边缘糊掉，识别率更差。
    // 渲染目标保持默认的 Image：FramebufferObject 那条路要求场景图跑在 OpenGL 上，
    // 换成软件后端或某些驱动上会画不出东西（表现就是一片空白）。
    setAntialiasing(false);
    setSmooth(false);
}

void QrView::setText(const QString &v)
{
    if (m_text == v)
        return;
    m_text = v;
    m_code = v.isEmpty() ? afmu::QrCode()
                         : afmu::QrCode::encode(v.toUtf8(), afmu::QrCode::Ecc::Medium);
    rebuildImage();
    emit textChanged();
    update();
}

void QrView::setForeground(const QColor &v)
{
    if (m_fg == v)
        return;
    m_fg = v;
    rebuildImage();
    emit colorsChanged();
    update();
}

void QrView::setBackground(const QColor &v)
{
    if (m_bg == v)
        return;
    m_bg = v;
    rebuildImage();
    emit colorsChanged();
    update();
}

void QrView::setQuietZone(int v)
{
    const int clamped = qBound(0, v, 8);
    if (m_quiet == clamped)
        return;
    m_quiet = clamped;
    rebuildImage();
    emit quietZoneChanged();
    update();
}

/**
 * 一个模块画成一个像素，缩放交给 drawImage。
 *
 * 比逐个 drawRect 稳：版本 10 就有 3000 多个模块，几千次 drawRect 在部分驱动上
 * 会走进各种慢路径甚至画丢；一张小图 + 整数倍最近邻放大，结果是逐像素确定的。
 */
void QrView::rebuildImage()
{
    if (!m_code.isValid()) {
        m_image = QImage();
        return;
    }

    const int span = m_code.size() + m_quiet * 2;
    QImage img(span, span, QImage::Format_ARGB32_Premultiplied);
    img.fill(m_bg);

    for (int y = 0; y < m_code.size(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y + m_quiet));
        for (int x = 0; x < m_code.size(); ++x) {
            if (m_code.module(x, y))
                line[x + m_quiet] = m_fg.rgba();
        }
    }

    m_image = img;
}

void QrView::paint(QPainter *painter)
{
    const QRectF box(0, 0, width(), height());
    painter->fillRect(box, m_bg);
    if (m_image.isNull() || box.width() < 1 || box.height() < 1)
        return;

    const int span = m_image.width();
    // 每个模块必须是整数像素，否则相邻模块会因为舍入差出一像素缝，扫码器会误判
    const int scale = qMax(1, int(qMin(box.width(), box.height())) / span);
    const int drawn = scale * span;
    const int ox = int((box.width() - drawn) / 2);
    const int oy = int((box.height() - drawn) / 2);

    painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter->drawImage(QRect(ox, oy, drawn, drawn), m_image);
}
