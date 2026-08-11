#include "AppController.h"
#include "I18n.h"

#include "AuthRequests.h"
#include "Config.h"
#include "Discovery.h"
#include "HttpServer.h"
#include "Models.h"
#include "PeerClient.h"
#include "Protocol.h"
#include "TransferModel.h"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_config(new Config(this))
    , m_discovery(new Discovery(this))
    , m_devices(new DeviceModel(this))
    , m_files(new RemoteFileModel(this))
    , m_client(new PeerClient(this))
    , m_server(new HttpServer(this))
    , m_incomingAuth(new AuthRequests(this))
{
    // 必须最先建：autoStartServer 会在本构造函数里就打日志，
    // I18n 还没就位的话 T() 只能退回中文原文
    m_i18n = new I18n(m_config, this);

    // 配置读不出来是**很要紧**的事：token 会跟着变成新的，所有已配对设备一起连不上。
    // 以前这种情况是静默重置，用户只能看到「怎么全变回默认了」。现在至少说出来，
    // 并告诉他原文件留在哪 —— token 还能从那里抠回来。
    if (!m_config->loadError().isEmpty())
        appendLog(m_config->loadError());

    m_transfers = new TransferModel(m_client, m_config, this);
    m_client->setToken(m_config->peerToken());

    connect(m_discovery, &Discovery::deviceFound, this,
            [this](const QString &name, const QString &os, const QString &host, int port) {
                m_devices->upsert(DeviceInfo{name, os, host, port});
            });
    connect(m_discovery, &Discovery::probeFinished, this, [this] {
        m_scanning = false;
        emit scanningChanged();
        if (m_devices->rowCount() > 0)
            return;
        // 很多路由器会吃掉广播，但单播 TCP 是通的：复用上次连过的地址
        if (!m_connected && !m_config->lastHost().isEmpty() && !m_config->peerToken().isEmpty()) {
            appendLog(T(QStringLiteral("没收到广播应答，复用上次地址 %1:%2"))
                          .arg(m_config->lastHost())
                          .arg(m_config->lastPort()));
            connectToDevice(m_config->lastHost(), m_config->lastPort(), m_config->lastHost(),
                            QStringLiteral("unknown"));
            return;
        }
        notify(T(QStringLiteral("没有发现设备。确认对方设备与本机在同一 Wi-Fi、接收服务已打开，"))
                   + T(QStringLiteral("或用「手动连接」直接输入地址。")),
               true);
    });
    connect(m_discovery, &Discovery::logMessage, this, &AppController::appendLog);

    // 配对模式：界面上有个倒计时，所以开着的时候每秒通知一次
    m_pairingTick = new QTimer(this);
    m_pairingTick->setInterval(1000);
    connect(m_pairingTick, &QTimer::timeout, this, &AppController::pairingModeChanged);
    connect(m_discovery, &Discovery::pairingModeChanged, this, [this] {
        if (m_discovery->pairingMode())
            m_pairingTick->start();
        else
            m_pairingTick->stop();
        emit pairingModeChanged();
    });

    connect(m_config, &Config::changed, this, [this] {
        m_client->setToken(m_config->peerToken());
        m_incomingAuth->setEnabled(m_config->allowAuthRequests());
        applyServerContext();
        emit pairUriChanged();
    });

    connect(m_server, &HttpServer::logMessage, this, &AppController::appendLog);
    connect(m_server, &HttpServer::portChanged, this, [this] {
        applyServerContext();
        emit serverChanged();
        emit pairUriChanged();
    });
    // 对端扫完码（或被本机授权之后），把自己的 token 回填过来：直接连上，用户什么都不用再点
    connect(m_server, &HttpServer::pairRequested, this,
            [this](const QString &host, int port, const QString &token, const QString &name,
                   const QString &os) {
                const QString who = name.isEmpty() ? host : name;
                appendLog(T(QStringLiteral("%1 已扫码配对（%2:%3）")).arg(who, host).arg(port));
                m_config->setPeerToken(token);
                m_devices->upsert(DeviceInfo{who, os.isEmpty() ? QStringLiteral("unknown") : os,
                                             host, port});
                notify(T(QStringLiteral("已与 %1 配对")).arg(who), false);
                if (!m_connected)
                    connectToDevice(host, port, who, os);
            });
    connect(m_server, &HttpServer::transferStarted, this,
            [this](qint64 id, const QString &name, qint64 total, bool incoming) {
                m_serverIds.insert(id, m_transfers->addServerTransfer(name, total, incoming));
                m_serverInbound.insert(id, incoming);
            });
    connect(m_server, &HttpServer::transferProgress, this, [this](qint64 id, qint64 done) {
        if (m_serverIds.contains(id))
            m_transfers->updateServerTransfer(m_serverIds.value(id), done);
    });
    connect(m_server, &HttpServer::transferFinished, this,
            [this](qint64 id, const QString &path, bool ok, const QString &error) {
                if (!m_serverIds.contains(id))
                    return;
                const bool incoming = m_serverInbound.take(id);
                m_transfers->finishServerTransfer(m_serverIds.take(id), path, ok, error);
                if (ok && !path.isEmpty())
                    appendLog(incoming ? T(QStringLiteral("收到文件 %1")).arg(path)
                                       : T(QStringLiteral("对端取走 %1")).arg(path));
                else if (!ok)
                    appendLog(T(QStringLiteral("传输失败: %1")).arg(error));
            });

    connect(m_transfers, &TransferModel::message, this, &AppController::notify);
    connect(m_transfers, &TransferModel::transferCompleted, this,
            [this](const QString &name, int kind) {
                const bool inbound = kind == TransferModel::Download || kind == TransferModel::ServerIncoming;
                notify(QStringLiteral("%1 %2").arg(inbound ? T(QStringLiteral("已保存")) : T(QStringLiteral("已发送")), name),
                       false);
            });

    // 语言切换后，C++ 侧算出来的属性文案（未连接 / 只读 …）也要跟着刷新
    if (I18n *lang = I18n::instance()) {
        connect(lang, &I18n::languageChanged, this, [this] {
            emit peerChanged();
            emit serverChanged();
        });
    }

    m_authTimer = new QTimer(this);
    m_authTimer->setInterval(1000);
    connect(m_authTimer, &QTimer::timeout, this, &AppController::pollAuthorization);

    // 别的设备来敲门（PROTOCOL.md §3.8）。服务端和界面看的是同一个登记处。
    m_server->setAuthRequests(m_incomingAuth);
    m_incomingAuth->setEnabled(m_config->allowAuthRequests());
    m_incomingAuthTimer = new QTimer(this);
    m_incomingAuthTimer->setInterval(1000);
    connect(m_incomingAuthTimer, &QTimer::timeout, this, [this] {
        // 超时按拒绝算，判定在 AuthRequests 里；这里只负责按秒推它一把并刷新界面
        if (incomingAuthRemaining() <= 0)
            m_incomingAuth->sweepExpired();
        emit incomingAuthChanged();
        if (!incomingAuthPending())
            m_incomingAuthTimer->stop();
    });
    connect(m_incomingAuth, &AuthRequests::pendingChanged, this, [this] {
        if (incomingAuthPending()) {
            appendLog(T(QStringLiteral("%1（%2）请求连接本机，确认码 %3"))
                          .arg(incomingAuthName(), incomingAuthHost(), incomingAuthCode()));
            m_incomingAuthTimer->start();
        } else {
            m_incomingAuthTimer->stop();
        }
        emit incomingAuthChanged();
    });

    applyServerContext();
    if (m_config->autoStartServer())
        startServer();
}

