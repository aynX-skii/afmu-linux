#include "HttpServer.h"
#include "I18n.h"

#include "AuthRequests.h"
#include "Identity.h"
#include "PathSafety.h"
#include "PeerStore.h"
#include "Protocol.h"
#include "Tls.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMimeDatabase>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <functional>
#include <memory>

namespace {

const qint64 kChunkSize = 64 * 1024;
const qint64 kWriteHighWater = 1024 * 1024;
const int kIdleTimeoutMs = afmu::kSocketTimeoutSec * 1000; // PROTOCOL.md §2.3

QByteArray statusText(int code)
{
    switch (code) {
    case 200: return "OK";
    case 206: return "Partial Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    case 416: return "Range Not Satisfiable";
    case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    default: return "Internal Server Error";
    }
}

QByteArray httpDate(const QDateTime &dt)
{
    return QLocale::c()
        .toString(dt.toUTC(), QStringLiteral("ddd, dd MMM yyyy HH:mm:ss 'GMT'"))
        .toLatin1();
}

// ------------------------------------------------------------------ multipart
// docs/LINUX-CLIENT.md §5.3：自己写带缓冲的边界扫描器，逐段直接落盘
class MultipartParser
{
public:
    explicit MultipartParser(const QByteArray &boundary)
        : m_delim(QByteArray("\r\n--") + boundary)
    {
        // 首个分隔符没有前导 CRLF，先塞一个进去，之后就能统一用 m_delim 扫描
        m_buf = QByteArray("\r\n");
    }

    std::function<bool(const QString &filename)> onPartBegin;
    std::function<bool(const char *, qint64)> onData;
    std::function<bool()> onPartEnd;

    bool finished() const { return m_state == Done; }
    bool failed() const { return m_state == Fail; }
    QString error() const { return m_error; }

    void feed(const char *data, qint64 len)
    {
        if (m_state == Done || m_state == Fail)
            return;
        m_buf.append(data, int(len));
        run();
    }

private:
    enum St { Preamble, AfterBoundary, Headers, Body, Done, Fail };

    void fail(const QString &e)
    {
        m_error = e;
        m_state = Fail;
    }

    void run()
    {
        bool again = true;
        while (again && m_state != Done && m_state != Fail) {
            again = false;
            switch (m_state) {
            case Preamble: {
                const int idx = m_buf.indexOf(m_delim);
                if (idx < 0) {
                    trimTail();
                    return;
                }
                m_buf.remove(0, idx + m_delim.size());
                m_state = AfterBoundary;
                again = true;
                break;
            }
            case AfterBoundary: {
                if (m_buf.size() < 2)
                    return;
                if (m_buf.startsWith("--")) {
                    m_state = Done;
                    return;
                }
                if (!m_buf.startsWith("\r\n")) {
                    // 允许分隔符后有 LWS，宽松处理
                    const int nl = m_buf.indexOf("\r\n");
                    if (nl < 0) {
                        if (m_buf.size() > 512)
                            fail(QStringLiteral("malformed multipart boundary"));
                        return;
                    }
                    m_buf.remove(0, nl + 2);
                } else {
                    m_buf.remove(0, 2);
                }
                m_state = Headers;
                again = true;
                break;
            }
            case Headers: {
                const int idx = m_buf.indexOf("\r\n\r\n");
                if (idx < 0) {
                    if (m_buf.size() > 16 * 1024)
                        fail(QStringLiteral("multipart part headers too large"));
                    return;
                }
                const QByteArray head = m_buf.left(idx);
                m_buf.remove(0, idx + 4);
                m_currentName = parseFilename(head);
                if (onPartBegin && !onPartBegin(m_currentName)) {
                    fail(QStringLiteral("failed to open target file"));
                    return;
                }
                m_state = Body;
                again = true;
                break;
            }
            case Body: {
                const int idx = m_buf.indexOf(m_delim);
                if (idx < 0) {
                    // 缓冲区里找不到分隔符时，尾部要留够，防止分隔符跨块被切开
                    const int safe = m_buf.size() - (m_delim.size() - 1);
                    if (safe > 0) {
                        if (onData && !onData(m_buf.constData(), safe)) {
                            fail(QStringLiteral("write failed"));
                            return;
                        }
                        m_buf.remove(0, safe);
                    }
                    return;
                }
                if (idx > 0 && onData && !onData(m_buf.constData(), idx)) {
                    fail(QStringLiteral("write failed"));
                    return;
                }
                m_buf.remove(0, idx + m_delim.size());
                if (onPartEnd && !onPartEnd()) {
                    fail(QStringLiteral("failed to finalize file"));
                    return;
                }
                m_state = AfterBoundary;
                again = true;
                break;
            }
            default:
                return;
            }
        }
    }

    void trimTail()
    {
        const int keep = m_delim.size() - 1;
        if (m_buf.size() > keep)
            m_buf.remove(0, m_buf.size() - keep);
    }

    // Content-Disposition: form-data; name="f"; filename="a.jpg"; filename*=UTF-8''a.jpg
    static QString parseFilename(const QByteArray &head)
    {
        const QList<QByteArray> lines = head.split('\n');
        for (const QByteArray &raw : lines) {
            const QByteArray line = raw.trimmed();
            if (!line.toLower().startsWith("content-disposition:"))
                continue;
            const QByteArray value = line.mid(line.indexOf(':') + 1);
            // 优先 filename*=UTF-8''<pct-encoded>
            int star = value.toLower().indexOf("filename*=");
            if (star >= 0) {
                QByteArray v = value.mid(star + 10).trimmed();
                const int semi = v.indexOf(';');
                if (semi >= 0)
                    v = v.left(semi);
                const int quote = v.indexOf("''");
                if (quote >= 0)
                    v = v.mid(quote + 2);
                return QUrl::fromPercentEncoding(v.trimmed());
            }
            int p = value.toLower().indexOf("filename=");
            if (p >= 0) {
                QByteArray v = value.mid(p + 9).trimmed();
                if (v.startsWith('"')) {
                    const int end = v.indexOf('"', 1);
                    v = end > 0 ? v.mid(1, end - 1) : v.mid(1);
                } else {
                    const int semi = v.indexOf(';');
                    if (semi >= 0)
                        v = v.left(semi);
                }
                return QString::fromUtf8(v.trimmed());
            }
            return QString(); // 普通表单字段，没有 filename
        }
        return QString();
    }

    QByteArray m_delim;
    QByteArray m_buf;
    St m_state = Preamble;
    QString m_error;
    QString m_currentName;
};

// ------------------------------------------------------------------ chunked
class ChunkedDecoder
{
public:
    std::function<bool(const char *, qint64)> onData;

    bool finished() const { return m_state == Done; }
    bool failed() const { return m_state == Fail; }

    void feed(const char *data, qint64 len)
    {
        if (m_state == Done || m_state == Fail)
            return;
        m_buf.append(data, int(len));
        run();
    }

private:
    enum St { Size, Data, DataCrlf, Trailer, Done, Fail };

    void run()
    {
        while (m_state != Done && m_state != Fail) {
            switch (m_state) {
            case Size: {
                const int nl = m_buf.indexOf("\r\n");
                if (nl < 0) {
                    if (m_buf.size() > 4096)
                        m_state = Fail;
                    return;
                }
                QByteArray line = m_buf.left(nl);
                m_buf.remove(0, nl + 2);
                const int semi = line.indexOf(';');
                if (semi >= 0)
                    line = line.left(semi);
                bool ok = false;
                m_remaining = line.trimmed().toLongLong(&ok, 16);
                if (!ok || m_remaining < 0) {
                    m_state = Fail;
                    return;
                }
                m_state = m_remaining == 0 ? Trailer : Data;
                break;
            }
            case Data: {
                if (m_buf.isEmpty())
                    return;
                const qint64 n = qMin<qint64>(m_remaining, m_buf.size());
                if (onData && !onData(m_buf.constData(), n)) {
                    m_state = Fail;
                    return;
                }
                m_buf.remove(0, int(n));
                m_remaining -= n;
                if (m_remaining == 0)
                    m_state = DataCrlf;
                break;
            }
            case DataCrlf: {
                if (m_buf.size() < 2)
                    return;
                m_buf.remove(0, 2);
                m_state = Size;
                break;
            }
            case Trailer: {
                const int nl = m_buf.indexOf("\r\n");
                if (nl < 0)
                    return;
                if (nl == 0) {
                    m_buf.remove(0, 2);
                    m_state = Done;
                    return;
                }
                m_buf.remove(0, nl + 2);
                break;
            }
            default:
                return;
            }
        }
    }

    QByteArray m_buf;
    qint64 m_remaining = 0;
    St m_state = Size;
};

} // namespace

// ------------------------------------------------------------------ 单条连接

