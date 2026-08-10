#include "TransferModel.h"
#include "I18n.h"

#include "Config.h"
#include "PathSafety.h"
#include "PeerClient.h"
#include "Protocol.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

TransferModel::TransferModel(PeerClient *client, Config *config, QObject *parent)
    : QAbstractListModel(parent)
    , m_client(client)
    , m_config(config)
{
    // 进度刷新节流：避免大文件传输时刷新本身成为瓶颈
    m_repaint = new QTimer(this);
    m_repaint->setInterval(120);
    connect(m_repaint, &QTimer::timeout, this, [this] {
        bool any = false;
        for (int i = 0; i < m_tasks.size(); ++i) {
            if (m_tasks[i]->state == Running) {
                emitRow(i, {DoneRole, TotalRole, ProgressRole, DetailRole});
                any = true;
            }
        }
        if (!any)
            m_repaint->stop();
    });

    // 这些文案是 data() 里现算的，语言变了得让整列表重新取一次
    if (I18n *lang = I18n::instance()) {
        connect(lang, &I18n::languageChanged, this, [this] {
            if (!m_tasks.isEmpty())
                emit dataChanged(index(0), index(m_tasks.size() - 1),
                                 {KindTextRole, StateTextRole, DetailRole});
        });
    }
}

TransferModel::~TransferModel()
{
    for (Task *t : std::as_const(m_tasks)) {
        if (t->reply)
            t->reply->abort();
        delete t->file;
        delete t;
    }
}

int TransferModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_tasks.size();
}

QHash<int, QByteArray> TransferModel::roleNames() const
{
    return {
        {IdRole, "tid"},
        {NameRole, "name"},
        {KindRole, "kind"},
        {KindTextRole, "kindText"},
        {StateRole, "state"},
        {StateTextRole, "stateText"},
        {DoneRole, "done"},
        {TotalRole, "total"},
        {ProgressRole, "progress"},
        {DetailRole, "detail"},
        {ErrorRole, "error"},
        {LocalPathRole, "localPath"},
        {RemotePathRole, "remotePath"},
    };
}

QVariant TransferModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tasks.size())
        return {};
    const Task *t = m_tasks.at(index.row());
    switch (role) {
    case IdRole: return t->id;
    case NameRole: return t->name;
    case KindRole: return t->kind;
    case KindTextRole:
        switch (t->kind) {
        case Download: return T(QStringLiteral("下载"));
        case Upload: return T(QStringLiteral("上传"));
        case ServerIncoming: return T(QStringLiteral("接收"));
        default: return T(QStringLiteral("发送"));
        }
    case StateRole: return t->state;
    case StateTextRole:
        switch (t->state) {
        case Queued: return T(QStringLiteral("排队中"));
        case Running: return T(QStringLiteral("传输中"));
        case Completed: return T(QStringLiteral("已完成"));
        case Failed: return T(QStringLiteral("失败"));
        default: return T(QStringLiteral("已取消"));
        }
    case DoneRole: return t->done;
    case TotalRole: return t->total;
    case ProgressRole: return t->total > 0 ? double(t->done) / double(t->total) : 0.0;
    case DetailRole: return detailText(t);
    case ErrorRole: return t->error;
    case LocalPathRole: return t->localPath;
    case RemotePathRole: return t->remotePath;
    default: return {};
    }
}

QString TransferModel::detailText(const Task *t) const
{
    if (t->state == Failed)
        return t->error.isEmpty() ? T(QStringLiteral("传输失败")) : t->error;
    if (t->state == Canceled)
        return T(QStringLiteral("已取消"));
    if (t->state == Completed) {
        const QString where = t->kind == Download || t->kind == ServerIncoming ? t->localPath
                                                                               : t->remotePath;
        return QStringLiteral("%1 · %2").arg(afmu::humanSize(t->total > 0 ? t->total : t->done),
                                             where.isEmpty() ? T(QStringLiteral("完成")) : where);
    }
    if (t->state == Queued)
        return T(QStringLiteral("等待中"));

    QString s = t->total > 0
                    ? QStringLiteral("%1 / %2").arg(afmu::humanSize(t->done), afmu::humanSize(t->total))
                    : afmu::humanSize(t->done);
    if (t->speed > 1.0) {
        s += QStringLiteral(" · %1/s").arg(afmu::humanSize(qint64(t->speed)));
        if (t->total > 0) {
            const qint64 left = qint64(double(t->total - t->done) / t->speed);
            if (left > 0 && left < 86400) {
                if (left >= 60)
                    s += T(QStringLiteral(" · 剩余 %1 分 %2 秒")).arg(left / 60).arg(left % 60);
                else
                    s += T(QStringLiteral(" · 剩余 %1 秒")).arg(left);
            }
        }
    }
    return s;
}