AppController::~AppController() = default;

QObject *AppController::devicesObj() const { return m_devices; }
QObject *AppController::filesObj() const { return m_files; }
QObject *AppController::transfersObj() const { return m_transfers; }

QString AppController::peerHost() const { return m_client->host(); }
int AppController::peerPort() const { return m_client->port(); }

QString AppController::peerSummary() const
{
    if (!m_connected)
        return T(QStringLiteral("未连接"));
    return QStringLiteral("%1 · %2:%3").arg(m_peerName).arg(m_client->host()).arg(m_client->port());
}

void AppController::setLoading(bool v)
{
    if (m_loading == v)
        return;
    m_loading = v;
    emit loadingChanged();
}

void AppController::notify(const QString &text, bool isError)
{
    m_toastText = text;
    m_toastIsError = isError;
    emit toast();
    if (isError)
        appendLog(text);
}

void AppController::appendLog(const QString &line)
{
    qInfo().noquote() << "[afmu]" << line;
    m_log.prepend(QStringLiteral("[%1] %2")
                      .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), line));
    while (m_log.size() > 300)
        m_log.removeLast();
    emit serverLogChanged();
}

void AppController::clearLog()
{
    m_log.clear();
    emit serverLogChanged();
}

// ---------------------------------------------------------------- 发现 / 连接

