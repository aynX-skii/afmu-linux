#pragma once

#include "PeerStore.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QJsonArray>

// ---------------------------------------------------------------- 设备列表

struct DeviceInfo
{
    QString name;
    QString os;
    QString host;
    int port = 0;
    /**
     * 非空 = 这台在配对表里，靠发现应答的滚动 `rid` 认出来的（草案 §6.1），
     * 或者它本来就是从配对表里列出来的。
     * 界面拿它显示「已配对」，连接时也不用再猜该钉哪个指纹。
     *
     * **它必须跟着配对表一起失效。** 见 afmu::mergeDevices。
     */
    QString fingerprint;

    /**
     * 这一轮发现**真的听到**它应答了。
     *
     * 假 = 这一行是从配对表里列出来的，地址是上次见到它的那个，此刻它可能开着、
     * 也可能关着 —— 本机无从判断：没收到 UDP 应答，和设备关机，在这里长得一模一样。
     * 所以界面上必须把它和真的应答过的区分开，否则「列表里有」会被读成「它在线」。
     */
    bool heard = false;
};

inline bool operator==(const DeviceInfo &a, const DeviceInfo &b)
{
    return a.name == b.name && a.os == b.os && a.host == b.host && a.port == b.port
           && a.fingerprint == b.fingerprint && a.heard == b.heard;
}

class DeviceModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        OsRole,
        HostRole,
        PortRole,
        AddressRole,
        FingerprintRole,
        PairedRole,
        HeardRole,
    };
    explicit DeviceModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /**
     * 整份替换。这份列表是**算出来的**（发现结果 ∪ 配对表，见 afmu::mergeDevices），
     * 不是攒出来的 —— 往里增量添加的话，解除配对的设备永远不会离开它。
     * 内容没变就什么都不做，免得每收到一个发现应答就把整个列表重置一次。
     */
    void setAll(const QList<DeviceInfo> &items);
    DeviceInfo at(int row) const;

signals:
    void countChanged();

private:
    QList<DeviceInfo> m_items;
};

namespace afmu {

/**
 * 写入或更新一条，按 host:port 匹配。
 *
 * 认出来过一次就不再退回「不认识」：丢一个 rid 应答（丢包、跨时间窗）不代表
 * 这台设备不是它了，而界面上的「已配对」闪一下会很难看。所以传进来的空指纹
 * 不会覆盖已有的那个 —— 真正让指纹失效的是配对表，见 mergeDevices。
 */
void upsertDevice(QList<DeviceInfo> &list, const DeviceInfo &d);

/** 按 host:port 摘掉一条。返回是否真的摘掉了。见 AppController::forgetDevice。 */
bool removeDevice(QList<DeviceInfo> &list, const QString &host, int port);

/**
 * 界面上那份设备列表 = 这次发现听到的 ∪ 配对表里的。
 *
 * 两条都不能少：
 *
 * - **配对表这一半**：已配对的设备就是能发送的设备，广播答没答上来都一样。
 *   广播答不上来一点都不少见 —— Windows 防火墙默认拦掉发往桌面客户端的入站
 *   UDP，AP 隔离会吃掉广播，手机息屏时自己也答得很慢甚至不答。少了这一半，
 *   症状是「配对成功、明明列在配对表里，一点扫描就从设备列表里消失了」。
 * - **发现这一半优先**：活的应答说的是它**现在**在哪，配对表只说上次见到它在哪。
 *
 * 指纹在这里失效：表里已经没有的指纹当场清掉。指纹是发现那一刻认出来的，
 * 它自己不会过期，而界面上非空指纹就等于「已配对」—— 留着它，刚解除配对的
 * 那一行会继续挂着锁、继续藏起「加密配对」按钮，直到下一次扫描才纠正。
 */
QList<DeviceInfo> mergeDevices(const QList<DeviceInfo> &heard, const QList<PeerRecord> &paired);

} // namespace afmu

// ---------------------------------------------------------------- 远端目录

struct RemoteEntry
{
    QString name;
    QString path;
    bool isDir = false;
    qint64 size = 0;
    qint64 mtime = 0;
};

class RemoteFileModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        PathRole,
        IsDirRole,
        SizeRole,
        SizeTextRole,
        MTimeTextRole,
        SelectedRole,
    };
    explicit RemoteFileModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // entries 为 /api/list 返回的数组（根列表时对端也用 entries 返回 roots）
    void setEntries(const QJsonArray &entries);
    void clear();

    RemoteEntry at(int row) const;
    int selectedCount() const;
    QList<RemoteEntry> selectedEntries() const;

    Q_INVOKABLE void toggleSelected(int row);
    Q_INVOKABLE void setSelected(int row, bool on);
    Q_INVOKABLE void selectAll(bool on);
    Q_INVOKABLE void clearSelection();

signals:
    void countChanged();
    void selectionChanged();

private:
    QList<RemoteEntry> m_items;
    QList<bool> m_selected;
};
