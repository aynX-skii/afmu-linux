#include "PeerClient.h"
#include "I18n.h"

#include "Identity.h"
#include "PeerStore.h"
#include "Protocol.h"
#include "Tls.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSslError>
#include <QSslKey>
#include <QSslSocket>

PeerClient::PeerClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    m_nam->setAutoDeleteReplies(false);
    // 空闲超时，不是总时长；对端 socket 超时是 120 秒
    m_nam->setTransferTimeout(std::chrono::seconds(60));

    // 钉扎做在 encrypted 里，不是 sslErrors 里（草案 §5.1）。
    //
    // 这个信号的时机正是要害：握手刚完成、**还没有发出任何用户数据**。
    // 在这里 abort 掉，token / 路径 / 文件内容一个字节都不会漏给冒充者。
    // 放到 finished 或者读响应时再查，数据早就出去了。
    connect(m_nam, &QNetworkAccessManager::encrypted, this, &PeerClient::checkPinning);
}

void PeerClient::setIdentity(const afmu::Identity *id, PeerStore *peers)
{
    m_identity = nullptr;
    m_peers = peers;
    m_tlsConfig = QSslConfiguration();

    if (!id || !id->isValid() || !peers || !QSslSocket::supportsSsl())
        return;
    QSslConfiguration cfg = afmu::clientTlsConfiguration(*id);
    if (cfg.privateKey().isNull() || cfg.localCertificate().isNull())
        return; // 拿不出客户端证书的话，服务端那边一定判空拒绝，不如现在就别装作能用
    m_tlsConfig = cfg;
    m_identity = id;
}

/**
 * 对端是不是配对表里那一台。不是就当场断，不给任何"仍然继续"的余地 ——
 * 钉扎的意义全在这句话上（§4.3）。
 */
void PeerClient::checkPinning(QNetworkReply *reply)
{
    if (!reply || (m_expectedFp.isEmpty() && !m_pairing && !m_discover))
        return;

    const QString actual = afmu::peerFingerprint(reply->sslConfiguration().peerCertificate());

    if (m_pairing) {
        // 配对模式：不比对，只把对端指纹交出去让用户确认（见 setPairingPeer）。
        // 没有证书仍然是错的 —— 那样连"跟谁配对"都说不出来。
        if (actual.isEmpty()) {
            reply->abort();
            emit pinningFailed(QString(), QString());
            return;
        }
        emit peerIdentified(actual);
        return;
    }

    if (m_discover) {
        // 不带期望值连上来的。认不认得出，全看指纹在不在表里 —— 地址一个字都不算。
        m_discover = false;
        if (!actual.isEmpty() && m_peers && m_peers->isPaired(actual)) {
            m_expectedFp = actual;            // 从这一刻起这条会话就是钉扎的
            m_peers->noteSeen(actual, m_host, m_port);
            emit recognisedAtNewAddress(m_peers->find(actual).name);
            return;
        }
        // 生人。连接是加密的，但对面是谁无从谈起 —— 和 v1 同级，仍然要 token。
        // 不中止：这正是「连一台还没配对的设备」，本来就该允许。
        return;
    }

    if (actual == m_expectedFp) {
        // 地址只是提示，但既然这次确实在这儿连上了，把提示更新一下（v2 §13 问题 3）
        if (m_peers)
            m_peers->noteSeen(actual, m_host, m_port);
        return;
    }

    reply->abort();
    emit pinningFailed(m_expectedFp, actual);
}

void PeerClient::setPairingPeer(const QString &host, int port)
{
    m_host = host;
    m_port = port;
    m_expectedFp.clear();
    m_pairing = m_identity != nullptr;
}

void PeerClient::endPairing()
{
    m_pairing = false;
}

void PeerClient::setPeer(const QString &host, int port)
{
    m_host = host;
    m_port = port;
    m_pairing = false;

    // 这个地址在配对表里有记录 → 这次必须走 v2，并钉住那条记录的指纹。
    //
    // 地址只用来**挑候选**，不用来判定身份：挑错了的后果是握手时指纹对不上、
    // 连接被拒，失败方向是安全的。反过来「地址对上了就信任」才是错的。
    m_expectedFp.clear();
    m_discover = false;
    if (!m_identity || !m_peers)
        return;
    const PeerRecord known = m_peers->findByAddressHint(host, port);
    if (!known.fp.isEmpty()) {
        m_expectedFp = known.fp;
        return;
    }
    // 查不到记录，但表里有设备 —— 对面可能是其中一台换了地址（见 discovering()）。
    // 先试加密，握手完了按指纹认人。表是空的就没有认人的余地，别白试。
    m_discover = m_peers->rowCount() > 0;
}

void PeerClient::setToken(const QString &token)
{
    m_token = token;
}

QUrl PeerClient::url(const QString &apiPath, const QUrlQuery &query) const
{
    QUrl u;
    u.setScheme(secure() ? QStringLiteral("https") : QStringLiteral("http"));
    u.setHost(m_host);
    u.setPort(m_port);
    u.setPath(apiPath);
    if (!query.isEmpty()) {
        // 空格必须编码为 %20，'+' 不被解释为空格
        u.setQuery(query);
    }
    return u;
}

