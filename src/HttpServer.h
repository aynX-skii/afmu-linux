#pragma once

#include "AuthThrottle.h"

#include <QPointer>
#include <QSslConfiguration>
#include <QStringList>
#include <QTcpServer>

class AuthRequests;
class PeerStore;

namespace afmu {
class Identity;
}

// docs/PROTOCOL.md §2/§3/§4 —— 本机服务端（对端推/拉本机时用）
struct ServerContext
{
    QString token;
    QString deviceName;
    QString inbox;
    QStringList roots;
    bool writable = true;
    /**
     * 访客模式（草案 §9）：密码认证这条路开不开。
     *
     * 它就是 v1 的访问方式 —— 一个长期共享密钥。挡得住被动嗅探（走 HTTPS 的话），
     * **挡不住中间人**，因为浏览器不会拿客户端证书做 mTLS。关掉之后只认配对表，
     * 那才是 v2 承诺的那道防线。
     */
    bool guest = true;
};

class HttpServer : public QTcpServer
{
    Q_OBJECT
public:
    explicit HttpServer(QObject *parent = nullptr);

    void setContext(const ServerContext &ctx);
    const ServerContext &context() const { return m_ctx; }

    /**
     * 待决授权请求的登记处（PROTOCOL.md §3.8）。没设就等于本机不实现 /api/authorize，
     * 对端会看到 404 并回退到手抄 token。
     */
    void setAuthRequests(AuthRequests *auth) { m_auth = auth; }
    AuthRequests *authRequests() const { return m_auth; }

    /**
     * 打开 v2：本机身份 + 配对表（PROTOCOL.md v2 §5）。
     *
     * 两个都给齐才算就绪 —— 有身份没配对表的话，握手能成但没有东西可比对，
     * 那等于 `VerifyNone`，是这一层最不该出现的状态。
     */
    void setIdentity(const afmu::Identity *id, PeerStore *peers);
    bool tlsReady() const { return m_tlsReady; }
    /** 本机身份，配对握手要用它算 SAS。 */
    const afmu::Identity *identity() const { return m_identity; }
    const QSslConfiguration &tlsConfiguration() const { return m_tlsConfig; }
    PeerStore *peerStore() const { return m_peers; }

    /**
     * 允许非 TLS 的 v1 明文连接（草案 §8.1 第 2/4 条）。
     *
     * 关掉之后，首字节不是 `0x16` 的连接**直接断开，不回任何 HTTP 报文** ——
     * 零信任模式下这个端口在效果上只听 TLS。
     *
     * 现在默认开着：两端的 v2 都跑通了，但用户手上还有旧版本。
     * 按 §8.2 的路线，它在第 3 阶段才翻成默认关。
     */
    void setAllowLegacyPlaintext(bool on) { m_allowLegacy = on; }
    bool allowLegacyPlaintext() const { return m_allowLegacy; }

    // 依次尝试 8765 / 8766 / 8767，全失败则绑定随机空闲端口
    bool start(quint16 preferred);
    void stop();
    quint16 actualPort() const { return m_port; }

    qint64 nextTransferId() { return ++m_transferId; }

    /** token 猜错的按 IP 退避（PROTOCOL.md §2.2）。连接之间共享，故挂在服务端上。 */
    AuthThrottle &throttle() { return m_throttle; }

signals:
    /** 对端扫码之后回填自己的地址和 token（PROTOCOL.md §3.9）。 */
    void pairRequested(const QString &host, int port, const QString &token, const QString &name,
                       const QString &os);
    void transferStarted(qint64 id, const QString &name, qint64 total, bool incoming);
    void transferProgress(qint64 id, qint64 done);
    void transferFinished(qint64 id, const QString &path, bool ok, const QString &error);
    void logMessage(const QString &msg);
    void portChanged();

protected:
    void incomingConnection(qintptr handle) override;

private:
    ServerContext m_ctx;
    QPointer<AuthRequests> m_auth;
    QPointer<PeerStore> m_peers;
    const afmu::Identity *m_identity = nullptr;
    QSslConfiguration m_tlsConfig;
    bool m_tlsReady = false;
    bool m_allowLegacy = true;
    AuthThrottle m_throttle;
    quint16 m_port = 0;
    qint64 m_transferId = 0;
};
