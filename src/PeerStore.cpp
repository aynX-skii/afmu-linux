#include "PeerStore.h"

#include "Identity.h"
#include "JsonFile.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>

namespace {

constexpr int kFingerprintBytes = 32; // SHA-256

} // namespace

PeerStore::PeerStore(QObject *parent)
    : QAbstractListModel(parent)
{
}

// ---------------------------------------------------------------- 指纹

QString PeerStore::normalizeFingerprint(const QString &fp)
{
    const QByteArray raw = afmu::Identity::fromBase32(fp);
    if (raw.size() != kFingerprintBytes)
        return {};
    return afmu::Identity::toBase32(raw);
}

bool PeerStore::isValidFingerprint(const QString &fp)
{
    return afmu::Identity::fromBase32(fp).size() == kFingerprintBytes;
}

// ---------------------------------------------------------------- 编解码

QList<PeerRecord> PeerStore::decode(const QJsonArray &arr, int *dropped)
{
    QList<PeerRecord> out;
    int bad = 0;
    for (const QJsonValue &v : arr) {
        if (!v.isObject()) {
            ++bad;
            continue;
        }
        const QJsonObject o = v.toObject();
        PeerRecord r;
        // 指纹进表之前一律先规范化。存进一个「差不多但不是」的形式，
        // 症状是明明配过却永远匹配不上，而且看上去完全正常。
        r.fp = normalizeFingerprint(o.value(QStringLiteral("fp")).toString());
        if (r.fp.isEmpty()) {
            ++bad;
            continue;
        }
        r.name = o.value(QStringLiteral("name")).toString();
        r.os = o.value(QStringLiteral("os")).toString();
        r.lastHost = o.value(QStringLiteral("lastHost")).toString();
        r.lastPort = o.value(QStringLiteral("lastPort")).toInt();
        r.pairedAt = qint64(o.value(QStringLiteral("pairedAt")).toDouble());
        r.pinned = o.value(QStringLiteral("pinned")).toBool();

        // 同一个指纹出现两次：后一条覆盖前一条，而不是并排存着。
        // 表里有两条指向同一台设备的记录，删掉一条另一条还开着门。
        bool merged = false;
        for (PeerRecord &existing : out) {
            if (existing.fp == r.fp) {
                existing = r;
                merged = true;
                ++bad;
                break;
            }
        }
        if (!merged)
            out.append(r);
    }
    if (dropped)
        *dropped = bad;
    return out;
}

QJsonArray PeerStore::encode(const QList<PeerRecord> &items)
{
    QJsonArray arr;
    for (const PeerRecord &r : items) {
        QJsonObject o;
        o.insert(QStringLiteral("fp"), r.fp);
        o.insert(QStringLiteral("name"), r.name);
        o.insert(QStringLiteral("os"), r.os);
        o.insert(QStringLiteral("lastHost"), r.lastHost);
        o.insert(QStringLiteral("lastPort"), r.lastPort);
        o.insert(QStringLiteral("pairedAt"), double(r.pairedAt));
        o.insert(QStringLiteral("pinned"), r.pinned);
        arr.append(o);
    }
    return arr;
}

// ---------------------------------------------------------------- 落盘

void PeerStore::load(const QString &path)
{
    m_path = path;
    m_loadError.clear();
    m_readOnly = false;

    const afmu::JsonLoadResult r = afmu::loadJson(path, afmu::JsonShape::Array);
    m_loadError = r.error;
    m_readOnly = r.readOnlyFallback;

    int dropped = 0;
    QList<PeerRecord> items = decode(r.doc.array(), &dropped);
    if (dropped > 0) {
        // 丢掉的是指纹不合法或者重复的记录 —— 留着只会让人以为某台设备还配着。
        // 但不能一声不吭：原文件还在（loadJson 没动它），说清楚才有得查。
        const QString note =
            QStringLiteral("peers.json 里有 %1 条记录被忽略（指纹不合法或重复）").arg(dropped);
        m_loadError = m_loadError.isEmpty() ? note : m_loadError + QStringLiteral("；") + note;
    }

    beginResetModel();
    m_items = std::move(items);
    endResetModel();
    emit countChanged();
    emit changed();

    // 有记录被丢掉时**不**顺手回写一份「干净的」：那等于在用户没看到提示之前
    // 就把原始内容删了。要清理，等用户在界面上动手。
}

void PeerStore::save()
{
    if (m_path.isEmpty() || m_readOnly)
        return;
    afmu::saveJson(m_path, QJsonDocument(encode(m_items)));
}

