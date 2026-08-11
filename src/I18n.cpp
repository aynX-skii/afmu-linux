#include "I18n.h"

#include "Config.h"

#include <QLocale>

namespace {

I18n *g_instance = nullptr;

// 中文原文 → 英文。key 必须和源码里的字面量逐字节一致，漏了只会退回显示中文，不会崩。
struct Entry
{
    const char *zh;
    const char *en;
};

const Entry kEnglish[] = {
    // ---------------------------------------------------------------- 导航 / 通用
    {"设备", "Devices"},
    {"浏览文件", "Browse"},
    {"传输", "Transfers"},
    {"接收服务", "Serving"},
    {"设置", "Settings"},
    {"关于", "About"},
    {"确定", "OK"},
    {"取消", "Cancel"},
    {"创建", "Create"},
    {"选择", "Choose"},
    {"复制", "Copy"},
    {"已复制", "Copied"},
    {"重试", "Retry"},
    {"清空", "Clear"},
    {"进入", "Open"},
    {"连接", "Connect"},
    {"名称", "Name"},
    {"大小", "Size"},
    {"修改时间", "Modified"},
    {"完成", "Done"},
    {"本机", "This PC"},
    {"客户端", "Client"},
    {"行为", "Behaviour"},
    {"中文", "中文"},
    {" 和 ", " and "},
    {" 或 ", " or "},

    // ---------------------------------------------------------------- 设备页
    {"向局域网广播 AFMU-DISCOVER 探测包，UDP ", "Broadcasts AFMU-DISCOVER probes on UDP "},
    {"扫描局域网", "Scan LAN"},
    {"扫描中…", "Scanning…"},
    {"正在扫描局域网…", "Scanning the LAN…"},
    {"还没有发现设备", "No devices found yet"},
    {"确认对方设备与本机在同一 Wi-Fi，并且它的接收服务已经打开。",
     "Check that the phone is on the same Wi-Fi and its server switch is on."},
    {"路由器开启 AP 隔离时广播会被吃掉，这种情况下用下面的手动连接。",
     "Routers with AP isolation swallow broadcasts — use manual connect below."},
    {"对端 token", "Peer token"},
    {"手机 App 首页显示的 10 位字符", "The 10 characters shown on the phone app's home screen"},
    {"例如 a7k2m9x4qp", "e.g. a7k2m9x4qp"},
    {"明文 HTTP，token 只防误连，不要在公共 Wi-Fi 上使用",
     "Plain HTTP — the token only prevents mistaken connections. Not for public Wi-Fi."},
    {"未加密", "Unencrypted"},
    {"允许被发现", "Make discoverable"},
    {"可被发现 · ", "Discoverable · "},
    {"允许被发现，%1 秒后自动恢复", "Discoverable — back to normal in %1s"},
    {"流量是明文 HTTP。同一网络里的任何人都能看到文件名和文件内容。请只在信任的网络里使用。",
     "Traffic is plain HTTP. Anyone on this network can read file names and contents. "
     "Use it on networks you trust."},
    {"手动连接", "Manual connect"},
    {"192.168.1.30:8765  或  127.0.0.1:18765（adb forward）",
     "192.168.1.30:8765  or  127.0.0.1:18765 (adb forward)"},
    {"已连接", "Connected"},
    {"未连接", "Not connected"},
    {"未命名设备", "Unnamed device"},
    {"协议是对称的：本机既能当客户端拉取手机上的文件，也能当服务端接收手机推来的文件。",
     "The protocol is symmetric: this PC can pull files from the phone and also host a "
     "server the phone pushes to."},

    // ---------------------------------------------------------------- 浏览页
    {"上一级", "Up"},
    {"刷新 (F5)", "Refresh (F5)"},
    {"根目录", "Root"},
    {"主目录", "Home"},
    {"新建目录", "New folder"},
    {"目录名", "Folder name"},
    {"上传文件", "Upload files"},
    {"上传", "Upload"},
    {"下载", "Download"},
    {"下载选中 (", "Download selected ("},
    {"删除选中", "Delete selected"},
    {"删除", "Delete"},
    {"失败 ", "Failed "},
    {"下载落到 ", "Saved to "},
    {"FileBridge Linux 客户端 · AFMU 协议 v1", "FileBridge Linux client · AFMU protocol v1"},
    {"删除 ", "Delete "},
    {" 项？", " items?"},
    {"删除不可逆，对端没有回收站。", "Deletion is irreversible — the peer has no trash."},
    {"删除是不可逆的，对端没有回收站。目录会被递归删除。",
     "Deletion is irreversible and the peer has no trash. Folders are removed recursively."},
    {"这个目录是空的", "This folder is empty"},
    {"先连接一台设备", "Connect to a device first"},
    {"在「设备」页扫描局域网，或手动输入地址连接。",
     "Scan the LAN on the Devices page, or enter an address manually."},
    {"把文件拖进窗口即可上传到当前目录。", "Drop files on the window to upload them here."},
    {"松开即上传到 ", "Release to upload to "},
    {"选择要上传的文件", "Choose files to upload"},

    // ---------------------------------------------------------------- 传输页
    {"还没有传输任务", "No transfers yet"},
    {"在「浏览文件」里下载，或把文件拖进窗口上传。",
     "Download from the Browse page, or drop files on the window to upload."},
    {"手机推过来的文件也会出现在这里。", "Files the phone pushes here show up in this list too."},
    {"清除已结束", "Clear finished"},
    {"全部取消", "Cancel all"},
    {"在文件管理器中显示", "Show in file manager"},
    {"打开下载目录", "Open download folder"},
    {"进行中 ", "Active "},
    {"暂无活动", "Nothing running"},
    {"排队中", "Queued"},
    {"传输中", "Transferring"},
    {"已完成", "Completed"},
    {"失败", "Failed"},
    {"已取消", "Canceled"},
    {"等待中", "Waiting"},
    {"传输失败", "Transfer failed"},
    {"发送", "Send"},
    {"接收", "Receive"},
    {" · 剩余 %1 分 %2 秒", " · %1 m %2 s left"},
    {" · 剩余 %1 秒", " · %1 s left"},

    // ---------------------------------------------------------------- 接收服务页
    {"在本机开一个同协议的服务端，让手机把文件推过来",
     "Run the same protocol here so the phone can push files to this PC"},
    {"启动服务", "Start server"},
    {"停止服务", "Stop server"},
    {"服务 ", "Server "},
    {"运行中 · 端口 ", "running · port "},
    {"已停止", "stopped"},
    {"服务未开启", "Server is off"},
    {"接收服务未开启", "Server is off"},
    {"本机 TOKEN", "THIS PC'S TOKEN"},
    {"在手机 App 的「PC token」里填这一串", "Type this into the phone app's PC token box"},
    {"重新生成", "Regenerate"},
    {"重新生成本机 token？", "Regenerate this PC's token?"},
    {"已经填了旧 token 的设备将无法再连接本机，需要重新抄一次。",
     "Devices holding the old token can no longer connect and will need the new one."},
    {"本机地址", "Addresses"},
    {"没有可用的局域网地址", "No usable LAN address"},
    {"共享目录（对端只能访问这些目录及其子目录）",
     "Shared folders (the peer can only reach these and their subfolders)"},
    {"添加目录", "Add folder"},
    {"从列表移除", "Remove from list"},
    {"选择要共享的目录", "Choose a folder to share"},
    {"活动日志", "Activity log"},
    {"token 只防同一局域网内的误连和顺手翻看，不是对抗嗅探的安全边界。",
     "The token only guards against mistaken connections on the same LAN. It is not "
     "protection against sniffing."},
    {"不要在不可信网络（公共 Wi-Fi、咖啡厅）上开启服务。",
     "Do not serve on untrusted networks (public Wi-Fi, cafés)."},

    // -------------------------------------------------- 配对（v2）
    {"加密配对", "Pair securely"},
    {"比对码", "Compare code"},
    {"对方指纹", "Their fingerprint"},
    {"等待对方确认配对", "Waiting for them to confirm"},
    {"已在 %1 上弹出配对确认，请核对下面的码再点「允许」。",
     "A pairing prompt is on %1. Check the code below matches, then tap Allow."},
    {"这个码是本机自己算出来的，不是对方发来的 —— 所以它和对方屏幕上的一致，",
     "This code was computed here, not sent by them — so if it matches what is on their "},
    {"就说明中间没有人在转发。不一致就直接取消。",
     "screen, nobody is relaying between you. If it differs, cancel."},
    {"只有对方屏幕上显示的比对码与此**一模一样**时才点「允许」。",
     "Only tap Allow if the compare code on their screen is exactly the same."},
    {"不一样就说明中间有人在转发，这时候点「允许」等于把门开给他。",
     " A different code means somebody is relaying, and allowing it opens the door to them."},
    {"允许之后这台设备会被记进「已配对设备」，此后它的连接全程加密。",
     "Once allowed, this device is recorded under Paired devices and its connections are "
     "encrypted from then on."},
    {"先选一台设备，再请求配对", "Pick a device first, then ask to pair"},
    {"本机的加密身份不可用，无法配对", "This device has no usable identity, so pairing is off"},
    {"配对比对码 %1，请与对端屏幕核对", "Pairing compare code %1 — check it against their screen"},
    {"两端算出的比对码不一致，已中止 —— 这是实现问题，不是攻击",
     "The two ends computed different compare codes; aborted. That is an implementation bug, "
     "not an attack."},
    {"对方拒绝了本次配对", "They declined the pairing"},
    {"配对超时，对方没有确认", "Pairing timed out — nobody confirmed"},
    {"对端还不支持加密配对", "That device does not support encrypted pairing yet"},
    {"配对失败", "Pairing failed"},
    {"已与 %1 配对", "Paired with %1"},

    // -------------------------------------------------- 日志 · 加密（v2）
    {"对端没有出示证书，已中止连接",
     "The other end presented no certificate; connection aborted."},
    {"指纹不匹配，已中止连接。期望 %1，实际 %2",
     "Fingerprint mismatch; connection aborted. Expected %1, got %2"},
    {"这不是你配对的那台设备，已中止连接",
     "This is not the device you paired with. Connection aborted."},
    {"连接已中止", "Connection aborted"},
    {"加密握手失败 —— 对端可能还没升级到加密连接，",
     "Encrypted handshake failed — the other end may not support encryption yet, "},
    {"或者本机的身份没被它认可", "or it does not recognise this device's identity"},
    {"已通过加密连接（%1）", "Encrypted connection (%1)"},
    {"本机指纹 %1", "This device's fingerprint: %1"},
    {"本机身份可用，但 TLS 没能就绪，只能走明文",
     "Identity is usable but TLS did not come up; plaintext only."},
    {"身份不可用：%1", "Identity unavailable: %1"},
    {"这个 Qt 构建没有 TLS 支持，加密连接不可用",
     "This Qt build has no TLS support; encrypted connections are unavailable."},
    {"身份无法交给 TLS 使用，加密连接不可用",
     "The identity could not be handed to TLS; encrypted connections are unavailable."},
    {"%1 的加密连接没有出示证书，已断开",
     "%1 opened an encrypted connection without presenting a certificate; dropped."},
    {"%1 已通过加密连接接入（%2）", "%1 connected over an encrypted link (%2)"},
    {"拒绝了未配对设备的加密连接，指纹 %1",
     "Refused an encrypted connection from an unpaired device, fingerprint %1"},
    {"%1 用明文连接，但已禁用明文，直接断开",
     "%1 connected in plaintext while plaintext is disabled; dropped."},
    {"握手告警（%1）：%2", "Handshake warning (%1): %2"},

    // -------------------------------------------------- 设置页 · 加密与配对表（v2）
    {"加密连接", "Encrypted connections"},
    {"服务端已就绪", "Server ready"},
    {"不可用", "Unavailable"},
    {"本机指纹 —— 配对时对端会显示同一串，两边一致才说明中间没有人。",
     "This device's fingerprint. The other device shows the same string while pairing; "
     "matching them is what rules out a man in the middle."},
    {"（尚未生成）", "(not generated yet)"},
    {"复制指纹", "Copy fingerprint"},
    {"允许未加密的旧版连接", "Allow unencrypted legacy connections"},
    {"关掉之后，本机只接受加密连接：非 TLS 的连接会被直接断开，不回任何响应。",
     "With this off, only encrypted connections are accepted: anything else is dropped "
     "without a reply."},
    {"目前加密只做到了服务端一半，客户端还在做 —— 现在关掉等于谁也连不上，包括手机 App。",
     "Only the server half of encryption is done so far; the client side is still being "
     "built — turning this off right now means nothing can connect, the phone app included."},
    {"已配对设备", "Paired devices"},
    {"台", "devices"},
    {"还没有配对过的设备。加密连接（协议 v2）启用后，配对成功的设备会出现在这里。",
     "No paired devices yet. Once encrypted connections (protocol v2) are on, devices you "
     "pair with appear here."},
    {"配对关系不会自动过期 —— 半年没用的设备下次还能直接连。要清理只能在这里手动解除。",
     "Pairings never expire on their own — a device you have not used in months still "
     "connects. Clearing one is manual, here."},
    {"仅加密", "Encrypted only"},
    {"配对于 ", "Paired "},
    {"上次 ", "Last seen "},
    {"解除配对", "Unpair"},
    {"解除后这台设备将无法再连接本机，要用需重新配对。",
     "This device will no longer be able to connect, and using it again means pairing from "
     "scratch."},

    // ---------------------------------------------------------------- 设置页
    {"显示给对端的名字", "The name shown to the other device"},
    {"设备名", "Device name"},
    {"服务端口", "Server port"},
    {"被占用时会依次退到 8766 / 8767 / 随机端口，实际端口以发现应答为准",
     "Falls back to 8766 / 8767 / a random port when taken; the discovery reply carries "
     "the real one"},
    {"收件箱", "Inbox"},
    {"选择收件箱目录", "Choose the inbox folder"},
    {"下载目录", "Download folder"},
    {"选择下载目录", "Choose the download folder"},
    {"发现超时", "Discovery timeout"},
    {"毫秒。建议 1000–2000，边收边等而不是固定 sleep",
     "ms. 1000–2000 works well; replies are collected as they arrive"},
    {"手机 App 首页显示的 token", "The token shown on the phone app's home screen"},
    {"可被发现（应答 UDP 探测）", "Discoverable (answer UDP probes)"},
    {"只读（拒绝上传 / 删除 / 建目录）", "Read-only (refuse upload / delete / mkdir)"},
    {"启动应用时自动开启服务", "Start the server when the app launches"},
    {"配置写在 ", "Config lives in "},
    {"（权限 600）", " (mode 600)"},
    {"界面", "Interface"},
    {"语言", "Language"},
    {"跟随系统", "Follow system"},
    {"界面语言，切换后立即生效", "Interface language, applied immediately"},

    // ---------------------------------------------------------------- 关于 / 状态栏
    {"Android File Manager Utils · 客户端", "Android File Manager Utils · desktop client"},
    {"AFMU 协议 v1", "AFMU protocol v1"},
    {"发现走 UDP 8766 广播，传输走 HTTP/1.1 明文，端口以发现应答为准。",
     "Discovery uses UDP 8766 broadcast; transfers use plain HTTP/1.1. The discovery "
     "reply carries the real port."},
    {"没有 Wi-Fi 时：adb forward tcp:18765 tcp:8765，然后连 127.0.0.1:18765；",
     "Without Wi-Fi: adb forward tcp:18765 tcp:8765, then connect to 127.0.0.1:18765;"},
    {"反向让手机推到本机用 adb reverse tcp:8765 tcp:8765。",
     "for the reverse direction use adb reverse tcp:8765 tcp:8765."},
    {"在「设备」页扫描或手动连接", "Scan or connect manually on the Devices page"},
    {" · 只读", " · read-only"},
    {"对端收件箱", "Peer inbox"},

    // ---------------------------------------------------------------- 提示 / 错误
    {"未连接到设备", "Not connected to a device"},
    {"未连接设备", "No device connected"},
    {"未连接到任何设备", "Not connected to any device"},
    {"先勾选要下载的文件", "Tick the files you want first"},
    {"选中的都是目录，已跳过", "Everything selected is a folder — skipped"},
    {"已加入 %1 个下载任务", "Queued %1 download(s)"},
    {"已加入 %1 个上传任务", "Queued %1 upload(s)"},
    {"已跳过 %1 个目录（暂不支持目录上传）",
     "Skipped %1 folder(s) — uploading folders is not supported yet"},
    {"对端为只读模式，无法上传", "The peer is read-only; uploads are refused"},
    {"请先进入某个目录再新建", "Open a folder first"},
    {"请输入设备地址，例如 192.168.1.30:8765", "Enter a device address, e.g. 192.168.1.30:8765"},
    {"还没有填对端 token，请在「设置」里填写手机 App 首页显示的 token",
     "No peer token yet — enter the token from the phone app's home screen"},
    {"没有发现设备。确认对方设备与本机在同一 Wi-Fi、接收服务已打开，",
     "No devices found. Check that the phone is on the same Wi-Fi with its server on, "},
    {"或用「手动连接」直接输入地址。", "or use manual connect to enter the address directly."},
    {"没收到广播应答，复用上次地址 %1:%2", "No broadcast reply; reusing the last address %1:%2"},
    {"连接失败：%1", "Connection failed: %1"},
    {"列目录失败：%1", "Listing failed: %1"},
    {"新建目录失败：%1", "Could not create folder: %1"},
    {"删除失败：%1", "Delete failed: %1"},
    {"已删除 %1", "Deleted %1"},
    {"已新建 %1", "Created %1"},
    {"已连接 %1 (%2:%3)", "Connected to %1 (%2:%3)"},
    {"已保存", "Saved"},
    {"已发送", "Sent"},
    {"对端协议版本 %1 高于本客户端支持的 %2",
     "The peer speaks protocol %1, newer than this client's %2"},
    {"对端发起的传输无法从本机重试", "Transfers started by the peer cannot be retried from here"},
    {"同一个文件已经在下载中", "That file is already downloading"},
    {"无法创建下载目录 %1", "Could not create the download folder %1"},
    {"无法写入 %1: %2", "Cannot write %1: %2"},
    {"无法读取 %1: %2", "Cannot read %1: %2"},
    {"写入失败: %1", "Write failed: %1"},
    {"重命名失败: %1", "Rename failed: %1"},
    {"续传区间越界（416），请删除临时文件后重试",
     "Resume range rejected (416) — delete the temporary file and retry"},
    {"连接中断", "Connection lost"},
    {"传输失败: %1", "Transfer failed: %1"},
    {"收到文件 %1", "Received %1"},
    {"对端取走 %1", "Peer fetched %1"},
    {"未知错误", "Unknown error"},
    {"token 不对（401）—— 对端 token 要填手机 App 首页显示的那 10 位；",
     "Wrong token (401) — the peer token is the 10 characters on the phone app's home "
     "screen;"},
    {"如果在手机上重新生成过，这里也要跟着改",
     "if you regenerated it on the phone, update it here too"},
    {"对端为只读模式（403）", "The peer is in read-only mode (403)"},
    {"路径不存在或越界（404）", "Path does not exist or is out of bounds (404)"},

    // ---------------------------------------------------------------- 服务端日志
    {"服务端已监听 0.0.0.0:%1", "Server listening on 0.0.0.0:%1"},
    {"服务端启动失败: %1", "Server failed to start: %1"},
    {"服务端已停止", "Server stopped"},
    {"服务端启动失败，端口可能被占用", "Server failed to start — the port may be in use"},
    {"服务已启动，端口 %1", "Server started on port %1"},
    {"UDP %1 绑定失败，手机将无法自动发现本机",
     "Could not bind UDP %1 — the phone will not discover this PC automatically"},
    {"发现应答端口 %1 绑定失败: %2", "Could not bind discovery port %1: %2"},
    {"探测 socket 绑定失败: %1", "Could not bind the probe socket: %1"},

    // ---------------------------------------------------------------- 服务端返回给对端的文本
    {"AFMU Linux 服务端已就绪。", "AFMU Linux server is ready."},
    {"本机没有内置网页界面，请用 FileBridge App 或 afmu 客户端连接。",
     "There is no built-in web UI here; connect with the FileBridge app or the afmu client."},
    {"设备名: %1", "Device: %1"},
    {"协议版本: %2", "Protocol: %2"},

    // ---------------------------------------------------------------- 扫码配对 / 授权连接
    {"显示二维码", "Show QR code"},
    {"显示配对二维码", "Show pairing QR code"},
    {"扫码连接", "Scan to connect"},
    {"在手机 App 里点「扫码连接」，对准下面的二维码。",
     "Tap \"Scan to connect\" in the phone app and point it at this code."},
    {"没有可用的局域网地址，无法生成二维码。",
     "No usable LAN address, so there is nothing to encode."},
    {"本机服务还没启动，手机扫完码会连不上。",
     "The server here is off, so a scan will not reach this PC."},
    {"二维码里含本机 token，别截图发出去。",
     "This code contains this PC's token. Don't share a screenshot of it."},
    {"复制内容", "Copy contents"},
    {"留空也行：点「请求授权」让对方在它自己屏幕上确认",
     "Optional — \"Connect\" asks the phone to approve instead"},

    {"请求授权", "Ask to connect"},
    {"等待对方授权", "Waiting for approval"},
    {"正在发送请求…", "Sending the request…"},
    {"已在 %1 上弹出通知，请点「允许」。", "A prompt is showing on %1 — tap Allow."},
    {"确认码", "Confirmation code"},
    {"对方屏幕上显示的确认码必须和这里一致，否则不要同意。",
     "Only approve if the code on the phone matches the one shown here."},
    {"剩余 ", "Expires in "},
    {" 秒", "s"},

    // 反方向：别的设备来敲本机的门
    {"有设备想连接本机", "A device wants to connect"},
    {"只有对方屏幕上显示的确认码与此相同时才点「允许」。",
     "Only tap Allow if the code on the other screen matches this one."},
    {"允许之后本机的 token 会交给它，它就能浏览、上传和拉取本机共享的目录。",
     "Allowing hands it this machine's token: it can then browse, upload to and pull from the shared folders."},
    {"允许", "Allow"},
    {"拒绝", "Deny"},
    {"允许连接请求（没有 token 的设备可以来敲门）",
     "Allow connection requests (devices without a token can ask)"},
    {"%1（%2）请求连接，确认码 %3", "%1 (%2) wants to connect — code %3"},
    {"%1（%2）请求连接本机，确认码 %3", "%1 (%2) wants to connect to this PC — code %3"},
    {"%1（%2）请求配对，比对码 %3", "%1 (%2) wants to pair — compare code %3"},
    {"%1（%2）请求配对", "%1 (%2) wants to pair"},
    {"未配对设备接入，只允许配对，指纹 %1",
     "An unpaired device connected; pairing is all it can reach. Fingerprint %1"},
    {"已与 %1 配对，指纹 %2", "Paired with %1, fingerprint %2"},
    {"已允许 %1（%2）连接本机", "Allowed %1 (%2) to connect"},
    {"已允许 %1 连接", "Allowed %1 to connect"},
    {"已拒绝 %1（%2）", "Denied %1 (%2)"},
    {"接收服务未启动，正在自动启动，否则手机发不过来",
     "The receiving server was not running — starting it, or nothing can be sent here"},

    {"%1 已扫码配对（%2:%3）", "%1 paired by QR code (%2:%3)"},
    {"已与 %1 配对", "Paired with %1"},
    {"没有对端 token，改为发起授权请求", "No peer token — asking the peer to approve instead"},
    {"token 已失效，改为发起授权请求", "Token rejected — asking the peer to approve instead"},
    {"先选一台设备，再请求授权", "Pick a device first, then ask for approval"},
    {"授权请求被拒绝：%1", "Authorization request refused: %1"},
    {"已向 %1 发起授权请求，确认码 %2", "Asked %1 to approve, confirmation code %2"},
    {"已获授权，正在连接 %1", "Approved — connecting to %1"},
    {"对方拒绝了本次连接", "The other device denied this connection"},
    {"授权请求已超时，对方没有确认", "The request timed out — nothing was confirmed on the phone"},
    {"对端不支持授权连接，请手动填写 token 或扫描本机二维码",
     "The peer does not support approval-based connecting — enter its token, or scan this PC's QR code"},
    {"对方关掉了「允许连接请求」，请在它的设置里打开",
     "The other device has connection requests switched off — turn them on in its settings"},
    {"对方正在处理另一个连接请求，稍后再试",
     "The other device is already handling another request; try again shortly"},
    {"授权请求失败，请稍后重试", "The authorization request failed; try again"},
    {"已把本机 token 回填给对端", "Sent this PC's token back to the peer"},
    {"对端未接受回填，对方仍需手填本机 token",
     "The peer did not accept the token — it still has to be entered on the phone"},
};

} // namespace

