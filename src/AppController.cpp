#include "AppController.h"
#include "I18n.h"

#include "AuthRequests.h"
#include "Config.h"
#include "Discovery.h"
#include "HttpServer.h"
#include "Models.h"
#include "Identity.h"
#include "PairSas.h"
#include "PeerClient.h"
#include "PeerStore.h"
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
#include <QCryptographicHash>
#include <QNetworkReply>
#include <QRandomGenerator>
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
    , m_peers(new PeerStore(this))
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

    // §8.2 第 3 阶段的一次性迁移动过手了，说一声。**必须说**：这一下会让还在跑
    // 旧版本的设备、以及浏览器界面，从今天起连不上，而用户看到的表现只是
    // 「今天开始连不上了」—— 不说的话他会去查网络、查防火墙、查路由器。
    if (m_config->plaintextJustDisabled()) {
        appendLog(T(QStringLiteral(
            "已停止接受明文连接（协议 §8.2 第 3 阶段）。还在用旧版本的设备和浏览器界面"
            "会连不上 —— 需要的话，去「设置 → 加密连接」重新打开「允许未加密的旧版连接」。")));
    }

    const QString configDir = QFileInfo(m_config->configFilePath()).absolutePath();

    // 配对表放在 config.json 旁边，跟着同一个 XDG 目录走。
    m_peers->load(QDir(configDir).filePath(QStringLiteral("peers.json")));
    if (!m_peers->loadError().isEmpty())
        appendLog(m_peers->loadError());

    // 本机身份（草案 §3）。第一次跑会生成一对密钥；读不出来就报错而不是重新生成 ——
    // 换密钥等于换设备，所有已配对关系一起作废，而症状只是「突然谁都连不上」。
    m_identity = std::make_unique<afmu::Identity>();
    if (m_identity->ensure(QDir(configDir).filePath(QStringLiteral("identity.pem")))) {
        m_server->setIdentity(m_identity.get(), m_peers);
        m_client->setIdentity(m_identity.get(), m_peers);
        m_discovery->setIdentity(m_identity.get(), m_peers);
        appendLog(m_server->tlsReady()
                      ? T(QStringLiteral("本机指纹 %1")).arg(m_identity->fingerprintDisplay())
                      : T(QStringLiteral("本机身份可用，但 TLS 没能就绪，只能走明文")));
    } else {
        appendLog(T(QStringLiteral("身份不可用：%1")).arg(m_identity->lastError()));
    }

    m_transfers = new TransferModel(m_client, m_config, this);
    m_client->setToken(m_config->peerToken());

    connect(m_discovery, &Discovery::deviceFound, this,
            [this](const QString &name, const QString &os, const QString &host, int port,
                   const QString &fp) {
                m_devices->upsert(DeviceInfo{name, os, host, port, fp});
            });
    connect(m_discovery, &Discovery::probeFinished, this, [this] {
        m_scanning = false;
        emit scanningChanged();
        if (m_devices->rowCount() > 0)
            return;
        // 很多路由器会吃掉广播，但单播 TCP 是通的：复用上次连过的地址
        // 有 token，或者配对表里有设备 —— 后者是 v2 的情况，它压根不需要 token，
        // 而对面正可能是其中一台换了地址（见 PeerClient::discovering）。
        if (!m_connected && !m_config->lastHost().isEmpty()
            && (!m_config->peerToken().isEmpty() || m_peers->rowCount() > 0)) {
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

    // 连上的不是配对表里那台。这是钉扎唯一会对用户可见的时刻，所以话要说明白：
    // 不是"证书有问题"，是"这不是你配对的那台设备"。
    // 配对模式下握手完成，拿到对端指纹。SAS 要用它，配对成功也要用它。
    connect(m_client, &PeerClient::peerIdentified, this,
            [this](const QString &fp) { m_pairPeerFp = fp; });

    // 手工输的地址原来是一台配过对的设备。它已经就地变成钉扎连接了，说一声就好。
    connect(m_client, &PeerClient::recognisedAtNewAddress, this, [this](const QString &name) {
        appendLog(T(QStringLiteral("认出这是已配对的 %1（换了地址），本次连接已加密并钉扎"))
                      .arg(name));
    });

    connect(m_client, &PeerClient::pinningFailed, this,
            [this](const QString &expected, const QString &actual) {
                appendLog(actual.isEmpty()
                              ? T(QStringLiteral("对端没有出示证书，已中止连接"))
                              : T(QStringLiteral("指纹不匹配，已中止连接。期望 %1，实际 %2"))
                                    .arg(afmu::Identity::group(expected),
                                         afmu::Identity::group(actual)));
                notify(T(QStringLiteral("这不是你配对的那台设备，已中止连接")), true);
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
                // 指纹留空：这是 v1 的回填路径，此刻没有握手过的证书可填。
                // DeviceModel::upsert 会保留已有的那个，不会把认出来的设备打回「不认识」。
                m_devices->upsert(DeviceInfo{who, os.isEmpty() ? QStringLiteral("unknown") : os,
                                             host, port, {}});
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
            // v2 配对没有 4 位确认码，只有 SAS，而 SAS 要等第 2 步（reveal）才算得出来 ——
            // 第 1 步就打一条「确认码 」的空日志只会让人以为出了错。
            const AuthRequests::Request r = m_incomingAuth->pending();
            if (r.isPairing()) {
                if (!r.sas.isEmpty()) {
                    appendLog(T(QStringLiteral("%1（%2）请求配对，比对码 %3"))
                                  .arg(r.name, r.host, afmu::formatSas(r.sas)));
                }
            } else {
                appendLog(T(QStringLiteral("%1（%2）请求连接本机，确认码 %3"))
                              .arg(incomingAuthName(), incomingAuthHost(), incomingAuthCode()));
            }
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
QObject *AppController::peersObj() const { return m_peers; }

QString AppController::localFingerprint() const
{
    return m_identity && m_identity->isValid() ? m_identity->fingerprintDisplay() : QString();
}

bool AppController::tlsReady() const { return m_server->tlsReady(); }

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
    m_authError.clear(); // 上一次的失败被这一次顶掉
    emit authChanged();
    // 倒计时从**按下按钮**这一刻开始走，不是从对端第一次应答开始。见 pollAuthorization。
    m_authTimer->start();

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
            // 403 有两个来源，指向的动作完全不同：对端关了「允许连接请求」，
            // 还是对端关了访客模式 —— 后者意味着 token 这条路整个不通了（v2 §9.3），
            // 让用户去开「允许连接请求」只会白忙一场，该说的是"改用加密配对"。
            const bool guestOff =
                o.value(QStringLiteral("error")).toString().contains(QLatin1String("guest mode"));
            finishAuthorization(code == 403 ? (guestOff ? QStringLiteral("guestoff")
                                                        : QStringLiteral("disabled"))
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
        // 定时器在发请求那一刻就起来了，这里不用再 start —— 再 start 一次只会
        // 把当前这一秒重新计起。轮询从下一跳自然开始（那时 request id 已经有了）。
    });
}


// ------------------------------------------------- v2 配对（草案 §4.2）

void AppController::requestPairing(const QString &host, int port, const QString &name,
                                   const QString &os)
{
    if (m_authPending)
        return;
    if (host.isEmpty()) {
        notify(T(QStringLiteral("先选一台设备，再请求配对")), true);
        return;
    }
    if (!m_server->tlsReady()) {
        notify(T(QStringLiteral("本机的加密身份不可用，无法配对")), true);
        return;
    }

    m_authHost = host;
    m_authPort = port > 0 ? port : int(afmu::kDefaultHttpPort);
    m_authOs = os;
    m_peerName = name.isEmpty() ? host : name;
    m_peerOs = os;
    m_authTarget = QStringLiteral("%1 · %2:%3").arg(m_peerName, m_authHost).arg(m_authPort);
    m_authCode.clear();
    m_authRequestId.clear();
    m_authRemaining = afmu::kAuthTimeoutSec;
    m_authStatus = QStringLiteral("sending");
    m_authPending = true;
    m_authIsPairing = true;

    m_pairSession.clear();
    m_pairPeerFp.clear();
    m_pairSas.clear();
    m_pairNonceB.clear();
    // n_a 现在就定死，而且在 commit 发出去之后**绝不重新生成**：
    // 换一个就等于允许在看到 n_b 之后改主意，commit 这一步就白做了（§4.2.2）。
    m_pairNonceA.resize(32);
    QRandomGenerator::system()->generate(m_pairNonceA.begin(), m_pairNonceA.end());
    m_authError.clear(); // 上一次的失败被这一次顶掉
    emit authChanged();
    // 同上：倒计时和超时都从按下按钮那一刻起算。见 pollAuthorization。
    m_authTimer->start();

    ensureServerRunning();
    // 配对模式：走 TLS 但不比对指纹 —— 此刻还不知道该比什么，这正是要解决的问题。
    m_client->setPairingPeer(m_authHost, m_authPort);
    // 上面那句只在**客户端**拿得出证书时才真的进入配对模式。拿不出来的话
    // secure() 是假的，接下来这一步会以明文发出去 —— 而明文配对没有任何意义：
    // 没有证书就没有可授权的指纹，整段交换还会被旁听（v2 §4.2.4）。
    // 服务端的 tlsReady 不能替它作答：那是两套配置，判空的条件也不一样。
    if (!m_client->pairing()) {
        appendLog(T(QStringLiteral("本机拿不出客户端证书，配对不能退回明文，已中止")));
        finishAuthorization(QStringLiteral("nolocaltls"));
        return;
    }
    pairingCommit();
}

void AppController::pairingCommit()
{
    const QByteArray commit =
        QCryptographicHash::hash(m_pairNonceA, QCryptographicHash::Sha256);

    QUrlQuery q;
    q.addQueryItem(QStringLiteral("step"), QStringLiteral("commit"));
    q.addQueryItem(QStringLiteral("commit"), QString::fromLatin1(commit.toHex()));
    q.addQueryItem(QStringLiteral("name"), m_config->deviceName());
    q.addQueryItem(QStringLiteral("os"), QStringLiteral("linux"));
    // 本机服务端口。对端从 socket 只看得到我们的 IP 和**源**端口，不知道我们在哪个
    // 端口收东西 —— 不给的话它存下的地址提示是猜的，将来反向连过来就连不上。
    // v1 的 /api/authorize 本来就带这个，v2 漏了。
    q.addQueryItem(QStringLiteral("port"),
                   QString::number(m_server->isListening() ? m_server->actualPort()
                                                           : quint16(m_config->serverPort())));

    QNetworkReply *reply = m_client->post(QStringLiteral("/api/pair-v2"), q);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (!m_authPending || !m_authIsPairing)
            return;
        const QByteArray body = reply->readAll();
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || !o.value(QStringLiteral("ok")).toBool(false)) {
            // **失败必须留下痕迹。** 这里原来只按状态码分个类就弹一句「配对失败」，
            // 连日志都不写 —— 而配对失败最常见的原因（对端只提供明文，握手压根没成）
            // 状态码是 0，于是两端日志都是空的，用户手上一点线索都没有。
            appendLog(T(QStringLiteral("配对第一步失败：%1"))
                          .arg(PeerClient::errorFrom(reply, body)));
            if (reply->error() == QNetworkReply::SslHandshakeFailedError)
                finishAuthorization(QStringLiteral("plaintext")); // 对端没在听 TLS
            else if (code == 404)
                finishAuthorization(QStringLiteral("unsupported")); // 对端还不会 v2
            else if (code == 429)
                finishAuthorization(QStringLiteral("denied"));
            else
                finishAuthorization(QStringLiteral("failed"));
            return;
        }
        m_pairSession = o.value(QStringLiteral("session")).toString();
        m_pairNonceB = afmu::hexDecodeStrict(o.value(QStringLiteral("nb")).toString());
        if (m_pairSession.isEmpty() || m_pairNonceB.size() != 32) {
            appendLog(T(QStringLiteral("对端第一步的应答不完整（缺 session 或 n_b），已中止")));
            finishAuthorization(QStringLiteral("failed"));
            return;
        }
        pairingReveal();
    });
}

