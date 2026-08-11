#include "Models.h"

#include "Protocol.h"

#include <QJsonObject>
#include <QLocale>

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
    };
}

void DeviceModel::clear()
{
    if (m_items.isEmpty())
        return;
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();
}

void DeviceModel::upsert(const DeviceInfo &d)
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].host == d.host && m_items[i].port == d.port) {
            const QString keepFp = d.fingerprint.isEmpty() ? m_items[i].fingerprint : d.fingerprint;
            m_items[i] = d;
            // 认出来过一次就不再退回「不认识」：丢一个 rid 应答（丢包、跨时间窗）
            // 不代表这台设备不是它了，而界面上的「已配对」闪一下会很难看。
            m_items[i].fingerprint = keepFp;
            emit dataChanged(index(i), index(i));
            return;
        }
    }
    beginInsertRows({}, m_items.size(), m_items.size());
    m_items.append(d);
    endInsertRows();
    emit countChanged();
}

DeviceInfo DeviceModel::at(int row) const
{
    if (row < 0 || row >= m_items.size())
        return {};
    return m_items.at(row);
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
