#include "HttpServer.h"
#include "I18n.h"

#include "AuthRequests.h"
#include "PathSafety.h"
#include "Protocol.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMimeDatabase>
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
const int kIdleTimeoutMs = 120 * 1000; // 服务端 socket 超时 120 秒

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
        , m_sock(new QTcpSocket(this))
    {
        if (!m_sock->setSocketDescriptor(handle)) {
            deleteLater();
            return;
        }
        m_sock->setSocketOption(QAbstractSocket::LowDelayOption, 1);
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

    void onReadyRead()
    {
        m_idle->start();
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
    enum class Phase { Head, Body, Sending, Closed };

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

    // 三种等价写法，按顺序取第一个非空值
    bool authorized() const
    {
        const ServerContext &ctx = m_server->context();
        QByteArray given = m_headers.value(QByteArray(afmu::kTokenHeader).toLower());
        if (given.isEmpty() && hasParam(QStringLiteral("token")))
            given = param(QStringLiteral("token")).toUtf8();
        if (given.isEmpty()) {
            const QByteArray auth = m_headers.value("authorization");
            if (auth.toLower().startsWith("bearer "))
                given = auth.mid(7).trimmed();
        }
        return afmu::tokenEquals(given, ctx.token.toUtf8());
    }

    // ---------------------------------------------------------- 路由

    void route()
    {
        const ServerContext &ctx = m_server->context();

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
        if (!authorized()) {
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

        sendJson(404, errObj(QStringLiteral("unknown endpoint")));
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
    QTcpSocket *m_sock = nullptr;
    QTimer *m_idle = nullptr;

    Phase m_phase = Phase::Head;
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
