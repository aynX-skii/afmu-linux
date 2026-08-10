# afmu-linux

FileBridge 的 Linux 桌面客户端：Qt 6 + Qt Quick，暗黑扁平风，标题栏 / 边框 / 圆角 / 缩放
全部自绘（`Qt.FramelessWindowHint` + `startSystemMove` / `startSystemResize`）。

协议严格按 `AndroidFileManagerUtils/docs/PROTOCOL.md` v1 实现，两半都做了：

- **客户端**：发现设备 → 浏览手机目录 → 下载（带断点续传）/ 上传
- **服务端**：本机开同协议的 HTTP 服务，手机可以直接把文件推过来

---

## 构建

需要 Qt ≥ 6.5（开发时用的是 6.10.2）、CMake ≥ 3.21、支持 C++20 的编译器，
以及 libqrencode（生成配对二维码）。

```bash
sudo apt install qt6-base-dev qt6-declarative-dev libqrencode-dev cmake ninja-build g++  # Debian/Ubuntu
sudo dnf install qt6-qtbase-devel qt6-qtdeclarative-devel qrencode-devel cmake ninja-build gcc-c++  # Fedora
sudo pacman -S qt6-base qt6-declarative qrencode cmake ninja gcc                          # Arch

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/afmu
```

---

## 界面

| 页面 | 做什么 |
|------|--------|
| **设备** | 广播扫描局域网、请求授权连接、显示配对二维码、填对端 token、手动输入 `IP:PORT` |
| **浏览文件** | 面包屑导航、多选下载、新建目录、删除、拖文件进窗口即上传 |
| **传输** | 每个任务的进度 / 速度 / 剩余时间，可取消、重试、定位到文件 |
| **接收服务** | 开关本机服务端、显示本机 token 和地址、管理共享目录、活动日志 |
| **设置** | 设备名、端口、收件箱、下载目录、发现超时、界面语言 |

快捷键：`Ctrl+1..5` 切页，`F5` / `Ctrl+R` 刷新当前目录。

### 界面语言

中英双语。默认**跟随系统语言**（`LANG` 是 `zh_*` 就用中文，否则英文），在「设置 → 界面 → 语言」
可以改成固定中文或英文。选择写进 `~/.config/afmu/config.json` 的 `language` 字段
（`system` / `zh` / `en`），下次启动自动加载，覆盖系统语言。切换即时生效，不用重启。

翻译表内置在 `src/I18n.cpp` 里，没走 Qt Linguist 的 `.ts` / `.qm` 流程——那样会给构建
加一个 `qt6-l10n-tools` 依赖，和本项目「构建依赖尽量少、装的都是发行版直接有的包」
的取向冲突。代价是翻译不能交给外部翻译工具；只有两种语言时这个代价可以接受。
以后语言变多要迁到 `.ts`，把 `Tr.t("中文")` / `T(QStringLiteral("中文"))` 换成
`qsTr()` / `tr()` 即可，调用点的形状是一样的。

---

## 用起来

### 从手机拉文件

1. 手机 App 打开服务。
2. 本机「设备」页点「扫描局域网」，双击列出的设备（或点「连接」）。
3. **手机上会弹一个授权通知**，点「允许」即可——token 自动送过来，不用手抄。
   两边屏幕上各显示一个 4 位确认码，一致才点允许。
4. 切到「浏览文件」，双击文件即开始下载。

也可以照旧手抄：把手机首页那 10 位 token 填进「设备」页的输入框，填了就用填的那个，
不会再弹授权。token 在手机上重新生成过之后这里会拿到 401，此时同样会自动改走授权流程。

授权连接的约束（细节见 [PROTOCOL.md §3.8](../AndroidFileManagerUtils/docs/PROTOCOL.md)）：
同一时刻只允许一个待决请求，被拒绝的地址会进冷却，60 秒没确认按拒绝处理，
两端都可以在设置里把这个功能整个关掉。

### 两台 PC 之间

同一套流程，方向反过来也成立：本机的服务端也实现了 `/api/authorize`，所以另一台跑
FileBridge 的 PC 扫到本机之后点「请求授权」，**本机**会弹确认框，点「允许」才把本机
token 交出去。之后对方回填自己的 token（§3.9），两个方向一次配好。

前提是本机的接收服务在跑——被发现、被连接、收文件都靠它。连接任何设备时会自动把它带起来，
想让它开机就在，去「接收服务」页勾上「启动应用时自动开启服务」。

下载落到「设置 → 下载目录」（默认 `~/Downloads/FileBridge`）。传输中是
`<文件名>.<远端路径指纹>.afmu-part`，完整收完才 `rename` 成正式名——中断了绝不会留下
半个文件冒充完整文件。重试同一个任务会拿 `.afmu-part` 的大小当 `Range` 起点续传。

文件名里带远端路径指纹是必要的：只用文件名的话，两个不同目录下的同名文件（或上次失败
遗留的残片）会共用同一个 part 文件，续传起点就是错的，落盘的文件会静默损坏。
残片大小 ≥ 已知总大小时也会被当成陈旧数据丢弃重来。

### 把文件推给手机

连上以后进到目标目录，直接把文件拖进窗口，或点右上角「上传文件」。
目录会被跳过并提示（暂不支持递归上传）。

