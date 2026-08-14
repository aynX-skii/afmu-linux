#include "Models.h"

#include "Protocol.h"

#include <QJsonObject>
#include <QLocale>

#include <algorithm>

// ---------------------------------------------------------------- DeviceModel

DeviceModel::DeviceModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int DeviceModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant DeviceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};
    const DeviceInfo &d = m_items.at(index.row());
    switch (role) {
    case NameRole: return d.name;
    case OsRole: return d.os;
    case HostRole: return d.host;
    case PortRole: return d.port;
    case AddressRole: return QStringLiteral("%1:%2").arg(d.host).arg(d.port);
    case FingerprintRole: return d.fingerprint;
    case PairedRole: return !d.fingerprint.isEmpty();
    case HeardRole: return d.heard;
    default: return {};
    }
}

QHash<int, QByteArray> DeviceModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {OsRole, "os"},
        {HostRole, "host"},
        {PortRole, "port"},
        {AddressRole, "address"},
        {FingerprintRole, "fingerprint"},
        {PairedRole, "paired"},
        {HeardRole, "heard"},
    };
}

void DeviceModel::setAll(const QList<DeviceInfo> &items)
{
    if (m_items == items)
        return;

    // 行还是那几行、只是某一行的内容变了（最常见的一种：rid 认出了一台，那一行
    // 多出一把锁）—— 这时只发 dataChanged。整份 reset 会把 ListView 的滚动位置
    // 弹回顶部，而这种更新在用户正看着列表的时候随时会来。
    const bool sameRows = m_items.size() == items.size()
                          && std::equal(m_items.cbegin(), m_items.cend(), items.cbegin(),
                                        [](const DeviceInfo &a, const DeviceInfo &b) {
                                            return a.host == b.host && a.port == b.port;
                                        });
    if (sameRows) {
        for (int i = 0; i < items.size(); ++i) {
            if (m_items[i] == items[i])
                continue;
            m_items[i] = items[i];
            emit dataChanged(index(i), index(i));
        }
        return;
    }

    const int before = m_items.size();
    beginResetModel();
    m_items = items;
    endResetModel();
    if (m_items.size() != before)
        emit countChanged();
}

DeviceInfo DeviceModel::at(int row) const
{
    if (row < 0 || row >= m_items.size())
        return {};
    return m_items.at(row);
}

void afmu::upsertDevice(QList<DeviceInfo> &list, const DeviceInfo &d)
{
    for (DeviceInfo &it : list) {
        if (it.host == d.host && it.port == d.port) {
            const QString keepFp = d.fingerprint.isEmpty() ? it.fingerprint : d.fingerprint;
            it = d;
            it.fingerprint = keepFp;
            return;
        }
    }
    list.append(d);
}

bool afmu::removeDevice(QList<DeviceInfo> &list, const QString &host, int port)
{
    const auto sameAddress = [&host, port](const DeviceInfo &d) {
        return d.host == host && d.port == port;
    };
    const auto it = std::remove_if(list.begin(), list.end(), sameAddress);
    if (it == list.end())
        return false;
    list.erase(it, list.end());
    return true;
}

QList<DeviceInfo> afmu::mergeDevices(const QList<DeviceInfo> &heard,
                                     const QList<PeerRecord> &paired)
{
    const auto stillPaired = [&paired](const QString &fp) {
        return !fp.isEmpty()
               && std::any_of(paired.cbegin(), paired.cend(),
                              [&fp](const PeerRecord &r) { return r.fp == fp; });
    };

    QList<DeviceInfo> out;
    out.reserve(heard.size() + paired.size());
    for (DeviceInfo d : heard) {
        if (!stillPaired(d.fingerprint))
            d.fingerprint.clear(); // 见头文件：解除配对必须当场退回「不认识」
        d.heard = true;
        out.append(d);
    }

    for (const PeerRecord &r : paired) {
        // 没有可用地址就列不出来 —— 这条记录目前只能靠对方先来敲门。
        if (r.lastHost.isEmpty() || r.lastPort <= 0)
            continue;
        const bool listed = std::any_of(out.cbegin(), out.cend(), [&r](const DeviceInfo &d) {
            // **先按指纹判重。** 这台设备刚从一个新地址应答过的话，它已经在上面了；
            // 再按「上次已知地址」补一行，用户看到的就是同一台设备的两行，
            // 其中一行还指向一个已经没人应的地址。
            return (!d.fingerprint.isEmpty() && d.fingerprint == r.fp)
                   || (d.host == r.lastHost && d.port == r.lastPort);
        });
        if (listed)
            continue;
        // heard = false：这一行是**记得**，不是**听到**。界面据此把它和真的应答过的
        // 区分开 —— 不然「扫描完列表里有它」会被读成「它开着」，而这次它可能根本没开。
        out.append(DeviceInfo{r.name.isEmpty() ? r.lastHost : r.name,
                              r.os.isEmpty() ? QStringLiteral("unknown") : r.os, r.lastHost,
                              r.lastPort, r.fp, false});
    }
    return out;
}

