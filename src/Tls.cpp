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
    cfg.setAllowedNextProtocols({QByteArray(kTlsAlpn)});
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