void AppController::pairingReveal()
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("step"), QStringLiteral("reveal"));
    q.addQueryItem(QStringLiteral("session"), m_pairSession);
    q.addQueryItem(QStringLiteral("na"), QString::fromLatin1(m_pairNonceA.toHex()));

    QNetworkReply *reply = m_client->post(QStringLiteral("/api/pair-v2"), q);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (!m_authPending || !m_authIsPairing)
            return;
        const QByteArray body = reply->readAll();
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        if (reply->error() != QNetworkReply::NoError || !o.value(QStringLiteral("ok")).toBool(false)) {
            appendLog(T(QStringLiteral("配对第二步失败：%1"))
                          .arg(PeerClient::errorFrom(reply, body)));
            finishAuthorization(QStringLiteral("failed"));
            return;
        }

        // **自己算一遍，显示自己算的那个。** 服务端回的只用来自检有没有实现 bug ——
        // 显示它等于让中间人回一个你期望的串就骗过去，整套机制归零（§4.2.3）。
        const QString mine = afmu::computeSas(m_identity->fingerprint(),
                                              afmu::Identity::fromBase32(m_pairPeerFp),
                                              m_pairNonceA, m_pairNonceB);
        if (mine.isEmpty()) {
            // 算不出来只有两种原因：拿不到对端指纹（握手里没有证书），或者长度不对。
            // 两种都得说出来，否则和"对端拒绝"看起来一模一样。
            appendLog(m_pairPeerFp.isEmpty()
                          ? T(QStringLiteral("没能从握手里取到对端指纹，算不出比对码，已中止"))
                          : T(QStringLiteral("比对码算不出来（指纹或随机数长度不对），已中止")));
            finishAuthorization(QStringLiteral("failed"));
            return;
        }
        if (mine != o.value(QStringLiteral("sas")).toString()) {
            // 两端算出的不一样 = 有一端实现不对。**一个码都不要显示** ——
            // 显示了用户就会去比对，而这时候比对本身已经没有意义了。
            appendLog(T(QStringLiteral("两端算出的比对码不一致，已中止 —— 这是实现问题，不是攻击")));
            finishAuthorization(QStringLiteral("failed"));
            return;
        }

        m_pairSas = mine;
        m_authStatus = QStringLiteral("pending");
        emit authChanged();
        appendLog(T(QStringLiteral("配对比对码 %1，请与对端屏幕核对"))
                      .arg(afmu::formatSas(m_pairSas)));
        // 同上：定时器早就在跑了，轮询从下一跳开始（那时 session 已经有了）。
    });
}

