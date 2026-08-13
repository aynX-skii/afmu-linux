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
    Q_PROPERTY(bool zeroTrustMode READ zeroTrustMode WRITE setZeroTrustMode NOTIFY changed)
    Q_PROPERTY(bool guestMode READ guestMode WRITE setGuestMode NOTIFY changed)
    Q_PROPERTY(bool guestModeAvailable READ guestModeAvailable NOTIFY changed)
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
    /** 允许未加密的 v1 连接（PROTOCOL.md v2 §8.1）。见 setter 处的说明。 */
    bool allowLegacyPlaintext() const;

    /**
     * 零信任模式（草案 §9）：只认配对表里的设备，别的一律不放行。
     * 打开之后访客模式强制关闭，界面上那个开关也置灰 —— 不是隐藏，
     * 是让用户看得见「它被这个模式关掉了」。
     */
    bool zeroTrustMode() const;

    /**
     * 访客模式（草案 §9）：浏览器界面 + 密码认证，也就是 v1 那套访问方式。
     *
     * **它达不到 v2 的安全标准，而且做不到** —— 浏览器不会拿客户端证书做 mTLS，
     * 自签服务端证书又只会弹一个「不安全」让用户点「继续」，点完就等于关掉了
     * 中间人防护。所以它是个便利功能，界面上必须照实说，不要包装成安全的。
     *
     * 开着的时候它挡住的是**被动嗅探**（走 HTTPS 的话），不是中间人。
     */
    bool guestMode() const;

    /** 零信任模式下访客模式不可用。界面拿它决定开关是否置灰。 */
    bool guestModeAvailable() const { return !zeroTrustMode(); }

    /** 实际生效的访客模式：开关开着**且**零信任没打开。服务端只该问这个。 */
    bool guestModeActive() const { return guestMode() && !zeroTrustMode(); }
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
    void setZeroTrustMode(bool v);
    void setGuestMode(bool v);
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

    /**
     * 本次启动的一次性迁移刚刚把明文关掉了（§8.2 第 3 阶段）。
     *
     * 只在**真的动过**的那一次启动为真：本来就关着、或者迁移早就跑过，都是假。
     * 界面据此说一次「已停止接受明文连接，老设备连不上就去设置里重新打开」——
     * 悄悄关掉的话，用户看到的是「今天开始连不上了」，然后去查网络和防火墙。
     */
    bool plaintextJustDisabled() const { return m_plaintextJustDisabled; }

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
    /** 见 plaintextJustDisabled()。 */
    bool m_plaintextJustDisabled = false;
};