void AppController::scan()
{
    if (m_scanning)
        return;
    m_devices->clear();
    m_scanning = true;
    emit scanningChanged();
    m_discovery->startProbe(m_config->discoverTimeoutMs());
}

void AppController::connectToDevice(const QString &host, int port, const QString &name, const QString &os)
{
    // 连上一台设备就意味着两个方向都要用。手机推文件走的是本机的 HTTP 服务，
    // 服务没起来时手机那头只会看到一句 "Failed to connect"，而这边毫无提示。
    ensureServerRunning();
    m_client->setPeer(host, port > 0 ? port : afmu::kDefaultHttpPort);
    m_client->setToken(m_config->peerToken());
    m_peerName = name.isEmpty() ? host : name;
    m_peerOs = os;
    m_config->setLastHost(host);
    m_config->setLastPort(port);
    m_connected = false;
    emit peerChanged();
    fetchInfo();
}

void AppController::connectManual(const QString &hostPort, const QString &token)
{
    QString host = hostPort.trimmed();
    int port = afmu::kDefaultHttpPort;
    if (host.startsWith(QLatin1String("http://")))
        host = host.mid(7);
    host = host.split(QLatin1Char('/')).first();
    const int colon = host.lastIndexOf(QLatin1Char(':'));
    if (colon > 0) {
        bool ok = false;
        const int p = host.mid(colon + 1).toInt(&ok);
        if (ok && p > 0) {
            port = p;
            host = host.left(colon);
        }
    }
    if (host.isEmpty()) {
        notify(T(QStringLiteral("请输入设备地址，例如 192.168.1.30:8765")), true);
        return;
    }
    if (!token.trimmed().isEmpty())
        m_config->setPeerToken(token.trimmed());
    connectToDevice(host, port, host, QStringLiteral("unknown"));
}

void AppController::disconnectPeer()
{
    m_client->setPeer(QString(), 0);
    m_connected = false;
    m_peerName.clear();
    m_peerOs.clear();
    m_peerRoots.clear();
    m_peerInbox.clear();
    m_currentPath.clear();
    m_parentPath.clear();
    m_files->clear();
    emit peerChanged();
    emit pathChanged();
}

// ---------------------------------------------------------------- 授权连接