int TransferModel::activeCount() const
{
    int n = 0;
    for (const Task *t : m_tasks)
        if (t->state == Running || t->state == Queued)
            ++n;
    return n;
}

int TransferModel::failedCount() const
{
    int n = 0;
    for (const Task *t : m_tasks)
        if (t->state == Failed)
            ++n;
    return n;
}

int TransferModel::indexOf(qint64 id) const
{
    for (int i = 0; i < m_tasks.size(); ++i)
        if (m_tasks[i]->id == id)
            return i;
    return -1;
}

TransferModel::Task *TransferModel::task(qint64 id) const
{
    const int i = indexOf(id);
    return i < 0 ? nullptr : m_tasks.at(i);
}

void TransferModel::emitRow(int row, const QList<int> &roles)
{
    if (row < 0 || row >= m_tasks.size())
        return;
    emit dataChanged(index(row), index(row), roles.isEmpty() ? QList<int>{} : roles);
}

// ---------------------------------------------------------------- 入队

qint64 TransferModel::addDownload(const QString &remotePath, const QString &name, qint64 size)
{
    auto *t = new Task;
    t->id = m_nextId++;
    t->kind = Download;
    t->state = Queued;
    t->name = name.isEmpty() ? remotePath.section(QLatin1Char('/'), -1) : name;
    t->remotePath = remotePath;
    t->total = size > 0 ? size : -1;

    beginInsertRows({}, m_tasks.size(), m_tasks.size());
    m_tasks.append(t);
    endInsertRows();
    emit countChanged();
    emit activeCountChanged();
    pump();
    return t->id;
}

qint64 TransferModel::addUpload(const QString &localPath, const QString &remoteDir)
{
    QFileInfo fi(localPath);
    auto *t = new Task;
    t->id = m_nextId++;
    t->kind = Upload;
    t->state = Queued;
    t->name = fi.fileName();
    t->localPath = fi.absoluteFilePath();
    t->remotePath = remoteDir;
    t->total = fi.size();

    beginInsertRows({}, m_tasks.size(), m_tasks.size());
    m_tasks.append(t);
    endInsertRows();
    emit countChanged();
    emit activeCountChanged();
    pump();
    return t->id;
}

qint64 TransferModel::addServerTransfer(const QString &name, qint64 total, bool incoming)
{
    auto *t = new Task;
    t->id = m_nextId++;
    t->kind = incoming ? ServerIncoming : ServerOutgoing;
    t->state = Running;
    t->name = name;
    t->total = total;
    t->clock.start();

    beginInsertRows({}, m_tasks.size(), m_tasks.size());
    m_tasks.append(t);
    endInsertRows();
    emit countChanged();
    emit activeCountChanged();
    if (!m_repaint->isActive())
        m_repaint->start();
    return t->id;
}

void TransferModel::updateServerTransfer(qint64 id, qint64 done)
{
    Task *t = task(id);
    if (!t || t->state != Running)
        return;
    tickProgress(t, done, t->total);
}

void TransferModel::finishServerTransfer(qint64 id, const QString &path, bool ok, const QString &error)
{
    Task *t = task(id);
    if (!t)
        return;
    t->localPath = path;
    if (ok && t->total <= 0)
        t->total = t->done;
    finish(t, ok ? Completed : Failed, error);
}

// ---------------------------------------------------------------- 调度

void TransferModel::pump()
{
    int running = 0;
    for (const Task *t : m_tasks)
        if (t->state == Running && (t->kind == Download || t->kind == Upload))
            ++running;

    for (Task *t : m_tasks) {
        if (running >= m_maxConcurrent)
            break;
        if (t->state != Queued)
            continue;
        startTask(t);
        ++running;
    }
}

