#include "Config.h"

#include "JsonFile.h"
#include "Protocol.h"

#include <QDir>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>

Config::Config(QObject *parent)
    : QObject(parent)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    m_path = QDir(base).filePath(QStringLiteral("afmu/config.json"));
    load();
}

namespace {
/** §8.2 第 3 阶段的迁移标记。见 Config::load 末尾。 */
const QString kStage3Key = QStringLiteral("plaintextStage3");
} // namespace

QString Config::configFilePath() const
{
    return m_path;
}

void Config::load()
{
    // 「文件不存在」和「文件在但读不出来」必须分开处理。
    //
    // 以前两种情况都会落到「m_json 为空 → 所有 ensure() 填默认值 → save() 覆盖」，
    // 于是一次崩溃时写了一半的文件、一次磁盘满、或者手改配置时打错一个逗号，
    // 都会让用户的 token、serveRoots、设备名**当场消失**，而且没有任何提示 ——
    // 下次打开只会发现「怎么全变回默认了」，token 变了则所有设备一起连不上。
    //
    // 判断本身在 JsonFile 里，peers.json 也要用同一套（丢配对关系和丢 token 一样糟）。
    const afmu::JsonLoadResult r = afmu::loadJson(m_path, afmu::JsonShape::Object);
    m_json = r.doc.object();
    m_loadError = r.error;
    // 备份失败时宁可不写：原文件里可能还留着能救回来的 token
    m_readOnlyFallback = r.readOnlyFallback;

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
    // 默认开着：接收服务不跑，本机既收不到文件也不会被别的设备发现 —— 这是整个
    // 应用一半的功能。想彻底关掉的人在「接收服务」页取消勾选即可，选择会被记住。
    ensure(QStringLiteral("autoStartServer"), true);
    // 默认开着：没有 token 的设备靠它来敲门，关掉之后只剩手抄 token / 扫码两条路
    ensure(QStringLiteral("allowAuthRequests"), true);
    // 新装默认关 —— 见下面的一次性迁移，升级安装最终也会落到关。
    ensure(QStringLiteral("allowLegacyPlaintext"), false);
    // 零信任模式（草案 §9）：打开之后只认配对表里的设备，访客模式一并强制关闭。
    ensure(QStringLiteral("zeroTrustMode"), false);
    // 访客模式 = 浏览器界面 + 密码认证，也就是 v1 那套访问方式（草案 §9）。
    //
    // 新装默认关，升级默认开 —— 两个默认值不同是有意的：
    //   · 草案要求默认关，那是对的，密码认证挡不住中间人。
    //   · 但对已经在用的人来说，升级一次浏览器界面就打不开了，而且没有任何提示，
    //     表现是「今天开始网页进不去了」。这和静默换密钥是同一类问题。
    // 所以只有**配置文件本来就不存在**（真·新装）才默认关。两种情况都会写进
    // 文件，于是设置页上看到的就是实际生效的值，不留任何隐式行为。
    ensure(QStringLiteral("guestMode"), r.existed);
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

    // ---- §8.2 第 3 阶段：明文默认关，升级安装也一样 -----------------------
    //
    // **只改默认值是没用的**，这一点值得写下来：上面那些 ensure() 只填缺失的键，
    // 而任何跑过旧版本的安装，`allowLegacyPlaintext: true` 早就写进文件里了 ——
    // 默认值再怎么改也碰不到它。所以第 3 阶段必须是一次**迁移**，不是一个默认值。
    //
    // 迁移只做一次，靠 stage3 这个标记记住。这一条是必须的，不是优化：
    // 用户在设置页上重新打开明文，是一个明确的决定（§8.2 「老设备需手动放行」
    // 说的就是这件事），下次启动再给他关掉就成了和他对着干。
    //
    // 关掉之后要**说一声**。悄悄关掉正是这个项目一路在防的那类事，只不过方向反过来：
    // 静默降级会让用户以为自己是安全的，静默升级会让他以为是网络坏了。
    if (!m_json.contains(kStage3Key)) {
        m_json.insert(kStage3Key, true);
        dirty = true;
        if (m_json.value(QStringLiteral("allowLegacyPlaintext")).toBool(false)) {
            m_json.insert(QStringLiteral("allowLegacyPlaintext"), false);
            m_plaintextJustDisabled = true;
        }
    }

    if (dirty)
        save();
}

void Config::save()
{
    if (m_readOnlyFallback)
        return; // 见 load()：原文件还在，里面可能有能救回来的 token
    afmu::saveJson(m_path, QJsonDocument(m_json));
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
bool Config::autoStartServer() const { return m_json.value(QStringLiteral("autoStartServer")).toBool(true); }
bool Config::allowAuthRequests() const { return m_json.value(QStringLiteral("allowAuthRequests")).toBool(true); }
bool Config::allowLegacyPlaintext() const { return m_json.value(QStringLiteral("allowLegacyPlaintext")).toBool(true); }
bool Config::zeroTrustMode() const { return m_json.value(QStringLiteral("zeroTrustMode")).toBool(false); }
bool Config::guestMode() const { return m_json.value(QStringLiteral("guestMode")).toBool(false); }
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
void Config::setAllowLegacyPlaintext(bool v) { setValue(QStringLiteral("allowLegacyPlaintext"), v); }
void Config::setZeroTrustMode(bool v) { setValue(QStringLiteral("zeroTrustMode"), v); }
void Config::setGuestMode(bool v) { setValue(QStringLiteral("guestMode"), v); }
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
