#pragma once

#include <QByteArray>
#include <QString>

/**
 * 短认证串（SAS）—— 没有二维码时，用户靠它确认中间没有人
 * （PROTOCOL.md v2 §4.2.2）。
 *
 * ```
 * sas = base32( SHA-256( "AFMU-SAS-v2" || min(fp_a,fp_b) || max(fp_a,fp_b) || n_a || n_b ) )[0:8]
 * ```
 *
 * 三件事必须完全照做，任何一件走样这套机制就归零：
 *
 * - **随机数是 commit-reveal 来的。** 谁都不能在看到对方的随机数之后再挑自己的，
 *   否则中间人可以针对对方的取值离线搜碰撞 —— 2²⁰ 次生日攻击，单核一两分钟，
 *   而且能在攻击发生之前就磨好（§4.2.1）。commit 把这条路堵死。
 * - **指纹按字典序排序**，两端才会算出同一个值，与谁是客户端无关。
 * - **发起方显示自己算的那个**，绝不显示服务端回来的。回带的值只用来自检，
 *   显示它等于让中间人回一个你期望的串就骗过去。
 *
 * 域分隔串是 ASCII，不含结尾 NUL。将来换算法就换这个串。
 */
namespace afmu {

/** 域分隔串。改它等于换协议版本。 */
inline const char *const kSasContext = "AFMU-SAS-v2";

/** SAS 的字符数（不含分隔的短横线）。40 bit —— 见 §4.2.2 为什么这够。 */
inline constexpr int kSasChars = 8;

/**
 * 计算 SAS。两个指纹传原始 32 字节，两个随机数传原始 32 字节。
 *
 * 参数不合法（长度不对）返回空串 —— **空串必须当作失败处理**，
 * 绝不能当成"没有码"继续往下走。
 */
QString computeSas(const QByteArray &fpA, const QByteArray &fpB, const QByteArray &nonceA,
                   const QByteArray &nonceB);

/** 展示形式：`XXXX-XXXX`。用户比对的是这个。 */
QString formatSas(const QString &sas);

} // namespace afmu
