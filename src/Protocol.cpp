#include "Protocol.h"

#include <QRandomGenerator>
#include <QUrlQuery>

namespace afmu {

bool tokenEquals(const QByteArray &a, const QByteArray &b)
{
    if (a.isEmpty() || b.isEmpty())
        return false;
    // 长度不同时仍然跑完整轮比较，避免通过耗时区分。
    // 累加器必须够宽：截成 quint8 会让相差 256 倍数的长度差被抹掉。
    quint32 diff = static_cast<quint32>(a.size() ^ b.size());
    const int n = qMax(a.size(), b.size());
    for (int i = 0; i < n; ++i) {
        const quint8 ca = i < a.size() ? static_cast<quint8>(a[i]) : 0;
        const quint8 cb = i < b.size() ? static_cast<quint8>(b[i]) : 0;
        diff |= static_cast<quint32>(ca ^ cb);
    }
    return diff == 0;
}

QString makeToken()
{
    static const QString alphabet = QStringLiteral("abcdefghjkmnpqrstuvwxyz23456789");
    QString out;
    out.reserve(10);
    for (int i = 0; i < 10; ++i)
        out.append(alphabet.at(QRandomGenerator::system()->bounded(alphabet.size())));
    return out;
}

QString makePairingCode()
{
    return QStringLiteral("%1").arg(QRandomGenerator::system()->bounded(1000, 10000));
}

QString buildPairUri(const QString &name, const QString &os, const QStringList &hosts, int port,
                     const QString &token)
{
    if (hosts.isEmpty() || token.isEmpty())
        return QString();

    QUrlQuery q;
    q.addQueryItem(QStringLiteral("v"), QString::number(kProtocolVersion));
    q.addQueryItem(QStringLiteral("host"), hosts.first());
    if (hosts.size() > 1) {
        // 多网卡时把候选都带上，手机逐个探，避免扫到一个连不通的地址
        q.addQueryItem(QStringLiteral("hosts"), hosts.join(QLatin1Char(',')));
    }
    q.addQueryItem(QStringLiteral("port"), QString::number(port));
    q.addQueryItem(QStringLiteral("token"), token);
    q.addQueryItem(QStringLiteral("name"), name);
    q.addQueryItem(QStringLiteral("os"), os);
    return QLatin1String(kPairUriPrefix) + q.toString(QUrl::FullyEncoded);
}

QString humanSize(qint64 bytes)
{
    if (bytes < 0)
        return QStringLiteral("-");
    static const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    double v = double(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 5) {
        v /= 1024.0;
        ++u;
    }
    if (u == 0)
        return QStringLiteral("%1 B").arg(bytes);
    return QStringLiteral("%1 %2").arg(v, 0, 'f', v < 10 ? 2 : 1).arg(QLatin1String(units[u]));
}

} // namespace afmu