I18n::I18n(Config *config, QObject *parent)
    : QObject(parent)
    , m_config(config)
{
    g_instance = this;
    m_en.reserve(int(std::size(kEnglish)) * 2);
    for (const Entry &e : kEnglish)
        m_en.insert(QString::fromUtf8(e.zh), QString::fromUtf8(e.en));

    m_language = config ? config->language() : QStringLiteral("system");
    resolve();
}

I18n *I18n::instance()
{
    return g_instance;
}

QString I18n::systemLanguage()
{
    // zh_CN / zh_TW / zh_HK 都算中文，其余一律英文
    return QLocale::system().language() == QLocale::Chinese ? QStringLiteral("zh")
                                                            : QStringLiteral("en");
}

void I18n::resolve()
{
    m_effective = m_language == QLatin1String("zh") || m_language == QLatin1String("en")
                      ? m_language
                      : systemLanguage();
}

void I18n::setLanguage(const QString &v)
{
    const QString next = (v == QLatin1String("zh") || v == QLatin1String("en"))
                             ? v
                             : QStringLiteral("system");
    if (next == m_language)
        return;
    m_language = next;
    resolve();
    if (m_config)
        m_config->setLanguage(next);
    emit languageChanged();
}

QString I18n::t(const QString &zh) const
{
    if (m_effective != QLatin1String("en"))
        return zh;
    const auto it = m_en.constFind(zh);
    return it == m_en.constEnd() ? zh : *it;
}
