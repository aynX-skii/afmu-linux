#include "Tls.h"

#include "Identity.h"
#include "ProtocolConstants.h"

#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>

namespace afmu {

namespace {

QSslConfiguration baseConfiguration(const Identity &id)
{
    QSslConfiguration cfg = QSslConfiguration::defaultConfiguration();
    cfg.setProtocol(QSsl::TlsV1_3);

    cfg.setLocalCertificate(QSslCertificate(id.certificateDer(), QSsl::Der));
    cfg.setPrivateKey(QSslKey(id.privateKeyPem(), QSsl::Ec, QSsl::Pem, QSsl::PrivateKey));

    // 自签，没有 CA 可言。留着系统 CA 只会让人误以为链校验起了作用。
    cfg.setCaCertificates({});
    // 校验不靠 TLS 栈，靠握手完成后手工比对指纹。QueryPeer 的意思是
    // 「要对方的证书，但别替我判断它可不可信」—— 判断是我们自己的事。
    cfg.setPeerVerifyMode(QSslSocket::QueryPeer);
    // ALPN 里必须同时留着 http/1.1，原因是实测出来的，不是设计选择：
    //
    // **QNetworkAccessManager 会覆盖掉请求里设的 ALPN**，自己提 {h2, http/1.1}。
    // 只提 afmu/2 的话，我们自己的客户端第一个连不上（实测 BAD_EXTENSION）。
    // 而且服务端这边只留 http/1.1 还有个额外好处：QNAM 默认允许 HTTP/2，
    // 由服务端在 ALPN 阶段把它挡掉，比在客户端逐个请求关 Http2Allowed 可靠 ——
    // 后者实测会让握手直接卡住。
    //
    // 代价是 ALPN 在这里**不是安全边界**：随便一个 HTTPS 客户端都能握上手。
    // 挡住它的从来不是 ALPN 而是钉扎 —— 没有已配对的客户端证书，握完也进不去。
    cfg.setAllowedNextProtocols({QByteArray(kTlsAlpn), QByteArrayLiteral("http/1.1")});
    return cfg;
}

} // namespace

QSslConfiguration serverTlsConfiguration(const Identity &id)
{
    return baseConfiguration(id);
}

QSslConfiguration clientTlsConfiguration(const Identity &id)
{
    // 和服务端完全一样。SNI 由连接时用的主机名决定，不在配置里 ——
    // 局域网里连的是 IP，而 SNI 按 RFC 6066 不能填 IP，所以本来就不会发出去。
    // 万一将来改成按主机名连，记得那一段是**明文**的（§5）。
    return baseConfiguration(id);
}

QString peerFingerprint(const QSslCertificate &cert)
{
    if (cert.isNull())
        return {};
    const QByteArray raw = Identity::fingerprintOf(cert.toDer());
    return raw.size() == 32 ? Identity::toBase32(raw) : QString();
}

} // namespace afmu