### 让手机推到本机

1. 「接收服务」页点「启动服务」。
2. 点「显示配对二维码」，在手机 App 里点「扫码连接」对着扫。
3. 扫完两个方向就都通了：手机拿到本机地址和 token，同时把自己的 token 回填过来，
   本机也直接连上。推过来的文件落到收件箱，同时出现在「传输」页。

不想扫码就照旧：把页面上显示的**本机 token** 抄到手机 App 的「PC token」输入框。

> 二维码里是明文 token，等价于把本机的访问权交出去——截图、转发、投屏都要当成泄露处理。

对端只能访问「共享目录」列表里的目录及其子目录，路径会先 canonicalize 再校验，
越界一律按 404 返回，不泄露真实原因。收件箱始终自动包含在共享目录里。

**默认只共享收件箱一个目录。** 想让手机浏览更多内容，在「接收服务」页显式添加——
默认把整个 `$HOME` 开成可读可写可删太宽了。共享根目录本身也删不掉（403），
根目录里的单个文件和子目录仍然可以正常删除。

### 没有 Wi-Fi 时

```bash
adb forward tcp:18765 tcp:8765     # 本机 127.0.0.1:18765 就是手机的服务端
adb reverse tcp:8765  tcp:8765     # 反过来，让手机能访问本机的服务端
```

然后在「设备」页手动连 `127.0.0.1:18765`。

---

## 已验证的行为

对着自己的服务端跑过
[../AndroidFileManagerUtils/docs/LINUX-CLIENT.md](../AndroidFileManagerUtils/docs/LINUX-CLIENT.md)
§6 的验证清单：

- 无 token / 错 token → `401`；只读模式下 upload / delete / mkdir → `403`，读仍然 `200`
- 路径穿越 `path=/home/ice/../../etc/hosts`、`path=/etc` → `404`
- 中文 + emoji 文件名的上传、下载（`filename*=UTF-8''`）、列目录
- 同名重复上传 → 自动改名成 `名字 (1).ext`
- 原始字节流上传、`Transfer-Encoding: chunked` 上传、`multipart/form-data` 上传（流式落盘，
  普通表单字段丢弃），三种方式 md5 全部一致
- `Range: bytes=100-199` → `206` + `Content-Range`；`bytes=-50` 后缀区间；
  越界 → `416` + `Content-Range: bytes */<total>`；`HEAD` 返回同样的头、无响应体
- 上传中途 kill 客户端 → 收件箱里什么都不剩，`.afmu-part` 已删
- multipart 请求体被截断（`Content-Length` 到了但结尾边界没到）→ `400`，
  **不会**回 `{"ok":true,"saved":[]}` 让对端误以为传输成功
- 错 token + 大请求体 → `401` 且 `Connection: close`，请求体不会被当成后续请求解析
- 停止服务端会立刻断开已有连接，不会出现「界面显示已停止、文件还在写」
- 删除共享根目录本身 → `403`
- 客户端断点续传：预置 500000 字节的 `.afmu-part`，续传后 md5 与源文件一致
- UDP 广播探测 → 应答 `{"afmu":1,"name":...,"os":"linux","port":8765}`，回到探测包的源端口，
  不含 token
- `/api/pair`：无 token → `401`，缺 `token` 参数 → `400`，`GET` → `405`；
  成功后对端地址取自 socket 而不是请求参数
- 二维码：版本 1–40（5 到 2331 字节）逐个编码后用独立解码器还原，文本逐字节相同

---

## 代码结构

```
src/
  Protocol.*        协议常量、常数时间 token 比较、token 生成、配对 URI 拼装
  QrCode.*          libqrencode 的薄封装（byte 模式，版本自动选）
  QrImage.*         QrView —— 把二维码画到 QML 里的 QQuickPaintedItem
  PathSafety.*      §4.1 越界防护 / §4.2 文件名安全化 / §4.4 自动改名
  Config.*          ~/.config/afmu/config.json（权限 600）
  I18n.*            中英文案表、语言解析与持久化
  Discovery.*       UDP 8766：广播探测 + 应答，过滤自己的应答
  PeerClient.*      客户端 HTTP 封装，token 走 X-AFMU-Token
  Models.*          设备列表、远端目录列表
  TransferModel.*   传输队列（并发 2），下载续传、上传进度、速度与 ETA
  HttpServer.*      服务端：异步 HTTP/1.1，含 Range、chunked、multipart 流式解析
  AppController.*   串起来暴露给 QML 的门面
qml/
  Theme.qml         唯一的颜色 / 间距 / 字号来源
  Tr.qml            文案入口 Tr.t("中文")，切换语言时绑定自动重算
  Main.qml          无边框窗体、缩放边、状态栏、拖拽上传
  TitleBar.qml      自绘标题栏与窗口按钮
  AppIcon.qml       Qt Quick Shapes 画的线性图标，无外部资源
  *Page.qml         五个页面
```

---

## 注意

协议是明文 HTTP，token 只防同一局域网内的误连和顺手翻看，**不是**对抗嗅探的安全边界。
不要在不可信网络（公共 Wi-Fi、咖啡厅）上开启接收服务。
