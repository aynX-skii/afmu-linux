# afmu-linux

FileBridge 的 Linux 桌面客户端：Qt 6 + Qt Quick，暗黑扁平风，标题栏 / 边框 / 圆角 / 缩放
全部自绘（`Qt.FramelessWindowHint` + `startSystemMove` / `startSystemResize`）。

协议严格按 `AndroidFileManagerUtils/docs/PROTOCOL.md` 实现，v1 和 v2 都做了，
客户端和服务端两半也都做了：

- **客户端**：发现设备 → 浏览对端目录 → 下载（带断点续传）/ 上传
- **服务端**：本机开同协议的 HTTP 服务，对端可以直接把文件推过来

v2（双向 TLS + 指纹钉扎，见 `AndroidFileManagerUtils/docs/PROTOCOL.md` 第二部分）
**已经全部落地，两端都真机实测跑通** —— 和手机之间同样全程加密。

- 服务端按首字节自动分流：是 TLS 就走加密，否则按 v1 处理，一个端口同时服务新旧客户端。
- 客户端看配对表决定：对方在表里就必须走加密，**握手失败绝不退回明文**，
  指纹对不上直接中止 —— 不给「仍然继续」。
- 「设置 → 加密连接」能看到本机指纹，也能关掉明文；关掉之后本机只接受加密连接。
- **全新安装默认只加密**（明文和访客模式都默认关），升级安装保持原样 ——
  否则升一次级浏览器/明文客户端就连不上了，而且没有任何提示。判据是
  `~/.config/afmu/config.json` 本来在不在，Android 端用同一套判据。

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

### 测试

默认不建。打开**不引入任何新依赖** —— 只多链 `Qt6::Core`、`Qt6::Network` 和
`OpenSSL::Crypto`，三个都已经是主程序的依赖，装的还是上面那几个包：

```bash
cmake -S . -B build -G Ninja -DAFMU_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure   # 或者直接跑 ./build/afmu_peerstore_test
```

127 条断言，覆盖配对表（指纹规范化、去重、落盘、坏文件留底）、SAS 与滚动 rid 的
跨实现向量、配对码拼装、失败退避、配置读写。其中的向量和 Android 端
`PeerCodecTest` / `PairSasTest` / `RollingIdTest` 的断言刻意一一对应 ——
两端算得不一样的地方都不会抛异常，只会安静地表现成功能没了。

协议层面的一致性由 `AndroidFileManagerUtils/tests/` 下的两套黑盒套件验证，
用法见那边的 `tests/README.md`：

- `conformance.py` —— v1 线格式。**注意它会对每个共享目录发删除请求**，
  只能对着隔离配置起的实例跑。
- `conformance_v2.py` —— v2 门禁：未配对连接只能碰 `/api/pair-v2`、
  commit-reveal 三步、SAS 与本机独立算出的是否逐字相同。需要一个 `openssl`
  命令行（生成客户端证书用），不写任何文件。

  门禁那一组要求对端**访客模式关掉**才跑得起来（访客模式是 §4.2.4 唯一的例外，
  开着时那几条会跳过，而「跳过」看起来和「通过」一样无害）。

---

## 界面

| 页面 | 做什么 |
|------|--------|
| **设备** | 广播扫描局域网、请求授权连接、显示配对二维码、填对端 token、手动输入 `IP:PORT` |
| **浏览文件** | 面包屑导航、多选下载、新建目录、删除、拖文件进窗口即上传 |
| **传输** | 每个任务的进度 / 速度 / 剩余时间，可取消、重试、定位到文件 |
| **接收服务** | 开关本机服务端、显示本机 token 和地址、管理共享目录、活动日志 |
| **设置** | 设备名、端口、收件箱、下载目录、发现超时、界面语言、本机指纹与加密开关、已配对设备（v2 配对表） |

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

授权连接的约束（细节见 [PROTOCOL.md §3.8](https://github.com/aynX-skii/AndroidFileManagerUtils/blob/main/docs/PROTOCOL.md)）：
同一时刻只允许一个待决请求，被拒绝的地址会进冷却，60 秒没确认按拒绝处理，
两端都可以在设置里把这个功能整个关掉。

### 两台 PC 之间

同一套流程，方向反过来也成立：本机的服务端也实现了 `/api/authorize`，所以另一台跑
FileBridge 的 PC 扫到本机之后点「请求授权」，**本机**会弹确认框，点「允许」才把本机
token 交出去。之后对方回填自己的 token（§3.9），两个方向一次配好。

前提是本机的接收服务在跑——被发现、被连接、收文件都靠它，所以它**默认随应用启动**。
另外，连接任何设备时也会顺手把它带起来：报给对方的端口必须真有人听着，否则对方推文件
过来只会撞上一句 "Failed to connect"，而本机这边毫无提示。

不想让它自己起，去「接收服务」页取消「启动应用时自动开启服务」，选择会被记住。
服务跑哪一套协议看「设置 → 加密连接」：只加密（新装默认）时它是 mTLS + 指纹钉扎，
任何网络都能开；允许明文（升级默认）时那条路仍然是 HTTP + 10 位 token，
只防误连，别在公共 Wi-Fi 上开着。详见下面的「安全边界」。

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
[docs/LINUX-CLIENT.md](https://github.com/aynX-skii/AndroidFileManagerUtils/blob/main/docs/LINUX-CLIENT.md)
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

## 安全边界

**有两套，取决于「设置 → 加密连接」那个开关。** 界面上常驻显示当前实际是哪一套：

| | v2（加密，新装默认） | v1（明文，升级默认） |
|---|---|---|
| 传输 | TLS 1.3 双向认证，自签证书 + SPKI 指纹钉扎 | 明文 HTTP |
| 凭什么信对面 | 对方持有配对表里那把私钥 | 10 位共享 token |
| 挡嗅探 / 中间人 | 都挡得住 | **都挡不住** |
| 什么网络能开 | 任意，包括公共 Wi-Fi | 只在你信任的网络 |

一台设备一旦用 v2 连过一次，配对表里就会给它置上 `pinned`，从此**不再允许它退回明文**
（PROTOCOL.md v2 §8.2）。这个标志只升不降，要降只能由用户手动解除配对。

私钥在 `~/.config/afmu/identity.pem`，权限 0600。**这和 Android 的 TEE 不对等** ——
那边私钥在安全硬件里出不来，这边一次完整的家目录备份就能把身份带走。
接系统 keyring 是 v2 之后的事，记在 PROTOCOL.md §14。

威胁模型的完整版（防谁、不防谁、加密之后还剩什么泄露）见
[PROTOCOL.md](https://github.com/aynX-skii/AndroidFileManagerUtils/blob/main/docs/PROTOCOL.md)
第二部分 §1 和 §10。

---

## 许可证

[LGPL-3.0](COPYING.LESSER)（在 [GPL-3.0](COPYING) 之上附加权限）。

Qt 6 本身以 LGPLv3 提供，本项目**动态链接**发行版打包的 Qt（上面「构建」一节装的
就是那些包），所以 LGPL 要求的「使用者能换掉这个库」天然满足 —— 换一个 Qt 6 的
共享库上去就行，不需要重新编译本程序。同一套约束下的另外两个依赖：
libqrencode 是 LGPL-2.1，OpenSSL 3.x 是 Apache-2.0，都相容。

如果你要**静态链接** Qt 再分发，那条路 LGPL 也允许，但你得自己提供能重新链接的
材料（目标文件或源码）。发行版包的动态链接方式不涉及这个。
