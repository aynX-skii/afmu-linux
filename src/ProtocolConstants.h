#pragma once

/*
 * 本文件由 AndroidFileManagerUtils/docs/constants.json 生成，**不要手改**。
 * 改值请编辑 docs/constants.json，然后跑 tools/gen_constants.py。
 *
 * 这些值必须和 Android 端的 ProtocolConstants.kt 逐字一致 —— 它们是同一份 JSON 生成的。
 */

namespace afmu {

// ---- 线格式 -------------------------------------------------------------
// PROTOCOL.md §1 / §2。改这些等于改协议，要同时升 protocolVersion。

/** 发现应答的 afmu 字段、/api/info 的 protocol 字段 */
inline constexpr int kProtocolVersion = 1;

/** 首选 HTTP 端口。客户端不能硬编码它，要以发现应答里的 port 为准（§2.1） */
inline constexpr int kDefaultHttpPort = 8765;

/** UDP 发现端口 */
inline constexpr int kDiscoveryPort = 8766;

/** 探测包只校验前缀，后面的版本号留作以后扩展（§1.1） */
inline const char *const kProbePrefix = "AFMU-DISCOVER";

/** 探测包的完整载荷 */
inline const char *const kProbePayload = "AFMU-DISCOVER/1\n";

/** 推荐的 token 头（§2.2） */
inline const char *const kTokenHeader = "X-AFMU-Token";

/** 落盘前的临时后缀。传输中断时删掉，绝不留下半个文件冒充完整文件（§4.3） */
inline const char *const kPartSuffix = ".afmu-part";

/** 配对二维码的载荷前缀（§5） */
inline const char *const kPairUriPrefix = "afmu://pair\?";

/** 服务端 socket 空闲超时（§2.3） */
inline constexpr int kSocketTimeoutSec = 120;

// ---- token -----------------------------------------------------------
// PROTOCOL.md §2.2。token 是手抄的，所以字母表去掉了易混字符。

/** 去掉了 i l o 0 1 —— 这串要用户看着屏幕敲到另一台机器上 */
inline const char *const kTokenAlphabet = "abcdefghjkmnpqrstuvwxyz23456789";

/** 约 49 bit 熵；配合失败退避足够 */
inline constexpr int kTokenLength = 10;

// ---- 失败退避 ------------------------------------------------------------
// PROTOCOL.md §2.2「失败退避」。两端不一致的话，同一个网络里表现不同，排查时会以为是网络问题。

/** 前几次失败不惩罚 —— token 是手抄的，打错很正常 */
inline constexpr int kAuthFailGrace = 5;

/** 退避上限，避免一次误操作把自己锁死太久 */
inline constexpr int kAuthBackoffMaxSec = 60;

/** 这么久没有新的失败就把记录忘掉 */
inline constexpr int kAuthFailForgetSec = 900;

// ---- 授权请求 ------------------------------------------------------------
// PROTOCOL.md §3.8。这是唯一免鉴权的接口，所有防滥用参数都在这里。

/** 等待用户决定的上限。两端不一致会出现『一端还在轮询、另一端已经把请求丢掉』的窗口 */
inline constexpr int kAuthTimeoutSec = 60;

/** 结果在超时之后再多留这么久，让最后一刻做的决定也能被取走 */
inline constexpr int kAuthResultRetentionExtraSec = 30;

/** 单地址被拒后的基础冷却，按拒绝次数翻倍 */
inline constexpr int kDenyCooldownSec = 60;

/** 单地址冷却上限 */
inline constexpr int kDenyCooldownMaxSec = 1800;

/** 超时算软拒绝：固定冷却，不参与升级计数 —— 对方没做错什么，是用户没来得及看 */
inline constexpr int kTimeoutCooldownSec = 60;

/** 全局冷却基数。不看是谁在请求 —— 这是唯一能挡住换 IP 刷屏的东西 */
inline constexpr int kGlobalCooldownSec = 10;

/** 全局冷却上限 */
inline constexpr int kGlobalCooldownMaxSec = 300;

/** 这么久没有新的拒绝就把升级计数忘掉 */
inline constexpr int kRefusalForgetSec = 1800;

// ---- 配对模式 ------------------------------------------------------------
// PROTOCOL.md §1.5。常态下发现应答不含设备名。

/** 用户点『允许被发现』之后，应答带上 name/os 的时长 */
inline constexpr int kPairingModeSec = 60;

// ---- 设备身份（v2） --------------------------------------------------------
// PROTOCOL.md v2 §3。v1 还用不到，但指纹的定义两端必须一模一样 —— 差一层封装就永远对不上，而症状是「证书明明对却一直不匹配」，极难查。

/** 不用 RSA：生成快、握手包小、Android KeyStore 原生支持 */
inline const char *const kIdentityCurve = "P-256";

/** 20 年。钉扎之后有效期本来就没有意义，只是别让 TLS 栈以过期为由拒绝 */
inline constexpr int kIdentityValidityDays = 7300;

/** base32 字母表，去掉 I 和 O（0 和 1 本来就不在里面）—— 这串要用户对着屏幕比对 */
inline const char *const kFingerprintAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

/** 展示时每几个字符空一格 */
inline constexpr int kFingerprintGroupSize = 5;

/**
 * 首选 ALPN。服务端必须同时接受 http/1.1 —— Qt 的 QNAM 会覆盖请求里设的 ALPN，只提 afmu/2
 * 的话自己的客户端都连不上（实测）。所以它不是安全边界，挡人的是钉扎（§5）
 */
inline const char *const kTlsAlpn = "afmu/2";

/** TLS record 的 handshake 类型 0x16。服务端靠首字节把 v2 和 v1 明文分流（§8.1 第 4 条） */
inline constexpr int kTlsHelloFirstByte = 22;

// ---- 下载券 -------------------------------------------------------------
// PROTOCOL.md §2.5。浏览器 <a href> 带不了自定义头，用它顶替 ?token=。

/** 约束的是**开始**下载，不是传完 —— 否则等于禁止下载大文件 */
inline constexpr int kTicketTtlSec = 10;

/** base64url 截断长度，22 字符 = 132 bit */
inline constexpr int kTicketMacChars = 22;

/** 域分隔前缀，保证这个 MAC 不会被当成同一把钥匙算出来的别的用途的 MAC */
inline const char *const kTicketDomain = "afmu-dl-v1\n";

} // namespace afmu
