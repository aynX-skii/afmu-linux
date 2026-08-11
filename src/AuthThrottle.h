#pragma once

#include <QHash>
#include <QString>

/**
 * token 校验失败的按来源 IP 退避（PROTOCOL.md §2.2）。
 *
 * v1 的 token 只有 10 位、字母表 31 个字符 —— 熵大约 49 bit，本身够用，
 * 但**前提是猜错要付出代价**。实测在没有这一层时，同一个 IP 连打 200 次
 * 错误 token 只花 0.78 秒且毫无惩罚，等于把爆破成本降到只剩带宽。
 *
 * 惩罚是**立即回 429**，不是 sleep：服务端跑在单个事件循环里，
 * 睡一下等于全站停摆，而那正是攻击者想要的。
 *
 * 参数必须和 Android 端 Throttle.kt 完全一致，否则两端在同一个网络里
 * 表现不同，排查时会以为是网络问题。
 */
namespace afmu {

// 前 N 次失败不惩罚：token 是手抄的，打错很正常
inline constexpr int kAuthFailGrace = 5;
// 退避上限，避免一次误操作把自己锁死太久
inline constexpr int kAuthBackoffMaxSec = 60;
// 这么久没有新的失败就把记录忘掉
inline constexpr qint64 kAuthFailForgetMs = 15 * 60 * 1000;

} // namespace afmu

class AuthThrottle
{
public:
    /** 返回还要等几秒；0 表示放行。放行时**不要**在这里计数。 */
    int retryAfterSec(const QString &host, qint64 nowMs);

    /** token 比对失败之后调用。返回本次失败导致的退避秒数（0 = 仍在宽限期内）。 */
    int noteFailure(const QString &host, qint64 nowMs);

    /** 校验通过：清掉该地址的账。 */
    void noteSuccess(const QString &host);

    void clear() { m_entries.clear(); }

private:
    struct Entry
    {
        int fails = 0;
        qint64 blockedUntil = 0;
        qint64 lastFail = 0;
    };

    void sweep(qint64 nowMs);

    QHash<QString, Entry> m_entries;
};
