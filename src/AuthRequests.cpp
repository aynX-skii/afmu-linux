#include "AuthRequests.h"

#include "Identity.h"
#include "PairSas.h"

#include "Protocol.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QRandomGenerator>

namespace {

// 秒 → 毫秒。取值本身在生成的 ProtocolConstants.h 里，和 Android 端出自
// 同一份 docs/constants.json，这里只做单位换算，不再各写一份数字。
constexpr qint64 sec(int s) { return qint64(s) * 1000; }

constexpr qint64 kTimeoutMs = sec(afmu::kAuthTimeoutSec);
// 请求方一秒轮询一次；结果多留一会儿，最后一刻做的决定也能被取走
constexpr qint64 kResultRetentionMs = kTimeoutMs + sec(afmu::kAuthResultRetentionExtraSec);

// 单地址被拒后的冷却，按拒绝次数翻倍。固定 60 秒挡不住有耐心的人：
// 拒绝、等一分钟、再来，可以一直弹下去。
constexpr qint64 kDenyCooldownMs = sec(afmu::kDenyCooldownSec);
constexpr qint64 kDenyCooldownMaxMs = sec(afmu::kDenyCooldownMaxSec);

// 超时按软拒绝算：对方确实没做错什么，但「发了就挂机等超时」同样能一分钟弹一次，
// 所以给一个不升级的基础冷却。
constexpr qint64 kTimeoutCooldownMs = sec(afmu::kTimeoutCooldownSec);

// 全局冷却 —— A3 的重点。上面两条都是按地址算的，而局域网里换个地址是零成本的事，
// 于是单靠按地址冷却挡不住刷屏。这一条不看是谁：只要连续被拒/超时，**所有**地址都要等。
// 用户点一次「允许」就清零，所以正常使用完全感觉不到。
constexpr qint64 kGlobalCooldownMs = sec(afmu::kGlobalCooldownSec);
constexpr qint64 kGlobalCooldownMaxMs = sec(afmu::kGlobalCooldownMaxSec);

// 这么久没有新的拒绝就把计数忘掉，别让一次误操作留一整天
constexpr qint64 kRefusalForgetMs = sec(afmu::kRefusalForgetSec);

// n 次拒绝对应的冷却：base * 2^(n-1)，封顶 cap
qint64 backoffMs(int refusals, qint64 base, qint64 cap)
{
    if (refusals <= 1)
        return base;
    if (refusals >= 30)
        return cap;
    return qMin(base << (refusals - 1), cap);
}

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
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_globalUntil > now)
        return {}; // 刚有人被拒，所有地址一起等（挡换 IP 刷屏）
    if (m_blocked.value(host, 0) > now)
        return {}; // 这个地址刚被拒过，冷却中

    Request r;
    r.id = newId();
    r.code = confirmCode(code);
    r.name = displayText(name, 64);
    if (r.name.isEmpty())
        r.name = host;
    r.os = displayText(os, 16);
    r.host = host;
    r.port = port;
    r.createdAt = now;
    r.status = Status::Pending;

    m_pending = r;
    emit pendingChanged();
    return r;
}

AuthRequests::Request AuthRequests::createPairing(const QString &name, const QString &os,
                                                  const QString &host, const QString &peerFp,
                                                  const QByteArray &commit, int port)
{
    if (!m_enabled)
        return {};
    if (peerFp.isEmpty() || commit.size() != 32)
        return {};
    sweep();
    // v1 和 v2 共用这一个待决位置：分开算的话，两边各来一个就同时弹两个窗，
    // 「一次只受理一个」也就白写了。
    if (!m_pending.isNull())
        return {};
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_globalUntil > now)
        return {};
    if (m_blocked.value(host, 0) > now)
        return {};

    Request r;
    r.id = newId();
    r.name = displayText(name, 64);
    if (r.name.isEmpty())
        r.name = host;
    r.os = displayText(os, 16);
    r.host = host;
    r.port = port;
    r.createdAt = now;
    r.status = Status::Pending;
    r.peerFp = peerFp;
    r.commit = commit;
    // 本机的随机数在**收到 commit 之后**才生成，但对端此时还看不到它 ——
    // 顺序不重要，重要的是对端已经把自己的锁死了（§4.2.2）。
    r.nonceB.resize(32);
    QRandomGenerator::system()->generate(r.nonceB.begin(), r.nonceB.end());

    m_pending = r;
    emit pendingChanged();
    return r;
}

