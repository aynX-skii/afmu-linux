#include "Discovery.h"
#include "I18n.h"

#include "Protocol.h"

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

        emit deviceFound(o.value(QStringLiteral("name")).toString(T(QStringLiteral("未命名设备"))),
                         o.value(QStringLiteral("os")).toString(QStringLiteral("unknown")),
                         hostStr, port);
    }
}

void Discovery::setAdvertisement(const QString &name, quint16 port, bool discoverable)
{
    m_advName = name;
    m_advPort = port;
    m_discoverable = discoverable;
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
        o.insert(QStringLiteral("name"), m_advName);
        o.insert(QStringLiteral("os"), QStringLiteral("linux"));
        o.insert(QStringLiteral("port"), int(m_advPort));
        // 应答里绝不包含 token
        const QByteArray reply = QJsonDocument(o).toJson(QJsonDocument::Compact);
        // 回到探测包的源地址和源端口，不回广播
        m_responder->writeDatagram(reply, from, fromPort);
    }
}
