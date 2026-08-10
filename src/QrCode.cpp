#include "QrCode.h"

#include <qrencode.h>

namespace afmu {
namespace {

QRecLevel toLevel(QrCode::Ecc ecc)
{
    switch (ecc) {
    case QrCode::Ecc::Low: return QR_ECLEVEL_L;
    case QrCode::Ecc::Medium: return QR_ECLEVEL_M;
    case QrCode::Ecc::Quartile: return QR_ECLEVEL_Q;
    case QrCode::Ecc::High: return QR_ECLEVEL_H;
    }
    return QR_ECLEVEL_M;
}

} // namespace

QrCode QrCode::encode(const QByteArray &data, Ecc ecc)
{
    QrCode out;
    if (data.isEmpty())
        return out;

    // version 0 = 让库自己挑最小的版本；用 encodeData 而不是 encodeString，
    // 免得库去猜字符集——配对 URI 已经是 percent-encoded 的纯字节了。
    QRcode *code = QRcode_encodeData(int(data.size()),
                                     reinterpret_cast<const unsigned char *>(data.constData()), 0,
                                     toLevel(ecc));
    if (!code)
        return out;

    out.m_version = code->version;
    out.m_size = code->width;
    out.m_modules.assign(std::size_t(code->width) * std::size_t(code->width), false);
    // data 每字节的最低位才是深浅，其余位是「这是定位/格式/纠错区」之类的标记
    for (std::size_t i = 0; i < out.m_modules.size(); ++i)
        out.m_modules[i] = (code->data[i] & 1) != 0;

    QRcode_free(code);
    return out;
}

} // namespace afmu
