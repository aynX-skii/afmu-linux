/**
 * 配对表的行为测试。
 *
 * 这张表在 v2 里同时是数据和访问控制列表 —— 往里写一条等于开一道门 ——
 * 所以「同一个指纹有两种写法」「删掉一条另一条还在」这类问题不是整洁性问题，
 * 是安全问题。而它们全都是纯逻辑，跑一遍只要几毫秒。
 *
 * 有意不引入 QtTest：整个项目的取向是 apt 只装 qt6-base-dev / qt6-declarative-dev
 * 就能编，测试也不该例外。默认不参与构建，用 -DAFMU_TESTS=ON 打开。
 *
 *   cmake -S . -B build -DAFMU_TESTS=ON && cmake --build build && ./build/afmu_peerstore_test
 */

#include "../src/Identity.h"
#include "../src/PeerStore.h"
#include "../src/ProtocolConstants.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdio>

namespace {

int g_failed = 0;
int g_passed = 0;

void check(bool ok, const char *what)
{
    if (ok) {
        ++g_passed;
    } else {
        ++g_failed;
        std::fprintf(stderr, "  失败：%s\n", what);
    }
}

/** 一个合法的 52 字符指纹（32 字节全 0xAB 之类，值本身无所谓，长度才重要）。 */
QString fpOf(char filler)
{
    return afmu::Identity::toBase32(QByteArray(32, filler));
}

QString readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const QString a = fpOf('\x11');
    const QString b = fpOf('\x22');

    // ------------------------------------------------------------ 指纹合法性
    check(PeerStore::isValidFingerprint(a), "全长指纹应当合法");
    check(a.size() == 52, "32 字节应当编码成 52 个字符");
    check(!PeerStore::isValidFingerprint(a.left(40)), "截断的指纹必须判为不合法");
    check(!PeerStore::isValidFingerprint(a + QStringLiteral("AAAA")), "过长的指纹必须判为不合法");
    check(!PeerStore::isValidFingerprint(QString()), "空串不是指纹");
    check(!PeerStore::isValidFingerprint(a.left(51) + QLatin1Char('!')),
          "字母表外的字符必须整串作废");

    // 规范化：分组空格、小写、连字符都是同一个指纹
    check(PeerStore::normalizeFingerprint(afmu::Identity::group(a)) == a, "分组形式应规范化回原形");
    check(PeerStore::normalizeFingerprint(a.toLower()) == a, "小写应规范化回原形");

    // 末字符的填充位：52 个 base32 字符是 260 bit，指纹只有 256 bit。
    // 低 4 bit 不属于指纹，写成什么都必须收敛到同一条记录 ——
    // 否则表里会出现两条指向同一台设备的记录，删掉一条另一条还开着门。
    {
        const QByteArray alphabet(afmu::kFingerprintAlphabet);
        const int last = alphabet.indexOf(a.at(51).toLatin1());
        check(last >= 0, "末字符应当在字母表里");
        const int variant = last ^ 0x0F; // 只翻填充位，不动最高那 1 bit
        QString twisted = a;
        twisted[51] = QLatin1Char(alphabet.at(variant));
        check(twisted != a, "构造出来的应当是不同的字符串");
        check(PeerStore::normalizeFingerprint(twisted) == a, "填充位不同必须归一到同一个指纹");
    }

    // ------------------------------------------------------------ 增删查改
    QTemporaryDir tmp;
    check(tmp.isValid(), "临时目录可用");
    const QString path = QDir(tmp.path()).filePath(QStringLiteral("peers.json"));

