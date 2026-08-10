#pragma once

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QPointer>

class PeerClient;
class Config;
class QNetworkReply;
class QFile;
class QTimer;

class TransferModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY activeCountChanged)
    Q_PROPERTY(int failedCount READ failedCount NOTIFY activeCountChanged)

public:
    enum Kind { Download = 0, Upload, ServerIncoming, ServerOutgoing };
    Q_ENUM(Kind)
    enum State { Queued = 0, Running, Completed, Failed, Canceled };
    Q_ENUM(State)

    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        KindRole,
        KindTextRole,
        StateRole,
        StateTextRole,
        DoneRole,
        TotalRole,
        ProgressRole,
        DetailRole,
        ErrorRole,
        LocalPathRole,
        RemotePathRole,
    };

    explicit TransferModel(PeerClient *client, Config *config, QObject *parent = nullptr);
    ~TransferModel() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int activeCount() const;
    int failedCount() const;

    // 客户端方向
    qint64 addDownload(const QString &remotePath, const QString &name, qint64 size);
    qint64 addUpload(const QString &localPath, const QString &remoteDir);

    // 服务端方向（手机推/拉本机）
    qint64 addServerTransfer(const QString &name, qint64 total, bool incoming);
    void updateServerTransfer(qint64 id, qint64 done);
    void finishServerTransfer(qint64 id, const QString &path, bool ok, const QString &error);

    Q_INVOKABLE void cancel(qint64 id);
    Q_INVOKABLE void retry(qint64 id);
    Q_INVOKABLE void remove(qint64 id);
    Q_INVOKABLE void clearFinished();
    Q_INVOKABLE void cancelAll();
    Q_INVOKABLE void revealInFileManager(qint64 id);
    Q_INVOKABLE void openFile(qint64 id);

signals:
    void countChanged();
    void activeCountChanged();
    void message(const QString &text, bool isError);
    void transferCompleted(const QString &name, int kind);

private:
    struct Task
    {
        qint64 id = 0;
        int kind = Download;
        int state = Queued;
        QString name;
        QString localPath;   // 下载：最终落盘路径；上传：源文件
        QString partPath;    // 下载中的 .afmu-part
        QString remotePath;  // 下载：远端文件；上传：远端目录
        QString error;
        qint64 done = 0;
        qint64 total = -1;
        qint64 resumeFrom = 0;
        double speed = 0;      // B/s
        QElapsedTimer clock;
        qint64 lastMs = 0;
        qint64 lastBytes = 0;
        QPointer<QNetworkReply> reply;
        QFile *file = nullptr;
    };

    int indexOf(qint64 id) const;
    Task *task(qint64 id) const;
    void emitRow(int row, const QList<int> &roles = {});
    void pump();               // 按并发上限启动排队中的任务
    void startTask(Task *t);
    void startDownload(Task *t);
    void startUpload(Task *t);
    void finish(Task *t, int state, const QString &error = {});
    void tickProgress(Task *t, qint64 done, qint64 total);
    QString detailText(const Task *t) const;

    PeerClient *m_client = nullptr;
    Config *m_config = nullptr;
    QList<Task *> m_tasks;
    qint64 m_nextId = 1;
    int m_maxConcurrent = 2;
    QTimer *m_repaint = nullptr;
};