// ---------------------------------------------------------------- 模型

int PeerStore::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant PeerStore::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};
    const PeerRecord &r = m_items.at(index.row());
    switch (role) {
    case FpRole: return r.fp;
    case FpDisplayRole: return afmu::Identity::group(r.fp);
    case NameRole: return r.name;
    case OsRole: return r.os;
    case LastHostRole: return r.lastHost;
    case LastPortRole: return r.lastPort;
    case LastAddressRole:
        return r.lastHost.isEmpty() ? QString()
                                    : QStringLiteral("%1:%2").arg(r.lastHost).arg(r.lastPort);
    case PairedAtRole: return double(r.pairedAt);
    case PairedAtTextRole:
        return r.pairedAt <= 0
            ? QString()
            : QLocale().toString(QDateTime::fromSecsSinceEpoch(r.pairedAt), QLocale::ShortFormat);
    case PinnedRole: return r.pinned;
    default: return {};
    }
}

QHash<int, QByteArray> PeerStore::roleNames() const
{
    return {
        {FpRole, "fp"},
        {FpDisplayRole, "fpDisplay"},
        {NameRole, "name"},
        {OsRole, "os"},
        {LastHostRole, "lastHost"},
        {LastPortRole, "lastPort"},
        {LastAddressRole, "lastAddress"},
        {PairedAtRole, "pairedAt"},
        {PairedAtTextRole, "pairedAtText"},
        {PinnedRole, "pinned"},
    };
}

// ---------------------------------------------------------------- 增删查改

int PeerStore::indexOf(const QString &fp) const
{
    const QString key = normalizeFingerprint(fp);
    if (key.isEmpty())
        return -1;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).fp == key)
            return i;
    }
    return -1;
}

bool PeerStore::isPaired(const QString &fp) const
{
    return indexOf(fp) >= 0;
}

bool PeerStore::isPinned(const QString &fp) const
{
    const int i = indexOf(fp);
    return i >= 0 && m_items.at(i).pinned;
}

PeerRecord PeerStore::find(const QString &fp) const
{
    const int i = indexOf(fp);
    return i >= 0 ? m_items.at(i) : PeerRecord{};
}

PeerRecord PeerStore::findByAddressHint(const QString &host, int port) const
{
    if (host.isEmpty())
        return {};
    for (const PeerRecord &r : m_items) {
        if (r.lastHost == host && (port <= 0 || r.lastPort == port))
            return r;
    }
    return {};
}

bool PeerStore::upsert(const PeerRecord &in)
{
    PeerRecord r = in;
    r.fp = normalizeFingerprint(r.fp);
    if (r.fp.isEmpty())
        return false;

    const int i = indexOf(r.fp);
    if (i >= 0) {
        // 认识的日子不能被重连刷掉；pinned 一旦置位也不该被一次普通更新抹掉 ——
        // 那正好是降级攻击想要的效果。
        r.pairedAt = m_items.at(i).pairedAt;
        r.pinned = r.pinned || m_items.at(i).pinned;
        m_items[i] = r;
        emit dataChanged(index(i), index(i));
        save();
        emit changed();
        return false;
    }

    if (r.pairedAt <= 0)
        r.pairedAt = QDateTime::currentSecsSinceEpoch();
    beginInsertRows({}, m_items.size(), m_items.size());
    m_items.append(r);
    endInsertRows();
    save();
    emit countChanged();
    emit changed();
    return true;
}

void PeerStore::noteSeen(const QString &fp, const QString &host, int port)
{
    const int i = indexOf(fp);
    if (i < 0)
        return; // 见到不等于信任
    if (m_items.at(i).lastHost == host && m_items.at(i).lastPort == port)
        return;
    m_items[i].lastHost = host;
    m_items[i].lastPort = port;
    emit dataChanged(index(i), index(i));
    save();
    emit changed();
}

bool PeerStore::setPinned(const QString &fp, bool on)
{
    const int i = indexOf(fp);
    if (i < 0 || m_items.at(i).pinned == on)
        return false;
    m_items[i].pinned = on;
    emit dataChanged(index(i), index(i));
    save();
    emit changed();
    return true;
}

bool PeerStore::remove(const QString &fp)
{
    return removeAt(indexOf(fp));
}

bool PeerStore::removeAt(int row)
{
    if (row < 0 || row >= m_items.size())
        return false;
    beginRemoveRows({}, row, row);
    m_items.removeAt(row);
    endRemoveRows();
    save();
    emit countChanged();
    emit changed();
    return true;
}
