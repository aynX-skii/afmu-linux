#include "AuthThrottle.h"

int AuthThrottle::retryAfterSec(const QString &host, qint64 nowMs)
{
    sweep(nowMs);
    auto it = m_entries.constFind(host);
    if (it == m_entries.cend())
        return 0;
    const qint64 left = it->blockedUntil - nowMs;
    if (left <= 0)
        return 0;
    // 向上取整：还剩 200ms 也得报 1 秒，报 0 等于告诉对端「现在就能再试」
    return int((left + 999) / 1000);
}

int AuthThrottle::noteFailure(const QString &host, qint64 nowMs)
{
    sweep(nowMs);
    Entry &e = m_entries[host];
    e.fails += 1;
    e.lastFail = nowMs;

    if (e.fails <= afmu::kAuthFailGrace)
        return 0;

    // 1, 2, 4, 8, 16, 32, 60, 60, …
    const int over = e.fails - afmu::kAuthFailGrace;
    int delay = afmu::kAuthBackoffMaxSec;
    if (over < 31) { // 1 << 31 会溢出，而 1 << 6 就已经超过上限了
        const int shifted = 1 << (over - 1);
        delay = qMin(shifted, afmu::kAuthBackoffMaxSec);
    }
    e.blockedUntil = nowMs + qint64(delay) * 1000;
    return delay;
}

void AuthThrottle::noteSuccess(const QString &host)
{
    m_entries.remove(host);
}

void AuthThrottle::sweep(qint64 nowMs)
{
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        // 只看最后一次失败：还在封禁中的条目一定也在遗忘窗口内
        if (nowMs - it->lastFail > qint64(afmu::kAuthFailForgetSec) * 1000)
            it = m_entries.erase(it);
        else
            ++it;
    }
}