    {
        PeerStore s;
        s.load(path);
        check(s.rowCount() == 0, "新表应当是空的");
        check(s.loadError().isEmpty(), "文件不存在不是错误");

        PeerRecord r;
        r.fp = a;
        r.name = QStringLiteral("Pixel 8");
        r.os = QStringLiteral("android");
        r.lastHost = QStringLiteral("192.168.1.42");
        r.lastPort = 8765;
        check(s.upsert(r), "第一次写入应当报告是新增");
        check(s.rowCount() == 1, "写完应当有一条");
        check(s.isPaired(a), "写进去的指纹应当算已配对");
        check(s.isPaired(afmu::Identity::group(a)), "分组形式查得到同一条");
        check(!s.isPaired(b), "没写过的指纹不该算已配对");
        check(s.find(a).pairedAt > 0, "pairedAt 应当自动填上");
        check(!s.isPinned(a), "新配对默认不 pinned");

        const qint64 firstPairedAt = s.find(a).pairedAt;

        // 换 IP 不是换设备（草案 §13 问题 3）
        s.noteSeen(a, QStringLiteral("10.0.0.7"), 9000);
        check(s.rowCount() == 1, "换地址不该多出一条记录");
        check(s.find(a).lastHost == QStringLiteral("10.0.0.7"), "地址提示应当更新");
        check(s.find(a).pairedAt == firstPairedAt, "重连不该刷掉认识的日子");

        // 没配对过的设备被看到，不等于被信任
        s.noteSeen(b, QStringLiteral("10.0.0.9"), 8765);
        check(s.rowCount() == 1, "见到陌生设备不该写进配对表");

        // 再次 upsert：更新而不是新增，pairedAt 保留
        PeerRecord again = r;
        again.name = QStringLiteral("改了名字");
        again.pairedAt = 1;
        check(!s.upsert(again), "第二次写入同一指纹应当报告不是新增");
        check(s.rowCount() == 1, "同一指纹不该出现第二条");
        check(s.find(a).name == QStringLiteral("改了名字"), "名字应当被更新");
        check(s.find(a).pairedAt == firstPairedAt, "pairedAt 不该被调用方覆盖");
        // upsert 是整条替换，只有 pairedAt 和 pinned 例外 —— 所以 again 里那个旧地址
        // 会把 noteSeen 写进去的新地址盖回去。这是对的：调用方给的是它此刻知道的全部。
        check(s.find(a).lastHost == r.lastHost, "upsert 是整条替换");

        // pinned 一旦置位，普通更新不能把它抹掉 —— 那正是降级攻击想要的效果
        check(s.setPinned(a, true), "置 pinned 应当生效");
        check(s.isPinned(a), "置完应当读得到");
        check(!s.upsert(again), "再更新一次");
        check(s.isPinned(a), "普通更新不该清掉 pinned");
        check(s.setPinned(a, false), "手工清除仍然可以");
        check(!s.isPinned(a), "清完应当读得到");
        check(!s.setPinned(a, false), "没有变化时返回假");

        // 不合法的指纹一律拒绝：存进去的话将来永远匹配不上
        PeerRecord bad;
        bad.fp = QStringLiteral("NOT-A-FINGERPRINT");
        check(!s.upsert(bad), "不合法指纹必须被拒绝");
        check(s.rowCount() == 1, "被拒绝的记录不该进表");

        check(s.remove(b) == false, "删不存在的指纹返回假");
        check(!s.removeAt(5), "越界删除返回假");
    }

    // ------------------------------------------------------------ 落盘
    {
        PeerStore s;
        s.load(path);
        check(s.rowCount() == 1, "重新载入应当读回那条记录");
        check(s.find(a).name == QStringLiteral("改了名字"), "字段应当原样读回");
        check(s.find(a).lastHost == QStringLiteral("192.168.1.42"), "地址提示应当原样读回");

        const QFile::Permissions perm = QFile(path).permissions();
        check(!(perm & (QFile::ReadGroup | QFile::ReadOther)),
              "peers.json 必须是 0600 —— 里面是这台机器信任谁");

        check(s.remove(a), "删除应当成功");
        check(s.rowCount() == 0, "删完应当是空的");
    }
    {
        PeerStore s;
        s.load(path);
        check(s.rowCount() == 0, "删除应当落盘");
    }

    // ------------------------------------------------------------ 坏文件
    {
        // 重复指纹：后一条覆盖前一条，不能并排存着
        QJsonArray arr;
        for (int i = 0; i < 2; ++i) {
            QJsonObject o;
            o.insert(QStringLiteral("fp"), i == 0 ? a : afmu::Identity::group(a));
            o.insert(QStringLiteral("name"), i == 0 ? QStringLiteral("旧") : QStringLiteral("新"));
            arr.append(o);
        }
        QJsonObject bad;
        bad.insert(QStringLiteral("fp"), QStringLiteral("短"));
        arr.append(bad);
        arr.append(QJsonValue(42)); // 根本不是对象

        QFile f(path);
        check(f.open(QIODevice::WriteOnly), "应当能写测试文件");
        f.write(QJsonDocument(arr).toJson());
        f.close();

        PeerStore s;
        s.load(path);
        check(s.rowCount() == 1, "重复指纹应当合成一条，坏记录应当被丢掉");
        check(s.find(a).name == QStringLiteral("新"), "重复时后一条生效");
        check(s.loadError().contains(QStringLiteral("被忽略")), "丢掉记录必须说出来");
    }

    {
        // 语法坏掉的文件：留底，绝不原地覆盖 —— 丢配对关系和丢 token 一样糟
        QFile f(path);
        check(f.open(QIODevice::WriteOnly), "应当能写测试文件");
        f.write("{ 这不是数组 ");
        f.close();

        PeerStore s;
        s.load(path);
        check(s.rowCount() == 0, "读不出来时表是空的");
        check(!s.loadError().isEmpty(), "读不出来必须有原因");
        check(s.loadError().contains(QStringLiteral("保留为")), "必须告诉用户原文件留在哪");

        const QStringList left = QDir(tmp.path()).entryList({QStringLiteral("peers.json.broken-*")},
                                                            QDir::Files);
        check(left.size() == 1, "原文件应当被改名留底");
        check(readAll(QDir(tmp.path()).filePath(left.value(0))).contains(QStringLiteral("这不是数组")),
              "留底的应当是原来的内容");
    }

    // ------------------------------------------------------------
    std::fprintf(stderr, "\n通过 %d / 失败 %d\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
