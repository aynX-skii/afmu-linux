#include "PairSas.h"

#include "Identity.h"

#include <QCryptographicHash>

namespace afmu {

namespace {

constexpr int kFingerprintBytes = 32;
constexpr int kNonceBytes = 32;

} // namespace

QString computeSas(const QByteArray &fpA, const QByteArray &fpB, const QByteArray &nonceA,
                   const QByteArray &nonceB)
{
    if (fpA.size() != kFingerprintBytes || fpB.size() != kFingerprintBytes
        || nonceA.size() != kNonceBytes || nonceB.size() != kNonceBytes) {
        return {};
    }
    // 长度对但两个指纹相同，说明要么接到了自己，要么有一端搞错了 —— 两种情况
    // 都不该产出一个看起来正常的码让用户去"比对成功"。
    if (fpA == fpB)
        return {};

    // 排序而不是按角色拼：谁是客户端不该影响结果，否则两端各算各的，
    // 用户看到两个不同的码，只会以为"被攻击了"。
    const QByteArray &lo = fpA < fpB ? fpA : fpB;
    const QByteArray &hi = fpA < fpB ? fpB : fpA;

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArray(kSasContext));
    hash.addData(lo);
    hash.addData(hi);
    // 随机数**不排序**：n_a 和 n_b 的角色是固定的（发起方 / 接收方），
    // 排序会白白丢掉一半的绑定强度。
    hash.addData(nonceA);
    hash.addData(nonceB);

    return Identity::toBase32(hash.result()).left(kSasChars);
}

QString formatSas(const QString &sas)
{
    if (sas.size() != kSasChars)
        return sas;
    return sas.left(4) + QLatin1Char('-') + sas.mid(4);
}

} // namespace afmu
