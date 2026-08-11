#pragma once

#include "Config.h"

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

#include <memory>

namespace afmu {
class Identity;
}

class QTimer;
class I18n;
class Discovery;
class DeviceModel;
class RemoteFileModel;
class TransferModel;
class PeerClient;
class PeerStore;
class HttpServer;
class AuthRequests;

class AppController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(Config *config READ config CONSTANT)
    Q_PROPERTY(QObject *devices READ devicesObj CONSTANT)
    Q_PROPERTY(QObject *files READ filesObj CONSTANT)
    Q_PROPERTY(QObject *transfers READ transfersObj CONSTANT)
    // 配对表：v2 的授权依据（PROTOCOL-v2-DRAFT.md §4.3）。v2 握手还没接上，
    // 所以现在只有「看和删」有意义 —— 但删除入口必须先于写入存在，
    // 否则第一次写进去的东西用户就拿不掉了。
    Q_PROPERTY(QObject *peers READ peersObj CONSTANT)

    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY peerChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)

    Q_PROPERTY(QString peerName READ peerName NOTIFY peerChanged)
    Q_PROPERTY(QString peerOs READ peerOs NOTIFY peerChanged)
    Q_PROPERTY(QString peerHost READ peerHost NOTIFY peerChanged)
    Q_PROPERTY(int peerPort READ peerPort NOTIFY peerChanged)
    Q_PROPERTY(bool peerWritable READ peerWritable NOTIFY peerChanged)
    Q_PROPERTY(QString peerInbox READ peerInbox NOTIFY peerChanged)
    Q_PROPERTY(QVariantList peerRoots READ peerRoots NOTIFY peerChanged)
    Q_PROPERTY(QString peerSummary READ peerSummary NOTIFY peerChanged)

    Q_PROPERTY(QString currentPath READ currentPath NOTIFY pathChanged)
    Q_PROPERTY(QString parentPath READ parentPath NOTIFY pathChanged)
    Q_PROPERTY(bool atRoot READ atRoot NOTIFY pathChanged)
    Q_PROPERTY(QVariantList breadcrumbs READ breadcrumbs NOTIFY pathChanged)

    Q_PROPERTY(bool serverRunning READ serverRunning NOTIFY serverChanged)
    Q_PROPERTY(int serverPort READ serverPort NOTIFY serverChanged)
    Q_PROPERTY(QStringList localAddresses READ localAddresses NOTIFY serverChanged)
    Q_PROPERTY(QStringList serverLog READ serverLog NOTIFY serverLogChanged)

    // 加密连接（PROTOCOL-v2-DRAFT.md §3/§5）。指纹是本机的身份，要给用户看、
    // 让他拿去跟对端屏幕上的比 —— 所以是展示形式（每 5 个字符一组）的全长。
    Q_PROPERTY(QString localFingerprint READ localFingerprint CONSTANT)
    Q_PROPERTY(bool tlsReady READ tlsReady NOTIFY serverChanged)

    // 配对模式：常态下发现应答不含设备名，只有用户点了才短暂公开（PROTOCOL.md §1.5）
    Q_PROPERTY(bool pairingMode READ pairingMode NOTIFY pairingModeChanged)
    Q_PROPERTY(int pairingRemaining READ pairingRemaining NOTIFY pairingModeChanged)

    // 手机扫这个二维码即可拿到本机地址 + token（PROTOCOL.md §5）
    Q_PROPERTY(QString pairUri READ pairUri NOTIFY pairUriChanged)

    // 授权连接：本机发起请求，对端弹窗确认（PROTOCOL.md §3.8）
    Q_PROPERTY(bool authPending READ authPending NOTIFY authChanged)
    Q_PROPERTY(QString authStatus READ authStatus NOTIFY authChanged)
    Q_PROPERTY(QString authCode READ authCode NOTIFY authChanged)
    Q_PROPERTY(QString authTarget READ authTarget NOTIFY authChanged)
    Q_PROPERTY(int authRemaining READ authRemaining NOTIFY authChanged)

    // 反过来：别的设备来请求连接本机，由用户在本机确认（Linux ↔ Linux 靠这条打通）
    Q_PROPERTY(bool incomingAuthPending READ incomingAuthPending NOTIFY incomingAuthChanged)
    Q_PROPERTY(QString incomingAuthName READ incomingAuthName NOTIFY incomingAuthChanged)
    Q_PROPERTY(QString incomingAuthHost READ incomingAuthHost NOTIFY incomingAuthChanged)
    Q_PROPERTY(QString incomingAuthOs READ incomingAuthOs NOTIFY incomingAuthChanged)
    Q_PROPERTY(QString incomingAuthCode READ incomingAuthCode NOTIFY incomingAuthChanged)
    // v2 配对请求：显示的不是 4 位确认码而是 8 字符 SAS，用户拿它跟对端屏幕比对
    Q_PROPERTY(bool incomingAuthIsPairing READ incomingAuthIsPairing NOTIFY incomingAuthChanged)
    Q_PROPERTY(QString incomingAuthFingerprint READ incomingAuthFingerprint NOTIFY incomingAuthChanged)
    /** 8 字符 SAS 的展示形式。第 2 步之前是空的 —— 空就别显示"允许"按钮。 */
    Q_PROPERTY(QString incomingAuthSas READ incomingAuthSas NOTIFY incomingAuthChanged)
    Q_PROPERTY(int incomingAuthRemaining READ incomingAuthRemaining NOTIFY incomingAuthChanged)

    Q_PROPERTY(QString toastText READ toastText NOTIFY toast)
    Q_PROPERTY(bool toastIsError READ toastIsError NOTIFY toast)

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    Config *config() const { return m_config; }
    /** 语言对象。构造得比一切会产生文案的东西都早。 */
    I18n *i18n() const { return m_i18n; }

    QObject *devicesObj() const;
    QObject *filesObj() const;
    QObject *transfersObj() const;
    QObject *peersObj() const;

    /** 配对表本身，给 C++ 侧的 TLS 钉扎用。 */
    PeerStore *peers() const { return m_peers; }

    bool scanning() const { return m_scanning; }
    bool connected() const { return m_connected; }
    bool loading() const { return m_loading; }

    QString peerName() const { return m_peerName; }
    QString peerOs() const { return m_peerOs; }
    QString peerHost() const;
    int peerPort() const;
    bool peerWritable() const { return m_peerWritable; }
    QString peerInbox() const { return m_peerInbox; }
    QVariantList peerRoots() const { return m_peerRoots; }
    QString peerSummary() const;

    QString currentPath() const { return m_currentPath; }
    QString parentPath() const { return m_parentPath; }
    bool atRoot() const { return m_currentPath.isEmpty() || m_currentPath == QLatin1String("/"); }
    QVariantList breadcrumbs() const;

    bool serverRunning() const;
    int serverPort() const;
    QStringList localAddresses() const;
    QStringList serverLog() const { return m_log; }
    QString localFingerprint() const;
    bool tlsReady() const;
    bool pairingMode() const;
    int pairingRemaining() const;

    QString pairUri() const;

    bool authPending() const { return m_authPending; }
    QString authStatus() const { return m_authStatus; }
    QString authCode() const { return m_authCode; }
    QString authTarget() const { return m_authTarget; }
    int authRemaining() const { return m_authRemaining; }

    bool incomingAuthPending() const;
    QString incomingAuthName() const;
    QString incomingAuthHost() const;
    QString incomingAuthOs() const;
    QString incomingAuthCode() const;
    bool incomingAuthIsPairing() const;
    QString incomingAuthFingerprint() const;
    QString incomingAuthSas() const;
    int incomingAuthRemaining() const;

    QString toastText() const { return m_toastText; }
    bool toastIsError() const { return m_toastIsError; }