QNetworkRequest PeerClient::request(const QString &apiPath, const QUrlQuery &query) const
{
    QNetworkRequest req(url(apiPath, query));
    if (secure()) {
        req.setSslConfiguration(m_tlsConfig);
        // v2 里没有 token 这回事：身份由那对被钉扎的密钥承担（§5.2）。
        // 还带着它只会把 v1 的弱点原样搬过来。
    } else {
        req.setRawHeader(afmu::kTokenHeader, m_token.toUtf8());
    }
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("User-Agent", "afmu-linux/1.0");
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    return req;
}

QNetworkReply *PeerClient::get(const QString &apiPath, const QUrlQuery &query)
{
    return track(m_nam->get(request(apiPath, query)));
}

QNetworkReply *PeerClient::getRaw(const QNetworkRequest &req)
{
    return track(m_nam->get(req));
}

/**
 * 放过「自签证书没有 CA 链」这一类校验错误 —— 那不是意外，是 §5 明确的设计：
 * 链校验被关掉了，可不可信由指纹说了算。
 *
 * **这不是在 sslErrors 里做钉扎**（那正是 §5.1 说的错误写法）。这里一个字节的
 * 身份判断都没有，只是告诉 Qt「CA 这条路我们本来就不走」；真正的比对在
 * checkPinning 里，时机是 encrypted —— 握手已完成、用户数据还没发出去。
 *
 * 只放过预期内的那几种，而且必须全部落在预期内才放过：多出任何一种没见过的错误，
 * 连接就该断。QNAM 的默认行为是「没有明确忽略就中止」，把这个默认留在原地。
 */
QNetworkReply *PeerClient::track(QNetworkReply *reply)
{
    if (!reply || !secure())
        return reply;
    connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &errors) {
        QList<QSslError> expected;
        for (const QSslError &e : errors) {
            switch (e.error()) {
            case QSslError::SelfSignedCertificate:
            case QSslError::SelfSignedCertificateInChain:
            case QSslError::HostNameMismatch:
            case QSslError::CertificateUntrusted:
            case QSslError::UnableToGetLocalIssuerCertificate:
            case QSslError::UnableToVerifyFirstCertificate:
                expected.append(e);
                break;
            default:
                break;
            }
        }
        if (expected.size() == errors.size())
            reply->ignoreSslErrors(expected);
    });
    return reply;
}

QNetworkReply *PeerClient::post(const QString &apiPath, const QUrlQuery &query, QIODevice *body,
                                const QByteArray &contentType)
{
    QNetworkRequest req = request(apiPath, query);
    if (!contentType.isEmpty())
        req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    if (body)
        return track(m_nam->post(req, body));
    req.setHeader(QNetworkRequest::ContentLengthHeader, 0);
    if (contentType.isEmpty()) {
        // 没有 body 的 POST。不声明的话 Qt 会替我们猜一个并打警告 ——
        // 参数全在 query 里，这里声明一个空 body 的类型就行。
        req.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    }
    return track(m_nam->post(req, QByteArray()));
}

QString PeerClient::errorFrom(QNetworkReply *reply, const QByteArray &body)
{
    QByteArray payload = body;
    if (payload.isEmpty() && reply && reply->isReadable())
        payload = reply->readAll();

    if (!payload.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(payload);
        if (doc.isObject()) {
            const QJsonObject o = doc.object();
            if (!o.value(QStringLiteral("ok")).toBool(false)) {
                const QString e = o.value(QStringLiteral("error")).toString();
                if (!e.isEmpty())
                    return e;
            }
        }
    }
    if (!reply)
        return T(QStringLiteral("未知错误"));

    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (code == 401)
        return T(QStringLiteral("token 不对（401）—— 对端 token 要填手机 App 首页显示的那 10 位；"))
             + T(QStringLiteral("如果在手机上重新生成过，这里也要跟着改"));
    if (code == 403)
        return T(QStringLiteral("对端为只读模式（403）"));
    if (code == 404)
        return T(QStringLiteral("路径不存在或越界（404）"));
    if (code >= 400)
        return QStringLiteral("HTTP %1").arg(code);
    // 钉扎失败走的是 abort，报出来是 OperationCanceled；真正的原因已经由
    // pinningFailed 说清楚了，这里别再套一层含糊的"操作被取消"。
    if (reply->error() == QNetworkReply::OperationCanceledError)
        return T(QStringLiteral("连接已中止"));
    if (reply->error() == QNetworkReply::SslHandshakeFailedError)
        return T(QStringLiteral("加密握手失败 —— 对端可能还没升级到加密连接，")) 
             + T(QStringLiteral("或者本机的身份没被它认可"));
    if (reply->error() != QNetworkReply::NoError)
        return reply->errorString();
    return T(QStringLiteral("未知错误"));
}