void AppController::requestAuthorization(const QString &host, int port, const QString &name,
                                         const QString &os)
{
    if (m_authPending)
        return;
    if (host.isEmpty()) {
        notify(T(QStringLiteral("先选一台设备，再请求授权")), true);
        return;
    }

    m_authHost = host;
    m_authPort = port > 0 ? port : int(afmu::kDefaultHttpPort);
    m_authOs = os;
    m_peerName = name.isEmpty() ? host : name;
    m_peerOs = os;
    m_authTarget = QStringLiteral("%1 · %2:%3").arg(m_peerName, m_authHost).arg(m_authPort);
    // 确认码两端同时显示。局域网里任何人都能触发对方弹窗，靠这个让用户看出
    // 弹的到底是不是自己刚点的那一下。
    m_authCode = afmu::makePairingCode();
    m_authRequestId.clear();
    m_authRemaining = afmu::kAuthTimeoutSec;
    m_authStatus = QStringLiteral("sending");
    m_authPending = true;
    emit authChanged();

    // 下面要把本机端口报给对端，报之前得保证那个端口真有人听。
    ensureServerRunning();

    m_client->setPeer(m_authHost, m_authPort);

    QUrlQuery q;
    q.addQueryItem(QStringLiteral("name"), m_config->deviceName());
    q.addQueryItem(QStringLiteral("os"), QStringLiteral("linux"));
    q.addQueryItem(QStringLiteral("code"), m_authCode);
    q.addQueryItem(QStringLiteral("port"),
                   QString::number(serverRunning() ? serverPort() : m_config->serverPort()));

    QNetworkReply *reply = m_client->post(QStringLiteral("/api/authorize"), q);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (!m_authPending)
            return; // 用户已经取消了
        const QByteArray body = reply->readAll();
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        // 没实现这个接口的对端有两种表现：404（没有这条路由），或者 401 —— 它把
        // /api/* 一律先过 token 检查，于是免鉴权的授权请求也被挡在门外。两种都是
        // 「不支持」，不是「失败」，提示要能让用户直接退回手抄 token 或扫码。
        if (code == 404 || code == 405 || code == 401) {
            finishAuthorization(QStringLiteral("unsupported"));
            return;
        }
        if (reply->error() != QNetworkReply::NoError || !o.value(QStringLiteral("ok")).toBool(false)) {
            appendLog(T(QStringLiteral("授权请求被拒绝：%1")).arg(PeerClient::errorFrom(reply, body)));
            finishAuthorization(code == 403   ? QStringLiteral("disabled")
                                : code == 429 ? QStringLiteral("busy")
                                              : QStringLiteral("failed"));
            return;
        }

        m_authRequestId = o.value(QStringLiteral("request")).toString();
        if (m_authRequestId.isEmpty()) {
            finishAuthorization(QStringLiteral("failed"));
            return;
        }
        m_authRemaining = qBound(5, o.value(QStringLiteral("expires")).toInt(afmu::kAuthTimeoutSec), 300);
        m_authStatus = QStringLiteral("pending");
        emit authChanged();
        appendLog(T(QStringLiteral("已向 %1 发起授权请求，确认码 %2")).arg(m_authTarget, m_authCode));
        m_authTimer->start();
    });
}

void AppController::pollAuthorization()
{
    if (!m_authPending || m_authRequestId.isEmpty())
        return;
    if (--m_authRemaining <= 0) {
        finishAuthorization(QStringLiteral("expired"));
        return;
    }
    emit authChanged(); // 只为了刷新倒计时

    QUrlQuery q;
    q.addQueryItem(QStringLiteral("request"), m_authRequestId);
    QNetworkReply *reply = m_client->get(QStringLiteral("/api/authorize"), q);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (!m_authPending)
            return;
        const QByteArray body = reply->readAll();
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        if (reply->error() != QNetworkReply::NoError || !o.value(QStringLiteral("ok")).toBool(false)) {
            // 404 = 请求已经被对端丢掉；网络抖动则下一秒再试
            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 404)
                finishAuthorization(QStringLiteral("expired"));
            return;
        }

        const QString status = o.value(QStringLiteral("status")).toString();
        if (status == QLatin1String("pending"))
            return;
        if (status == QLatin1String("denied")) {
            finishAuthorization(QStringLiteral("denied"));
            return;
        }
        if (status != QLatin1String("granted")) {
            finishAuthorization(QStringLiteral("expired"));
            return;
        }

        const QString token = o.value(QStringLiteral("token")).toString();
        if (token.isEmpty()) {
            finishAuthorization(QStringLiteral("failed"));
            return;
        }
        const QString name = o.value(QStringLiteral("name")).toString(m_peerName);
        finishAuthorization(QStringLiteral("granted"));
        m_config->setPeerToken(token);
        connectToDevice(m_authHost, m_authPort, name, m_authOs);
        // 反向也配上：手机推文件到本机时不用再手抄 PC token
        pushPairBack();
    });
}

void AppController::cancelAuthorization()
{
    if (!m_authPending)
        return;
    finishAuthorization(QStringLiteral("cancelled"));
}

