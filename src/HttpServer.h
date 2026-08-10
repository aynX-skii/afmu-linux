#pragma once

#include <QPointer>
#include <QStringList>
#include <QTcpServer>

class AuthRequests;

// docs/PROTOCOL.md §2/§3/§4 —— 本机服务端（对端推/拉本机时用）
struct ServerContext
{
    QString token;
    QString deviceName;
    QString inbox;
    QStringList roots;
    bool writable = true;
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

    // 依次尝试 8765 / 8766 / 8767，全失败则绑定随机空闲端口
    bool start(quint16 preferred);
    void stop();
    quint16 actualPort() const { return m_port; }

    qint64 nextTransferId() { return ++m_transferId; }

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
    quint16 m_port = 0;
    qint64 m_transferId = 0;
};
