#include "Protocol.h"

#include <QCryptographicHash>
#include <QHostAddress>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QUrl>
#include <QUrlQuery>

namespace afmu {

namespace {

int hexNibble(QChar c)
{
    const ushort u = c.unicode();
    if (u >= '0' && u <= '9')
        return u - '0';
    if (u >= 'a' && u <= 'f')
        return u - 'a' + 10;
    if (u >= 'A' && u <= 'F')
        return u - 'A' + 10;
    return -1;
}

} // namespace

QByteArray hexDecodeStrict(const QString &text)
{
    if (text.isEmpty() || text.size() % 2 != 0)
        return {};
    QByteArray out(text.size() / 2, Qt::Uninitialized);
    for (int i = 0; i < out.size(); ++i) {
        const int hi = hexNibble(text.at(i * 2));
        const int lo = hexNibble(text.at(i * 2 + 1));
        if (hi < 0 || lo < 0)
            return {};
        out[i] = char((hi << 4) | lo);
    }
    return out;
}

namespace {

/** 从 Host / Origin 里取出主机名，剥掉端口和 IPv6 的方括号。 */
QString hostNameOf(const QString &raw)
{
    QString s = raw.trimmed();
    if (s.isEmpty())
        return QString();

    if (s.startsWith(QLatin1Char('['))) {
        // [::1]:8765 —— 方括号里整段都是地址，冒号不是端口分隔符
        const int close = s.indexOf(QLatin1Char(']'));
        return close > 0 ? s.mid(1, close - 1) : QString();
    }
    // 裸 IPv6（没加方括号，不合规但见得到）会有多个冒号，此时没有端口可言
    if (s.count(QLatin1Char(':')) > 1)
        return s;
    const int colon = s.indexOf(QLatin1Char(':'));
    return colon >= 0 ? s.left(colon) : s;
}

/**
 * 严格的点分四段判定。**不能用 QHostAddress::setAddress** —— 它沿用了
 * inet_aton 的宽松规矩，"0x7f.0.0.1"、"1.2.3"、"2130706433" 全都接受，
 * 而宽松的 IP 解析器正是 DNS rebinding 想要的（PROTOCOL.md §2.4）。
 */
bool isStrictIpv4(const QString &s)
{
    const QStringList parts = s.split(QLatin1Char('.'));
    if (parts.size() != 4)
        return false;
    for (const QString &p : parts) {
        if (p.isEmpty() || p.size() > 3)
            return false;
        for (const QChar c : p) {
            if (c < u'0' || c > u'9')
                return false;
        }
        if (p.toInt() > 255)
            return false;
    }
    return true;
}

} // namespace

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

bool isLocalHostHeader(const QString &hostHeader)
{
    const QString name = hostNameOf(hostHeader);
    if (name.isEmpty())
        return false; // HTTP/1.1 要求必须有 Host；没有就当不合规

    if (name.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0)
        return true;
    if (name.endsWith(QLatin1String(".local"), Qt::CaseInsensitive))
        return true;

    // IPv6 交给 Qt：那套语法没有 IPv4 那种十进制/十六进制的宽松歧义
    if (name.contains(QLatin1Char(':'))) {
        QHostAddress addr;
        return addr.setAddress(name) && addr.protocol() == QAbstractSocket::IPv6Protocol;
    }
    return isStrictIpv4(name);
}

bool originMatchesHost(const QString &origin, const QString &hostHeader)
{
    const QString o = origin.trimmed();
    if (o.isEmpty())
        return true; // 原生客户端不发 Origin
    // 有些浏览器在隐私上下文里发字面量 "null"，那不是本机，直接拒
    if (o.compare(QLatin1String("null"), Qt::CaseInsensitive) == 0)
        return false;

    const QUrl u(o);
    if (!u.isValid() || u.host().isEmpty())
        return false;

    // 主机名和端口都要对上。**端口不能放过** —— 只比主机名的话，
    // 同一台设备上任何别的 HTTP 服务（:9999 上的某个页面）都能驱动本机的 API。
    if (u.host().compare(hostNameOf(hostHeader), Qt::CaseInsensitive) != 0)
        return false;

    // 两边的端口都可能是隐含的：Origin 省略端口表示协议默认端口，
    // Host 省略端口表示 80（v1 是明文 http）。补齐之后再比。
    const int originPort =
        u.port() > 0 ? u.port() : (u.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0 ? 443 : 80);

    const QString hostName = hostNameOf(hostHeader);
    const QString rest = hostHeader.trimmed().mid(
        hostHeader.trimmed().startsWith(QLatin1Char('[')) ? hostName.size() + 2 : hostName.size());
    const int hostPort = rest.startsWith(QLatin1Char(':')) ? rest.mid(1).toInt() : 80;

    return originPort == hostPort;
}

namespace {

/**
 * 域分隔前缀保证这个 MAC 永远不会被当成同一把钥匙算出来的别的用途的 MAC，
 * 换行符保证 exp 和 path 不会黏成一个有歧义的串。
 */
QString ticketMac(const QString &token, qint64 exp, const QString &path)
{
    // 直接拼而不用 arg()：域前缀现在来自生成的常量，把它塞进格式串会让
    // 「哪个 %n 对应哪一段」依赖于前缀里有没有 % —— MAC 的输入不该有这种隐含前提。
    const QByteArray msg = QByteArray(afmu::kTicketDomain) + QByteArray::number(exp) + '\n'
        + path.toUtf8();
    const QByteArray digest = QMessageAuthenticationCode::hash(msg, token.toUtf8(),
                                                              QCryptographicHash::Sha256);
    return QString::fromLatin1(
        digest.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals).left(afmu::kTicketMacChars));
}

} // namespace