void AppController::finishAuthorization(const QString &status)
{
    m_authTimer->stop();
    m_authPending = false;
    m_authStatus = status;
    m_authRequestId.clear();
    m_authRemaining = 0;
    emit authChanged();

    if (status == QLatin1String("granted")) {
        notify(T(QStringLiteral("已获授权，正在连接 %1")).arg(m_peerName), false);
    } else if (status == QLatin1String("denied")) {
        notify(T(QStringLiteral("对方拒绝了本次连接")), true);
    } else if (status == QLatin1String("expired")) {
        notify(T(QStringLiteral("授权请求已超时，对方没有确认")), true);
    } else if (status == QLatin1String("unsupported")) {
        notify(T(QStringLiteral("对端不支持授权连接，请手动填写 token 或扫描本机二维码")), true);
    } else if (status == QLatin1String("disabled")) {
        notify(T(QStringLiteral("对方关掉了「允许连接请求」，请在它的设置里打开")), true);
    } else if (status == QLatin1String("busy")) {
        notify(T(QStringLiteral("对方正在处理另一个连接请求，稍后再试")), true);
    } else if (status == QLatin1String("failed")) {
        notify(T(QStringLiteral("授权请求失败，请稍后重试")), true);
    }
}

// ------------------------------------------------- 别的设备来请求连接本机

bool AppController::incomingAuthPending() const { return !m_incomingAuth->pending().isNull(); }
QString AppController::incomingAuthName() const { return m_incomingAuth->pending().name; }
QString AppController::incomingAuthHost() const { return m_incomingAuth->pending().host; }
QString AppController::incomingAuthOs() const { return m_incomingAuth->pending().os; }
QString AppController::incomingAuthCode() const { return m_incomingAuth->pending().code; }

int AppController::incomingAuthRemaining() const
{
    return m_incomingAuth->pending().remainingSec(QDateTime::currentMSecsSinceEpoch());
}

void AppController::approveIncomingAuth()
{
    const AuthRequests::Request r = m_incomingAuth->pending();
    if (r.isNull())
        return;
    // token 到这一刻才离开本机。对方随后会用它调 /api/pair 把自己的 token 回填过来，
    // 于是两个方向一起通（PROTOCOL.md §3.9）。
    m_incomingAuth->decide(r.id, true);
    m_devices->upsert(DeviceInfo{r.name, r.os.isEmpty() ? QStringLiteral("unknown") : r.os, r.host,
                                 r.port > 0 ? r.port : int(afmu::kDefaultHttpPort)});
    appendLog(T(QStringLiteral("已允许 %1（%2）连接本机")).arg(r.name, r.host));
    notify(T(QStringLiteral("已允许 %1 连接")).arg(r.name), false);
}

void AppController::denyIncomingAuth()
{
    const AuthRequests::Request r = m_incomingAuth->pending();
    if (r.isNull())
        return;
    m_incomingAuth->decide(r.id, false);
    appendLog(T(QStringLiteral("已拒绝 %1（%2）")).arg(r.name, r.host));
}

void AppController::pushPairBack()
{
    if (m_config->localToken().isEmpty() || !m_client->hasPeer())
        return;
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("name"), m_config->deviceName());
    q.addQueryItem(QStringLiteral("os"), QStringLiteral("linux"));
    q.addQueryItem(QStringLiteral("port"),
                   QString::number(serverRunning() ? serverPort() : m_config->serverPort()));
    q.addQueryItem(QStringLiteral("token"), m_config->localToken());

    QNetworkReply *reply = m_client->post(QStringLiteral("/api/pair"), q);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            appendLog(T(QStringLiteral("已把本机 token 回填给对端")));
            return;
        }
        // 对端没实现 /api/pair 不是错误，只是手机侧还得自己填一次 PC token
        appendLog(T(QStringLiteral("对端未接受回填，对方仍需手填本机 token")));
    });
}

QString AppController::pairUri() const
{
    return afmu::buildPairUri(m_config->deviceName(), QStringLiteral("linux"), localAddresses(),
                              serverRunning() ? serverPort() : m_config->serverPort(),
                              m_config->localToken());
}

