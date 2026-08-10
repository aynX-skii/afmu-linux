#include "PeerClient.h"
#include "I18n.h"

#include "Protocol.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

PeerClient::PeerClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    m_nam->setAutoDeleteReplies(false);
    // 空闲超时，不是总时长；对端 socket 超时是 120 秒
    m_nam->setTransferTimeout(std::chrono::seconds(60));
}

void PeerClient::setPeer(const QString &host, int port)
{
    m_host = host;
    m_port = port;
}

void PeerClient::setToken(const QString &token)
{
    m_token = token;
}

QUrl PeerClient::url(const QString &apiPath, const QUrlQuery &query) const
{
    QUrl u;
    u.setScheme(QStringLiteral("http"));
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
    req.setRawHeader(afmu::kTokenHeader, m_token.toUtf8());
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("User-Agent", "afmu-linux/1.0");
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    return req;
}

QNetworkReply *PeerClient::get(const QString &apiPath, const QUrlQuery &query)
{
    return m_nam->get(request(apiPath, query));
}

QNetworkReply *PeerClient::getRaw(const QNetworkRequest &req)
{
    return m_nam->get(req);
}

QNetworkReply *PeerClient::post(const QString &apiPath, const QUrlQuery &query, QIODevice *body,
                                const QByteArray &contentType)
{
    QNetworkRequest req = request(apiPath, query);
    if (!contentType.isEmpty())
        req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    if (body)
        return m_nam->post(req, body);
    req.setHeader(QNetworkRequest::ContentLengthHeader, 0);
    return m_nam->post(req, QByteArray());
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
    if (reply->error() != QNetworkReply::NoError)
        return reply->errorString();
    return T(QStringLiteral("未知错误"));
}
