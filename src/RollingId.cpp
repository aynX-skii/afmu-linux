#include "RollingId.h"

#include <QCryptographicHash>

namespace afmu {

namespace {

constexpr int kFingerprintBytes = 32;

} // namespace

QString rollingId(const QByteArray &fp, qint64 unixSeconds)
{
    if (fp.size() != kFingerprintBytes)
        return {};

    // 负的时间戳（时钟没设好、或者传了个错的值）会让窗口号带上负号，
    // 两端一旦有一端是这种状态就永远对不上。直接判为算不出来。
    if (unixSeconds < 0)
        return {};

    const qint64 window = unixSeconds / kRidWindowSec;

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArray(kRidContext));
    hash.addData(fp);
    hash.addData(QByteArray::number(window));

    return QString::fromLatin1(hash.result().left(kRidBytes).toHex());
}

bool ridMatches(const QByteArray &fp, const QString &rid, qint64 unixSeconds)
{
    if (rid.isEmpty())
        return false;

    // 大小写不敏感：hex 两种写法都合法，因为这个而认不出自己配过的设备太蠢了。
    const QString want = rid.toLower();

    for (qint64 back = 0; back <= 1; ++back) {
        const QString mine = rollingId(fp, unixSeconds - back * kRidWindowSec);
        if (mine.isEmpty())
            return false; // 指纹本身不合法，换个窗口也一样
        if (mine == want)
            return true;
    }
    return false;
}

} // namespace afmu