void AppController::fetchInfo()
{
    if (!m_client->hasPeer())
        return;
    // token 是空的就别白跑一趟 401：直接请对端弹窗授权，用户在手机上点一下即可。
    // 手抄 token 的老路子仍然有效，填了就走填的那个。
    if (m_config->peerToken().isEmpty()) {
        appendLog(T(QStringLiteral("没有对端 token，改为发起授权请求")));
        requestAuthorization(m_client->host(), m_client->port(), m_peerName, m_peerOs);
        return;
    }
    setLoading(true);
    QNetworkReply *reply = m_client->get(QStringLiteral("/api/info"));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        setLoading(false);
        const QByteArray body = reply->readAll();
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        if (reply->error() != QNetworkReply::NoError || !o.value(QStringLiteral("ok")).toBool(false)) {
            m_connected = false;
            emit peerChanged();
            const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            // token 过期 / 手机上重新生成过：与其让用户去抄新的，不如直接请对方授权
            if (code == 401 && !m_authPending) {
                appendLog(T(QStringLiteral("token 已失效，改为发起授权请求")));
                requestAuthorization(m_client->host(), m_client->port(), m_peerName, m_peerOs);
                return;
            }
            notify(T(QStringLiteral("连接失败：%1")).arg(PeerClient::errorFrom(reply, body)), true);
            return;
        }
        // 未知字段必须忽略；缺失的 Android 专有字段必须容忍
        const int proto = o.value(QStringLiteral("protocol")).toInt(afmu::kProtocolVersion);
        if (proto > afmu::kProtocolVersion) {
            notify(T(QStringLiteral("对端协议版本 %1 高于本客户端支持的 %2")).arg(proto).arg(afmu::kProtocolVersion),
                   true);
            return;
        }
        m_peerName = o.value(QStringLiteral("name")).toString(m_peerName);
        m_peerOs = o.value(QStringLiteral("os")).toString(m_peerOs);
        m_peerWritable = o.value(QStringLiteral("writable")).toBool(true);
        m_peerInbox = o.value(QStringLiteral("inbox")).toString();
        m_peerRoots.clear();
        const QJsonArray roots = o.value(QStringLiteral("roots")).toArray();
        for (const QJsonValue &v : roots) {
            const QJsonObject r = v.toObject();
            QVariantMap m;
            m.insert(QStringLiteral("name"), r.value(QStringLiteral("name")).toString());
            m.insert(QStringLiteral("path"), r.value(QStringLiteral("path")).toString());
            m_peerRoots.append(m);
        }
        m_connected = true;
        emit peerChanged();
        appendLog(T(QStringLiteral("已连接 %1 (%2:%3)")).arg(m_peerName).arg(m_client->host()).arg(m_client->port()));
        goRoot();
    });
}

// ---------------------------------------------------------------- 浏览

void AppController::refresh()
{
    navigate(m_currentPath);
}

void AppController::goRoot()
{
    navigate(QStringLiteral("/"));
}

void AppController::goParent()
{
    if (m_parentPath.isEmpty())
        navigate(QStringLiteral("/"));
    else
        navigate(m_parentPath);
}

void AppController::navigate(const QString &path)
{
    if (!m_client->hasPeer() || !m_connected)
        return;
    setLoading(true);
    QUrlQuery q;
    if (!path.isEmpty() && path != QLatin1String("/"))
        q.addQueryItem(QStringLiteral("path"), path);

    QNetworkReply *reply = m_client->get(QStringLiteral("/api/list"), q);
    connect(reply, &QNetworkReply::finished, this, [this, reply, path] {
        reply->deleteLater();
        setLoading(false);
        const QByteArray body = reply->readAll();
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        if (reply->error() != QNetworkReply::NoError || !o.value(QStringLiteral("ok")).toBool(false)) {
            const QString err = PeerClient::errorFrom(reply, body);
            notify(T(QStringLiteral("列目录失败：%1")).arg(err), true);
            emit listingFailed(err);
            return;
        }
        m_currentPath = o.value(QStringLiteral("path")).toString(path);
        const QJsonValue parent = o.value(QStringLiteral("parent"));
        m_parentPath = parent.isNull() ? QString() : parent.toString();
        m_files->setEntries(o.value(QStringLiteral("entries")).toArray());
        emit pathChanged();
    });
}

