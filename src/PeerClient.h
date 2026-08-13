#pragma once

#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QSslConfiguration>
#include <QUrlQuery>

class QNetworkAccessManager;
class QNetworkReply;
class QIODevice;
class PeerStore;

namespace afmu {
class Identity;
}

// docs/PROTOCOL.md §2 —— HTTP 客户端一侧
class PeerClient : public QObject
{
    Q_OBJECT
public:
    explicit PeerClient(QObject *parent = nullptr);

    void setPeer(const QString &host, int port);
    void setToken(const QString &token);

    /**
     * 打开 v2（PROTOCOL.md v2 §5）：本机身份 + 配对表。
     * 两个都给齐才算数 —— 有身份没配对表的话没有东西可钉，那等于没校验。
     */
    void setIdentity(const afmu::Identity *id, PeerStore *peers);

    /**
     * 这次连接走不走 TLS，以及钉的是哪个指纹。
     *
     * 选择在 setPeer() 时就定死：地址在配对表里有记录就必须走 v2，
     * 之后**绝不会**因为握手失败而退回明文（§8.1 第 1 条）——
     * 那正是降级攻击想要的，也是这一层唯一真正的防线。
     */
    /**
     * 这次连接走不走 TLS。**这和「对端是谁」是两个问题**，别合并 ——
     * 合并过一次，代价是两个 bug：
     *
     *  - 探测模式下（还不知道对面是谁）不带 token，对面是生人就吃 401，
     *    而调用方会把它读成「token 失效了」，转头去发一个多余的授权请求；
     *  - 握手完发现是生人之后，如果这里变回假，后续请求会**悄悄退回明文** ——
     *    同一个会话前半段加密后半段不加密，正是最不该有的形状。
     */
    bool secure() const
    {
        return !m_expectedFp.isEmpty() || m_pairing || m_discover || m_tlsGuest;
    }

    /**
     * 对端的身份**已经由钉扎确认**。只有这时候才不需要 token ——
     * 握手成功 + 指纹在配对表里就是认证本身（v2 §5.2）。
     * 加密但没钉扎（访客、探测中）仍然要带 token，它和 v1 同级。
     */
    bool pinned() const { return !m_expectedFp.isEmpty(); }

    QString expectedFingerprint() const { return m_expectedFp; }

    /**
     * 「先试一下加密」模式：地址在配对表里查不到，但本机有身份、表里也有设备，
     * 所以对面**有可能**是某台已配对设备只是换了地址。
     *
     * 地址反查在正常路径上够用 —— 发现协议的滚动 `rid` 会在见到设备时刷新地址提示
     * （v2 §6.1）。它兜不住的是「用户手工输了一个发现没见过的地址」：那时候查不到
     * 记录，于是连接会退回明文，而对面可能正是你早就配过的那台设备。
     *
     * 所以这里不带期望值地先走一次 TLS，握手完成后按**指纹**认人：
     *   · 指纹在表里 → 当场变成钉扎连接，并刷新地址提示，下次直接命中；
     *   · 指纹不在表里 → 对面是生人，这条连接加密但未认证，和 v1 同级（仍要 token）；
     *   · 握手失败 → 对面多半只会 v1，调用方退回明文重试一次。
     *
     * 判定发生在 `encrypted` 上，也就是**任何请求数据发出之前** —— 和正常钉扎
     * 是同一个时机，不是 §5.1 说的那种「先连上再验」。
     */
    bool discovering() const { return m_discover; }
    /** 握手失败之后关掉它，好让调用方按明文重试一次。 */
    void stopDiscovering()
    {
        m_discover = false;
        m_tlsGuest = false; // 连 TLS 都没握上，别再假装这条会话是加密的
    }

    /**
     * 配对模式：走 TLS，但**不比对指纹** —— 因为此刻还不知道该比什么，
     * 这正是配对要解决的问题（草案 §4.2.4）。
     *
     * 唯一能放心这么做的原因：服务端在同一状态下只放行 `/api/pair-v2`，
     * 别的一律 403。所以这条连接除了走完配对流程之外做不了任何事，
     * 而配对本身的抗中间人靠的是用户比对 SAS，不是钉扎。
     *
     * 握手完成后通过 [peerIdentified] 交出对端指纹。**用完必须关掉**，
     * 否则后面的正常请求会变成"加密但谁都信"。
     */
    void setPairingPeer(const QString &host, int port);
    void endPairing();
    bool pairing() const { return m_pairing; }

    /**
     * 丢掉连接池里空闲的 socket，好让下一个请求重新握手。
     *
     * 对端的身份是握手那一刻定下来的，一条连接终生不变 —— 所以身份状态一变（配对
     * 前后、被撤销之后），复用旧 socket 就是拿旧身份去发新请求。实现里说了细节。
     */
    void dropIdleConnections();

    QString host() const { return m_host; }
    int port() const { return m_port; }
    QString token() const { return m_token; }
    bool hasPeer() const { return !m_host.isEmpty() && m_port > 0; }

    QUrl url(const QString &apiPath, const QUrlQuery &query = {}) const;
    QNetworkRequest request(const QString &apiPath, const QUrlQuery &query = {}) const;

    QNetworkReply *get(const QString &apiPath, const QUrlQuery &query = {});
    QNetworkReply *post(const QString &apiPath, const QUrlQuery &query, QIODevice *body = nullptr,
                        const QByteArray &contentType = {});
    QNetworkReply *getRaw(const QNetworkRequest &req);

    QNetworkAccessManager *nam() const { return m_nam; }

    // 从 reply 里抽出人类可读的错误：优先 {"ok":false,"error":...}，再退回 HTTP 状态
    static QString errorFrom(QNetworkReply *reply, const QByteArray &body = {});

signals:
    /**
     * 握手成功但对端不是配对表里那台。`actual` 为空表示对端根本没给证书。
     *
     * 这条信号只为了让界面能说人话：连接在发出它之前**已经被中止**，
     * 接收方没有"仍然继续"这个选项。
     */
    void pinningFailed(const QString &expected, const QString &actual);

    /** 配对模式下握手完成，这是对端的指纹。空表示对端没出示证书。 */
    void peerIdentified(const QString &fingerprint);

    /**
     * 手工输的地址原来是一台已配对设备（换了 IP）。连接已经就地升级成钉扎的，
     * 地址提示也刷新了 —— 说出来只是让用户知道这次是加密且认过身份的。
     */
    void recognisedAtNewAddress(const QString &name);

private:
    void checkPinning(QNetworkReply *reply);
    QNetworkReply *track(QNetworkReply *reply);

    QNetworkAccessManager *m_nam = nullptr;
    QString m_host;
    int m_port = 0;
    QString m_token;

    const afmu::Identity *m_identity = nullptr;
    QPointer<PeerStore> m_peers;
    QSslConfiguration m_tlsConfig;
    /** 非空 = 这次连接走 TLS，且对端指纹必须正好等于它。 */
    QString m_expectedFp;
    /** 配对模式：走 TLS 但不比对。见 setPairingPeer。 */
    bool m_pairing = false;
    /** 见 discovering()：不带期望值先试 TLS，握手后按指纹认人。 */
    bool m_discover = false;
    /** 探测发现对面是生人：连接保持加密，但身份未认证，照旧带 token。 */
    bool m_tlsGuest = false;
};