void AppController::pollPairing()
{
    // 倒计时的递减和到期判断统一由 pollAuthorization 做，这里只负责问一次结果。
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("session"), m_pairSession);
    QNetworkReply *reply = m_client->get(QStringLiteral("/api/pair-v2"), q);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (!m_authPending || !m_authIsPairing)
            return;
        const QByteArray body = reply->readAll();
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        if (reply->error() != QNetworkReply::NoError || !o.value(QStringLiteral("ok")).toBool(false)) {
            // 网络抖动下一秒就再试，所以这里**不**每次都记日志 —— 一秒一条会把
            // 真正有用的那几行冲走。会话没了是终局，那条要说。
            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 404) {
                appendLog(T(QStringLiteral("对端已经不认这个配对会话：%1"))
                              .arg(PeerClient::errorFrom(reply, body)));
                finishAuthorization(QStringLiteral("expired"));
            }
            return;
        }
        const QString status = o.value(QStringLiteral("status")).toString();
        if (status == QLatin1String("pending"))
            return;
        if (status != QLatin1String("granted")) {
            finishAuthorization(status == QLatin1String("denied") ? QStringLiteral("denied")
                                                                  : QStringLiteral("expired"));
            return;
        }

        // 对端点了「允许」。把它写进配对表 —— 这一下才是真正开门。
        PeerRecord rec;
        rec.fp = m_pairPeerFp;
        rec.name = o.value(QStringLiteral("name")).toString();
        if (rec.name.isEmpty())
            rec.name = m_peerName;
        rec.os = o.value(QStringLiteral("os")).toString();
        rec.lastHost = m_authHost;
        rec.lastPort = o.value(QStringLiteral("port")).toInt(m_authPort);
        m_peers->upsert(rec);

        appendLog(T(QStringLiteral("已与 %1 配对，指纹 %2"))
                      .arg(rec.name, afmu::Identity::group(rec.fp)));
        const QString who = rec.name;
        const QString host = m_authHost;
        const int port = rec.lastPort;
        const QString os = rec.os;
        finishAuthorization(QStringLiteral("granted"));
        notify(T(QStringLiteral("已与 %1 配对")).arg(who), false);
        // 配对完立刻按正常方式连一次：这次会走钉扎，是真正的 v2 连接。
        connectToDevice(host, port, who, os);
    });
}

