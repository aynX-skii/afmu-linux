#include "Discovery.h"
#include "I18n.h"

#include <QDateTime>

#include "Identity.h"
#include "PeerStore.h"
#include "Protocol.h"
#include "RollingId.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QTimer>
#include <QUdpSocket>

Discovery::Discovery(QObject *parent)
    : QObject(parent)
{
    m_probeTimer = new QTimer(this);
    m_probeTimer->setSingleShot(true);
    connect(m_probeTimer, &QTimer::timeout, this, [this] {
        m_probing = false;
        if (m_probeSock) {
            m_probeSock->deleteLater();
            m_probeSock = nullptr;
        }
        emit probeFinished();
    });

    // 配对模式到点自动回到常态。用定时器而不是「下次应答时顺手检查」：
    // 界面上的倒计时归零时得有人通知它。
    m_pairingTimer = new QTimer(this);
    m_pairingTimer->setSingleShot(true);
    connect(m_pairingTimer, &QTimer::timeout, this, [this] { stopPairingMode(); });
}

void Discovery::startPairingMode()
{
    m_pairingUntil = QDateTime::currentMSecsSinceEpoch() + qint64(afmu::kPairingModeSec) * 1000;
    m_pairingTimer->start(afmu::kPairingModeSec * 1000);
    emit logMessage(T(QStringLiteral("允许被发现，%1 秒后自动恢复")).arg(afmu::kPairingModeSec));
    emit pairingModeChanged();
}

void Discovery::stopPairingMode()
{
    if (m_pairingUntil == 0)
        return;
    m_pairingUntil = 0;
    m_pairingTimer->stop();
    emit pairingModeChanged();
}

int Discovery::pairingSecondsLeft() const
{
    if (m_pairingUntil == 0)
        return 0;
    const qint64 left = m_pairingUntil - QDateTime::currentMSecsSinceEpoch();
    return left <= 0 ? 0 : int((left + 999) / 1000);
}

QSet<QString> Discovery::localAddresses()
{
    QSet<QString> out;
    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : ifaces) {
        const auto entries = iface.addressEntries();
        for (const QNetworkAddressEntry &e : entries) {
            if (!e.ip().isNull())
                out.insert(e.ip().toString());
        }
    }
    return out;
}

QList<QHostAddress> Discovery::broadcastAddresses()
{
    QList<QHostAddress> out;
    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : ifaces) {
        const auto flags = iface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp)
            || !flags.testFlag(QNetworkInterface::IsRunning)
            || flags.testFlag(QNetworkInterface::IsLoopBack))
            continue;
        const auto entries = iface.addressEntries();
        for (const QNetworkAddressEntry &e : entries) {
            if (e.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            const QHostAddress b = e.broadcast();
            if (!b.isNull() && !out.contains(b))
                out.append(b);
        }
    }
    // 兜底
    if (!out.contains(QHostAddress::Broadcast))
        out.append(QHostAddress(QHostAddress::Broadcast));
    return out;
}

void Discovery::startProbe(int timeoutMs)
{
    m_seen.clear();

    if (!m_probeSock) {
        m_probeSock = new QUdpSocket(this);
        if (!m_probeSock->bind(QHostAddress::AnyIPv4, 0)) {
            emit logMessage(T(QStringLiteral("探测 socket 绑定失败: %1")).arg(m_probeSock->errorString()));
            m_probeSock->deleteLater();
            m_probeSock = nullptr;
            emit probeFinished();
            return;
        }
        connect(m_probeSock, &QUdpSocket::readyRead, this, &Discovery::readProbeReplies);
    }

    m_probing = true;
    const QByteArray payload(afmu::kProbePayload);
    const auto targets = broadcastAddresses();
    for (const QHostAddress &addr : targets)
        m_probeSock->writeDatagram(payload, addr, afmu::kDiscoveryPort);

    // 丢包很常见，隔一小段再补一发
    QTimer::singleShot(qMin(250, timeoutMs / 3), this, [this, payload] {
        if (!m_probing || !m_probeSock)
            return;
        const auto again = broadcastAddresses();
        for (const QHostAddress &addr : again)
            m_probeSock->writeDatagram(payload, addr, afmu::kDiscoveryPort);
    });

    m_probeTimer->start(timeoutMs);
}