void TransferModel::startTask(Task *t)
{
    if (!m_client->hasPeer()) {
        finish(t, Failed, T(QStringLiteral("未连接到任何设备")));
        return;
    }
    t->state = Running;
    t->error.clear();
    t->clock.start();
    t->lastMs = 0;
    t->lastBytes = 0;
    t->speed = 0;
    emitRow(indexOf(t->id));
    emit activeCountChanged();
    if (!m_repaint->isActive())
        m_repaint->start();

    if (t->kind == Download)
        startDownload(t);
    else
        startUpload(t);
}

void TransferModel::startDownload(Task *t)
{
    const QString destDir = m_config->downloadDir();
    if (!QDir().mkpath(destDir)) {
        finish(t, Failed, T(QStringLiteral("无法创建下载目录 %1")).arg(destDir));
        return;
    }

    const QString safeName = afmu::sanitizeFileName(t->name);
    // part 文件名里带上远端路径指纹：两个不同目录下的同名文件（或上次遗留的残片）
    // 不会再共用同一个 .afmu-part，否则续传起点是错的，落盘的文件直接损坏
    const QByteArray fingerprint =
        QCryptographicHash::hash(t->remotePath.toUtf8(), QCryptographicHash::Sha1).toHex().left(8);
    t->partPath = QDir(destDir).filePath(safeName + QLatin1Char('.')
                                         + QString::fromLatin1(fingerprint)
                                         + QLatin1String(afmu::kPartSuffix));

    // 同一个远端文件已经在下载中时，两个任务会写同一个 part 文件
    for (const Task *other : std::as_const(m_tasks)) {
        if (other != t && other->state == Running && other->partPath == t->partPath) {
            finish(t, Failed, T(QStringLiteral("同一个文件已经在下载中")));
            return;
        }
    }

    // 续传：保留 .afmu-part 并用其大小作为 Range 起点
    qint64 have = QFileInfo::exists(t->partPath) ? QFileInfo(t->partPath).size() : 0;
    // 残片不可能大于等于完整大小；对不上说明是陈旧数据，丢掉重来
    if (have > 0 && t->total > 0 && have >= t->total)
        have = 0;
    t->resumeFrom = have;
    t->done = have;

    auto *f = new QFile(t->partPath);
    if (!f->open(have > 0 ? (QIODevice::WriteOnly | QIODevice::Append)
                          : (QIODevice::WriteOnly | QIODevice::Truncate))) {
        const QString err = f->errorString();
        delete f;
        finish(t, Failed, T(QStringLiteral("无法写入 %1: %2")).arg(t->partPath, err));
        return;
    }
    t->file = f;

    QUrlQuery q;
    q.addQueryItem(QStringLiteral("path"), t->remotePath);
    QNetworkRequest req = m_client->request(QStringLiteral("/api/download"), q);
    if (have > 0)
        req.setRawHeader("Range", QByteArray("bytes=") + QByteArray::number(have) + "-");

    QNetworkReply *reply = m_client->getRaw(req);
    t->reply = reply;
    const qint64 id = t->id;

    connect(reply, &QNetworkReply::readyRead, this, [this, id, reply] {
        Task *tt = task(id);
        if (!tt || !tt->file)
            return;
        const QByteArray chunk = reply->readAll();
        if (chunk.isEmpty())
            return;
        if (tt->file->write(chunk) != chunk.size()) {
            const QString err = tt->file->errorString();
            reply->abort();
            finish(tt, Failed, T(QStringLiteral("写入失败: %1")).arg(err));
            return;
        }
        tickProgress(tt, tt->done + chunk.size(), tt->total);
    });

    connect(reply, &QNetworkReply::metaDataChanged, this, [this, id, reply] {
        Task *tt = task(id);
        if (!tt)
            return;
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const qint64 len = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        if (code == 206) {
            // Content-Range: bytes <start>-<end>/<total>
            const QByteArray cr = reply->rawHeader("Content-Range");
            const int slash = cr.lastIndexOf('/');
            if (slash > 0) {
                bool ok = false;
                const qint64 total = cr.mid(slash + 1).trimmed().toLongLong(&ok);
                if (ok && total > 0)
                    tt->total = total;
            } else if (len > 0) {
                tt->total = tt->resumeFrom + len;
            }
        } else if (code == 200) {
            // 服务端忽略了 Range：从头开始，把已有的部分丢掉
            if (tt->resumeFrom > 0 && tt->file) {
                tt->file->seek(0);
                tt->file->resize(0);
                tt->resumeFrom = 0;
                tt->done = 0;
            }
            if (len > 0)
                tt->total = len;
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, id, reply] {
        reply->deleteLater();
        Task *tt = task(id);
        if (!tt)
            return;
        if (tt->state == Canceled) {
            if (tt->file) {
                tt->file->close();
                delete tt->file;
                tt->file = nullptr;
            }
            return;
        }

        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool netOk = reply->error() == QNetworkReply::NoError;
        const bool httpOk = code == 200 || code == 206;

        if (tt->file) {
            tt->file->flush();
            tt->file->close();
        }

        if (!netOk || !httpOk) {
            QString err;
            if (code == 416) {
                err = T(QStringLiteral("续传区间越界（416），请删除临时文件后重试"));
            } else {
                // 错误响应体已经被写进了 part 文件，读回来解析
                QByteArray body;
                if (tt->file && QFileInfo(tt->partPath).size() < 4096) {
                    QFile rf(tt->partPath);
                    if (rf.open(QIODevice::ReadOnly))
                        body = rf.readAll();
                }
                err = PeerClient::errorFrom(reply, body);
            }
            if (code >= 400) // 错误响应体不是文件内容，别留着冒充续传数据
                QFile::remove(tt->partPath);
            delete tt->file;
            tt->file = nullptr;
            finish(tt, Failed, err);
            return;
        }

        delete tt->file;
        tt->file = nullptr;

        // 原子落盘：收完整了才 rename 到正式名
        const QString finalPath =
            afmu::uniqueTarget(QFileInfo(tt->partPath).absolutePath(), afmu::sanitizeFileName(tt->name));
        if (!QFile::rename(tt->partPath, finalPath)) {
            finish(tt, Failed, T(QStringLiteral("重命名失败: %1")).arg(finalPath));
            return;
        }
        tt->localPath = finalPath;
        if (tt->total <= 0)
            tt->total = tt->done;
        finish(tt, Completed);
    });
}

void TransferModel::startUpload(Task *t)
{
    auto *f = new QFile(t->localPath);
    if (!f->open(QIODevice::ReadOnly)) {
        const QString err = f->errorString();
        delete f;
        finish(t, Failed, T(QStringLiteral("无法读取 %1: %2")).arg(t->localPath, err));
        return;
    }
    t->file = f;
    t->total = f->size();

    QUrlQuery q;
    q.addQueryItem(QStringLiteral("name"), t->name);
    if (!t->remotePath.isEmpty() && t->remotePath != QLatin1String("/"))
        q.addQueryItem(QStringLiteral("dir"), t->remotePath);

    // 原始字节流是客户端首选
    QNetworkReply *reply =
        m_client->post(QStringLiteral("/api/upload"), q, f, QByteArrayLiteral("application/octet-stream"));
    t->reply = reply;
    f->setParent(reply);
    const qint64 id = t->id;

    connect(reply, &QNetworkReply::uploadProgress, this, [this, id](qint64 sent, qint64 total) {
        Task *tt = task(id);
        if (tt && tt->state == Running)
            tickProgress(tt, sent, total > 0 ? total : tt->total);
    });

    connect(reply, &QNetworkReply::finished, this, [this, id, reply] {
        reply->deleteLater();
        Task *tt = task(id);
        if (!tt)
            return;
        tt->file = nullptr; // 归 reply 所有
        if (tt->state == Canceled)
            return;

        const QByteArray body = reply->readAll();
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        // 先看 ok 再看 HTTP 状态码
        if (reply->error() != QNetworkReply::NoError || !o.value(QStringLiteral("ok")).toBool(false)) {
            Q_UNUSED(code)
            finish(tt, Failed, PeerClient::errorFrom(reply, body));
            return;
        }
        const QJsonArray saved = o.value(QStringLiteral("saved")).toArray();
        if (!saved.isEmpty())
            tt->remotePath = saved.first().toString();
        tt->done = tt->total;
        finish(tt, Completed);
    });
}

void TransferModel::tickProgress(Task *t, qint64 done, qint64 total)
{
    t->done = done;
    if (total > 0)
        t->total = total;

    const qint64 now = t->clock.isValid() ? t->clock.elapsed() : 0;
    const qint64 dt = now - t->lastMs;
    if (dt >= 400) {
        const double inst = double(done - t->lastBytes) * 1000.0 / double(dt);
        t->speed = t->speed > 0 ? t->speed * 0.6 + inst * 0.4 : inst;
        t->lastMs = now;
        t->lastBytes = done;
    }
    if (!m_repaint->isActive())
        m_repaint->start();
}

void TransferModel::finish(Task *t, int state, const QString &error)
{
    t->state = state;
    t->error = error;
    t->speed = 0;
    if (t->file) {
        t->file->close();
        if (t->file->parent() == nullptr)
            delete t->file;
        t->file = nullptr;
    }
    emitRow(indexOf(t->id));
    emit activeCountChanged();

    if (state == Completed)
        emit transferCompleted(t->name, t->kind);
    else if (state == Failed)
        emit message(QStringLiteral("%1：%2").arg(t->name, error), true);

    pump();
}

// ---------------------------------------------------------------- 操作

void TransferModel::cancel(qint64 id)
{
    Task *t = task(id);
    if (!t || t->state == Completed)
        return;
    t->state = Canceled;
    if (t->reply)
        t->reply->abort();
    // 只关文件句柄，.afmu-part 故意留着给「重试」当续传起点（PROTOCOL.md §4.3）。
    // 半个文件不会冒充完整文件：正式名只在收完后 rename 才出现。
    if (t->kind == Download && !t->partPath.isEmpty()) {
        if (t->file) {
            t->file->close();
            delete t->file;
            t->file = nullptr;
        }
    }
    t->error.clear();
    emitRow(indexOf(t->id));
    emit activeCountChanged();
    pump();
}

void TransferModel::retry(qint64 id)
{
    Task *t = task(id);
    if (!t || t->state == Running || t->state == Queued)
        return;
    t->state = Queued;
    t->error.clear();
    if (t->kind == ServerIncoming || t->kind == ServerOutgoing) {
        t->state = Failed;
        emit message(T(QStringLiteral("对端发起的传输无法从本机重试")), true);
        return;
    }
    if (t->kind == Download)
        t->done = 0;
    emitRow(indexOf(t->id));
    emit activeCountChanged();
    pump();
}

void TransferModel::remove(qint64 id)
{
    const int row = indexOf(id);
    if (row < 0)
        return;
    Task *t = m_tasks.at(row);
    if (t->state == Running || t->state == Queued)
        cancel(id);
    beginRemoveRows({}, row, row);
    m_tasks.removeAt(row);
    endRemoveRows();
    delete t;
    emit countChanged();
    emit activeCountChanged();
}

void TransferModel::clearFinished()
{
    for (int i = m_tasks.size() - 1; i >= 0; --i) {
        const int s = m_tasks[i]->state;
        if (s == Completed || s == Failed || s == Canceled) {
            beginRemoveRows({}, i, i);
            Task *t = m_tasks.takeAt(i);
            endRemoveRows();
            delete t;
        }
    }
    emit countChanged();
    emit activeCountChanged();
}

void TransferModel::cancelAll()
{
    const auto ids = [this] {
        QList<qint64> v;
        for (const Task *t : m_tasks)
            if (t->state == Running || t->state == Queued)
                v << t->id;
        return v;
    }();
    for (qint64 id : ids)
        cancel(id);
}

void TransferModel::revealInFileManager(qint64 id)
{
    const Task *t = task(id);
    if (!t)
        return;
    const QString p = t->localPath.isEmpty() ? m_config->downloadDir() : t->localPath;
    const QString dir = QFileInfo(p).isDir() ? p : QFileInfo(p).absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void TransferModel::openFile(qint64 id)
{
    const Task *t = task(id);
    if (!t || t->localPath.isEmpty() || !QFileInfo::exists(t->localPath))
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(t->localPath));
}
