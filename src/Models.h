#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QJsonArray>

// ---------------------------------------------------------------- 设备列表

struct DeviceInfo
{
    QString name;
    QString os;
    QString host;
    int port = 0;
};

class DeviceModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        OsRole,
        HostRole,
        PortRole,
        AddressRole,
    };
    explicit DeviceModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void clear();
    void upsert(const DeviceInfo &d);
    DeviceInfo at(int row) const;

signals:
    void countChanged();

private:
    QList<DeviceInfo> m_items;
};

// ---------------------------------------------------------------- 远端目录

struct RemoteEntry
{
    QString name;
    QString path;
    bool isDir = false;
    qint64 size = 0;
    qint64 mtime = 0;
};

class RemoteFileModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        PathRole,
        IsDirRole,
        SizeRole,
        SizeTextRole,
        MTimeTextRole,
        SelectedRole,
    };
    explicit RemoteFileModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // entries 为 /api/list 返回的数组（根列表时对端也用 entries 返回 roots）
    void setEntries(const QJsonArray &entries);
    void clear();

    RemoteEntry at(int row) const;
    int selectedCount() const;
    QList<RemoteEntry> selectedEntries() const;

    Q_INVOKABLE void toggleSelected(int row);
    Q_INVOKABLE void setSelected(int row, bool on);
    Q_INVOKABLE void selectAll(bool on);
    Q_INVOKABLE void clearSelection();

signals:
    void countChanged();
    void selectionChanged();

private:
    QList<RemoteEntry> m_items;
    QList<bool> m_selected;
};