// ---------------------------------------------------------------- RemoteFileModel

RemoteFileModel::RemoteFileModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int RemoteFileModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant RemoteFileModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};
    const RemoteEntry &e = m_items.at(index.row());
    switch (role) {
    case NameRole: return e.name;
    case PathRole: return e.path;
    case IsDirRole: return e.isDir;
    case SizeRole: return e.size;
    case SizeTextRole: return e.isDir ? QStringLiteral("—") : afmu::humanSize(e.size);
    case MTimeTextRole:
        if (e.mtime <= 0)
            return QStringLiteral("—");
        // mtime 是 Unix 秒，不是毫秒
        return QLocale().toString(QDateTime::fromSecsSinceEpoch(e.mtime),
                                  QStringLiteral("yyyy-MM-dd HH:mm"));
    case SelectedRole: return m_selected.value(index.row(), false);
    default: return {};
    }
}

QHash<int, QByteArray> RemoteFileModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {PathRole, "path"},
        {IsDirRole, "isDir"},
        {SizeRole, "size"},
        {SizeTextRole, "sizeText"},
        {MTimeTextRole, "mtimeText"},
        {SelectedRole, "selected"},
    };
}

void RemoteFileModel::clear()
{
    beginResetModel();
    m_items.clear();
    m_selected.clear();
    endResetModel();
    emit countChanged();
    emit selectionChanged();
}

void RemoteFileModel::setEntries(const QJsonArray &entries)
{
    beginResetModel();
    m_items.clear();
    m_selected.clear();
    for (const QJsonValue &v : entries) {
        const QJsonObject o = v.toObject();
        RemoteEntry e;
        e.name = o.value(QStringLiteral("name")).toString();
        e.path = o.value(QStringLiteral("path")).toString();
        e.isDir = o.value(QStringLiteral("dir")).toBool(false);
        e.size = qint64(o.value(QStringLiteral("size")).toDouble(0));
        e.mtime = qint64(o.value(QStringLiteral("mtime")).toDouble(0));
        if (e.name.isEmpty() && !e.path.isEmpty())
            e.name = e.path.section(QLatin1Char('/'), -1);
        m_items.append(e);
        m_selected.append(false);
    }
    endResetModel();
    emit countChanged();
    emit selectionChanged();
}

RemoteEntry RemoteFileModel::at(int row) const
{
    if (row < 0 || row >= m_items.size())
        return {};
    return m_items.at(row);
}

int RemoteFileModel::selectedCount() const
{
    int n = 0;
    for (bool b : m_selected)
        if (b)
            ++n;
    return n;
}

QList<RemoteEntry> RemoteFileModel::selectedEntries() const
{
    QList<RemoteEntry> out;
    for (int i = 0; i < m_items.size(); ++i)
        if (m_selected.value(i))
            out.append(m_items.at(i));
    return out;
}

void RemoteFileModel::setSelected(int row, bool on)
{
    if (row < 0 || row >= m_selected.size() || m_selected[row] == on)
        return;
    m_selected[row] = on;
    emit dataChanged(index(row), index(row), {SelectedRole});
    emit selectionChanged();
}

void RemoteFileModel::toggleSelected(int row)
{
    if (row < 0 || row >= m_selected.size())
        return;
    setSelected(row, !m_selected[row]);
}

void RemoteFileModel::selectAll(bool on)
{
    if (m_selected.isEmpty())
        return;
    for (int i = 0; i < m_selected.size(); ++i)
        m_selected[i] = on;
    emit dataChanged(index(0), index(m_selected.size() - 1), {SelectedRole});
    emit selectionChanged();
}

void RemoteFileModel::clearSelection()
{
    selectAll(false);
}
