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

    /**
     * 配对模式（PROTOCOL.md §1.5）。
     *
     * 常态下应答**不含设备名和系统**：往 UDP 8766 发一个包，局域网里任何人都能
     * 收到「这台机器叫 icelab、跑 Linux」—— 这是一次不需要任何凭证的信息泄露。
     * 只有用户显式点了「允许被发现」，才在 kPairingModeSec 秒内应答完整信息。
     *
     * 「陌生人能看到设备名」的窗口就从「永远」缩短到「用户主动开启的那一分钟」。
     */
    void startPairingMode();
    void stopPairingMode();
    bool pairingMode() const { return m_pairingUntil > 0; }
    /** 还剩几秒，给界面倒计时用；0 表示没开。 */
    int pairingSecondsLeft() const;

signals:
    void pairingModeChanged();

public:

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

    /** 配对模式的截止时刻（ms since epoch），0 = 没开。 */
    qint64 m_pairingUntil = 0;
    QTimer *m_pairingTimer = nullptr;

    // 见过一次就记住，于是常态应答虽然不带名字，认识的设备仍显示得出来（§1.5）。
    // 按 host:port 存，DHCP 换地址会丢 —— 那时退回显示地址，不是错误。
    QHash<QString, QString> m_knownNames;
    QHash<QString, QString> m_knownOs;
};
