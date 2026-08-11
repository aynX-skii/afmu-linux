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
     * 打开 v2（PROTOCOL-v2-DRAFT.md §5）：本机身份 + 配对表。
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
    bool secure() const { return !m_expectedFp.isEmpty() || m_pairing; }
    QString expectedFingerprint() const { return m_expectedFp; }

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
};
