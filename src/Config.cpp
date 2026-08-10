#include "Config.h"

#include "Protocol.h"

#include <QDir>
#include <QFile>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

Config::Config(QObject *parent)
    : QObject(parent)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    m_path = QDir(base).filePath(QStringLiteral("afmu/config.json"));
    load();
}

QString Config::configFilePath() const
{
    return m_path;
}

void Config::load()
{
    QFile f(m_path);
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isObject())
            m_json = doc.object();
    }

    bool dirty = false;
    auto ensure = [&](const QString &key, const QJsonValue &def) {
        if (!m_json.contains(key) || m_json.value(key).isNull()) {
            m_json.insert(key, def);
            dirty = true;
        }
    };

    const QString home = QDir::homePath();
    const QString downloads =
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation).isEmpty()
            ? QDir(home).filePath(QStringLiteral("Downloads"))
            : QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);

    ensure(QStringLiteral("deviceName"), QHostInfo::localHostName());
    ensure(QStringLiteral("localToken"), afmu::makeToken());
    ensure(QStringLiteral("peerToken"), QString());
    ensure(QStringLiteral("downloadDir"), QDir(downloads).filePath(QStringLiteral("FileBridge")));
    ensure(QStringLiteral("inboxDir"), QDir(downloads).filePath(QStringLiteral("FileBridge")));
    ensure(QStringLiteral("serverPort"), int(afmu::kDefaultHttpPort));
    ensure(QStringLiteral("discoverable"), true);
    ensure(QStringLiteral("readOnly"), false);
    ensure(QStringLiteral("autoStartServer"), false);
    // 默认开着：没有 token 的设备靠它来敲门，关掉之后只剩手抄 token / 扫码两条路
    ensure(QStringLiteral("allowAuthRequests"), true);
    ensure(QStringLiteral("discoverTimeoutMs"), 1500);
    ensure(QStringLiteral("lastHost"), QString());
    ensure(QStringLiteral("lastPort"), int(afmu::kDefaultHttpPort));
    // "system" = 跟随系统语言；用户改过之后存 "zh" / "en"
    ensure(QStringLiteral("language"), QStringLiteral("system"));
    if (!m_json.contains(QStringLiteral("serveRoots"))
        || !m_json.value(QStringLiteral("serveRoots")).isArray()
        || m_json.value(QStringLiteral("serveRoots")).toArray().isEmpty()) {
        // 默认只共享收件箱。整个 $HOME 可读可写可删的默认值太宽，
        // 需要更大范围由用户在「接收服务」页显式添加。
        QJsonArray arr;
        arr.append(inboxDir());
        m_json.insert(QStringLiteral("serveRoots"), arr);
        dirty = true;
    }

    if (dirty)
        save();
}

void Config::save()
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QJsonDocument(m_json).toJson(QJsonDocument::Indented));
    if (f.commit())
        QFile::setPermissions(m_path, QFile::ReadOwner | QFile::WriteOwner);
}

void Config::setValue(const QString &key, const QJsonValue &v)
{
    if (m_json.value(key) == v)
        return;
    m_json.insert(key, v);
    save();
    emit changed();
}

QString Config::deviceName() const { return m_json.value(QStringLiteral("deviceName")).toString(); }
QString Config::localToken() const { return m_json.value(QStringLiteral("localToken")).toString(); }
QString Config::peerToken() const { return m_json.value(QStringLiteral("peerToken")).toString(); }
QString Config::downloadDir() const { return m_json.value(QStringLiteral("downloadDir")).toString(); }
QString Config::inboxDir() const { return m_json.value(QStringLiteral("inboxDir")).toString(); }
int Config::serverPort() const { return m_json.value(QStringLiteral("serverPort")).toInt(afmu::kDefaultHttpPort); }
bool Config::discoverable() const { return m_json.value(QStringLiteral("discoverable")).toBool(true); }
bool Config::readOnly() const { return m_json.value(QStringLiteral("readOnly")).toBool(false); }
bool Config::autoStartServer() const { return m_json.value(QStringLiteral("autoStartServer")).toBool(false); }
bool Config::allowAuthRequests() const { return m_json.value(QStringLiteral("allowAuthRequests")).toBool(true); }
int Config::discoverTimeoutMs() const { return m_json.value(QStringLiteral("discoverTimeoutMs")).toInt(1500); }
QString Config::lastHost() const { return m_json.value(QStringLiteral("lastHost")).toString(); }
int Config::lastPort() const { return m_json.value(QStringLiteral("lastPort")).toInt(afmu::kDefaultHttpPort); }
QString Config::language() const { return m_json.value(QStringLiteral("language")).toString(QStringLiteral("system")); }

QStringList Config::serveRoots() const
{
    QStringList out;
    const QJsonArray arr = m_json.value(QStringLiteral("serveRoots")).toArray();
    for (const QJsonValue &v : arr) {
        const QString s = v.toString();
        if (!s.isEmpty())
            out << s;
    }
    return out;
}

void Config::setDeviceName(const QString &v) { setValue(QStringLiteral("deviceName"), v); }
void Config::setLocalToken(const QString &v) { setValue(QStringLiteral("localToken"), v); }
void Config::setPeerToken(const QString &v) { setValue(QStringLiteral("peerToken"), v); }
void Config::setDownloadDir(const QString &v) { setValue(QStringLiteral("downloadDir"), v); }
void Config::setInboxDir(const QString &v) { setValue(QStringLiteral("inboxDir"), v); }
void Config::setServerPort(int v) { setValue(QStringLiteral("serverPort"), v); }
void Config::setDiscoverable(bool v) { setValue(QStringLiteral("discoverable"), v); }
void Config::setReadOnly(bool v) { setValue(QStringLiteral("readOnly"), v); }
void Config::setAutoStartServer(bool v) { setValue(QStringLiteral("autoStartServer"), v); }
void Config::setAllowAuthRequests(bool v) { setValue(QStringLiteral("allowAuthRequests"), v); }
void Config::setDiscoverTimeoutMs(int v) { setValue(QStringLiteral("discoverTimeoutMs"), qBound(300, v, 10000)); }
void Config::setLastHost(const QString &v) { setValue(QStringLiteral("lastHost"), v); }
void Config::setLastPort(int v) { setValue(QStringLiteral("lastPort"), v); }
void Config::setLanguage(const QString &v) { setValue(QStringLiteral("language"), v); }

void Config::setServeRoots(const QStringList &v)
{
    QJsonArray arr;
    for (const QString &s : v)
        arr.append(s);
    setValue(QStringLiteral("serveRoots"), arr);
}

void Config::addServeRoot(const QString &path)
{
    if (path.isEmpty())
        return;
    QStringList roots = serveRoots();
    const QString clean = QDir::cleanPath(path);
    if (roots.contains(clean))
        return;
    roots << clean;
    setServeRoots(roots);
}

void Config::removeServeRoot(const QString &path)
{
    QStringList roots = serveRoots();
    if (roots.removeAll(QDir::cleanPath(path)) > 0)
        setServeRoots(roots);
}

QString Config::regenerateLocalToken()
{
    const QString t = afmu::makeToken();
    setLocalToken(t);
    return t;
}
