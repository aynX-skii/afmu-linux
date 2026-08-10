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
