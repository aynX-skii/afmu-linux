#pragma once

#include <QAbstractListModel>
#include <QJsonArray>
#include <QList>
#include <QString>

/**
 * 一条配对关系（PROTOCOL.md v2 §4.3）。
 *
 * **fp 是身份，别的字段都不是。** 名字会被对端改，IP 会被 DHCP 换，
 * 只有指纹换了才等于换了设备。所有查找、去重、钉扎一律按 fp 来。
 */
struct PeerRecord
{
    /** base32 指纹，规范化后 52 个字符。见 PeerStore::normalizeFingerprint。 */
    QString fp;
    QString name;
    QString os;

    /**
     * 上次见到它的地址。**只是重连提示，不参与身份判定**（PROTOCOL.md v2 §13 问题 3）。
     * 多网卡的 PC 换个 IP 是常事，指纹没变就还是同一台机器。
     */
    QString lastHost;
    int lastPort = 0;

    /** 首次配对的 Unix 秒。重连不会刷新它 —— 用户想知道的是「什么时候认识的」。 */
    qint64 pairedAt = 0;

    /**
     * 这个对端只走 v2，TLS 握不上就是失败，绝不回退明文（草案 §8.1 第 1 条）。
     * 这是降级攻击的唯一防线，所以它由「首次 v2 连接成功」置位，不由用户随手勾。
     */
    bool pinned = false;
};

/**
 * 配对表：指纹 → 对端。落盘在 config.json 旁边的 peers.json，权限 0600。
 *
 * v2 里「已配对」就是全部的授权依据：表里没有的指纹连 TLS 都握不上，
 * 所以这张表既是数据也是访问控制列表 —— 往里写一条等于开一道门。
 *
 * 有意不做的两件事（PROTOCOL.md v2 §13 问题 2）：不限条数，不自动过期。
 * 自动删掉一台半年没用的设备，下次用要重新配对，而用户只会觉得「怎么又要配一遍」。
 * 要清理只有界面上手动删。
 */
class PeerStore : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString loadError READ loadError NOTIFY changed)

public:
    enum Roles {
        FpRole = Qt::UserRole + 1,
        FpDisplayRole, // 分组后的展示形式，用户拿它跟对端屏幕上的比
        NameRole,
        OsRole,
        LastHostRole,
        LastPortRole,
        LastAddressRole,
        PairedAtRole,
        PairedAtTextRole,
        PinnedRole,
    };
    explicit PeerStore(QObject *parent = nullptr);

    /** 载入并记住路径；之后每次改动都写回这里。可以重复调用（换配置目录）。 */
    void load(const QString &path);
    QString path() const { return m_path; }

    /** 读不出来时的原因，空表示一切正常。界面和日志用。 */
    QString loadError() const { return m_loadError; }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /** 这个指纹在不在表里 —— TLS 钉扎唯一该问的问题。 */
    Q_INVOKABLE bool isPaired(const QString &fp) const;
    /** 已配对且只许走 v2。没配对过的返回假。 */
    Q_INVOKABLE bool isPinned(const QString &fp) const;

    PeerRecord find(const QString &fp) const;
    QList<PeerRecord> all() const { return m_items; }

    /**
     * 按地址提示找一条记录，用来决定「连这个地址该钉哪个指纹」。
     *
     * **这不是身份判定。** 地址只用来挑候选：挑错了的后果是握手时指纹对不上、
     * 连接被拒 —— 失败方向是安全的。绝不能反过来用成「地址对上了就信任」。
     *
     * 同一个地址理论上可能对应多条（换过设备但没删旧记录），返回第一条；
     * 真连上的若是另一台，钉扎会拒绝，用户会看到明确的报错而不是被静默接受。
     */
    PeerRecord findByAddressHint(const QString &host, int port) const;

    /**
     * 写入或更新一条，按 fp 匹配。返回是否是新增的一条（即真的多开了一道门）。
     *
     * 已存在时保留原来的 pairedAt：那是「什么时候认识的」，不是「上次见面」。
     * fp 不合法一律拒绝 —— 存进去的话，将来比对时永远匹配不上，
     * 表现是「明明配过了却连不上」。
     */
    bool upsert(const PeerRecord &r);

    /**
     * 只更新重连提示，不动身份也不动 pairedAt。表里没有这个指纹就什么都不做 ——
     * 见到一台设备不等于信任它，配对是另一条路径上的显式动作。
     */
    void noteSeen(const QString &fp, const QString &host, int port);

    /** 首次 v2 连接成功后置位，从此这个对端不再允许明文回退（草案 §8.1）。 */
    bool setPinned(const QString &fp, bool on);

    Q_INVOKABLE bool remove(const QString &fp);
    Q_INVOKABLE bool removeAt(int row);

    /** 指纹合法 = base32 能解回**正好 32 字节**（SHA-256 的长度）。 */
    static bool isValidFingerprint(const QString &fp);

    /**
     * 去掉分组空格/连字符、转大写，并把末尾那半组的填充位归零。
     *
     * 最后一点值得说明：52 个 base32 字符是 260 bit，而指纹只有 256 bit，
     * 末字符低 4 bit 是填充。手抄或者别的实现可能把它们填成非零，
     * 于是同一个指纹会有 16 种写法。解码再编码一遍就都收敛到同一个形式 ——
     * 不然表里会出现两条指向同一台设备的记录，删掉一条另一条还在开着门。
     */
    static QString normalizeFingerprint(const QString &fp);

    /** JSON ↔ 记录。dropped 回填被丢掉的条数（字段缺失或指纹不合法）。 */
    static QList<PeerRecord> decode(const QJsonArray &arr, int *dropped = nullptr);
    static QJsonArray encode(const QList<PeerRecord> &items);

signals:
    void countChanged();
    void changed();

private:
    int indexOf(const QString &fp) const;
    void save();

    QString m_path;
    QString m_loadError;
    /** 见 JsonFile：备份失败过，原文件里可能还有能救回来的配对关系，绝不覆盖。 */
    bool m_readOnly = false;
    QList<PeerRecord> m_items;
};