QVariantList AppController::breadcrumbs() const
{
    QVariantList out;
    QVariantMap root;
    root.insert(QStringLiteral("name"), T(QStringLiteral("根目录")));
    root.insert(QStringLiteral("path"), QStringLiteral("/"));
    out.append(root);
    if (atRoot())
        return out;

    // 从 roots 里找最长匹配前缀，作为面包屑的起点
    QString base;
    QString baseName;
    for (const QVariant &v : m_peerRoots) {
        const QVariantMap m = v.toMap();
        const QString p = m.value(QStringLiteral("path")).toString();
        if (p.isEmpty())
            continue;
        if (m_currentPath == p || m_currentPath.startsWith(p + QLatin1Char('/'))) {
            if (p.size() > base.size()) {
                base = p;
                baseName = m.value(QStringLiteral("name")).toString();
            }
        }
    }

    QString accum;
    if (!base.isEmpty()) {
        QVariantMap m;
        m.insert(QStringLiteral("name"), baseName.isEmpty() ? base : baseName);
        m.insert(QStringLiteral("path"), base);
        out.append(m);
        accum = base;
    }

    const QString rest = base.isEmpty() ? m_currentPath : m_currentPath.mid(base.size());
    const QStringList parts = rest.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &p : parts) {
        accum += QLatin1Char('/') + p;
        QVariantMap m;
        m.insert(QStringLiteral("name"), p);
        m.insert(QStringLiteral("path"), accum);
        out.append(m);
    }
    return out;
}

// ---------------------------------------------------------------- 收发

void AppController::downloadPath(const QString &remotePath, const QString &name, double size)
{
    if (!m_connected) {
        notify(T(QStringLiteral("未连接到设备")), true);
        return;
    }
    m_transfers->addDownload(remotePath, name, qint64(size));
}

void AppController::downloadSelected()
{
    const auto sel = m_files->selectedEntries();
    if (sel.isEmpty()) {
        notify(T(QStringLiteral("先勾选要下载的文件")), true);
        return;
    }
    int n = 0;
    for (const RemoteEntry &e : sel) {
        if (e.isDir)
            continue; // 目录暂不递归下载
        m_transfers->addDownload(e.path, e.name, e.size);
        ++n;
    }
    m_files->clearSelection();
    if (n == 0)
        notify(T(QStringLiteral("选中的都是目录，已跳过")), true);
    else
        notify(T(QStringLiteral("已加入 %1 个下载任务")).arg(n), false);
}

void AppController::uploadUrls(const QList<QUrl> &urls)
{
    QStringList paths;
    for (const QUrl &u : urls) {
        const QString p = urlToLocalPath(u);
        if (!p.isEmpty())
            paths << p;
    }
    uploadPaths(paths);
}

void AppController::uploadPaths(const QStringList &paths)
{
    if (!m_connected) {
        notify(T(QStringLiteral("未连接到设备")), true);
        return;
    }
    if (!m_peerWritable) {
        notify(T(QStringLiteral("对端为只读模式，无法上传")), true);
        return;
    }
    int n = 0;
    int skipped = 0;
    for (const QString &p : paths) {
        QFileInfo fi(p);
        if (!fi.exists())
            continue;
        if (fi.isDir()) {
            ++skipped; // 目录跳过并警告，而不是报错退出
            continue;
        }
        m_transfers->addUpload(fi.absoluteFilePath(), atRoot() ? QString() : m_currentPath);
        ++n;
    }
    if (skipped > 0)
        notify(T(QStringLiteral("已跳过 %1 个目录（暂不支持目录上传）")).arg(skipped), true);
    if (n > 0)
        notify(T(QStringLiteral("已加入 %1 个上传任务")).arg(n), false);
}

void AppController::makeDirectory(const QString &name)
{
    if (!m_connected || name.trimmed().isEmpty())
        return;
    if (atRoot()) {
        notify(T(QStringLiteral("请先进入某个目录再新建")), true);
        return;
    }
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("path"), m_currentPath);
    q.addQueryItem(QStringLiteral("name"), name.trimmed());
    QNetworkReply *reply = m_client->post(QStringLiteral("/api/mkdir"), q);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        if (reply->error() != QNetworkReply::NoError || !o.value(QStringLiteral("ok")).toBool(false)) {
            notify(T(QStringLiteral("新建目录失败：%1")).arg(PeerClient::errorFrom(reply, body)), true);
            return;
        }
        notify(T(QStringLiteral("已新建 %1")).arg(o.value(QStringLiteral("path")).toString()), false);
        refresh();
    });
}