void AppController::pollAuthorization()
{
    if (!m_authPending)
        return;

    // **倒计时从按下按钮那一刻起算，两条路径共用这一处递减。**
    //
    // 原来只有拿到 request id / session 之后才开始走，于是第一步还没回来时
    // 界面上那句「剩余 60 秒」是**死的** —— 用户看着一个不动的数字，分不清是在
    // 等网络还是已经卡死（对端手机息屏时这一步要等十几秒是常事）。更要命的是
    // 那段时间里没有任何东西会收掉弹窗：兜底的只剩 QNAM 那个 60 秒**空闲**超时，
    // 比协议这个 60 秒还长。
    if (--m_authRemaining <= 0) {
        finishAuthorization(QStringLiteral("expired"));
        return;
    }
    emit authChanged(); // 只为了刷新倒计时

    // 第一步还没回来的时候只走倒计时，不轮询：没有 session / request id 可问。
    if (m_authIsPairing) {
        if (!m_pairSession.isEmpty())
            pollPairing();
        return;
    }
    if (m_authRequestId.isEmpty())
        return;

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

QString AppController::failureMessage(const QString &status, bool wasPairing)
{
    // granted 是成功；cancelled 是用户自己点的「取消」—— 两种都不用解释。
    if (status == QLatin1String("granted") || status == QLatin1String("cancelled"))
        return {};

    if (wasPairing) {
        if (status == QLatin1String("denied"))
            return T(QStringLiteral("对方拒绝了本次配对"));
        if (status == QLatin1String("expired"))
            return T(QStringLiteral("配对超时，对方没有确认"));
        if (status == QLatin1String("unsupported"))
            return T(QStringLiteral("对端还不支持加密配对"));
        if (status == QLatin1String("plaintext"))
            // 握手就没成，对端那个端口上没有 TLS 在听。这在手机上是**一个开关**的事，
            // 而不是"再试一次"能解决的 —— Android 一次只能提供一种协议（v2 §5.3），
            // 明文开着的时候它压根不建 TLS。所以直接把要点哪里说出来。
            return T(QStringLiteral("对端只提供明文连接，加密握手没能建立。请在手机的设置里"
                                    "打开「只接受加密连接」，再点一次加密配对"));
        if (status == QLatin1String("nolocaltls"))
            return T(QStringLiteral("本机拿不出客户端证书，无法加密配对（明文配对没有意义）"));
        if (status == QLatin1String("failed"))
            return T(QStringLiteral("配对失败，原因见日志"));
        return {};
    }

    if (status == QLatin1String("denied"))
        return T(QStringLiteral("对方拒绝了本次连接"));
    if (status == QLatin1String("expired"))
        return T(QStringLiteral("授权请求已超时，对方没有确认"));
    if (status == QLatin1String("unsupported"))
        return T(QStringLiteral("对端不支持授权连接，请手动填写 token 或扫描本机二维码"));
    if (status == QLatin1String("disabled"))
        return T(QStringLiteral("对方关掉了「允许连接请求」，请在它的设置里打开"));
    if (status == QLatin1String("guestoff"))
        return T(QStringLiteral("对端关掉了访客模式，token 这条路已经不通 —— 请改用「加密配对」"));
    if (status == QLatin1String("busy"))
        return T(QStringLiteral("对方正在处理另一个连接请求，稍后再试"));
    if (status == QLatin1String("failed"))
        return T(QStringLiteral("授权请求失败，请稍后重试"));
    return {};
}

void AppController::finishAuthorization(const QString &status)
{
    m_authTimer->stop();
    m_authPending = false;
    m_authStatus = status;
    m_authRequestId.clear();
    m_authRemaining = 0;

    const bool wasPairing = m_authIsPairing;
    m_authIsPairing = false;
    m_pairSession.clear();
    // 随机数用完就丢：留着只会诱惑下一次复用，而复用一组随机数等于把
    // commit-reveal 的绑定作废（§4.2.2）。
    m_pairNonceA.clear();
    m_pairNonceB.clear();
    // 配对模式下这条客户端不比对指纹，用完必须关掉，否则后面的正常请求
    // 会变成"加密但谁都信"。
    m_client->endPairing();

    // **失败留在弹窗里，不再只发一条会自己消失的提示条。**
    //
    // 弹窗是 visible: App.authPending 绑上去的，所以只要这里把 pending 置假，
    // 它当场就没了。而最快的一种失败（对端只提供明文，TLS 握手当场崩）从点击到
    // 这一行只要 86 毫秒 —— 用户看到的就是"弹窗闪一下就没了、手机上什么都没弹"，
    // 而唯一的解释在窗口底部那条 5 秒的提示条上，视线根本不在那儿。
    // 失败是这条流程的常态，常态不能只有一个转瞬即逝的落点。
    m_authErrorPairing = wasPairing;
    m_authError = failureMessage(status, wasPairing);
    emit authChanged();
    if (!m_authError.isEmpty())
        appendLog(m_authError);

    // 成功那两句仍然走提示条：它不需要用户做任何事，也不该挡住下一步操作。
    // （配对成功的那句已经在 pollPairing 里说过了。）
    if (!wasPairing && status == QLatin1String("granted"))
        notify(T(QStringLiteral("已获授权，正在连接 %1")).arg(m_peerName), false);
}

void AppController::dismissAuthError()
{
    if (m_authError.isEmpty())
        return;
    m_authError.clear();
    m_authErrorPairing = false;
    emit authChanged();
}

// ------------------------------------------------- 别的设备来请求连接本机

bool AppController::incomingAuthPending() const { return !m_incomingAuth->pending().isNull(); }
QString AppController::incomingAuthName() const { return m_incomingAuth->pending().name; }
QString AppController::incomingAuthHost() const { return m_incomingAuth->pending().host; }
QString AppController::incomingAuthOs() const { return m_incomingAuth->pending().os; }
QString AppController::incomingAuthCode() const { return m_incomingAuth->pending().code; }

bool AppController::incomingAuthIsPairing() const
{
    return m_incomingAuth->pending().isPairing();
}

QString AppController::incomingAuthFingerprint() const
{
    return afmu::Identity::group(m_incomingAuth->pending().peerFp);
}

QString AppController::incomingAuthSas() const
{
    return afmu::formatSas(m_incomingAuth->pending().sas);
}

int AppController::incomingAuthRemaining() const
{
    return m_incomingAuth->pending().remainingSec(QDateTime::currentMSecsSinceEpoch());
}

void AppController::approveIncomingAuth()
{
    const AuthRequests::Request r = m_incomingAuth->pending();
    if (r.isNull())
        return;

    if (r.isPairing()) {
        // v2：用户点「允许」这一下，就是把对端指纹写进配对表 —— 也就是开门
        // （草案 §4.2.3）。**没有 token 交出去**，身份就是那对密钥。
        PeerRecord rec;
        rec.fp = r.peerFp;
        rec.name = r.name;
        rec.os = r.os;
        rec.lastHost = r.host;
        rec.lastPort = r.port > 0 ? r.port : int(afmu::kDefaultHttpPort);
        m_peers->upsert(rec);
        m_incomingAuth->decide(r.id, true);
        // 列表里同时标成「已配对」：否则用户点完允许，那台设备在列表里看起来毫无变化。
        m_devices->upsert(DeviceInfo{rec.name.isEmpty() ? rec.lastHost : rec.name,
                                     rec.os.isEmpty() ? QStringLiteral("unknown") : rec.os,
                                     rec.lastHost, rec.lastPort, rec.fp});
        appendLog(T(QStringLiteral("已与 %1 配对，指纹 %2"))
                      .arg(r.name, afmu::Identity::group(r.peerFp)));
        notify(T(QStringLiteral("已与 %1 配对")).arg(r.name), false);
        return;
    }

    // token 到这一刻才离开本机。对方随后会用它调 /api/pair 把自己的 token 回填过来，
    // 于是两个方向一起通（PROTOCOL.md §3.9）。
    m_incomingAuth->decide(r.id, true);
    // 同上：v1 授权连接没有证书，指纹留空由 upsert 保留旧值
    m_devices->upsert(DeviceInfo{r.name, r.os.isEmpty() ? QStringLiteral("unknown") : r.os, r.host,
                                 r.port > 0 ? r.port : int(afmu::kDefaultHttpPort), {}});
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
    // 能走 v2 就出 v2 的码 —— 里面是公钥指纹，泄露它不造成任何损失。
    // 加密不可用时才退回 v1（码里是 token，截图就等于交出访问权）。
    const QString fp = m_server->tlsReady() && m_identity ? m_identity->fingerprintBase32()
                                                          : QString();
    return afmu::buildPairUri(m_config->deviceName(), QStringLiteral("linux"), localAddresses(),
                              serverRunning() ? serverPort() : m_config->serverPort(),
                              m_config->localToken(), fp);
}

void AppController::fetchInfo()
{
    if (!m_client->hasPeer())
        return;
    // token 是空的就别白跑一趟 401：直接请对端弹窗授权，用户在手机上点一下即可。
    // 手抄 token 的老路子仍然有效，填了就走填的那个。
    //
    // 但走加密的连接不在此列：**握手成功 + 指纹在配对表里就是认证本身**（v2 §5.2），
    // 没有任何 token 需要交换。在这里拦下来的话，v2 会比 v1 还难用 —— 提示用户去填
    // 一个对面已经不再接受的密码。「先试一下加密」的连接也放行：它要么认出对方
    // 从而变成钉扎的，要么在下面吃一个 401 再走授权那条路。
    if (m_config->peerToken().isEmpty() && !m_client->secure()) {
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
            // 「先试一下加密」没试成：对面多半只会 v1。关掉再走一次明文。
            //
            // 只在这一个探测请求上重试，别的请求路径一概不碰 —— 一个会自己换协议
            // 重发的客户端，正是降级攻击最想要的东西。这里之所以可以，是因为它
            // **只发生在连接建立那一刻**，而且只往「本来就没有钉扎期望」的方向退。
            if (m_client->discovering()) {
                m_client->stopDiscovering();
                appendLog(T(QStringLiteral("对端似乎不支持加密连接，退回明文重试一次")));
                fetchInfo();
                return;
            }
            // token 过期 / 手机上重新生成过：与其让用户去抄新的，不如直接请对方授权
            if (code == 401 && !m_authPending) {
                appendLog(T(QStringLiteral("token 已失效，改为发起授权请求")));
                requestAuthorization(m_client->host(), m_client->port(), m_peerName, m_peerOs);
                return;
            }
            // 配对表里的对端必须走 v2，握手不成就是不成，**绝不退回明文**（v2 §8.1 第 1 条）。
            // 但这时候界面上只会看到一句「连接关闭」，和网线松了长得一模一样 ——
            // 这是一次**有意的拒绝**，得说出来，否则用户会去查网络、查防火墙。
            if (!m_client->expectedFingerprint().isEmpty() && code == 0) {
                appendLog(T(QStringLiteral("%1 在配对表里，只接受加密连接，"
                                           "而这次握手没成 —— 已拒绝，不会退回明文"))
                              .arg(m_peerName));
                notify(T(QStringLiteral("这台设备只接受加密连接，握手失败，已拒绝")), true);
                return;
            }
            // 「本机在你表里，你不在我表里」—— 配对只剩了一半（对方删了配对、
            // 重装了应用、或者换了身份）。这句话只有真正握手成功的对端说得出来，
            // 所以它是可信的，但**不能**因此就把本机这边的记录删掉：删除是用户的
            // 决定。能做的是说清楚，并把「加密配对」那颗按钮放回来 —— 界面本来
            // 对已配对设备把它藏了起来，于是唯一的出路正好在需要它的时候消失。
            if (code == 403
                && o.value(QStringLiteral("error")).toString().contains(
                    QLatin1String("only /api/pair-v2"))) {
                setForgotUsPeer(QStringLiteral("%1:%2").arg(m_client->host()).arg(m_client->port()));
                appendLog(T(QStringLiteral("%1 那边已经没有本机的配对记录了（只剩单边）"))
                              .arg(m_peerName));
                notify(T(QStringLiteral("对端不再认得本机 —— 在设备列表点「加密配对」重新配一次")),
                       true);
                return;
            }
            notify(T(QStringLiteral("连接失败：%1")).arg(PeerClient::errorFrom(reply, body)), true);
            return;
        }
        setForgotUsPeer(QString()); // 连上了，那"只剩单边"的判断就过期了
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
    // 服务端只该问「实际生效的访客模式」：零信任打开时，那个开关记着的值不算数。
    ctx.guest = m_config->guestModeActive();
    ctx.roots = m_config->serveRoots();
    // inbox 必须在 roots 里，否则手机拉不回自己刚推上来的东西
    QDir().mkpath(ctx.inbox);
    if (!ctx.roots.contains(ctx.inbox))
        ctx.roots.prepend(ctx.inbox);
    m_server->setContext(ctx);
    // 零信任模式一并关掉明文（草案 §8.1）：只认配对表却还听明文，等于把那道
    // 防线留了个洞 —— 明文连接连指纹都没有，无从判断对面是谁。
    m_server->setAllowLegacyPlaintext(m_config->allowLegacyPlaintext()
                                      && !m_config->zeroTrustMode());

    m_discovery->setAdvertisement(m_config->deviceName(),
                                  m_server->isListening() ? m_server->actualPort()
                                                          : quint16(m_config->serverPort()),
                                  m_config->discoverable());
}

bool AppController::serverRunning() const { return m_server->isListening(); }
int AppController::serverPort() const { return m_server->actualPort(); }

void AppController::setForgotUsPeer(const QString &hostPort)
{
    if (m_forgotUsPeer == hostPort)
        return;
    m_forgotUsPeer = hostPort;
    emit forgotUsPeerChanged();
}

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