class HttpConnection : public QObject
{
    Q_OBJECT
public:
    HttpConnection(qintptr handle, HttpServer *server)
        : QObject(server)
        , m_server(server)
        , m_sock(new QSslSocket(this))
    {
        if (!m_sock->setSocketDescriptor(handle)) {
            deleteLater();
            return;
        }
        m_sock->setSocketOption(QAbstractSocket::LowDelayOption, 1);

        // v2 就绪时先看一眼首字节再决定这条连接是 TLS 还是 v1 明文（草案 §8.1 第 4 条）。
        // 一个端口同时服务新旧客户端，不用占两个。
        if (m_server->tlsReady()) {
            m_phase = Phase::Sniff;
            connect(m_sock, &QSslSocket::encrypted, this, &HttpConnection::onEncrypted);
            connect(m_sock, &QSslSocket::sslErrors, this, &HttpConnection::onSslErrors);
        }

        connect(m_sock, &QTcpSocket::readyRead, this, &HttpConnection::onReadyRead);
        connect(m_sock, &QTcpSocket::bytesWritten, this, &HttpConnection::onBytesWritten);
        connect(m_sock, &QTcpSocket::disconnected, this, &HttpConnection::onDisconnected);
        connect(m_sock, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
            // 用户中断下载是正常现象，不刷日志
            abortAll();
            m_sock->disconnectFromHost();
        });

        m_idle = new QTimer(this);
        m_idle->setSingleShot(true);
        m_idle->setInterval(kIdleTimeoutMs);
        connect(m_idle, &QTimer::timeout, this, [this] {
            abortAll();
            m_sock->disconnectFromHost();
        });
        m_idle->start();
    }

    ~HttpConnection() override { abortAll(); }

    // 服务端停止时立刻掐断：否则界面显示已停止，传输还在继续
    void shutdown()
    {
        abortAll();
        if (m_sock)
            m_sock->abort();
        deleteLater();
    }

private slots:
    void onDisconnected()
    {
        abortAll();
        deleteLater();
    }

    /**
     * 握手成功。这里是**唯一**做钉扎的地方，也是唯一能挡住陌生设备的地方 ——
     * `QueryPeer` 只负责把证书要过来，可不可信从来不归 TLS 栈判断。
     */
    void onEncrypted()
    {
        m_tls = true;
        m_idle->start();

        const QString fp = afmu::peerFingerprint(m_sock->peerCertificate());
        if (fp.isEmpty()) {
            // QueryPeer 下客户端不交证书握手照样成功。判空是挡住匿名连接的唯一一道，
            // 漏掉它等于把 mTLS 写成了单向 TLS。
            //
            // 只有访客模式例外，而且这个例外是**必须**的：浏览器永远不会出示客户端
            // 证书（除非用户手工装一张，那是 UX 灾难），所以「HTTPS 的访客模式」
            // 在这里放不放行，决定的就是它到底能不能用（草案 §9）。
            // 放行之后它也只是个访客：m_peerPaired 为假，往下要过密码那道门。
            if (!m_server->context().guest) {
                emit m_server->logMessage(
                    T(QStringLiteral("%1 的加密连接没有出示证书，已断开")).arg(peerHost()));
                m_sock->abort();
                deleteLater();
                return;
            }
            m_peerPaired = false;
            m_phase = Phase::Head;
            emit m_server->logMessage(
                T(QStringLiteral("%1 以访客身份接入（已加密，但对方身份未经验证）")).arg(peerHost()));
            if (m_sock->bytesAvailable() > 0)
                onReadyRead();
            return;
        }

        m_peerFp = fp;
        PeerStore *peers = m_server->peerStore();
        m_peerPaired = peers && peers->isPaired(fp);
        m_phase = Phase::Head;

        if (m_peerPaired) {
            // 一次成功的 v2 连接就是「这个对端会说 v2」的证据，从此不再允许它退回明文
            // （草案 §8.2 第 2 阶段）。这个标志只升不降，降只能由用户解除配对。
            peers->setPinned(fp, true);
            emit m_server->logMessage(T(QStringLiteral("%1 已通过加密连接接入（%2）"))
                                          .arg(peerHost(), peers->find(fp).name));
        } else {
            // 不认识，但**不断开**：配对握手就跑在这个状态里 —— TLS 已建立、
            // 双方都还没钉扎（草案 §4.2.4）。这条连接从此只能碰 /api/pair-v2，
            // 别的一律 403，包括 /api/info。门禁在 route() 里。
            emit m_server->logMessage(
                T(QStringLiteral("未配对设备接入，只允许配对，指纹 %1"))
                    .arg(afmu::Identity::group(fp)));
        }

        // 握手期间对端可能已经把请求一起发过来了，此时不会再有新的 readyRead
        if (m_sock->bytesAvailable() > 0)
            onReadyRead();
    }

    /**
     * 自签证书必然触发校验错误，这是设计的一部分：链校验被关掉了，可信与否由
     * `onEncrypted` 里比对指纹决定。
     *
     * `QueryPeer` 下这些错误不会中断握手，所以这里**不调用** `ignoreSslErrors()` ——
     * 调了反而会把「我们检查过了」这件事写成假的。留这个槽是为了两件事：
     * 把非预期的错误打出来，以及让后来的人看到这里是有意为之，别顺手改成忽略全部。
     */
    void onSslErrors(const QList<QSslError> &errors)
    {
        for (const QSslError &e : errors) {
            switch (e.error()) {
            case QSslError::SelfSignedCertificate:
            case QSslError::SelfSignedCertificateInChain:
            case QSslError::HostNameMismatch:
            case QSslError::CertificateUntrusted:
            case QSslError::UnableToGetLocalIssuerCertificate:
            case QSslError::UnableToVerifyFirstCertificate:
                break; // 意料之中，钉扎不看这些
            default:
                emit m_server->logMessage(
                    T(QStringLiteral("握手告警（%1）：%2")).arg(peerHost(), e.errorString()));
                break;
            }
        }
    }

    void onReadyRead()
    {
        m_idle->start();

        // 首字节分流（草案 §8.1 第 4 条）。注意用 peek —— 这个字节属于对端的
        // ClientHello，读掉的话 TLS 引擎就看不到完整的握手了。
        if (m_phase == Phase::Sniff) {
            char first = 0;
            if (m_sock->peek(&first, 1) < 1)
                return;
            if (quint8(first) == afmu::kTlsHelloFirstByte) {
                m_phase = Phase::Handshake;
                m_sock->setSslConfiguration(m_server->tlsConfiguration());
                m_sock->startServerEncryption();
                return;
            }
            if (!m_server->allowLegacyPlaintext()) {
                // 零信任模式下这个端口在效果上只听 TLS：不回 400，不回任何 HTTP 报文。
                // 回什么都等于告诉扫端口的人这里有个 HTTP 服务。
                emit m_server->logMessage(
                    T(QStringLiteral("%1 用明文连接，但已禁用明文，直接断开")).arg(peerHost()));
                m_sock->abort();
                deleteLater();
                return;
            }
            m_phase = Phase::Head;
        }
        if (m_phase == Phase::Handshake)
            return; // 握手的字节归 QSslSocket，轮不到这里

        for (;;) {
            if (m_phase == Phase::Head) {
                m_buf += m_sock->readAll();
                const int idx = m_buf.indexOf("\r\n\r\n");
                if (idx < 0) {
                    if (m_buf.size() > 64 * 1024) {
                        m_close = true;
                        m_method = "GET";
                        sendJson(431, errObj(QStringLiteral("header too large")));
                    }
                    return;
                }
                const QByteArray head = m_buf.left(idx);
                m_buf.remove(0, idx + 4);
                resetRequest();
                if (!parseHead(head))
                    return;
                route();
                if (m_phase == Phase::Head && !m_buf.isEmpty())
                    continue; // 流水线里还有下一个请求
            }
            if (m_phase == Phase::Body) {
                QByteArray d = m_buf;
                m_buf.clear();
                d += m_sock->readAll();
                if (!d.isEmpty())
                    feedBody(d);
            }
            return;
        }
    }

    void onBytesWritten(qint64)
    {
        m_idle->start();
        pumpFile();
    }

