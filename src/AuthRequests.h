#pragma once

#include <QHash>
#include <QObject>
#include <QString>

/**
 * 「有设备想连接本机」的待决请求（PROTOCOL.md §3.8）。
 *
 * 本机作为**被请求方**时用这个类。对端手上还没有 token，就来敲 /api/authorize；
 * 本机弹窗，用户点「允许」之后 token 才交出去。
 *
 * 这是整套协议里唯一免鉴权的接口，所有防滥用的约束都落在这里：
 *
 *  - 同一时刻只留一个待决请求，多的一律拒掉，免得被刷一屏弹窗；
 *  - 取结果的 id 是 128 位随机数，只发给请求方，它是拿到 token 的唯一凭证；
 *  - 用户拒绝之后该地址进冷却，堵死「一直弹到用户点错为止」；
 *  - 超时按拒绝算，时限由本机说了算，不听请求方的；
 *  - 有一个能彻底关掉这个接口的开关。
 *
 * 服务端是单线程 Qt 事件循环（HttpServer 每条连接一个 QObject，不开线程），
 * 所以这里不需要加锁。
 */
class AuthRequests : public QObject
{
    Q_OBJECT

public:
    enum class Status { Pending, Granted, Denied, Expired };

    struct Request
    {
        QString id;
        /** 4 位确认码，请求方生成、两端同时显示，用户靠它分辨弹的是不是自己那一下。 */
        QString code;
        QString name;
        QString os;
        QString host;
        int port = 0;
        qint64 createdAt = 0;
        Status status = Status::Pending;

        // ---- v2 配对（PROTOCOL.md v2 §4.2.3）。v1 请求里这几项都是空的。

        /** 对端的 SPKI 指纹，取自 TLS 握手 —— **不是**请求里自己报的。非空即 v2。 */
        QString peerFp;
        /** 对端在第 1 步交的 `SHA-256(n_a)`，第 2 步用来验它没在看到 n_b 之后改主意。 */
        QByteArray commit;
        QByteArray nonceA;
        QByteArray nonceB;
        /** 两端各自算出来的 8 字符码，用户比对的就是它。第 2 步之后才有值。 */
        QString sas;

        bool isPairing() const { return !peerFp.isEmpty(); }

        bool isNull() const { return id.isEmpty(); }
        bool expired(qint64 now) const;
        /** 还剩几秒，给界面倒计时用。 */
        int remainingSec(qint64 now) const;
    };

    explicit AuthRequests(QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    /** 关掉时连带清空待决请求：开关关了还留着一个弹窗没有意义。 */
    void setEnabled(bool v);

    /** 登记一个请求。已有请求在等 / 该地址在冷却 / 全局冷却中 / 开关关着 → 返回空的 Request。 */
    Request create(const QString &name, const QString &os, const QString &host, int port,
                   const QString &code);

    /**
     * v2 配对的第 1 步（§4.2.3）。防滥用走的是**同一套**：同一时刻只留一个待决请求，
     * v1 和 v2 共用这一个位置 —— 否则混着来就能绕开"一次只弹一个"。
     *
     * `peerFp` 必须来自 TLS 握手，绝不能取请求体里对端自报的值。
     */
    Request createPairing(const QString &name, const QString &os, const QString &host,
                          const QString &peerFp, const QByteArray &commit, int port = 0);

    /**
     * v2 配对的第 2 步：对端揭示 `n_a`。
     *
     * 校验 `SHA-256(n_a) == commit`，不等则**作废整个 session** 并返回空 —— 不给重试，
     * 重试等于允许它换一个 n_a 再试一次，commit 就白做了。
     * 通过则算出 SAS 并返回（同时存进待决请求，界面显示同一个值）。
     */
    QString revealPairing(const QString &id, const QByteArray &nonceA,
                          const QByteArray &localFingerprint);

    /**
     * create() 返回空之后问「还要等几秒」，给 429 的 Retry-After 用。
     * 0 表示不是冷却导致的（开关关着，或已有一个待决请求在等用户）。
     */
    int retryAfterSec(const QString &host) const;

    /** 按 id 查结果。只有请求方知道 id。查不到 → 空的 Request（对应 404）。 */
    Request lookup(const QString &id);

    void decide(const QString &id, bool granted);

    /** 服务端停掉时调用：没人能再来取结果了，留着待决请求只会挂在界面上。 */
    void clear();

    /**
     * 主动清一次过期的东西。平时靠上面几个入口顺手清理就够了，但没人来敲门的那 60 秒里
     * 一个入口都不会被调到 —— 界面的倒计时定时器负责在归零时叫一下。
     */
    void sweepExpired() { sweep(); }

    Request pending() const { return m_pending; }

signals:
    void pendingChanged();

private:
    /** 清掉过期的东西。每个入口都调一次，省掉一个只为了清理而存在的定时器。 */
    void sweep();
    static QString newId();

    /** 一次拒绝/超时之后记账：该地址进冷却，全局也进冷却（PROTOCOL.md §3.8）。 */
    void noteRefusal(const QString &host, bool escalate);

    bool m_enabled = true;
    Request m_pending;
    QHash<QString, Request> m_decided;
    QHash<QString, qint64> m_blocked;

    /** 每个地址被拒过几次，决定它的冷却翻几倍。 */
    QHash<QString, int> m_denials;

    // 全局冷却：按地址的那套挡不住换 IP，这一条不看是谁
    qint64 m_globalUntil = 0;
    int m_consecutiveRefusals = 0;
    qint64 m_lastRefusalAt = 0;
};
