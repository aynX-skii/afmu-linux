#pragma once

#include <QHostAddress>
#include <QObject>
#include <QSet>

class QUdpSocket;
class QTimer;

// docs/PROTOCOL.md §1 —— UDP 8766 设备发现
class Discovery : public QObject
{
    Q_OBJECT
public:
    explicit Discovery(QObject *parent = nullptr);

    // 客户端：向所有接口的广播地址发探测包，timeoutMs 内边收边等
    void startProbe(int timeoutMs);
    // 定向探测：广播被 AP 隔离吃掉时用
    void probeHost(const QString &host, quint16 port = 0);
    bool probing() const { return m_probing; }

    // 服务端：监听 8766 并应答
    bool startResponder();
    void stopResponder();
    bool responderRunning() const;

    // 应答内容
    void setAdvertisement(const QString &name, quint16 port, bool discoverable);

    static QSet<QString> localAddresses();
    static QList<QHostAddress> broadcastAddresses();

signals:
    void deviceFound(const QString &name, const QString &os, const QString &host, int port);
    void probeFinished();
    void logMessage(const QString &msg);

private:
    void readProbeReplies();
    void readRequests();

    QUdpSocket *m_probeSock = nullptr;
    QUdpSocket *m_responder = nullptr;
    QTimer *m_probeTimer = nullptr;
    bool m_probing = false;
    QSet<QString> m_seen;

    QString m_advName;
    quint16 m_advPort = 0;
    bool m_discoverable = true;
};
