#pragma once

#include <QByteArray>
#include <QString>

/**
 * 发现应答里的滚动标识 `rid`（PROTOCOL.md v2 §6.1）。
 *
 * ```
 * rid = hex( SHA-256( "AFMU-RID-v2" || fp || 十进制ASCII(floor(unix_time / 300)) )[0:4] )
 * ```
 *
 * v1 的应答里有设备名和系统：往 UDP 8766 发一个包，局域网里任何人都能拿到
 * 「这台机器叫 icelab、跑 Linux」。`rid` 换掉的就是这个 —— 陌生人只看到一串
 * 随机 hex，而**手里已经有对方指纹的设备能自己算出同一个值**，于是照样认得出
 * 「这是我的 Pixel」。
 *
 * 顺带解决了草案 §13 第 3 问：PC 换了 IP 之后靠 `rid` 就能认出来，不必重新配对。
 *
 * 别把它当匿名 —— 挡住了什么、没挡住什么，§6.1 那张表写得很清楚：
 * 连续观测多个时间窗仍然能把设备关联起来，而**任何一次见过 `fp` 的观测者
 * 此后永久能算出它每个窗口的 rid**。这是广播发现的固有代价。
 *
 * 两处与草案的字面写法不同，都是为了「两端逐字节一致」这一个目的：
 *
 * - **加了域分隔串。** 同一个 `fp` 在 SAS 里也进哈希，不隔开的话两处的输入
 *   在理论上可能撞到一起。代价是零，所以加。
 * - **时间窗按十进制 ASCII 编码。** 草案只写了 `|| floor(...)`，没说是几字节、
 *   什么字节序 —— 这种含糊正是跨实现对不上的经典原因。定成十进制 ASCII 还有个
 *   好处：一行 shell 就能独立复核（`printf 'AFMU-RID-v2'; cat fp.bin; printf '5892745'`）。
 */
namespace afmu {

/** 域分隔串。改它等于换协议版本。 */
inline const char *const kRidContext = "AFMU-RID-v2";

/** 时间窗长度（秒）。两端必须一致，否则永远算不出同一个值。 */
inline constexpr qint64 kRidWindowSec = 300;

/** rid 的字节数 → 8 位 hex。32 bit：16 bit 在多设备环境下误认率 1/65536，不值得省这两字节。 */
inline constexpr int kRidBytes = 4;

/**
 * 算某个时间点的 rid。`fp` 传原始 32 字节，长度不对返回空串。
 *
 * 空串必须当作「算不出来」处理，绝不能当成一个能参与比对的值 ——
 * 否则两个都算不出 rid 的设备会「匹配成功」。
 */
QString rollingId(const QByteArray &fp, qint64 unixSeconds);

/**
 * 收到的 `rid` 是不是这个指纹的。
 *
 * 同时接受当前窗口和上一个窗口：窗口边界上两端各在一侧是常态，
 * 只认当前窗口的话，每 5 分钟就有一小段时间谁也认不出谁。
 * 也因此接受窗口实际是 5–10 分钟，两端时钟差在这个量级内都没问题。
 */
bool ridMatches(const QByteArray &fp, const QString &rid, qint64 unixSeconds);

} // namespace afmu