private:
    // Sniff / Handshake 只在 v2 就绪时出现：先看首字节决定这条连接是 TLS 还是
    // v1 明文，是 TLS 就把字节全交给 QSslSocket 直到 encrypted() 为止。
    enum class Phase { Sniff, Handshake, Head, Body, Sending, Closed };

    // ---------------------------------------------------------- 请求解析

    void resetRequest()
    {
        m_method.clear();
        m_target.clear();
        m_headers.clear();
        m_query.clear();
        m_close = false;
        m_bodyRemaining = -1;
        m_multipart.reset();
        m_chunked.reset();
        m_savedPaths.clear();
        m_uploadError.clear();
        m_useMultipart = false;
        m_skipPart = false;
        m_overwrite = false;
        m_uploadDir.clear();
        m_finalName.clear();
        m_partPath.clear();
        m_inDone = 0;
        m_lastProgressMs = 0;
    }

    bool parseHead(const QByteArray &head)
    {
        const QList<QByteArray> lines = head.split('\n');
        if (lines.isEmpty()) {
            sendJson(400, errObj(QStringLiteral("bad request")));
            return false;
        }
        const QList<QByteArray> parts = lines.first().trimmed().split(' ');
        if (parts.size() < 2) {
            sendJson(400, errObj(QStringLiteral("bad request line")));
            return false;
        }
        m_method = parts.at(0).toUpper();
        m_target = parts.at(1);
        const QByteArray version = parts.size() > 2 ? parts.at(2).trimmed() : QByteArray("HTTP/1.0");
        m_close = !version.endsWith("1.1");

        for (int i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines.at(i).trimmed();
            if (line.isEmpty())
                continue;
            const int colon = line.indexOf(':');
            if (colon <= 0)
                continue;
            m_headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
        }
        if (m_headers.value("connection").toLower().contains("close"))
            m_close = true;

        const QUrl u = QUrl::fromEncoded(m_target);
        m_path = u.path();
        m_query = QUrlQuery(u.query());
        return true;
    }

    QString param(const QString &key) const
    {
        // '+' 不被解释为空格；空格必须编码为 %20
        return m_query.queryItemValue(key, QUrl::FullyDecoded);
    }

    /** 请求方地址一律取 socket 的对端 IP，不信任任何参数里的 host。 */
    QString peerHost() const
    {
        QString host = m_sock->peerAddress().toString();
        if (host.startsWith(QLatin1String("::ffff:")))
            host = host.mid(7); // IPv4-mapped 前缀要剥掉，否则存进配置连不上
        return host;
    }

    bool hasParam(const QString &key) const { return m_query.hasQueryItem(key); }

    /**
     * 两种等价写法，按顺序取第一个非空值（PROTOCOL.md §2.2）。
     *
     * `?token=` 曾经是第三种，已经去掉：凭证进了 URL 就会落进代理日志、
     * 浏览器历史和 Referer。它当初存在的理由 —— 浏览器 `<a href>` 带不了自定义头 ——
     * 现在由下载券承担（§2.5）。
     */
    bool authorized() const
    {
        const ServerContext &ctx = m_server->context();
        QByteArray given = m_headers.value(QByteArray(afmu::kTokenHeader).toLower());
        if (given.isEmpty()) {
            const QByteArray auth = m_headers.value("authorization");
            if (auth.toLower().startsWith("bearer "))
                given = auth.mid(7).trimmed();
        }
        return afmu::tokenEquals(given, ctx.token.toUtf8());
    }

    /**
     * 下载是唯一还认券的接口 —— 它也是唯一会被浏览器直接导航到的接口。
     * 券绑定这一个路径（§2.5）。
     */
    bool authorizedForDownload() const
    {
        if (authorized())
            return true;
        const QString ticket = param(QStringLiteral("ticket"));
        if (ticket.isEmpty())
            return false;
        return afmu::verifyDownloadTicket(m_server->context().token, param(QStringLiteral("path")),
                                          ticket, QDateTime::currentMSecsSinceEpoch());
    }

    /**
     * 「这个指纹**现在**配过对吗」—— 每个请求问一次，而不是握手时问一次记一辈子。
     *
     * 指纹本身是握手验过的（对方用私钥签过 CertificateVerify），整条连接终生不变，
     * 所以重新查表不会引入任何新的信任来源：变的只有表里的答案。而它确实会变，
     * 两个方向都会：
     *
     *  · 配对成功那一刻 —— 配对握手跑在一条「还没配对」的连接上，用户点允许之后
     *    表里就有对方了。不重新查的话，对端紧接着的第一个真正请求还是被 403
     *    「not paired」挡住 —— 配对明明成功了，第一下连接却被拒。HTTP 连接复用
     *    是常态（Qt 的 QNAM 默认就这么干），所以这不是罕见路径。
     *  · 解除配对那一刻 —— 用户在界面上把某台设备删掉，它**正在进行**的那条连接
     *    应该当场失效，而不是等它自己断开。
     */
    void refreshPairedState()
    {
        if (!m_tls || m_peerFp.isEmpty())
            return; // 明文，或者加密访客（没有证书 → 没有身份可查）
        PeerStore *peers = m_server->peerStore();
        const bool nowPaired = peers && peers->isPaired(m_peerFp);
        if (nowPaired == m_peerPaired)
            return;
        m_peerPaired = nowPaired;
        if (nowPaired) {
            // 和握手时那条路径一样：一次成功的 v2 连接就是「它会说 v2」的证据。
            peers->setPinned(m_peerFp, true);
            emit m_server->logMessage(T(QStringLiteral("%1 刚刚完成配对，本连接已升级为已配对"))
                                          .arg(peerHost()));
        } else {
            emit m_server->logMessage(
                T(QStringLiteral("%1 的配对已被解除，本连接从此只能走配对流程")).arg(peerHost()));
        }
    }

    // ---------------------------------------------------------- 路由

    void route()
    {
        const ServerContext &ctx = m_server->context();
        refreshPairedState();

        // DNS rebinding / 跨站请求防护（PROTOCOL.md §2.4）。排在所有路由之前 ——
        // 包括 GET /，因为浏览器界面正是这类攻击的入口。
        const QString hostHeader = QString::fromLatin1(m_headers.value("host"));
        if (!afmu::isLocalHostHeader(hostHeader)) {
            emit m_server->logMessage(
                T(QStringLiteral("拒绝了 Host 为「%1」的请求（疑似 DNS rebinding）")).arg(hostHeader));
            sendJson(403, errObj(QStringLiteral("host not allowed")));
            return;
        }
        if (!afmu::originMatchesHost(QString::fromLatin1(m_headers.value("origin")), hostHeader)) {
            emit m_server->logMessage(
                T(QStringLiteral("拒绝了来自「%1」的跨站请求"))
                    .arg(QString::fromLatin1(m_headers.value("origin"))));
            sendJson(403, errObj(QStringLiteral("cross-origin request refused")));
            return;
        }

        // 未钉扎的 v2 连接只有一条路可走（草案 §4.2.4）。这个判断必须排在所有
        // 路由之前，包括根路径和 /api/info —— 「除了配对什么都不能碰」才是它的意思。
        //
        // 唯一的例外是访客模式：那时候它可以往下走，去过 v1 那道密码认证。
        // 这不是把门开大了 —— 明文连接本来就走那道门，访客模式只是让它**也能加密**。
        // 真要收紧就关掉访客模式，那时两条路一起断。
        if (m_tls && !m_peerPaired) {
            if (m_path == QLatin1String("/api/pair-v2"))
                return handlePairV2();
            if (!ctx.guest) {
                sendJson(403, errObj(QStringLiteral("not paired; only /api/pair-v2 is available")));
                return;
            }
        }

        // 访客模式关掉 = 密码认证这条路整个不存在（草案 §7/§9）。
        //
        // /api/authorize 也一起挡住：它的作用是把 token 发出去，而这时候 token
        // 什么都打不开。发一个用不了的凭证，比明说「这条路关了」糟得多 ——
        // 用户会拿着它反复试，然后去查网络、查防火墙。
        if (!ctx.guest && !m_peerPaired
            && (m_path == QLatin1String("/api/authorize") || m_path == QLatin1String("/api/pair"))) {
            sendJson(403,
                     errObj(QStringLiteral("guest mode is off; pair over an encrypted connection")));
            return;
        }

        if (m_path == QLatin1String("/") || m_path.isEmpty()) {
            sendText(200,
                     T(QStringLiteral("AFMU Linux 服务端已就绪。")) + QLatin1Char('\n')
                         + T(QStringLiteral("本机没有内置网页界面，请用 FileBridge App 或 afmu 客户端连接。"))
                         + QLatin1Char('\n')
                         + T(QStringLiteral("设备名: %1")).arg(ctx.deviceName) + QLatin1Char('\n')
                         + T(QStringLiteral("协议版本: %2")).arg(afmu::kProtocolVersion)
                         + QLatin1Char('\n'));
            return;
        }

        if (!m_path.startsWith(QLatin1String("/api/"))) {
            sendJson(404, errObj(QStringLiteral("not found")));
            return;
        }
        // 故意排在 token 检查前面：这个接口存在的意义就是「对端还没有 token」。
        // 防滥用的那一套全在 AuthRequests 里（PROTOCOL.md §3.8）。
        if (m_path == QLatin1String("/api/authorize"))
            return handleAuthorize();

        // 猜错 token 要付出代价（PROTOCOL.md §2.2）。退避期内连比对都不做 ——
        // 比对本身是常数时间的，但「有没有走到比对」是可观测的，直接挡在门外最干净。
        const QString peer = peerHost();
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (const int wait = m_server->throttle().retryAfterSec(peer, now); wait > 0) {
            sendJson(429,
                     errObj(QStringLiteral("too many failed attempts, retry in %1s").arg(wait)),
                     {{"Retry-After", QByteArray::number(wait)}});
            return;
        }
        // 下载额外认券：它是唯一会被浏览器直接导航到的接口，那里挂不上头（§2.5）
        //
        // v2 连接跳过这一整套：**握手成功 + 指纹在配对表里 = 认证已经完成**
        // （草案 §5）。对端持有的是钉扎过的私钥，比"每个请求带个长期共享密钥"强一个
        // 量级，再要一次 token 只是把 v1 的弱点原样搬过来。
        //
        // 反过来，访客模式关掉时**任何** token 都不算数：密码认证就是 v1 的访问方式，
        // 关掉它就是关掉它，不留「密码对了还是放行」的后门。
        const bool ok = (m_tls && m_peerPaired) ? true
            : !ctx.guest                        ? false
            : (m_path == QLatin1String("/api/download") ? authorizedForDownload() : authorized());
        if (!ok && !ctx.guest) {
            // 和「token 不对」分开报：这不是猜错密码，是这条路根本不通，
            // 再试一百次也一样。退避在这里没有意义，说清楚才有。
            sendJson(403,
                     errObj(QStringLiteral("guest mode is off; pair over an encrypted connection")));
            return;
        }
        if (!ok) {
            const int wait = m_server->throttle().noteFailure(peer, now);
            if (wait > 0) {
                emit m_server->logMessage(
                    T(QStringLiteral("%1 连续猜错 token，暂停响应 %2 秒")).arg(peer).arg(wait));
                sendJson(429,
                         errObj(QStringLiteral("too many failed attempts, retry in %1s").arg(wait)),
                         {{"Retry-After", QByteArray::number(wait)}});
                return;
            }
            sendJson(401, errObj(QStringLiteral("invalid or missing token")));
            return;
        }
        m_server->throttle().noteSuccess(peer);

        // 也在这里路由一次，和上面未配对分支里的那次并存，是有意的：用户点「允许」
        // 那一刻对端就**变成已配对**，它随后的轮询走的是已配对连接。只在上面路由的话，
        // 轮询会撞 404，发起方于是把一次成功的配对报成超时 —— 而会不会撞上，取决于
        // 客户端有没有复用连接，这不是任何一端该依赖的东西。
        if (m_path == QLatin1String("/api/pair-v2"))
            return handlePairV2();
        if (m_path == QLatin1String("/api/info"))
            return handleInfo();
        if (m_path == QLatin1String("/api/list"))
            return handleList();
        if (m_path == QLatin1String("/api/download"))
            return handleDownload();
        if (m_path == QLatin1String("/api/upload"))
            return handleUpload();
        if (m_path == QLatin1String("/api/mkdir"))
            return handleMkdir();
        if (m_path == QLatin1String("/api/delete"))
            return handleDelete();
        if (m_path == QLatin1String("/api/pair"))
            return handlePair();
        if (m_path == QLatin1String("/api/ticket"))
            return handleTicket();

        sendJson(404, errObj(QStringLiteral("unknown endpoint")));
    }

    /**
     * v2 配对握手（PROTOCOL.md v2 §4.2.3）。未钉扎的 TLS 连接只能碰这一个接口。
     *
     * 三步：`step=commit` 登记并拿到 `nb`，`step=reveal` 揭示 `na` 并得到 SAS，
     * `GET ?session=` 轮询用户的决定。
     *
     * **参数走 query 而不是 JSON body**，和 v1 的每一个接口保持一致。这不只是省事：
     * 这个接口对**未认证**的对端开放，让它够得到 body 解析那一整套（Content-Length、
     * chunked、multipart）等于凭空多出一大片攻击面。query 走的是已经被一致性套件
     * 反复捶过的那条路径。草案原文写的是 JSON body，已按此修正。
     */
    void handlePairV2()
    {
        // 明文下配对没有意义：没有证书可授权，整个交换还会被旁听全看去。下面的路由表
        // 从明文路径也能走到这里（必须能，否则配对中途变成已配对的对端就轮询不到），
        // 所以在这里明说，而不是让它在更深处以某种难懂的方式失败。
        if (!m_tls) {
            sendJson(400, errObj(QStringLiteral("pairing requires an encrypted connection")));
            return;
        }
        AuthRequests *auth = m_server->authRequests();
        if (!auth) {
            sendJson(404, errObj(QStringLiteral("unknown endpoint")));
            return;
        }
        const afmu::Identity *id = m_server->identity();
        if (!id) {
            sendJson(500, errObj(QStringLiteral("no local identity")));
            return;
        }

        if (m_method == "GET") {
            const AuthRequests::Request r = auth->lookup(param(QStringLiteral("session")));
            if (r.isNull() || !r.isPairing()) {
                // 客户端要把 404 当 expired 处理，别当网络错误一直重试
                sendJson(404, errObj(QStringLiteral("unknown or expired session")));
                return;
            }
            QJsonObject o;
            o.insert(QStringLiteral("ok"), true);
            switch (r.status) {
            case AuthRequests::Status::Pending:
                o.insert(QStringLiteral("status"), QStringLiteral("pending"));
                break;
            case AuthRequests::Status::Denied:
                o.insert(QStringLiteral("status"), QStringLiteral("denied"));
                break;
            case AuthRequests::Status::Expired:
                o.insert(QStringLiteral("status"), QStringLiteral("expired"));
                break;
            case AuthRequests::Status::Granted:
                o.insert(QStringLiteral("status"), QStringLiteral("granted"));
                o.insert(QStringLiteral("name"), m_server->context().deviceName);
                o.insert(QStringLiteral("os"), QStringLiteral("linux"));
                o.insert(QStringLiteral("port"), int(m_server->actualPort()));
                // 响应里**没有 token**：v2 的身份就是那对密钥，没有东西需要交出去。
                break;
            }
            sendJson(200, o);
            return;
        }

        if (m_method != "POST" && m_method != "PUT") {
            sendJson(405, errObj(QStringLiteral("method not allowed")));
            return;
        }

        const QString step = param(QStringLiteral("step"));

        if (step == QLatin1String("commit")) {
            const QByteArray commit =
                afmu::hexDecodeStrict(param(QStringLiteral("commit")));
            if (commit.size() != 32) {
                sendJson(400, errObj(QStringLiteral("commit must be 32 bytes of hex")));
                return;
            }
            // 对端的指纹取自握手，**不取请求里自报的**：自报的东西在这一层
            // 一个字都不能信。
            // 端口取对端自报的（见客户端那边的说明）。自报的东西只用来当**重连提示**，
            // 不参与任何判定 —— 报错了的后果是将来连不上，失败方向是安全的。
            const int peerPort =
                param(QStringLiteral("port")).toInt();
            const AuthRequests::Request r =
                auth->createPairing(param(QStringLiteral("name")), param(QStringLiteral("os")),
                                    peerHost(), m_peerFp, commit,
                                    (peerPort > 0 && peerPort <= 65535) ? peerPort : 0);
            if (r.isNull()) {
                const int wait = auth->retryAfterSec(peerHost());
                QList<QPair<QByteArray, QByteArray>> extra;
                if (wait > 0)
                    extra.append({"Retry-After", QByteArray::number(wait)});
                sendJson(429, errObj(QStringLiteral("another pairing is pending, or this address "
                                                    "was refused recently")),
                         extra);
                return;
            }
            emit m_server->logMessage(T(QStringLiteral("%1（%2）请求配对")).arg(r.name, r.host));

            QJsonObject o;
            o.insert(QStringLiteral("ok"), true);
            o.insert(QStringLiteral("session"), r.id);
            o.insert(QStringLiteral("nb"), QString::fromLatin1(r.nonceB.toHex()));
            o.insert(QStringLiteral("expires"), afmu::kAuthTimeoutSec);
            sendJson(200, o);
            return;
        }

        if (step == QLatin1String("reveal")) {
            const QByteArray na = afmu::hexDecodeStrict(param(QStringLiteral("na")));
            const QString sas = auth->revealPairing(param(QStringLiteral("session")), na,
                                                    id->fingerprint());
            if (sas.isEmpty()) {
                // commit 对不上、session 不在、长度不对 —— 一律作废，不区分原因：
                // 区分了就等于告诉对方哪一步猜错了。
                sendJson(400, errObj(QStringLiteral("commit does not match, or unknown session")));
                return;
            }
            QJsonObject o;
            o.insert(QStringLiteral("ok"), true);
            // 回带 sas 只是让发起方**自检**两端算的一致（防实现 bug）。
            // 发起方必须显示自己算的那个 —— 显示这个等于让中间人回一个你期望的串。
            o.insert(QStringLiteral("sas"), sas);
            o.insert(QStringLiteral("expires"), afmu::kAuthTimeoutSec);
            sendJson(200, o);
            return;
        }

        sendJson(400, errObj(QStringLiteral("unknown step")));
    }

    /**
     * 免鉴权的敲门接口（PROTOCOL.md §3.8）：`POST` 登记一个请求，
     * `GET ?request=<id>` 轮询结果。
     *
     * token 只有在用户点了「允许」之后才交出去，而且只交给拿着 id 的那一个人 ——
     * id 是登记时单独发给请求方的，别人问不到。
     */
    void handleAuthorize()
    {
        AuthRequests *auth = m_server->authRequests();
        if (!auth) {
            // 没挂登记处 = 本机不实现这个接口，对端会当成「不支持」回退到手抄 token
            sendJson(404, errObj(QStringLiteral("unknown endpoint")));
            return;
        }

        if (m_method == "POST" || m_method == "PUT") {
            if (!auth->enabled()) {
                sendJson(403, errObj(QStringLiteral("authorization requests are disabled")));
                return;
            }
            const int port = param(QStringLiteral("port")).toInt();
            const AuthRequests::Request r =
                auth->create(param(QStringLiteral("name")), param(QStringLiteral("os")), peerHost(),
                             port > 0 && port <= 65535 ? port : 0, param(QStringLiteral("code")));
            if (r.isNull()) {
                // 冷却导致的才有 Retry-After；「已有一个请求在等用户点」是另一回事，
                // 那个要等多久取决于用户，报不出准确秒数就不要瞎报
                const int wait = auth->retryAfterSec(peerHost());
                QList<QPair<QByteArray, QByteArray>> extra;
                if (wait > 0)
                    extra.append({"Retry-After", QByteArray::number(wait)});
                sendJson(429,
                         errObj(QStringLiteral(
                             "another request is pending, or this address was refused recently")),
                         extra);
                return;
            }
            emit m_server->logMessage(
                T(QStringLiteral("%1（%2）请求连接，确认码 %3")).arg(r.name, r.host, r.code));

            QJsonObject o;
            o.insert(QStringLiteral("ok"), true);
            o.insert(QStringLiteral("request"), r.id);
            o.insert(QStringLiteral("expires"), afmu::kAuthTimeoutSec);
            sendJson(200, o);
            return;
        }

        if (m_method == "GET") {
            const AuthRequests::Request r = auth->lookup(param(QStringLiteral("request")));
            if (r.isNull()) {
                // 客户端要把 404 当 expired 处理，不能当成网络错误一直重试
                sendJson(404, errObj(QStringLiteral("unknown or expired request")));
                return;
            }
            QJsonObject o;
            o.insert(QStringLiteral("ok"), true);
            switch (r.status) {
            case AuthRequests::Status::Pending:
                o.insert(QStringLiteral("status"), QStringLiteral("pending"));
                break;
            case AuthRequests::Status::Denied:
                o.insert(QStringLiteral("status"), QStringLiteral("denied"));
                break;
            case AuthRequests::Status::Expired:
                o.insert(QStringLiteral("status"), QStringLiteral("expired"));
                break;
            case AuthRequests::Status::Granted: {
                const ServerContext &ctx = m_server->context();
                o.insert(QStringLiteral("status"), QStringLiteral("granted"));
                o.insert(QStringLiteral("token"), ctx.token);
                o.insert(QStringLiteral("name"), ctx.deviceName);
                o.insert(QStringLiteral("port"), int(m_server->actualPort()));
                break;
            }
            }
            sendJson(200, o);
            return;
        }

        sendJson(405, errObj(QStringLiteral("method not allowed")));
    }

    /**
     * 对端扫了本机的二维码，已经拿到本机 token，现在把它自己的 token 和端口回填过来，
     * 于是两个方向一次配对全部打通（PROTOCOL.md §3.9）。
     *
     * 地址取 socket 的对端 IP，不信 body 里的 host —— 免得被拿来指向第三方。
     */
    void handlePair()
    {
        if (m_method != "POST" && m_method != "PUT") {
            sendJson(405, errObj(QStringLiteral("method not allowed")));
            return;
        }
        const QString token = param(QStringLiteral("token"));
        if (token.isEmpty()) {
            sendJson(400, errObj(QStringLiteral("need token")));
            return;
        }
        const QString host = peerHost();
        const int port = param(QStringLiteral("port")).toInt();
        emit m_server->pairRequested(host, port > 0 ? port : int(afmu::kDefaultHttpPort), token,
                                     param(QStringLiteral("name")), param(QStringLiteral("os")));

        const ServerContext &ctx = m_server->context();
        QJsonObject o;
        o.insert(QStringLiteral("ok"), true);
        o.insert(QStringLiteral("name"), ctx.deviceName);
        o.insert(QStringLiteral("os"), QStringLiteral("linux"));
        o.insert(QStringLiteral("protocol"), afmu::kProtocolVersion);
        sendJson(200, o);
    }

    void handleInfo()
    {
        const ServerContext &ctx = m_server->context();
        QJsonObject o;
        o.insert(QStringLiteral("ok"), true);
        o.insert(QStringLiteral("name"), ctx.deviceName);
        o.insert(QStringLiteral("os"), QStringLiteral("linux"));
        o.insert(QStringLiteral("protocol"), afmu::kProtocolVersion);
        o.insert(QStringLiteral("writable"), ctx.writable);
        o.insert(QStringLiteral("inbox"), ctx.inbox);
        QJsonArray roots;
        for (const QString &r : ctx.roots) {
            QJsonObject e;
            const QString display = r == QDir::homePath() ? T(QStringLiteral("主目录")) : QFileInfo(r).fileName();
            e.insert(QStringLiteral("name"), display.isEmpty() ? r : display);
            e.insert(QStringLiteral("path"), r);
            roots.append(e);
        }
        o.insert(QStringLiteral("roots"), roots);
        sendJson(200, o);
    }

    void handleList()
    {
        const ServerContext &ctx = m_server->context();
        const QString raw = param(QStringLiteral("path"));

        // 省略或传 "/" 时返回根目录列表
        if (raw.isEmpty() || raw == QLatin1String("/")) {
            QJsonArray entries;
            for (const QString &r : ctx.roots) {
                QFileInfo fi(r);
                if (!fi.isDir())
                    continue;
                QJsonObject e;
                const QString display = r == QDir::homePath() ? T(QStringLiteral("主目录")) : fi.fileName();
                e.insert(QStringLiteral("name"), display.isEmpty() ? r : display);
                e.insert(QStringLiteral("path"), fi.absoluteFilePath());
                e.insert(QStringLiteral("dir"), true);
                e.insert(QStringLiteral("size"), 0);
                e.insert(QStringLiteral("mtime"), double(fi.lastModified().toSecsSinceEpoch()));
                entries.append(e);
            }
            QJsonObject o;
            o.insert(QStringLiteral("ok"), true);
            o.insert(QStringLiteral("path"), QStringLiteral("/"));
            o.insert(QStringLiteral("parent"), QJsonValue::Null);
            o.insert(QStringLiteral("entries"), entries);
            sendJson(200, o);
            return;
        }

        const QString dirPath = afmu::resolveUnderRoots(raw, ctx.roots);
        QFileInfo dfi(dirPath);
        if (dirPath.isEmpty() || !dfi.isDir()) {
            sendJson(404, errObj(QStringLiteral("no such directory")));
            return;
        }

        QDir dir(dirPath);
        const auto infos = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
        QList<QFileInfo> sorted = infos;
        // 目录在前，然后按文件名小写升序
        std::sort(sorted.begin(), sorted.end(), [](const QFileInfo &a, const QFileInfo &b) {
            if (a.isDir() != b.isDir())
                return a.isDir();
            return a.fileName().toLower() < b.fileName().toLower();
        });

        QJsonArray entries;
        for (const QFileInfo &fi : sorted) {
            QJsonObject e;
            e.insert(QStringLiteral("name"), fi.fileName());
            e.insert(QStringLiteral("path"), fi.absoluteFilePath());
            e.insert(QStringLiteral("dir"), fi.isDir());
            e.insert(QStringLiteral("size"), fi.isDir() ? 0 : double(fi.size()));
            e.insert(QStringLiteral("mtime"), double(fi.lastModified().toSecsSinceEpoch()));
            entries.append(e);
        }

        QJsonObject o;
        o.insert(QStringLiteral("ok"), true);
        o.insert(QStringLiteral("path"), dirPath);
        // 根列表时为 null；父目录越界时为 "/"（回到根列表）
        bool isRoot = false;
        for (const QString &r : ctx.roots) {
            if (QFileInfo(r).canonicalFilePath() == dirPath) {
                isRoot = true;
                break;
            }
        }
        if (isRoot) {
            o.insert(QStringLiteral("parent"), QStringLiteral("/"));
        } else {
            const QString parent = dfi.absolutePath();
            o.insert(QStringLiteral("parent"),
                     afmu::resolveUnderRoots(parent, ctx.roots).isEmpty() ? QStringLiteral("/") : parent);
        }
        o.insert(QStringLiteral("entries"), entries);
        sendJson(200, o);
    }

    void handleDownload()
    {
        if (m_method != "GET" && m_method != "HEAD") {
            sendJson(405, errObj(QStringLiteral("method not allowed")));
            return;
        }
        const ServerContext &ctx = m_server->context();
        const QString path = afmu::resolveUnderRoots(param(QStringLiteral("path")), ctx.roots);
        QFileInfo fi(path);
        if (path.isEmpty() || !fi.isFile() || !fi.isReadable()) {
            sendJson(404, errObj(QStringLiteral("no such file")));
            return;
        }

        auto *f = new QFile(path);
        if (!f->open(QIODevice::ReadOnly)) {
            delete f;
            sendJson(404, errObj(QStringLiteral("no such file")));
            return;
        }

        const qint64 total = f->size();
        qint64 start = 0;
        qint64 end = total - 1;
        bool partial = false;

        const QByteArray range = m_headers.value("range");
        if (range.startsWith("bytes=")) {
            QByteArray spec = range.mid(6).trimmed();
            const int comma = spec.indexOf(','); // 只支持单区间，多区间只取第一段
            if (comma >= 0)
                spec = spec.left(comma);
            const int dash = spec.indexOf('-');
            if (dash < 0) {
                sendRangeError(f, total);
                return;
            }
            const QByteArray a = spec.left(dash).trimmed();
            const QByteArray b = spec.mid(dash + 1).trimmed();
            bool ok = true;
            if (a.isEmpty()) {
                // bytes=-<suffix>
                const qint64 suffix = b.toLongLong(&ok);
                if (!ok || suffix <= 0) {
                    sendRangeError(f, total);
                    return;
                }
                start = qMax<qint64>(0, total - suffix);
                end = total - 1;
            } else {
                start = a.toLongLong(&ok);
                if (!ok) {
                    sendRangeError(f, total);
                    return;
                }
                if (!b.isEmpty()) {
                    end = b.toLongLong(&ok);
                    if (!ok) {
                        sendRangeError(f, total);
                        return;
                    }
                }
                end = qMin(end, total - 1);
            }
            if (start > end || start >= total) {
                sendRangeError(f, total);
                return;
            }
            partial = true;
        }

        const qint64 length = end - start + 1;
        f->seek(start);

        QList<QPair<QByteArray, QByteArray>> extra;
        const QString mime = QMimeDatabase().mimeTypeForFile(fi).name();
        extra.append({"Content-Type", mime.isEmpty() ? QByteArray("application/octet-stream")
                                                     : mime.toLatin1()});
        extra.append(QPair<QByteArray, QByteArray>("Accept-Ranges", "bytes"));
        extra.append({"Last-Modified", httpDate(fi.lastModified())});
        const QByteArray nameUtf8 = fi.fileName().toUtf8();
        QByteArray asciiName;
        for (char c : nameUtf8)
            asciiName.append((c >= 0x20 && c < 0x7f && c != '"' && c != '\\') ? c : '_');
        extra.append({"Content-Disposition",
                      "attachment; filename=\"" + asciiName + "\"; filename*=UTF-8''"
                          + QUrl::toPercentEncoding(fi.fileName())});
        if (partial)
            extra.append({"Content-Range", "bytes " + QByteArray::number(start) + "-"
                                               + QByteArray::number(end) + "/"
                                               + QByteArray::number(total)});

        sendHeaders(partial ? 206 : 200, length, extra);

        if (m_method == "HEAD") {
            delete f;
            finishResponse();
            return;
        }

        m_outFile = f;
        m_outRemaining = length;
        m_phase = Phase::Sending;
        m_outId = m_server->nextTransferId();
        m_outDone = 0;
        emit m_server->transferStarted(m_outId, fi.fileName(), length, false);
        pumpFile();
    }

    void sendRangeError(QFile *f, qint64 total)
    {
        delete f;
        sendHeaders(416, 0, {{"Content-Range", "bytes */" + QByteArray::number(total)}});
        finishResponse();
    }

    void handleUpload()
    {
        const ServerContext &ctx = m_server->context();
        if (m_method != "POST" && m_method != "PUT") {
            sendJson(405, errObj(QStringLiteral("method not allowed")));
            return;
        }
        if (!ctx.writable) {
            sendJson(403, errObj(QStringLiteral("read-only mode")));
            return;
        }
        m_close = true; // 带请求体的请求处理完发 Connection: close

        // 目标目录：省略、越界或不可写时落到 inbox
        QString dir = ctx.inbox;
        const QString wanted = param(QStringLiteral("dir"));
        if (!wanted.isEmpty() && wanted != QLatin1String("/")) {
            const QString resolved = afmu::resolveUnderRoots(wanted, ctx.roots);
            QFileInfo dfi(resolved);
            if (!resolved.isEmpty() && dfi.isDir() && dfi.isWritable())
                dir = resolved;
        }
        if (!QDir().mkpath(dir)) {
            sendJson(500, errObj(QStringLiteral("cannot create target directory")));
            return;
        }
        m_uploadDir = dir;
        m_overwrite = param(QStringLiteral("overwrite")) == QLatin1String("1");

        const QByteArray ctype = m_headers.value("content-type");
        const QByteArray lenHeader = m_headers.value("content-length");
        const bool chunked = m_headers.value("transfer-encoding").toLower().contains("chunked");
        m_bodyRemaining = lenHeader.isEmpty() ? -1 : lenHeader.toLongLong();

        if (ctype.toLower().startsWith("multipart/form-data")) {
            if (chunked) {
                // 分块编码的 multipart 得先解 chunk 再喂边界扫描器，本端没串这一层。
                // 直接拒绝，好过把 chunk 头当成文件内容写进文件里
                sendJson(400, errObj(QStringLiteral("chunked multipart is not supported")));
                return;
            }
            const int bpos = ctype.toLower().indexOf("boundary=");
            if (bpos < 0) {
                sendJson(400, errObj(QStringLiteral("missing multipart boundary")));
                return;
            }
            QByteArray boundary = ctype.mid(bpos + 9).trimmed();
            if (boundary.startsWith('"') && boundary.endsWith('"'))
                boundary = boundary.mid(1, boundary.size() - 2);
            const int semi = boundary.indexOf(';');
            if (semi >= 0)
                boundary = boundary.left(semi);

            m_multipart.reset(new MultipartParser(boundary));
            m_multipart->onPartBegin = [this](const QString &filename) {
                if (filename.isEmpty()) {
                    m_skipPart = true; // 普通表单字段丢弃
                    return true;
                }
                m_skipPart = false;
                return beginFile(filename, -1);
            };
            m_multipart->onData = [this](const char *d, qint64 n) {
                if (m_skipPart)
                    return true;
                return writeFile(d, n);
            };
            m_multipart->onPartEnd = [this] {
                if (m_skipPart)
                    return true;
                return endFile();
            };
            m_useMultipart = true;
        } else {
            if (m_bodyRemaining < 0 && !chunked) {
                sendJson(500, errObj(QStringLiteral("missing Content-Length")));
                return;
            }
            const QString name = param(QStringLiteral("name"));
            if (!beginFile(name.isEmpty() ? QStringLiteral("unnamed") : name, m_bodyRemaining)) {
                sendJson(500, errObj(m_uploadError.isEmpty() ? QStringLiteral("cannot open target")
                                                             : m_uploadError));
                return;
            }
            if (chunked) {
                m_chunked.reset(new ChunkedDecoder);
                m_chunked->onData = [this](const char *d, qint64 n) { return writeFile(d, n); };
            }
            m_useMultipart = false;
        }

        m_phase = Phase::Body;
        if (!m_useMultipart && !m_chunked && m_bodyRemaining == 0)
            finishUpload();
    }

    /**
     * 为一个路径签一张下载券（PROTOCOL.md §2.5）。走的是头鉴权，
     * 所以页面本来就得先拿到 token；券只是让 `<a href>` 不必带着它。
     *
     * 先解析路径再签：给一个越界的路径签券，等于向对方确认了它存在。
     */
    void handleTicket()
    {
        const ServerContext &ctx = m_server->context();
        const QString raw = param(QStringLiteral("path"));
        const QString resolved = afmu::resolveUnderRoots(raw, ctx.roots);
        if (resolved.isEmpty() || !QFileInfo(resolved).isFile()) {
            sendJson(404, errObj(QStringLiteral("no readable file")));
            return;
        }
        QJsonObject o;
        o.insert(QStringLiteral("ok"), true);
        o.insert(QStringLiteral("ticket"),
                 afmu::issueDownloadTicket(ctx.token, raw, QDateTime::currentMSecsSinceEpoch()));
        o.insert(QStringLiteral("expires"), afmu::kTicketTtlSec);
        sendJson(200, o);
    }

    void handleMkdir()
    {
        const ServerContext &ctx = m_server->context();
        if (m_method != "POST") {
            sendJson(405, errObj(QStringLiteral("method not allowed")));
            return;
        }
        if (!ctx.writable) {
            sendJson(403, errObj(QStringLiteral("read-only mode")));
            return;
        }
        // 参数缺失是客户端错误（400），路径解析不出来才是 404。
        // 两者顺序不能颠倒：缺 path 时 resolveUnderRoots("") 也返回空，
        // 先解析就会把「没传参数」误报成「目录不存在」，和 Android 端不一致。
        const QString rawPath = param(QStringLiteral("path"));
        const QString rawName = param(QStringLiteral("name"));
        if (rawPath.trimmed().isEmpty() || rawName.trimmed().isEmpty()) {
            // name 是必填的；空值以前会静默建出一个 "unnamed" 目录
            sendJson(400, errObj(QStringLiteral("need path and name")));
            return;
        }
        const QString parent = afmu::resolveUnderRoots(rawPath, ctx.roots);
        const QString name = afmu::sanitizeFileName(rawName);
        if (parent.isEmpty() || !QFileInfo(parent).isDir()) {
            sendJson(404, errObj(QStringLiteral("no such directory")));
            return;
        }
        const QString target = QDir(parent).filePath(name);
        if (!QDir().mkpath(target)) {
            sendJson(500, errObj(QStringLiteral("mkdir failed")));
            return;
        }
        QJsonObject o;
        o.insert(QStringLiteral("ok"), true);
        o.insert(QStringLiteral("path"), target);
        sendJson(200, o);
    }

    void handleDelete()
    {
        const ServerContext &ctx = m_server->context();
        if (m_method != "POST") {
            sendJson(405, errObj(QStringLiteral("method not allowed")));
            return;
        }
        if (!ctx.writable) {
            sendJson(403, errObj(QStringLiteral("read-only mode")));
            return;
        }
        const QString path = afmu::resolveUnderRoots(param(QStringLiteral("path")), ctx.roots);
        QFileInfo fi(path);
        if (path.isEmpty() || !fi.exists()) {
            sendJson(404, errObj(QStringLiteral("no such path")));
            return;
        }
        // root 本身不允许删
        for (const QString &r : ctx.roots) {
            if (QFileInfo(r).canonicalFilePath() == path) {
                sendJson(403, errObj(QStringLiteral("refusing to delete a shared root")));
                return;
            }
        }
        bool ok = false;
        if (fi.isDir()) {
            QDir d(path);
            const bool empty = d.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty();
            if (!empty && param(QStringLiteral("recursive")) != QLatin1String("1")) {
                sendJson(400, errObj(QStringLiteral("directory not empty, pass recursive=1")));
                return;
            }
            ok = d.removeRecursively();
        } else {
            ok = QFile::remove(path);
        }
        if (!ok) {
            sendJson(500, errObj(QStringLiteral("delete failed")));
            return;
        }
        QJsonObject o;
        o.insert(QStringLiteral("ok"), true);
        sendJson(200, o);
    }

    // ---------------------------------------------------------- 上传落盘

    bool beginFile(const QString &rawName, qint64 expectedTotal)
    {
        const ServerContext &ctx = m_server->context();
        Q_UNUSED(ctx)
        const QString safe = afmu::sanitizeFileName(rawName);
        m_finalName = safe;
        // 临时名按本次传输编号区分，不能只用最终名：正式名此刻还不存在，uniqueTarget
        // 会把同一个名字发给两条并发连接，两边交错写进同一个 .afmu-part，最后各自
        // rename 出一个静默损坏的文件。服务端这侧没有续传，随机名不损失任何东西。
        m_inId = m_server->nextTransferId();
        m_partPath = QDir(m_uploadDir)
                         .filePath(safe + QLatin1Char('.') + QString::number(m_inId, 16)
                                   + QLatin1String(afmu::kPartSuffix));

        delete m_inFile;
        m_inFile = new QFile(m_partPath);
        if (!m_inFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            m_uploadError = m_inFile->errorString();
            delete m_inFile;
            m_inFile = nullptr;
            return false;
        }
        m_inDone = 0;
        emit m_server->transferStarted(m_inId, safe, expectedTotal, true);
        return true;
    }

    bool writeFile(const char *d, qint64 n)
    {
        if (!m_inFile)
            return false;
        if (m_inFile->write(d, n) != n) {
            m_uploadError = m_inFile->errorString();
            return false;
        }
        m_inDone += n;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_lastProgressMs > 120) {
            m_lastProgressMs = now;
            emit m_server->transferProgress(m_inId, m_inDone);
        }
        return true;
    }

    bool endFile()
    {
        if (!m_inFile)
            return false;
        m_inFile->flush();
        m_inFile->close();
        delete m_inFile;
        m_inFile = nullptr;

        const QString target = m_overwrite ? QDir(m_uploadDir).filePath(m_finalName)
                                           : afmu::uniqueTarget(m_uploadDir, m_finalName);
        if (m_overwrite && QFile::exists(target))
            QFile::remove(target);
        // 原子落盘：完整收完后才 rename 到正式名
        if (!QFile::rename(m_partPath, target)) {
            m_uploadError = QStringLiteral("rename failed");
            QFile::remove(m_partPath);
            emit m_server->transferFinished(m_inId, QString(), false, m_uploadError);
            return false;
        }
        m_savedPaths << target;
        emit m_server->transferProgress(m_inId, m_inDone);
        emit m_server->transferFinished(m_inId, target, true, QString());
        return true;
    }

    void abortIncomingFile()
    {
        if (m_inFile) {
            m_inFile->close();
            delete m_inFile;
            m_inFile = nullptr;
            QFile::remove(m_partPath); // 传输中断绝不留下半个文件
            emit m_server->transferFinished(m_inId, QString(), false,
                                            m_uploadError.isEmpty() ? T(QStringLiteral("连接中断"))
                                                                    : m_uploadError);
        }
    }

    void feedBody(const QByteArray &d)
    {
        if (m_useMultipart && m_multipart) {
            m_multipart->feed(d.constData(), d.size());
            if (m_multipart->failed()) {
                m_uploadError = m_multipart->error();
                abortIncomingFile();
                sendJson(500, errObj(m_uploadError));
                return;
            }
            if (m_multipart->finished()) {
                finishUpload();
            } else if (m_bodyRemaining > 0) {
                m_bodyRemaining -= d.size();
                if (m_bodyRemaining <= 0) {
                    // Content-Length 用完了但结尾边界没到 —— 请求体被截断，
                    // 绝不能回 ok:true，否则对端以为传完了，文件其实没落盘
                    m_uploadError = QStringLiteral("multipart body truncated");
                    abortIncomingFile();
                    sendJson(400, errObj(m_uploadError));
                }
            }
            return;
        }

        if (m_chunked) {
            m_chunked->feed(d.constData(), d.size());
            if (m_chunked->failed()) {
                m_uploadError = QStringLiteral("malformed chunked body");
                abortIncomingFile();
                sendJson(400, errObj(m_uploadError));
                return;
            }
            if (m_chunked->finished()) {
                if (!endFile()) {
                    sendJson(500, errObj(m_uploadError));
                    return;
                }
                finishUpload();
            }
            return;
        }

        qint64 n = d.size();
        if (m_bodyRemaining >= 0)
            n = qMin(n, m_bodyRemaining);
        if (n > 0 && !writeFile(d.constData(), n)) {
            abortIncomingFile();
            sendJson(500, errObj(m_uploadError));
            return;
        }
        if (m_bodyRemaining >= 0) {
            m_bodyRemaining -= n;
            if (m_bodyRemaining <= 0) {
                if (!endFile()) {
                    sendJson(500, errObj(m_uploadError));
                    return;
                }
                finishUpload();
            }
        }
    }

    void finishUpload()
    {
        // 兜底：还有没收尾的文件说明请求体不完整，宁可报错也不能谎报成功
        if (m_inFile) {
            m_uploadError = QStringLiteral("upload body truncated");
            abortIncomingFile();
            sendJson(400, errObj(m_uploadError));
            return;
        }
        if (m_savedPaths.isEmpty()) {
            sendJson(400, errObj(QStringLiteral("request contained no file part")));
            return;
        }
        QJsonArray saved;
        for (const QString &s : std::as_const(m_savedPaths))
            saved.append(s);
        QJsonObject o;
        o.insert(QStringLiteral("ok"), true);
        o.insert(QStringLiteral("saved"), saved);
        sendJson(200, o);
    }

    // ---------------------------------------------------------- 响应

    static QJsonObject errObj(const QString &msg)
    {
        QJsonObject o;
        o.insert(QStringLiteral("ok"), false);
        o.insert(QStringLiteral("error"), msg);
        return o;
    }

    void sendHeaders(int code, qint64 contentLength,
                     const QList<QPair<QByteArray, QByteArray>> &extra = {})
    {
        closeIfBodyPending();
        QByteArray h;
        h += "HTTP/1.1 " + QByteArray::number(code) + " " + statusText(code) + "\r\n";
        h += "Cache-Control: no-store\r\n";
        h += "Content-Length: " + QByteArray::number(contentLength) + "\r\n";
        for (const auto &kv : extra)
            h += kv.first + ": " + kv.second + "\r\n";
        h += m_close ? "Connection: close\r\n" : "Connection: keep-alive\r\n";
        h += "\r\n";
        m_sock->write(h);
    }

    // 请求体还没读就回响应时，流位置必然错乱：剩下的 body 会被当成流水线里的下一个请求，
    // 解析出一堆莫名其妙的 400。PROTOCOL.md §2.3 要求这种情况发 Connection: close。
    //
    // 和状态码无关：/api/pair、/api/authorize、/api/mkdir、/api/delete 都不读请求体，
    // 它们回 200 时同样错位。判据只有一个 —— 还在 Phase::Head 说明这条 body 一字未读。
    void closeIfBodyPending()
    {
        if (m_phase != Phase::Head)
            return;
        if (m_headers.value("transfer-encoding").toLower().contains("chunked")
            || m_headers.value("content-length").toLongLong() > 0) {
            m_close = true;
        }
    }

    void sendJson(int code, const QJsonObject &o,
                  const QList<QPair<QByteArray, QByteArray>> &extra = {})
    {
        const QByteArray body = QJsonDocument(o).toJson(QJsonDocument::Compact);
        QList<QPair<QByteArray, QByteArray>> headers{
            {"Content-Type", "application/json; charset=utf-8"}};
        headers += extra;
        sendHeaders(code, body.size(), headers);
        if (m_method != "HEAD")
            m_sock->write(body);
        finishResponse();
    }

    void sendText(int code, const QString &text)
    {
        const QByteArray body = text.toUtf8();
        sendHeaders(code, body.size(), {{"Content-Type", "text/plain; charset=utf-8"}});
        if (m_method != "HEAD")
            m_sock->write(body);
        finishResponse();
    }

    void pumpFile()
    {
        if (m_phase != Phase::Sending || !m_outFile)
            return;
        while (m_outRemaining > 0 && m_sock->bytesToWrite() < kWriteHighWater) {
            const QByteArray chunk = m_outFile->read(qMin(kChunkSize, m_outRemaining));
            if (chunk.isEmpty())
                break;
            m_outRemaining -= chunk.size();
            m_outDone += chunk.size();
            m_sock->write(chunk);
        }
        emit m_server->transferProgress(m_outId, m_outDone);
        if (m_outRemaining <= 0) {
            const QString name = QFileInfo(*m_outFile).fileName();
            delete m_outFile;
            m_outFile = nullptr;
            emit m_server->transferFinished(m_outId, name, true, QString());
            finishResponse();
        }
    }

    void finishResponse()
    {
        m_phase = Phase::Head;
        if (m_close) {
            m_phase = Phase::Closed;
            m_sock->flush();
            m_sock->disconnectFromHost();
            return;
        }
        // 也要看 socket 里还压着没有：发送文件期间 readyRead 来过一次就被丢掉了
        // （那时 m_phase 是 Sending，两个分支都不进），数据留在内核缓冲区里，
        // 不会再有第二次通知，下一个请求就这么挂到 120 秒 idle 超时。
        if (!m_buf.isEmpty() || m_sock->bytesAvailable() > 0)
            QTimer::singleShot(0, this, &HttpConnection::onReadyRead);
    }

    void abortAll()
    {
        abortIncomingFile();
        if (m_outFile) {
            delete m_outFile;
            m_outFile = nullptr;
            emit m_server->transferFinished(m_outId, QString(), false, T(QStringLiteral("连接中断")));
        }
    }

    HttpServer *m_server = nullptr;
    QSslSocket *m_sock = nullptr;
    QTimer *m_idle = nullptr;

    Phase m_phase = Phase::Head;

    /** 这条连接是不是 TLS。v1 明文连接一直是 false。 */
    bool m_tls = false;
    /** 对端的 SPKI 指纹，base32。只在 m_tls 为真时有值。 */
    QString m_peerFp;
    /** 指纹在配对表里。**握手成功 + 已配对 = 认证完成**，不再需要 token。 */
    bool m_peerPaired = false;
    QByteArray m_buf;

    QByteArray m_method;
    QByteArray m_target;
    QString m_path;
    QUrlQuery m_query;
    QHash<QByteArray, QByteArray> m_headers;
    bool m_close = false;

    // 上传
    qint64 m_bodyRemaining = -1;
    bool m_useMultipart = false;
    bool m_skipPart = false;
    bool m_overwrite = false;
    QString m_uploadDir;
    QString m_finalName;
    QString m_partPath;
    QString m_uploadError;
    QStringList m_savedPaths;
    QFile *m_inFile = nullptr;
    qint64 m_inDone = 0;
    qint64 m_inId = 0;
    qint64 m_lastProgressMs = 0;
    std::unique_ptr<MultipartParser> m_multipart;
    std::unique_ptr<ChunkedDecoder> m_chunked;

    // 下载
    QFile *m_outFile = nullptr;
    qint64 m_outRemaining = 0;
    qint64 m_outDone = 0;
    qint64 m_outId = 0;
};