void Discovery::probeHost(const QString &host, quint16 port)
{
    if (!m_probeSock) {
        m_probeSock = new QUdpSocket(this);
        m_probeSock->bind(QHostAddress::AnyIPv4, 0);
        connect(m_probeSock, &QUdpSocket::readyRead, this, &Discovery::readProbeReplies);
    }
    m_probing = true;
    m_probeSock->writeDatagram(QByteArray(afmu::kProbePayload), QHostAddress(host),
                               port ? port : afmu::kDiscoveryPort);
    if (!m_probeTimer->isActive())
        m_probeTimer->start(1500);
}

void Discovery::readProbeReplies()
{
    if (!m_probeSock)
        return;
    const QSet<QString> mine = localAddresses();

    while (m_probeSock->hasPendingDatagrams()) {
        QByteArray buf;
        buf.resize(int(m_probeSock->pendingDatagramSize()));
        QHostAddress from;
        quint16 fromPort = 0;
        const qint64 n = m_probeSock->readDatagram(buf.data(), buf.size(), &from, &fromPort);
        if (n <= 0)
            continue;
        buf.truncate(int(n));

        // 自己的应答（serve 模式下会收到）直接丢
        QString hostStr = from.toString();
        if (hostStr.startsWith(QLatin1String("::ffff:")))
            hostStr = hostStr.mid(7);
        if (mine.contains(hostStr))
            continue;

        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(buf.trimmed(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            continue;
        const QJsonObject o = doc.object();
        if (o.value(QStringLiteral("afmu")).toInt(0) != afmu::kProtocolVersion)
            continue; // 字段缺失或大版本不认识 → 非本协议设备

        const int port = o.value(QStringLiteral("port")).toInt(afmu::kDefaultHttpPort);
        const QString key = QStringLiteral("%1:%2").arg(hostStr).arg(port);
        if (m_seen.contains(key))
            continue; // 多网卡会答多次
        m_seen.insert(key);

        // name/os 从 §1.5 起是**可选**的：对端不在配对模式时不会给。
        // 见过一次就记下来，于是日常那份列表里，已经认识的设备照样显示名字，
        // 只有素未谋面的才是一串地址 —— 泄露没了，熟悉感还在。
        QString name = o.value(QStringLiteral("name")).toString();
        QString os = o.value(QStringLiteral("os")).toString();
        if (!name.isEmpty())
            m_knownNames.insert(key, name);
        if (!os.isEmpty())
            m_knownOs.insert(key, os);

        // 配对表比 m_knownNames 强：后者只是这次进程里碰巧见过，按 host:port 存，
        // 换个 IP 就丢了；配对表按指纹存，换 IP 照样认得出来。
        const QString fp = identify(o, hostStr, port, &name, &os);

        if (name.isEmpty())
            name = m_knownNames.value(key);
        if (os.isEmpty())
            os = m_knownOs.value(key, QStringLiteral("unknown"));

        emit deviceFound(name.isEmpty() ? hostStr : name, os, hostStr, port, fp);
    }
}

void Discovery::setAdvertisement(const QString &name, quint16 port, bool discoverable)
{
    m_advName = name;
    m_advPort = port;
    m_discoverable = discoverable;
}

void Discovery::setIdentity(const afmu::Identity *id, PeerStore *peers)
{
    m_identity = (id && id->isValid()) ? id : nullptr;
    m_peers = peers;
}

/**
 * 收到的应答是不是配对表里的某一台。认出来了就回填名字/系统并刷新地址提示。
 *
 * 认不出来是常态，不是错误：陌生设备、v1 设备、以及所有还没配对的设备都认不出来。
 */
QString Discovery::identify(const QJsonObject &reply, const QString &host, int port,
                            QString *name, QString *os)
{
    if (!m_peers)
        return {};

    // 配对模式下对端直接给了 fp（§6.2）。它是明文的、故意的 —— 指纹是公钥指纹，
    // 泄露它不造成访问权损失。但**给了不等于可信**：只有表里已有的才算认识，
    // 否则任何人报一个指纹就能冒充成「你认识的设备」。
    const QString claimed = PeerStore::normalizeFingerprint(
        reply.value(QStringLiteral("fp")).toString());
    if (!claimed.isEmpty() && m_peers->isPaired(claimed))
        return noteIdentified(claimed, host, port, name, os);

    const QString rid = reply.value(QStringLiteral("rid")).toString();
    if (rid.isEmpty())
        return {};

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const auto records = m_peers->all();
    for (const PeerRecord &r : records) {
        if (afmu::ridMatches(afmu::Identity::fromBase32(r.fp), rid, now))
            return noteIdentified(r.fp, host, port, name, os);
    }
    return {};
}

QString Discovery::noteIdentified(const QString &fp, const QString &host, int port, QString *name,
                                  QString *os)
{
    // 应答里自带的名字优先：那是对端此刻的名字，配对表里那个可能是几个月前的。
    // 名字是给人看的，不参与任何判定，所以「新的更好」。
    const PeerRecord r = m_peers->find(fp);
    if (name && name->isEmpty() && !r.name.isEmpty())
        *name = r.name;
    if (os && os->isEmpty() && !r.os.isEmpty())
        *os = r.os;
    // 换了 IP 也认得出来，正是 rid 存在的理由之一（草案 §13 问题 3）。
    // 刷新提示之后，下次连接就能直接找到该钉哪个指纹。
    m_peers->noteSeen(fp, host, port);
    return fp;
}

bool Discovery::responderRunning() const
{
    return m_responder != nullptr;
}

bool Discovery::startResponder()
{
    if (m_responder)
        return true;
    m_responder = new QUdpSocket(this);
    // SO_REUSEADDR，否则重启时 8766 会被 TIME_WAIT 占住
    if (!m_responder->bind(QHostAddress::AnyIPv4, afmu::kDiscoveryPort,
                           QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        emit logMessage(T(QStringLiteral("发现应答端口 %1 绑定失败: %2"))
                            .arg(afmu::kDiscoveryPort)
                            .arg(m_responder->errorString()));
        m_responder->deleteLater();
        m_responder = nullptr;
        return false;
    }
    connect(m_responder, &QUdpSocket::readyRead, this, &Discovery::readRequests);
    return true;
}

void Discovery::stopResponder()
{
    if (!m_responder)
        return;
    m_responder->deleteLater();
    m_responder = nullptr;
}

void Discovery::readRequests()
{
    if (!m_responder)
        return;
    while (m_responder->hasPendingDatagrams()) {
        QByteArray buf;
        buf.resize(int(m_responder->pendingDatagramSize()));
        QHostAddress from;
        quint16 fromPort = 0;
        const qint64 n = m_responder->readDatagram(buf.data(), buf.size(), &from, &fromPort);
        if (n <= 0)
            continue;
        buf.truncate(int(n));

        // 只检查前缀，版本号忽略（留作以后扩展）
        if (!buf.startsWith(afmu::kProbePrefix))
            continue;
        if (!m_discoverable)
            continue; // “可被发现”关闭时直接忽略，不应答

        QJsonObject o;
        o.insert(QStringLiteral("afmu"), afmu::kProtocolVersion);
        o.insert(QStringLiteral("port"), int(m_advPort));
        // 设备名和系统只在配对模式下给（PROTOCOL.md §1.5）。常态下应答它们，
        // 等于任何人发一个 UDP 包就能拿到「这台机器叫 icelab、跑 Linux」——
        // 一次不需要任何凭证的信息泄露，而且发生在用户完全不知情的时候。
        // 滚动标识（草案 §6.1）。常态下也给：陌生人看到的只是一串随机 hex，
        // 而已经配过对的设备算得出同一个值，于是「不泄露设备名」和
        // 「认识的设备还认得出来」可以同时成立。
        if (m_identity) {
            const QString rid =
                afmu::rollingId(m_identity->fingerprint(), QDateTime::currentSecsSinceEpoch());
            if (!rid.isEmpty())
                o.insert(QStringLiteral("rid"), rid);
        }
        if (pairingMode()) {
            o.insert(QStringLiteral("name"), m_advName);
            o.insert(QStringLiteral("os"), QStringLiteral("linux"));
            // 配对模式下才给指纹：这 60 秒是用户主动开的。
            // 注意这是**一次泄露 = 永久可追踪** —— 谁在这段时间抓到 fp，
            // 此后就能算出本机每个窗口的 rid（§6.2 的注）。所以它限时。
            if (m_identity)
                o.insert(QStringLiteral("fp"), m_identity->fingerprintBase32());
        }
        // 应答里绝不包含 token
        const QByteArray reply = QJsonDocument(o).toJson(QJsonDocument::Compact);
        // 回到探测包的源地址和源端口，不回广播
        m_responder->writeDatagram(reply, from, fromPort);
    }
}
