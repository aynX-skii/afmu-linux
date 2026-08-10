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

// 这是唯一免鉴权的接口，局域网里谁都能调，而传进来的文本直接进弹窗和日志。
// 去掉控制字符并截断，免得有人用一个精心构造的设备名把按钮顶出对话框。
QString displayText(const QString &raw, int maxLen)
{
    QString out;
    out.reserve(qMin(raw.size(), maxLen));
    for (const QChar c : raw) {
        if (out.size() >= maxLen)
            break;
        if (c.isPrint())
            out.append(c);
    }
    return out.trimmed();
}

// 确认码由请求方生成、两端同时显示，接收方必须原样显示。
// 不是 4 位数字就说明对端在乱来，显示占位符而不是它给的东西。
QString confirmCode(const QString &raw)
{
    if (raw.size() != 4)
        return QStringLiteral("----");
    for (const QChar c : raw) {
        if (c < u'0' || c > u'9')
            return QStringLiteral("----");
    }
    return raw;
}

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
    r.code = confirmCode(code);
    r.name = displayText(name, 64);
    if (r.name.isEmpty())
        r.name = host;
    r.os = displayText(os, 16);
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