// ------------------------------------------------------------------ HttpServer

HttpServer::HttpServer(QObject *parent)
    : QTcpServer(parent)
{
}

void HttpServer::setContext(const ServerContext &ctx)
{
    m_ctx = ctx;
}

void HttpServer::setIdentity(const afmu::Identity *id, PeerStore *peers)
{
    m_peers = peers;
    m_identity = nullptr;
    m_tlsReady = false;
    m_tlsConfig = QSslConfiguration();

    if (!id || !id->isValid() || !peers)
        return;
    if (!QSslSocket::supportsSsl()) {
        emit logMessage(T(QStringLiteral("这个 Qt 构建没有 TLS 支持，加密连接不可用")));
        return;
    }

    QSslConfiguration cfg = afmu::serverTlsConfiguration(*id);
    // 私钥没被 Qt 认出来的话，握手会在运行时才失败，而且报的是含糊的
    // 「No private key」——在这里当场发现要好得多。
    if (cfg.privateKey().isNull() || cfg.localCertificate().isNull()) {
        emit logMessage(T(QStringLiteral("身份无法交给 TLS 使用，加密连接不可用")));
        return;
    }
    m_tlsConfig = cfg;
    m_tlsReady = true;
    m_identity = id;
}

bool HttpServer::start(quint16 preferred)
{
    if (isListening())
        stop();

    QList<quint16> candidates;
    if (preferred)
        candidates << preferred;
    for (quint16 p : {quint16(8765), quint16(8766), quint16(8767)})
        if (!candidates.contains(p))
            candidates << p;
    candidates << 0; // 随机空闲端口

    for (quint16 p : std::as_const(candidates)) {
        if (listen(QHostAddress::Any, p)) {
            m_port = serverPort();
            emit portChanged();
            emit logMessage(T(QStringLiteral("服务端已监听 0.0.0.0:%1")).arg(m_port));
            return true;
        }
    }
    emit logMessage(T(QStringLiteral("服务端启动失败: %1")).arg(errorString()));
    return false;
}

void HttpServer::stop()
{
    if (!isListening())
        return;
    close();
    // 只停监听不够：已建立的连接会继续收发，界面显示"已停止"但文件还在往磁盘写
    const auto conns = findChildren<HttpConnection *>(QString(), Qt::FindDirectChildrenOnly);
    for (HttpConnection *c : conns)
        c->shutdown();
    m_port = 0;
    emit portChanged();
    emit logMessage(T(QStringLiteral("服务端已停止")));
}

void HttpServer::incomingConnection(qintptr handle)
{
    // 单个请求出异常只断这一条连接，不能拖垮服务
    new HttpConnection(handle, this);
}

#include "HttpServer.moc"
