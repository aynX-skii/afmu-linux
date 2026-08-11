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
#include "../src/PairSas.h"
#include "../src/RollingId.h"
#include "../src/PeerStore.h"
#include "../src/Protocol.h"
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

        // 换 IP 不是换设备（v2 §13 问题 3）
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

    // ------------------------------------------------------------ SAS
    //
    // 这几条同时是**给 Android 端用的测试向量**：两端算出来必须一模一样，
    // 不一样的表现是"两个屏幕上的码对不上"，而用户唯一合理的反应是
    // 认为自己正在被攻击 —— 一个编码 bug 会被读成一次安全事件。
    {
        const QByteArray fp1(32, '\x11');
        const QByteArray fp2(32, '\x22');
        const QByteArray na(32, '\x33');
        const QByteArray nb(32, '\x44');

        const QString sas = afmu::computeSas(fp1, fp2, na, nb);
        check(sas.size() == 8, "SAS 是 8 个字符");
        check(afmu::formatSas(sas).size() == 9, "展示形式是 XXXX-XXXX");
        std::fprintf(stderr, "  [向量] SAS(0x11,0x22,0x33,0x44) = %s\n",
                     qPrintable(afmu::formatSas(sas)));

        // 谁是客户端不该影响结果 —— 否则两端各算各的，用户看到两个不同的码
        check(afmu::computeSas(fp2, fp1, na, nb) == sas, "指纹顺序不影响结果");

        // 随机数的角色是固定的，交换它们必须是另一个值：排序会白丢一半绑定强度
        check(afmu::computeSas(fp1, fp2, nb, na) != sas, "随机数不参与排序");

        // 任何一位变了，码就得变 —— 这正是它能起作用的原因
        QByteArray na2 = na;
        na2[31] = na2.at(31) ^ 0x01;
        check(afmu::computeSas(fp1, fp2, na2, nb) != sas, "随机数变一位，码就变");

        // 长度不对必须返回空，而不是凑一个看起来正常的码
        check(afmu::computeSas(fp1.left(31), fp2, na, nb).isEmpty(), "指纹长度不对返回空");
        check(afmu::computeSas(fp1, fp2, na.left(16), nb).isEmpty(), "随机数长度不对返回空");
        check(afmu::computeSas(fp1, fp1, na, nb).isEmpty(), "两个指纹相同必须拒绝");

        // 排序必须按**无符号**比。0x88 当有符号字节是负数，于是一半的指纹对会被
        // 两端排成相反的顺序 —— 那是个"测试时好好的、装到用户手上一半设备对不上"
        // 的 bug，而症状是两个屏幕显示不同的码，用户只会理解成正在被攻击。
        {
            const QByteArray high(32, '\x88'); // 有符号看是 -120，无符号看是 136
            const QByteArray low(32, '\x11');
            const QString s1 = afmu::computeSas(high, low, na, nb);
            const QString s2 = afmu::computeSas(low, high, na, nb);
            check(!s1.isEmpty() && s1 == s2, "高位字节的指纹也要两端一致");
            std::fprintf(stderr, "  [向量] SAS(0x88,0x11,0x33,0x44) = %s\n",
                         qPrintable(afmu::formatSas(s1)));
        }
    }

    // ------------------------------------------------------------ 配对二维码
    {
        const QStringList hosts{QStringLiteral("192.168.1.30"), QStringLiteral("10.42.0.1")};

        // v1：码里是 token。截图 / 转发 / 投屏等于交出访问权 —— 这正是 v2 要改掉的。
        const QString v1 = afmu::buildPairUri(QStringLiteral("ice"), QStringLiteral("linux"),
                                              hosts, 8765, QStringLiteral("abc123xyz9"));
        check(v1.contains(QStringLiteral("v=1")), "v1 的码标 v=1");
        check(v1.contains(QStringLiteral("token=abc123xyz9")), "v1 的码带 token");
        check(!v1.contains(QStringLiteral("fp=")), "v1 的码不带指纹");

        // v2：码里是公钥指纹，**没有 token**。指纹本来就是公开信息，泄露不造成损失。
        const QString v2 = afmu::buildPairUri(QStringLiteral("ice"), QStringLiteral("linux"),
                                              hosts, 8765, QStringLiteral("abc123xyz9"), a);
        check(v2.contains(QStringLiteral("v=2")), "v2 的码标 v=2");
        check(v2.contains(QStringLiteral("fp=") + a), "v2 的码带完整指纹");
        check(!v2.contains(QStringLiteral("token=")),
              "v2 的码里绝不能有 token —— 身份是那对密钥，没有东西需要交出去");
        check(v2.contains(QStringLiteral("hosts=")), "多网卡时带上候选地址");
        check(v2.startsWith(QLatin1String(afmu::kPairUriPrefix)), "前缀不变，老客户端能识别出这是配对码");

        // 指纹不截断：用户比对的是全长，而二维码容量在这里根本不是约束
        check(v2.contains(a) && a.size() == 52, "指纹在码里是完整的 52 个字符");

        // 打出来当 Android 端 PairPayloadTest 的向量：两端对不上的表现是
        // 「扫了没反应」，用户完全无从下手。
        std::fprintf(stderr, "  [向量] 配对码 %s\n",
                     qPrintable(afmu::buildPairUri(QStringLiteral("客厅 电脑"),
                                                   QStringLiteral("linux"),
                                                   {QStringLiteral("192.168.1.30")}, 8765,
                                                   QString(), a)));

        // 没有 token 也没有指纹 = 这个码什么都干不了，不如不出
        check(afmu::buildPairUri(QStringLiteral("ice"), QStringLiteral("linux"), hosts, 8765,
                                 QString())
                  .isEmpty(),
              "既没 token 也没指纹时不出码");
    }

    // ------------------------------------------------------------ 滚动 rid（草案 §6.1）
    {
        const QByteArray fp1(32, '\x11');
        const QByteArray fp2(32, '\x22');
        // 时刻本身不重要，重要的是它落在窗口的**正中间**：这样 ±100 秒不跨窗口，
        // 测的才是「同窗口一致」而不是「碰巧边界对上」。
        // 窗口 5666667 从 1700000100 开始（1700000100 / 300 恰好整除），中点 +150。
        const qint64 t = 1700000250;

        const QString rid = afmu::rollingId(fp1, t);
        check(rid.size() == 8, "rid 是 8 位 hex（4 字节）");
        check(rid == rid.toLower(), "统一小写，免得两端因为大小写认不出对方");
        check(afmu::rollingId(fp1, t + 100) == rid, "同一个窗口内不变");
        check(afmu::rollingId(fp1, t - 100) == rid, "同一个窗口内不变（往前）");
        check(afmu::rollingId(fp1, t + 300) != rid, "跨一个窗口就换值 —— 不换的话它就是个长期标识");
        check(afmu::rollingId(fp2, t) != rid, "不同设备不同值");

        check(afmu::rollingId(QByteArray(31, '\x11'), t).isEmpty(), "指纹长度不对返回空");
        check(afmu::rollingId(fp1, -1).isEmpty(), "负时间戳返回空，不产出一个能参与比对的值");

        // 空串绝不能算「匹配上了」：两台都算不出 rid 的设备会互相认成对方。
        check(!afmu::ridMatches(fp1, QString(), t), "空 rid 不匹配任何东西");
        check(!afmu::ridMatches(QByteArray(31, '\x11'), rid, t), "指纹不合法时不匹配");

        check(afmu::ridMatches(fp1, rid, t), "自己算的自然匹配");
        check(afmu::ridMatches(fp1, rid.toUpper(), t), "hex 大小写不敏感");
        check(!afmu::ridMatches(fp2, rid, t), "别人的 rid 不匹配");

        // 边界抖动：对方在上一个窗口发的应答，我这边已经跨过去了，必须还认得出来。
        // 只认当前窗口的话，每 5 分钟就有一小段时间谁也认不出谁。
        check(afmu::ridMatches(fp1, rid, t + 300), "上一个窗口的 rid 也接受");
        check(!afmu::ridMatches(fp1, rid, t + 600), "再往前就不接受了，接受窗口不是无限的");

        // 打出来当 Android 端 RollingIdTest 的向量。两端算得不一样的表现是
        // 「配过对的设备在列表里永远显示成陌生地址」—— 不报错，只是功能悄悄没了。
        std::fprintf(stderr, "  [向量] rid(fp=0x11×32, t=%lld) = %s\n", static_cast<long long>(t),
                     qPrintable(rid));
        std::fprintf(stderr, "  [向量] rid(fp=0x22×32, t=%lld) = %s\n", static_cast<long long>(t),
                     qPrintable(afmu::rollingId(fp2, t)));
        std::fprintf(stderr, "  [向量] rid(fp=0x11×32, t=0)          = %s\n",
                     qPrintable(afmu::rollingId(fp1, 0)));
    }

    // ------------------------------------------------------------
    std::fprintf(stderr, "\n通过 %d / 失败 %d\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