public slots:
    void scan();
    void connectToDevice(const QString &host, int port, const QString &name, const QString &os);
    void connectManual(const QString &hostPort, const QString &token);
    void disconnectPeer();

    void refresh();
    void navigate(const QString &path);
    void goParent();
    void goRoot();

    void downloadPath(const QString &remotePath, const QString &name, double size);
    void downloadSelected();
    void uploadUrls(const QList<QUrl> &urls);
    void uploadPaths(const QStringList &paths);
    void makeDirectory(const QString &name);
    void deletePath(const QString &path, bool recursive);
    void deleteSelected();

    /** 没有 token 时的连接方式：请对端弹窗授权，同意后自动拿到 token 并连上。 */
    void requestAuthorization(const QString &host, int port, const QString &name, const QString &os);
    void cancelAuthorization();

    /** 别的设备来敲门，用户在本机点了「允许」/「拒绝」。 */
    void approveIncomingAuth();
    void denyIncomingAuth();

    void startServer();
    void stopServer();
    void restartServerIfRunning();

    /** 让本机在 60 秒内的发现应答里带上设备名（PROTOCOL.md §1.5）。 */
    void startPairingMode();
    void stopPairingMode();

    void copyToClipboard(const QString &text);
    void openLocalFolder(const QString &path);
    QString urlToLocalPath(const QUrl &url) const;
    void notify(const QString &text, bool isError = false);
    void clearLog();

