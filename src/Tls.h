#pragma once

#include <QByteArray>
#include <QSslConfiguration>
#include <QString>

class QSslCertificate;

namespace afmu {

class Identity;

/**
 * v2 的 TLS 配置（PROTOCOL.md v2 §5）。服务端和客户端共用一份，只差校验模式。
 *
 * 几个选择的理由，改之前先读：
 *
 * - **只接受 TLS 1.3**。1.2 不加密证书消息，握手时设备身份是明文可见的 ——
 *   而 v2 的整个目的就是别在不可信网络上泄露"这是谁的手机"。
 * - **关掉 CA 链校验，改成手工比对 SPKI 指纹**。自签证书没有 CA 可言。
 *   Qt 侧的正确写法是 `QueryPeer` + 在 `encrypted()` 里比对不符就 `abort()`；
 *   **不是** `VerifyNone` + 在 `sslErrors` 里比对 —— 那样钉扎代码可能一次都不执行（§5.1）。
 * - **ALPN `afmu/2`**。让协议不匹配的客户端在握手阶段就失败，
 *   而不是握上之后才在 HTTP 层发现对不上。
 * - **不发 SNI**。那是明文的，把设备名放进去等于白加密。
 */
QSslConfiguration serverTlsConfiguration(const Identity &id);
QSslConfiguration clientTlsConfiguration(const Identity &id);

/**
 * 对端证书的 SPKI 指纹，base32。没有证书或者算不出来返回空串。
 *
 * 空串必须当作"不通过"处理，绝不能当成"没关系"：`QueryPeer` 下客户端不交证书
 * 时握手照样成功，判空是唯一挡住匿名连接的地方。
 */
QString peerFingerprint(const QSslCertificate &cert);

} // namespace afmu
