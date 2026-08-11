#pragma once

#include <QByteArray>
#include <QString>

// 协议常量，严格对应 docs/PROTOCOL.md v1
namespace afmu {

inline constexpr int kProtocolVersion = 1;
inline constexpr quint16 kDiscoveryPort = 8766;
inline constexpr quint16 kDefaultHttpPort = 8765;

inline const char *const kProbePrefix = "AFMU-DISCOVER";
inline const char *const kProbePayload = "AFMU-DISCOVER/1\n";

inline const char *const kTokenHeader = "X-AFMU-Token";
inline const char *const kPartSuffix = ".afmu-part";

// 二维码载荷（PROTOCOL.md §5）：afmu://pair?v=1&host=…&port=…&token=…
inline const char *const kPairUriPrefix = "afmu://pair?";

// 授权连接（PROTOCOL.md §3.8）的等待上限，两端必须一致，否则会出现
// 一端还在轮询、另一端已经把请求丢掉的窗口
inline constexpr int kAuthTimeoutSec = 60;

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

// 生成 10 位小写字母数字 token（去掉 i l o 0 1 等易混字符）
QString makeToken();

// 授权请求的确认码：两端同时显示，用户比对后才点「允许」
QString makePairingCode();

/**
 * 拼一个可以直接塞进二维码的配对 URI。
 * hosts 是本机所有可用地址，手机扫到之后逐个试，省得用户自己挑网卡。
 */
QString buildPairUri(const QString &name, const QString &os, const QStringList &hosts, int port,
                     const QString &token);

QString humanSize(qint64 bytes);

} // namespace afmu
