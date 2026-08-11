#pragma once

#include <QJsonObject>
#include <QObject>
#include <QStringList>

// ~/.config/afmu/config.json（权限 600）
class Config : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString deviceName READ deviceName WRITE setDeviceName NOTIFY changed)
    Q_PROPERTY(QString localToken READ localToken WRITE setLocalToken NOTIFY changed)
    Q_PROPERTY(QString peerToken READ peerToken WRITE setPeerToken NOTIFY changed)
    Q_PROPERTY(QString downloadDir READ downloadDir WRITE setDownloadDir NOTIFY changed)
    Q_PROPERTY(QString inboxDir READ inboxDir WRITE setInboxDir NOTIFY changed)
    Q_PROPERTY(QStringList serveRoots READ serveRoots WRITE setServeRoots NOTIFY changed)
    Q_PROPERTY(int serverPort READ serverPort WRITE setServerPort NOTIFY changed)
    Q_PROPERTY(bool discoverable READ discoverable WRITE setDiscoverable NOTIFY changed)
    Q_PROPERTY(bool readOnly READ readOnly WRITE setReadOnly NOTIFY changed)
    Q_PROPERTY(bool autoStartServer READ autoStartServer WRITE setAutoStartServer NOTIFY changed)
    Q_PROPERTY(bool allowAuthRequests READ allowAuthRequests WRITE setAllowAuthRequests NOTIFY changed)
    Q_PROPERTY(bool allowLegacyPlaintext READ allowLegacyPlaintext WRITE setAllowLegacyPlaintext NOTIFY changed)
    Q_PROPERTY(int discoverTimeoutMs READ discoverTimeoutMs WRITE setDiscoverTimeoutMs NOTIFY changed)
    Q_PROPERTY(QString lastHost READ lastHost WRITE setLastHost NOTIFY changed)
    Q_PROPERTY(int lastPort READ lastPort WRITE setLastPort NOTIFY changed)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY changed)

public:
    explicit Config(QObject *parent = nullptr);

    QString deviceName() const;
    QString localToken() const;
    QString peerToken() const;
    QString downloadDir() const;
    QString inboxDir() const;
    QStringList serveRoots() const;
    int serverPort() const;
    bool discoverable() const;
    bool readOnly() const;
    bool autoStartServer() const;
    bool allowAuthRequests() const;
    /** 允许未加密的 v1 连接（PROTOCOL-v2-DRAFT.md §8.1）。见 setter 处的说明。 */
    bool allowLegacyPlaintext() const;
    int discoverTimeoutMs() const;
    QString lastHost() const;
    int lastPort() const;
    QString language() const;

    void setDeviceName(const QString &v);
    void setLocalToken(const QString &v);
    void setPeerToken(const QString &v);
    void setDownloadDir(const QString &v);
    void setInboxDir(const QString &v);
    void setServeRoots(const QStringList &v);
    void setServerPort(int v);
    void setDiscoverable(bool v);
    void setReadOnly(bool v);
    void setAutoStartServer(bool v);
    void setAllowAuthRequests(bool v);
    void setAllowLegacyPlaintext(bool v);
    void setDiscoverTimeoutMs(int v);
    void setLastHost(const QString &v);
    void setLastPort(int v);
    void setLanguage(const QString &v);

    Q_INVOKABLE void save();
    Q_INVOKABLE QString regenerateLocalToken();
    Q_INVOKABLE void addServeRoot(const QString &path);
    Q_INVOKABLE void removeServeRoot(const QString &path);
    Q_INVOKABLE QString configFilePath() const;

    /**
     * 上次加载出的问题，为空表示一切正常。
     *
     * 配置文件在但读不出来（崩溃时写了一半、磁盘满、手改打错）时，**不能**
     * 原地覆盖成默认值 —— 那会让 token 和 serveRoots 无声消失，用户只会发现
     * 「怎么全变回默认了」，而 token 一变所有设备一起连不上。
     * 这里给出原因和留底路径，由界面/日志告诉用户。
     */
    QString loadError() const { return m_loadError; }

signals:
    void changed();

private:
    void load();
    void setValue(const QString &key, const QJsonValue &v);

    QJsonObject m_json;
    QString m_path;
    QString m_loadError;
    /** 原文件读不出来又备份不掉时置位：宁可不写，也不覆盖可能还能救的 token。 */
    bool m_readOnlyFallback = false;
};
