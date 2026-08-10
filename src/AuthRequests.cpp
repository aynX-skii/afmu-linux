#include "AuthRequests.h"

#include "Protocol.h"

#include <QDateTime>
#include <QRandomGenerator>

namespace {

// 和 Android 端 AuthRequests 的常量一一对应，两边必须一致
constexpr qint64 kTimeoutMs = qint64(afmu::kAuthTimeoutSec) * 1000;
// 请求方一秒轮询一次；结果多留一会儿，最后一刻做的决定也能被取走
constexpr qint64 kResultRetentionMs = kTimeoutMs + 30 * 1000;
constexpr qint64 kDenyCooldownMs = 60 * 1000;

} // namespace

bool AuthRequests::Request::expired(qint64 now) const
{
    return now - createdAt > kTimeoutMs;
}

int AuthRequests::Request::remainingSec(qint64 now) const
{
    if (isNull())
        return 0;
    const qint64 left = kTimeoutMs - (now - createdAt);
    return left > 0 ? int((left + 999) / 1000) : 0;
}

AuthRequests::AuthRequests(QObject *parent)
    : QObject(parent)
{
}

void AuthRequests::setEnabled(bool v)
{
    if (m_enabled == v)
        return;
    m_enabled = v;
    if (!v)
        clear();
}

AuthRequests::Request AuthRequests::create(const QString &name, const QString &os,
                                           const QString &host, int port, const QString &code)
{
    if (!m_enabled)
        return {};
    sweep();
    if (!m_pending.isNull())
        return {}; // 一次只受理一个
    if (m_blocked.value(host, 0) > QDateTime::currentMSecsSinceEpoch())
        return {}; // 刚被拒过，冷却中

    Request r;
    r.id = newId();
    r.code = code.isEmpty() ? QStringLiteral("----") : code;
    r.name = name.isEmpty() ? host : name;
    r.os = os;
    r.host = host;
    r.port = port;
    r.createdAt = QDateTime::currentMSecsSinceEpoch();
    r.status = Status::Pending;

    m_pending = r;
    emit pendingChanged();
    return r;
}

AuthRequests::Request AuthRequests::lookup(const QString &id)
{
    if (id.isEmpty())
        return {};
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // 先读再清：刚好卡在超时点上的请求要回「过期」，而不是「查无此事」
    Request answer;
    if (!m_pending.isNull() && m_pending.id == id) {
        answer = m_pending;
        if (answer.expired(now))
            answer.status = Status::Expired;
    } else {
        answer = m_decided.value(id);
    }
    sweep();
    return answer;
}

void AuthRequests::decide(const QString &id, bool granted)
{
    if (m_pending.isNull() || m_pending.id != id)
        return;
    Request settled = m_pending;
    settled.status = granted ? Status::Granted : Status::Denied;
    m_decided.insert(id, settled);
    if (!granted)
        m_blocked.insert(settled.host, QDateTime::currentMSecsSinceEpoch() + kDenyCooldownMs);
    m_pending = {};
    emit pendingChanged();
}

void AuthRequests::clear()
{
    const bool had = !m_pending.isNull();
    m_pending = {};
    m_decided.clear();
    m_blocked.clear();
    if (had)
        emit pendingChanged();
}

void AuthRequests::sweep()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (!m_pending.isNull() && m_pending.expired(now)) {
        // 超时按拒绝算，但不进冷却：对方没做错什么，是用户没来得及看
        m_pending = {};
        emit pendingChanged();
    }
    for (auto it = m_decided.begin(); it != m_decided.end();)
        it = (now - it->createdAt > kResultRetentionMs) ? m_decided.erase(it) : ++it;
    for (auto it = m_blocked.begin(); it != m_blocked.end();)
        it = (it.value() < now) ? m_blocked.erase(it) : ++it;
}

QString AuthRequests::newId()
{
    // 128 位，取结果的唯一凭证，不能可预测
    quint32 words[4];
    QRandomGenerator::system()->fillRange(words);
    QString out;
    out.reserve(32);
    for (quint32 w : words)
        out += QStringLiteral("%1").arg(w, 8, 16, QLatin1Char('0'));
    return out;
}