signals:
    void scanningChanged();
    void peerChanged();
    void loadingChanged();
    void pathChanged();
    void serverChanged();
    void serverLogChanged();
    void pairingModeChanged();
    void pairUriChanged();
    void authChanged();
    void incomingAuthChanged();
    void toast();
    void listingFailed(const QString &error);

private:
    void setLoading(bool v);
    void applyServerContext();
    void fetchInfo();
    void appendLog(const QString &line);

    /** 接收方向的前提：本机服务没听着，手机就推不过来。已在跑则什么都不做。 */
    bool ensureServerRunning();

    void pollAuthorization();
    void finishAuthorization(const QString &status);
    /** 授权通过后把本机的地址和 token 回填给对端，一次配对打通两个方向。 */
    void pushPairBack();

    Config *m_config = nullptr;
    I18n *m_i18n = nullptr;
    Discovery *m_discovery = nullptr;
    DeviceModel *m_devices = nullptr;
    RemoteFileModel *m_files = nullptr;
    TransferModel *m_transfers = nullptr;
    PeerClient *m_client = nullptr;
    PeerStore *m_peers = nullptr;
    /** 本机长期身份。QObject 之外，所以用 unique_ptr 而不是父子关系托管。 */
    std::unique_ptr<afmu::Identity> m_identity;
    HttpServer *m_server = nullptr;
    AuthRequests *m_incomingAuth = nullptr;

    bool m_scanning = false;
    bool m_connected = false;
    bool m_loading = false;

    QString m_peerName;
    QString m_peerOs;
    bool m_peerWritable = true;
    QString m_peerInbox;
    QVariantList m_peerRoots;

    QString m_currentPath;
    QString m_parentPath;

    QStringList m_log;
    QString m_toastText;
    bool m_toastIsError = false;

    // 授权连接
    QTimer *m_authTimer = nullptr;
    /** 配对模式倒计时的界面刷新（每秒），不是它本身的到期定时器。 */
    QTimer *m_pairingTick = nullptr;
    // 只为了刷新来访请求弹窗上的倒计时
    QTimer *m_incomingAuthTimer = nullptr;
    bool m_authPending = false;
    QString m_authStatus;    // "" / sending / pending / granted / denied / expired / unsupported / failed
    QString m_authCode;      // 4 位确认码，两端同时显示
    QString m_authTarget;    // 显示用的「名字 · host:port」
    QString m_authRequestId; // 只有发起方知道，等于取结果的凭证
    QString m_authHost;
    QString m_authOs;
    int m_authPort = 0;
    int m_authRemaining = 0;

    // HttpServer 的传输 id → TransferModel 的任务 id / 方向
    QHash<qint64, qint64> m_serverIds;
    QHash<qint64, bool> m_serverInbound;
};
