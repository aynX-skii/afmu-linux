#pragma once

#include <QNetworkRequest>
#include <QObject>
#include <QUrlQuery>

class QNetworkAccessManager;
class QNetworkReply;
class QIODevice;

// docs/PROTOCOL.md §2 —— HTTP 客户端一侧
class PeerClient : public QObject
{
    Q_OBJECT
public:
    explicit PeerClient(QObject *parent = nullptr);

    void setPeer(const QString &host, int port);
    void setToken(const QString &token);

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

private:
    QNetworkAccessManager *m_nam = nullptr;
    QString m_host;
    int m_port = 0;
    QString m_token;
};