void AppController::deletePath(const QString &path, bool recursive)
{
    if (!m_connected || path.isEmpty())
        return;
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("path"), path);
    if (recursive)
        q.addQueryItem(QStringLiteral("recursive"), QStringLiteral("1"));
    QNetworkReply *reply = m_client->post(QStringLiteral("/api/delete"), q);
    connect(reply, &QNetworkReply::finished, this, [this, reply, path] {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        if (reply->error() != QNetworkReply::NoError || !o.value(QStringLiteral("ok")).toBool(false)) {
            notify(T(QStringLiteral("删除失败：%1")).arg(PeerClient::errorFrom(reply, body)), true);
            return;
        }
        notify(T(QStringLiteral("已删除 %1")).arg(path.section(QLatin1Char('/'), -1)), false);
        refresh();
    });
}

void AppController::deleteSelected()
{
    const auto sel = m_files->selectedEntries();
    for (const RemoteEntry &e : sel)
        deletePath(e.path, e.isDir);
    m_files->clearSelection();
}

// ---------------------------------------------------------------- 服务端

void AppController::applyServerContext()
{
    ServerContext ctx;
    ctx.token = m_config->localToken();
    ctx.deviceName = m_config->deviceName();
    ctx.inbox = m_config->inboxDir();
    ctx.writable = !m_config->readOnly();
    ctx.roots = m_config->serveRoots();
    // inbox 必须在 roots 里，否则手机拉不回自己刚推上来的东西
    QDir().mkpath(ctx.inbox);
    if (!ctx.roots.contains(ctx.inbox))
        ctx.roots.prepend(ctx.inbox);
    m_server->setContext(ctx);

    m_discovery->setAdvertisement(m_config->deviceName(),
                                  m_server->isListening() ? m_server->actualPort()
                                                          : quint16(m_config->serverPort()),
                                  m_config->discoverable());
}

bool AppController::serverRunning() const { return m_server->isListening(); }
int AppController::serverPort() const { return m_server->actualPort(); }

bool AppController::pairingMode() const { return m_discovery->pairingMode(); }
int AppController::pairingRemaining() const { return m_discovery->pairingSecondsLeft(); }

void AppController::startPairingMode() { m_discovery->startPairingMode(); }
void AppController::stopPairingMode() { m_discovery->stopPairingMode(); }

QStringList AppController::localAddresses() const
{
    QStringList out;
    const auto set = Discovery::localAddresses();
    for (const QString &a : set) {
        if (a == QLatin1String("127.0.0.1") || a.contains(QLatin1Char(':')))
            continue;
        out << a;
    }
    out.sort();
    return out;
}

void AppController::startServer()
{
    applyServerContext();
    if (!m_server->start(quint16(m_config->serverPort()))) {
        notify(T(QStringLiteral("服务端启动失败，端口可能被占用")), true);
        emit serverChanged();
        return;
    }
    applyServerContext();
    if (!m_discovery->startResponder())
        notify(T(QStringLiteral("UDP %1 绑定失败，手机将无法自动发现本机")).arg(afmu::kDiscoveryPort), true);
    emit serverChanged();
    notify(T(QStringLiteral("服务已启动，端口 %1")).arg(m_server->actualPort()), false);
}

bool AppController::ensureServerRunning()
{
    if (serverRunning())
        return true;
    appendLog(T(QStringLiteral("接收服务未启动，正在自动启动，否则手机发不过来")));
    startServer();
    return serverRunning();
}

void AppController::stopServer()
{
    // 服务没了就没人能来取结果，界面上还挂着一个待决请求只会让人困惑
    m_incomingAuth->clear();
    m_server->stop();
    m_discovery->stopResponder();
    emit serverChanged();
}

void AppController::restartServerIfRunning()
{
    if (!m_server->isListening())
        return;
    stopServer();
    startServer();
}

// ---------------------------------------------------------------- 杂项

void AppController::copyToClipboard(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
    notify(T(QStringLiteral("已复制")), false);
}

void AppController::openLocalFolder(const QString &path)
{
    QString p = path;
    if (p.isEmpty())
        p = m_config->downloadDir();
    QDir().mkpath(p);
    QDesktopServices::openUrl(QUrl::fromLocalFile(p));
}

QString AppController::urlToLocalPath(const QUrl &url) const
{
    return url.isLocalFile() ? url.toLocalFile() : QString();
}