QString AuthRequests::revealPairing(const QString &id, const QByteArray &nonceA,
                                    const QByteArray &localFingerprint)
{
    sweep();
    if (id.isEmpty() || m_pending.isNull() || m_pending.id != id || !m_pending.isPairing())
        return {};
    if (nonceA.size() != 32)
        return {};

    // commit 对不上 = 对端在看到 n_b 之后换了 n_a，或者中间有人改了。
    // 整个 session 作废，不给重试 —— 允许重试等于允许它一直换 n_a 试下去，
    // commit 这一步就白做了。
    if (QCryptographicHash::hash(nonceA, QCryptographicHash::Sha256) != m_pending.commit) {
        m_pending = Request();
        emit pendingChanged();
        return {};
    }

    const QByteArray peerRaw = afmu::Identity::fromBase32(m_pending.peerFp);
    const QString sas = afmu::computeSas(peerRaw, localFingerprint, nonceA, m_pending.nonceB);
    if (sas.isEmpty()) {
        // 算不出来就没有可比对的东西。宁可整个作废，也不能弹一个没有码的窗
        // 让用户去点"允许"。
        m_pending = Request();
        emit pendingChanged();
        return {};
    }

    m_pending.nonceA = nonceA;
    m_pending.sas = sas;
    emit pendingChanged();
    return sas;
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
    if (granted) {
        // 用户点了「允许」，说明这一串请求是正常使用，不是骚扰：把账全清了。
        // 没有这一步，配对成功之后紧接着的第二次配对会被自己刚才的冷却挡住。
        m_denials.remove(settled.host);
        m_blocked.remove(settled.host);
        m_consecutiveRefusals = 0;
        m_globalUntil = 0;
    } else {
        noteRefusal(settled.host, /*escalate=*/true);
    }
    m_pending = {};
    emit pendingChanged();
}

void AuthRequests::noteRefusal(const QString &host, bool escalate)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (escalate) {
        const int n = (m_denials[host] += 1);
        m_blocked.insert(host, now + backoffMs(n, kDenyCooldownMs, kDenyCooldownMaxMs));
    } else {
        // 超时不升级计数，但仍要冷却：挂机等超时同样能一分钟弹一次
        m_blocked.insert(host, now + kTimeoutCooldownMs);
    }

    m_consecutiveRefusals += 1;
    m_lastRefusalAt = now;
    m_globalUntil = now + backoffMs(m_consecutiveRefusals, kGlobalCooldownMs, kGlobalCooldownMaxMs);
}

int AuthRequests::retryAfterSec(const QString &host) const
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 until = qMax(m_globalUntil, m_blocked.value(host, 0));
    const qint64 left = until - now;
    // 向上取整：还剩 200ms 也得报 1 秒，报 0 等于说「现在就能再来」
    return left <= 0 ? 0 : int((left + 999) / 1000);
}

void AuthRequests::clear()
{
    const bool had = !m_pending.isNull();
    m_pending = {};
    m_decided.clear();
    m_blocked.clear();
    m_denials.clear();
    m_globalUntil = 0;
    m_consecutiveRefusals = 0;
    m_lastRefusalAt = 0;
    if (had)
        emit pendingChanged();
}

void AuthRequests::sweep()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (!m_pending.isNull() && m_pending.expired(now)) {
        // 超时按软拒绝算：对方确实没做错什么（是用户没来得及看），所以不升级计数，
        // 但仍然要冷却 —— 否则「发一个然后挂机等超时」照样能一分钟弹一次。
        const QString host = m_pending.host;
        m_pending = {};
        noteRefusal(host, /*escalate=*/false);
        emit pendingChanged();
    }
    for (auto it = m_decided.begin(); it != m_decided.end();)
        it = (now - it->createdAt > kResultRetentionMs) ? m_decided.erase(it) : ++it;
    for (auto it = m_blocked.begin(); it != m_blocked.end();)
        it = (it.value() < now) ? m_blocked.erase(it) : ++it;

    // 安静够久就把升级计数忘掉，别让一次误操作留一整天
    if (m_consecutiveRefusals > 0 && now - m_lastRefusalAt > kRefusalForgetMs) {
        m_consecutiveRefusals = 0;
        m_denials.clear();
    }
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
