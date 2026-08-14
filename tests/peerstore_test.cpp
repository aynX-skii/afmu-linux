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

#include "../src/AuthRequests.h"
#include "../src/Config.h"
#include "../src/Identity.h"
#include "../src/Models.h"
#include "../src/PairSas.h"
#include "../src/RollingId.h"
#include "../src/PeerStore.h"
#include "../src/Protocol.h"
#include "../src/ProtocolConstants.h"

#include <QCoreApplication>
#include <QCryptographicHash>
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

    // ------------------------------------------------------------ 指纹的分隔符
    //
    // 指纹是复制粘贴出来的东西，而两个平台对「什么算空白」的判定不一样：
    // QChar::isSpace() 认 U+00A0 / U+2007 / U+202F，Kotlin 的 isWhitespace() 不认。
    // 两端各问各的平台，同一个字符串就会得到两种答案 —— 偏偏它是决定
    // 「你在跟哪台设备说话」的那个值。所以跳过哪些字符是显式定死的。
    {
        const QString grouped = afmu::Identity::group(a);
        check(PeerStore::normalizeFingerprint(grouped) == a, "普通空格分组能读回");
        check(PeerStore::normalizeFingerprint(a.left(26) + QStringLiteral("-") + a.mid(26)) == a,
              "连字符也当分隔符");
        for (const auto sep : {QChar(0x00A0), QChar(0x2007), QChar(0x202F), QChar(0x3000)}) {
            const QString withSep = a.left(26) + sep + a.mid(26);
            check(PeerStore::normalizeFingerprint(withSep) == a,
                  "各种空格变体都要跳过（两端必须给同一个答案）");
        }
        // 字母表外的字符仍然整串作废，不是「跳过看不懂的」
        check(PeerStore::normalizeFingerprint(a.left(26) + QStringLiteral("!") + a.mid(26)).isEmpty(),
              "非分隔符的杂字符必须整串作废");
    }

    // ------------------------------------------------------------ 严格 hex
    //
    // commit / na 是对端**还没被授权**时就能从查询参数塞进来的东西，而两端对
    // 「什么算合法 hex」必须是同一个答案。Qt 的 fromHex 会跳过看不懂的字符，
    // Kotlin 的 digitToInt(16) 会接受阿拉伯-印度数字 —— 都不行。
    {
        check(afmu::hexDecodeStrict(QStringLiteral("0a1b")) == QByteArray::fromHex("0a1b"),
              "正常的 hex 应当正确解出");
        check(afmu::hexDecodeStrict(QStringLiteral("AB")) == QByteArray::fromHex("ab"),
              "大写也认");
        // 下面每一条 QByteArray::fromHex 都会「宽容」地解出点什么来，正是不要的行为
        check(afmu::hexDecodeStrict(QStringLiteral("11 22")).isEmpty(), "带空格整串作废");
        check(afmu::hexDecodeStrict(QStringLiteral("11zz")).isEmpty(),
              "前缀合法、后面是垃圾 → 整串作废，绝不返回半截");
        check(afmu::hexDecodeStrict(QStringLiteral("abc")).isEmpty(), "奇数长度作废");
        check(afmu::hexDecodeStrict(QStringLiteral("0x11")).isEmpty(), "带前缀作废");
        check(afmu::hexDecodeStrict(QString::fromUtf8("١١")).isEmpty(),
              "阿拉伯-印度数字不是 hex");
        check(afmu::hexDecodeStrict(QString()).isEmpty(), "空串解成空");
        check(afmu::hexDecodeStrict(QString(64, QLatin1Char('1'))).size() == 32,
              "64 个字符解出 32 字节");
    }

    // ------------------------------------------------- 配对会话（commit-reveal，v2 §4.2.3）
    //
    // 这段逻辑是整个抗中间人机制的核心，而**它坏掉的时候什么都看不出来**：
    // 削弱了的 commit 校验照样出码、照样弹框、照样配对成功，变的只是能中继连接的
    // 攻击者可以挑一个用户会欣然确认的码。所以下面几乎全是失败路径，
    // 而且每一条都断言 session 被**销毁**而不是允许重试。
    {
        const QByteArray peerRaw(32, '\x11');
        const QString peerFp = afmu::Identity::toBase32(peerRaw);
        const QByteArray localFp(32, '\x22');
        auto nonce = [](char fill) { return QByteArray(32, fill); };
        auto commitOf = [](const QByteArray &n) {
            return QCryptographicHash::hash(n, QCryptographicHash::Sha256);
        };

        {
            AuthRequests auth;
            auth.setEnabled(true);
            const QByteArray na = nonce('\x33');
            const auto r = auth.createPairing(QStringLiteral("PC"), QStringLiteral("linux"),
                                              QStringLiteral("10.0.0.5"), peerFp, commitOf(na));
            check(!r.isNull(), "合法的 commit 应当开出 session");
            check(r.isPairing(), "这是一条 v2 配对请求");
            check(r.nonceB.size() == 32, "本机随机数应当是 32 字节");

            const QString sas = auth.revealPairing(r.id, na, localFp);
            check(!sas.isEmpty(), "reveal 应当算出比对码");
            // 发起方那边角色对调算一次：指纹在 computeSas 里排序，两端必须落到同一个串。
            // 落不到的话两个屏幕显示不同的码，用户只会理解成正在被攻击。
            check(sas == afmu::computeSas(localFp, peerRaw, na, r.nonceB),
                  "两端算出的比对码必须相同");
            check(auth.pending().sas == sas, "比对码要存进待决请求，界面才显示得出来");
        }

        {
            // commit 存在的全部意义。没有它，攻击者中继完看到 n_b，再去搜一个
            // 能凑出它想要的码的 n_a —— 8 字符的码约 2²⁰ 次，单核一两分钟。
            AuthRequests auth;
            auth.setEnabled(true);
            const auto r = auth.createPairing(QStringLiteral("PC"), QStringLiteral("linux"),
                                              QStringLiteral("10.0.0.5"), peerFp,
                                              commitOf(nonce('\x33')));
            check(auth.revealPairing(r.id, nonce('\x99'), localFp).isEmpty(),
                  "对不上的 nonce 必须拒绝");
            // 是**销毁**不是拒绝：拿正确的 nonce 再来一次也必须失败，
            // 否则攻击者就是可以一直猜下去。
            check(auth.revealPairing(r.id, nonce('\x33'), localFp).isEmpty(),
                  "commit 对不上之后整个 session 作废，正确的 nonce 也救不回来");
            check(auth.pending().isNull(), "作废之后不该还留着待决请求");
        }

        {
            AuthRequests auth;
            auth.setEnabled(true);
            check(auth.createPairing(QStringLiteral("PC"), QStringLiteral("linux"),
                                     QStringLiteral("10.0.0.5"), peerFp, QByteArray(31, '\x00'))
                      .isNull(),
                  "commit 长度不对不该开 session");
            // 指纹为空 = 握手没给出任何可授权的东西。配对表存的就是指纹，
            // 「和某个人配对」不是一件成立的事。
            check(auth.createPairing(QStringLiteral("PC"), QStringLiteral("linux"),
                                     QStringLiteral("10.0.0.5"), QString(),
                                     commitOf(nonce('\x33')))
                      .isNull(),
                  "没有指纹不该开 session");
        }

        {
            AuthRequests auth;
            auth.setEnabled(true);
            const QByteArray na = nonce('\x33');
            const auto r = auth.createPairing(QStringLiteral("PC"), QStringLiteral("linux"),
                                              QStringLiteral("10.0.0.5"), peerFp, commitOf(na));
            check(auth.revealPairing(QStringLiteral("不是这个 session"), na, localFp).isEmpty(),
                  "错的 session id 什么都不该发生");
            check(!auth.revealPairing(r.id, na, localFp).isEmpty(),
                  "别人猜错不该影响真正的 session");
        }

        {
            // 接到了自己，或者有一端搞错了。两种情况都不该产出一个看起来正常的码
            // 让用户去「比对成功」。
            AuthRequests auth;
            auth.setEnabled(true);
            const QByteArray na = nonce('\x33');
            const auto r = auth.createPairing(QStringLiteral("PC"), QStringLiteral("linux"),
                                              QStringLiteral("10.0.0.5"),
                                              afmu::Identity::toBase32(localFp), commitOf(na));
            check(auth.revealPairing(r.id, na, localFp).isEmpty(), "指纹和自己相同时不出码");
            check(auth.pending().isNull(), "而且整个 session 作废");
        }

        {
            // v1 和 v2 共用同一个待决位置：分开算的话两边各来一个就同时弹两个窗，
            // 「一次只受理一个」也就白写了。
            AuthRequests auth;
            auth.setEnabled(true);
            check(!auth.createPairing(QStringLiteral("PC"), QStringLiteral("linux"),
                                      QStringLiteral("10.0.0.5"), peerFp, commitOf(nonce('\x33')))
                       .isNull(),
                  "第一条应当开得出来");
            check(auth.create(QStringLiteral("别的"), QStringLiteral("linux"),
                              QStringLiteral("10.0.0.9"), 8765, QStringLiteral("1234"))
                      .isNull(),
                  "已有待决请求时 v1 请求应当被拒");
            check(auth.createPairing(QStringLiteral("别的"), QStringLiteral("linux"),
                                     QStringLiteral("10.0.0.9"), peerFp, commitOf(nonce('\x55')))
                      .isNull(),
                  "已有待决请求时 v2 配对也应当被拒");
        }

        {
            AuthRequests auth;
            auth.setEnabled(false);
            check(auth.createPairing(QStringLiteral("PC"), QStringLiteral("linux"),
                                     QStringLiteral("10.0.0.5"), peerFp, commitOf(nonce('\x33')))
                      .isNull(),
                  "开关关掉时配对也一并关掉");
        }
    }

    // ------------------------------- §8.2 第 3 阶段：明文的一次性迁移
    //
    // 这一组的重点是**只做一次**。迁移本身很短，容易写对；写错的是「每次启动都跑」，
    // 而那个错误只有在用户重新打开明文之后才看得见 —— 他关掉又被打开，
    // 会以为是设置没保存。
    {
        QTemporaryDir cfgDir;
        check(cfgDir.isValid(), "配置临时目录可用");
        qputenv("XDG_CONFIG_HOME", cfgDir.path().toUtf8());
        const QString cfgPath =
            QDir(cfgDir.path()).filePath(QStringLiteral("afmu/config.json"));

        // 真·新装：配置本来就不存在 → 明文默认关，而且**不该**报「刚刚关掉了」，
        // 因为它从来没开过。
        {
            Config c;
            check(!c.allowLegacyPlaintext(), "新装应当默认只加密");
            check(!c.plaintextJustDisabled(), "新装没动过明文，不该提示刚关掉");
        }

        // 升级安装：伪造一份旧版本写下的配置（明文开着、没有迁移标记）。
        QDir().mkpath(QFileInfo(cfgPath).absolutePath());
        {
            QFile f(cfgPath);
            check(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "能写出旧版本配置");
            f.write(R"({"allowLegacyPlaintext": true, "guestMode": true,
                        "localToken": "test2test9", "deviceName": "老机器"})");
        }
        {
            Config c;
            check(!c.allowLegacyPlaintext(), "升级安装的明文应当被迁移关掉");
            check(c.plaintextJustDisabled(), "关掉了就必须报出来，界面要说一声");
            check(c.localToken() == QStringLiteral("test2test9"), "迁移不该动到别的键");
            check(c.deviceName() == QStringLiteral("老机器"), "迁移不该动到设备名");
        }

        // 再启动一次：标记已经在文件里了，什么都不该发生。
        {
            Config c;
            check(!c.plaintextJustDisabled(), "迁移只做一次，第二次启动不该再提示");
        }

        // 用户在设置页上重新打开明文 —— 这是一个明确的决定（§8.2「老设备需手动
        // 放行」）。下次启动**不许**再给他关掉，否则就是和用户对着干，
        // 而他看到的表现是「这个开关存不住」。
        {
            Config c;
            c.setAllowLegacyPlaintext(true);
        }
        {
            Config c;
            check(c.allowLegacyPlaintext(), "用户重新打开的明文必须留住");
            check(!c.plaintextJustDisabled(), "不该第二次触发迁移");
        }
    }

    // ------------------------------------------------------------
    // 设备列表 = 发现听到的 ∪ 配对表。这三条都是真机上出过的症状，不是假想：
    // 「配对成功的电脑一点扫描就没了」「解除配对之后那一行还挂着锁」
    // 「同一台设备在列表里出现两行」。
    {
        const QString fpA = fpOf('\x41');
        const QString fpB = fpOf('\x42');

        PeerRecord a;
        a.fp = fpA;
        a.name = QStringLiteral("台式机");
        a.os = QStringLiteral("linux");
        a.lastHost = QStringLiteral("192.168.1.10");
        a.lastPort = 8765;

        PeerRecord b;
        b.fp = fpB;
        b.name = QStringLiteral("笔记本");
        b.lastHost = QStringLiteral("192.168.1.11");
        b.lastPort = 8765;

        // 广播一台都没答上来 —— Windows 防火墙、AP 隔离、手机息屏都会造成这个。
        {
            const QList<DeviceInfo> out = afmu::mergeDevices({}, {a, b});
            check(out.size() == 2, "广播没应答时，配对表里的设备仍然要在列表里");
            check(out[0].fingerprint == fpA, "从配对表列出来的设备带着指纹（界面据此显示锁）");
            check(out[0].name == QStringLiteral("台式机"), "名字取配对表里的");
            check(out[1].os == QStringLiteral("unknown"), "配对表里没记 os 时填 unknown");
            // 「列表里有它」不等于「它开着」：本机分不清设备关机和广播被吃掉。
            check(!out[0].heard && !out[1].heard, "从配对表列出来的必须标成没听到应答");
        }

        // 陌生人应答了，已配对的那台没答 —— 老代码在这里把 A 丢掉了。
        {
            const DeviceInfo stranger{QStringLiteral("谁"), QStringLiteral("linux"),
                                      QStringLiteral("192.168.1.99"), 8765, {}};
            const QList<DeviceInfo> out = afmu::mergeDevices({stranger}, {a});
            check(out.size() == 2, "扫到陌生设备不该把没应答的已配对设备挤掉");
            check(out[0].host == QStringLiteral("192.168.1.99"), "听到的排在前面");
            check(out[0].fingerprint.isEmpty(), "陌生人没有指纹，界面上不该有锁");
            check(out[0].heard && !out[1].heard, "同一份列表里两种来源要分得开");
        }

        // 换了地址的已配对设备：rid 认出来了，所以它带着指纹从新地址应答。
        // 配对表里那条还写着旧地址，不能因此再补一行。
        {
            const DeviceInfo moved{QStringLiteral("台式机"), QStringLiteral("linux"),
                                   QStringLiteral("192.168.1.77"), 8765, fpA};
            const QList<DeviceInfo> out = afmu::mergeDevices({moved}, {a});
            check(out.size() == 1, "同一台设备不该因为换了地址在列表里出现两行");
            check(out[0].host == QStringLiteral("192.168.1.77"), "活的应答说的才是它现在在哪");
            check(out[0].heard, "它刚应答过，不该被标成未应答");
        }

        // **解除配对之后那把锁必须当场消失。** 指纹是发现那一刻认出来的，
        // 它自己不会过期；留着的话界面会继续说这台设备是"已配对、加密的"，
        // 而配对表里已经没有它了 —— 那句话就是假的。
        {
            const DeviceInfo heard{QStringLiteral("台式机"), QStringLiteral("linux"),
                                   QStringLiteral("192.168.1.10"), 8765, fpA};
            const QList<DeviceInfo> out = afmu::mergeDevices({heard}, {});
            check(out.size() == 1, "设备还在网上，行还要在");
            check(out[0].fingerprint.isEmpty(), "配对表里没有了，指纹就得当场清掉");
        }

        // 没有可用地址的记录列出来也点不动（对方还没来敲过门）。
        {
            PeerRecord noAddr;
            noAddr.fp = fpOf('\x43');
            check(afmu::mergeDevices({}, {noAddr}).isEmpty(), "没有地址的配对记录不进设备列表");
        }

        // 同一批设备、只是某一行多了把锁：模型不该整份 reset（那会把列表弹回顶部）。
        {
            DeviceModel m;
            const DeviceInfo plain{QStringLiteral("台式机"), QStringLiteral("linux"),
                                   QStringLiteral("192.168.1.10"), 8765, {}};
            DeviceInfo known = plain;
            known.fingerprint = fpA;
            m.setAll({plain});
            int resets = 0;
            int changes = 0;
            QObject::connect(&m, &QAbstractItemModel::modelReset, [&resets] { ++resets; });
            QObject::connect(&m, &QAbstractItemModel::dataChanged, [&changes] { ++changes; });
            m.setAll({known});
            check(resets == 0 && changes == 1, "行没变时只发 dataChanged");
            check(m.at(0).fingerprint == fpA, "内容确实更新了");
            m.setAll({known});
            check(changes == 1, "内容一样时什么都不该发");
            const DeviceInfo other{QStringLiteral("笔记本"), QStringLiteral("linux"),
                                   QStringLiteral("192.168.1.11"), 8765, {}};
            m.setAll({known, other});
            check(resets == 1 && m.rowCount() == 2, "多出一行只能整份 reset");
        }

        // 认出来过一次就不再退回「不认识」：丢一个 rid 应答不代表换了设备。
        {
            QList<DeviceInfo> heard;
            afmu::upsertDevice(heard, DeviceInfo{QStringLiteral("台式机"), QStringLiteral("linux"),
                                                 QStringLiteral("192.168.1.10"), 8765, fpA});
            afmu::upsertDevice(heard, DeviceInfo{QStringLiteral("台式机"), QStringLiteral("linux"),
                                                 QStringLiteral("192.168.1.10"), 8765, {}});
            check(heard.size() == 1, "同一个 host:port 只占一行");
            check(heard[0].fingerprint == fpA, "空指纹不该把认出来的设备打回不认识");

            // 「忘记设备」把这一行从「听到的」里摘掉。配对关系是另一半，由
            // PeerStore::remove 负责 —— 见 AppController::forgetDevice。
            check(!afmu::removeDevice(heard, QStringLiteral("192.168.1.99"), 8765),
                  "摘一个不在里面的地址应当报告没摘到");
            check(heard.size() == 1, "没摘到就不该动列表");
            check(afmu::removeDevice(heard, QStringLiteral("192.168.1.10"), 8765), "摘掉应当成功");
            check(heard.isEmpty(), "摘完就没了");
        }
    }

    // ------------------------------------------------------------
    std::fprintf(stderr, "\n通过 %d / 失败 %d\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
