#pragma once

#include <QByteArray>
#include <vector>

namespace afmu {

/**
 * libqrencode 的薄封装：把 QRcode* 抄成一张自有的模块位图，调用方不用碰 C 结构，
 * 也不用管 QRcode_free。
 *
 * 以前这里是自己按 ISO/IEC 18004 写的编码器，为的是「apt 只装 qt6 开发包就能编」。
 * 现在换成 libqrencode——Debian/Ubuntu 的 libqrencode-dev、Fedora 的 qrencode-devel、
 * Arch 的 qrencode，常见发行版都直接有，为一个二维码维护一份 RS 编码实现不划算。
 */
class QrCode
{
public:
    enum class Ecc { Low = 0, Medium = 1, Quartile = 2, High = 3 };

    QrCode() = default;

    /** 编码失败（空数据 / 超出版本 40 容量）时返回 isValid() == false 的对象。 */
    static QrCode encode(const QByteArray &data, Ecc ecc = Ecc::Medium);

    bool isValid() const { return m_size > 0; }
    int size() const { return m_size; }
    int version() const { return m_version; }

    /** 越界一律当成浅色，方便调用方直接带静区循环。 */
    bool module(int x, int y) const
    {
        return x >= 0 && x < m_size && y >= 0 && y < m_size
            && m_modules[std::size_t(y) * std::size_t(m_size) + std::size_t(x)];
    }

private:
    int m_version = 0;
    int m_size = 0;
    std::vector<bool> m_modules;
};

} // namespace afmu
