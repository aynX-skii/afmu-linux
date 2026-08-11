#pragma once

#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>

class QUdpSocket;
class QTimer;
class PeerStore;

namespace afmu {
class Identity;
}

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
     * 打开发现协议的 v2 部分（PROTOCOL-v2-DRAFT.md §6）：本机身份 + 配对表。
     *
     * 给了之后，常态应答里多一个滚动 `rid`：陌生人只看到一串随机 hex，
     * 而**手里有本机指纹的设备算得出同一个值**，于是认识的设备照样显示名字。
     * 收到应答时反过来做同一件事，所以配过对的 PC 换了 IP 也能认出来
     * （草案 §13 问题 3），不必重新配对。
     *
     * 两个都给齐才有意义：没有身份就算不出自己的 rid，没有配对表就没有
     * 任何指纹可以拿来比对收到的 rid。
     */
    void setIdentity(const afmu::Identity *id, PeerStore *peers);

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
    /**
     * `fingerprint` 非空 = 这台是配对表里的某一台，靠 `rid`（或配对模式下的 `fp`）认出来的。
     * 空表示不认识 —— 那是绝大多数情况，也包括所有 v1 设备。
     */
    void deviceFound(const QString &name, const QString &os, const QString &host, int port,
                     const QString &fingerprint);
    void probeFinished();
    void logMessage(const QString &msg);

private:
    void readProbeReplies();
    void readRequests();
    QString identify(const QJsonObject &reply, const QString &host, int port, QString *name,
                     QString *os);
    QString noteIdentified(const QString &fp, const QString &host, int port, QString *name,
                           QString *os);

    QUdpSocket *m_probeSock = nullptr;
    QUdpSocket *m_responder = nullptr;
    QTimer *m_probeTimer = nullptr;
    bool m_probing = false;
    QSet<QString> m_seen;

    QString m_advName;
    quint16 m_advPort = 0;
    bool m_discoverable = true;

    const afmu::Identity *m_identity = nullptr;
    QPointer<PeerStore> m_peers;

    /** 配对模式的截止时刻（ms since epoch），0 = 没开。 */
    qint64 m_pairingUntil = 0;
    QTimer *m_pairingTimer = nullptr;

    // 见过一次就记住，于是常态应答虽然不带名字，认识的设备仍显示得出来（§1.5）。
    // 按 host:port 存，DHCP 换地址会丢 —— 那时退回显示地址，不是错误。
    QHash<QString, QString> m_knownNames;
    QHash<QString, QString> m_knownOs;
};
