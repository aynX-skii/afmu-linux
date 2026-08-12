#pragma once

// 两端共享的常量都在这里，由 AndroidFileManagerUtils/docs/constants.json 生成。
// 别在本文件里再定义一份 —— 那正是之前靠注释互指同步、迟早会漂掉的做法。
#include "ProtocolConstants.h"

#include <QByteArray>
#include <QString>

// 协议辅助函数，严格对应 docs/PROTOCOL.md v1
namespace afmu {

/**
 * 严格的 hex 解码：只认 ASCII `0-9a-fA-F`，长度必须是偶数，**任何一处不合法就整串作废**。
 *
 * 不用 `QByteArray::fromHex`：它会**跳过**看不懂的字符，于是 `"11 22"` 解成 `0x1122`、
 * `"11zz"` 解成 `0x11`。Kotlin 那边的 `Char.digitToInt(16)` 又会接受任意 Unicode
 * 十进制数字（`١١` 解得出真实字节）。同一个字符串三种答案 —— 而配对握手里的
 * `commit` / `na` 正是从查询参数来的、对端还没被授权时就能塞进来的东西。
 * 两端对「什么算合法 hex」必须是同一个答案。
 *
 * 半截解码尤其危险：调用方都按长度校验，而「前 11 字节合法、后面是垃圾」
 * 如果解成 11 字节，某个长度刚好对得上的检查就会放它过去。所以要么全对，要么空。
 */
QByteArray hexDecodeStrict(const QString &text);

// 常数时间比较，避免用 == 泄露前缀信息
bool tokenEquals(const QByteArray &a, const QByteArray &b);

/**
 * Host 头是否指向「本机」（PROTOCOL.md §2.4）—— 挡 DNS rebinding。
 *
 * 攻击面：受害者在浏览器里打开攻击者的页面，页面请求
 * http://evil.example.com:8765/，而那个域名被解析到 192.168.1.42（受害者的手机）。
 * 同源策略帮不上忙 —— 源就是攻击者的域名。服务端唯一能分辨的地方就是 Host 头：
 * 它写的是 evil.example.com，而不是一个 IP 或 .local 名字。
 *
 * 于是规则很简单：**主机名必须是 IP 字面量、localhost、或 .local 结尾**。
 * 不去枚举本机地址（多网卡 / DHCP 下会漂），只看形态就够了 ——
 * DNS rebinding 的前提就是用一个 DNS 名字，而这三种形态都不是。
 *
 * hostHeader 是 Host 头的原始值，含端口，可能是 [::1]:8765 这种形式。
 */
bool isLocalHostHeader(const QString &hostHeader);

/**
 * Origin 头是否和本机的 Host 一致（PROTOCOL.md §2.4）—— 挡跨站请求。
 *
 * 原生客户端不发 Origin，所以**缺失视为通过**；一旦带了就必须对得上。
 * 浏览器对跨源的 fetch / 表单 POST 一定会发 Origin，所以这条能挡住
 * 「攻击者页面直接往 http://192.168.1.42:8765 发请求」。
 */
bool originMatchesHost(const QString &origin, const QString &hostHeader);

/**
 * 短时、绑定单个路径的下载券（PROTOCOL.md §2.5）。
 *
 * 浏览器的 `<a href>` 带不了自定义头，这是 `?token=` 当初存在的唯一理由。
 * 但凭证进了 URL 就会落进代理日志、浏览器历史和 Referer，所以接口不再接受它。
 * 券顶上来：
 *
 *  - 它是对**某一个路径**的 MAC，只能打开那一个文件；
 *  - 几秒就过期，链接漏出去几乎立刻作废；
 *  - **无状态**校验，服务端不记任何已签发的券。
 *
 * **它不是「一次性」的。** 真一次性要服务端存一张已用 nonce 表，
 * 而那和无状态校验直接矛盾。在它活着的那几秒里券可以被重放 ——
 * 但只能重放同一个路径的下载，所以这笔交换划算。别在界面或文档里叫它一次性。
 *
 * 时长、MAC 截断长度、域分隔前缀都在生成的 ProtocolConstants.h 里。
 */

/** 形如 `<exp>.<mac>`：exp 是 Unix 秒，mac 是 HMAC-SHA256 截到 132 bit 的 base64url。 */
QString issueDownloadTicket(const QString &token, const QString &path, qint64 nowMs);

/** 同时校验有效期**和**这张券是不是为这个路径签的。 */
bool verifyDownloadTicket(const QString &token, const QString &path, const QString &ticket,
                          qint64 nowMs);

// 生成 10 位小写字母数字 token（去掉 i l o 0 1 等易混字符）
QString makeToken();

// 授权请求的确认码：两端同时显示，用户比对后才点「允许」
QString makePairingCode();

/**
 * 拼一个可以直接塞进二维码的配对 URI。
 * hosts 是本机所有可用地址，手机扫到之后逐个试，省得用户自己挑网卡。
 */
QString buildPairUri(const QString &name, const QString &os, const QStringList &hosts, int port,
                     const QString &token, const QString &fingerprint = QString());

QString humanSize(qint64 bytes);

} // namespace afmu