QString issueDownloadTicket(const QString &token, const QString &path, qint64 nowMs)
{
    const qint64 exp = nowMs / 1000 + kTicketTtlSec;
    return QStringLiteral("%1.%2").arg(exp).arg(ticketMac(token, exp, path));
}

bool verifyDownloadTicket(const QString &token, const QString &path, const QString &ticket,
                          qint64 nowMs)
{
    if (token.isEmpty() || ticket.isEmpty())
        return false;
    const int dot = ticket.indexOf(QLatin1Char('.'));
    if (dot <= 0 || dot == ticket.size() - 1)
        return false;

    bool ok = false;
    const qint64 exp = ticket.left(dot).toLongLong(&ok);
    if (!ok)
        return false;
    // 先看有效期：过期的券不该再花代价去算 MAC
    if (nowMs / 1000 > exp)
        return false;

    return tokenEquals(ticket.mid(dot + 1).toUtf8(), ticketMac(token, exp, path).toUtf8());
}

QString makeToken()
{
    static const QString alphabet = QString::fromLatin1(afmu::kTokenAlphabet);
    QString out;
    out.reserve(afmu::kTokenLength);
    for (int i = 0; i < afmu::kTokenLength; ++i)
        out.append(alphabet.at(QRandomGenerator::system()->bounded(alphabet.size())));
    return out;
}

QString makePairingCode()
{
    return QStringLiteral("%1").arg(QRandomGenerator::system()->bounded(1000, 10000));
}

QString buildPairUri(const QString &name, const QString &os, const QStringList &hosts, int port,
                     const QString &token, const QString &fingerprint)
{
    // v2 的码里带指纹、**不带 token**；v1 的反过来。两者必须至少有一个，
    // 否则扫出来的码什么都干不了。
    const bool v2 = !fingerprint.isEmpty();
    if (hosts.isEmpty() || (token.isEmpty() && !v2))
        return QString();

    QUrlQuery q;
    q.addQueryItem(QStringLiteral("v"), QString::number(v2 ? 2 : int(kProtocolVersion)));
    q.addQueryItem(QStringLiteral("host"), hosts.first());
    if (hosts.size() > 1) {
        // 多网卡时把候选都带上，手机逐个探，避免扫到一个连不通的地址
        q.addQueryItem(QStringLiteral("hosts"), hosts.join(QLatin1Char(',')));
    }
    q.addQueryItem(QStringLiteral("port"), QString::number(port));
    if (v2) {
        // 指纹是公开信息：截图、转发、投屏都不会造成任何损失。
        // v1 的码里那个 token 不是 —— 那才是 v2 顺手解决的一个真问题（草案 §4.1）。
        q.addQueryItem(QStringLiteral("fp"), fingerprint);
    } else {
        q.addQueryItem(QStringLiteral("token"), token);
    }
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
