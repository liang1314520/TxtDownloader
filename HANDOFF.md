# NovDecoder 项目交接文档

> 更新日期：2026-11-10。供下一会话或开发者接续使用。
> 本文记录当前状态与操作边界，不构成运行程序、联网验收、发布或修改共享系统的额外授权。

## 1. 接手摘要

项目在既有 Qt 小说工作台上加入了独立的视频目录模块，首版仅做分类、搜索、元数据详情与本机收藏，不做视频播放或视频下载。

当前功能提交为 `b7aa105`，本地与服务器分支已同步。两套现有构建目录的应用与测试目标均已编译链接，但测试、GUI 与当前版本在线流程尚未执行验收。网络封面仍明确禁用，因为 CDN 尚未核实。`dist` 未更新。

**下一步不要直接补推送或打包发布。先确认用户希望继续哪一项，以及是否授权运行程序、测试或新的联网核实。**

## 2. 仓库与当前 Git 状态

| 项目 | 当前值 |
| --- | --- |
| 本地目录 | `E:/Res/Projects/Personally/Qt/NovDecoder` |
| 远端仓库 | `https://github.com/liang1314520/TxtDownloader.git` |
| 工作分支 | `ui/fog-white-workbench` |
| 上游 | `origin/ui/fog-white-workbench` |
| 完整 HEAD | `b7aa105175511bb2a2f54c284c417e918f6f2655` |
| 功能提交 | `b7aa105 feat: 增加小说视频双入口与独立视频目录收藏` |
| 上一提交 | `2c86337 feat: 引入雾白工作台与本机收藏并修正章节正序` |
| `main` 当前本地位置 | `e4591da`，本轮未合并或修改 |

编写本文前的只读核验结果：

- `git status --short --branch` 无文件改动、无 ahead/behind 标记。
- `git rev-parse HEAD "@{upstream}"` 两者均为上述完整 HEAD。
- `git -c http.version=HTTP/1.1 ls-remote --heads origin refs/heads/ui/fog-white-workbench` 返回同一完整 HEAD，已核实服务器状态，而不只是本地跟踪引用。
- 先前推送确实遇到 Schannel/OpenSSL 提前 EOF、直连 443 不通；这是历史过程，不再是当前未完成项。没有关闭 TLS 证书校验。
- 本交接文档在功能提交之后新增，未代用户提交或推送。

另有 `local-only-before-sanitization` 本地分支。它不属于本轮提交或推送范围，不要擅自推送该分支、使用 `--all`/`--mirror`、删除分支或重写历史。

## 3. 必须延续的操作边界

此前执行约束为：**只编译，不运行程序或测试，不更新 `dist`**。本轮撰写文档没有解除这些约束。

在用户另行授权之前：

- 不启动 `NovDecoder.exe`、任何测试 EXE 或 `ctest`。
- 不发起新的视频服务业务请求、游客登记、域名发现或封面 CDN 探测。
- 不运行部署或打包，不将构建产物复制到 `dist`。
- 不把“编译成功”写成“测试通过”“GUI 验收通过”或“线上功能可用”。
- 不修改、公开或上传真实 token、会话文件、采集文件、签名链接及其他私密资料。
- 不自动提交、推送、合并到 `main` 或创建 PR；历史授权不自动覆盖新的变更。
- 不关闭证书校验，不通过 `ignoreSslErrors`、宽泛域名匹配或自动跟随重定向绕开联网边界。

用户要求的是独立视频目录接入，不是重做小说工作流。不要顺带重构原小说模块或增加账号登录、播放器、媒体下载等功能。

## 4. 已实现内容与代码入口

功能提交包含 20 个源码、构建及测试文件，合计 4077 行新增、14 行删除；不含发布产物。

| 文件 | 主要职责 |
| --- | --- |
| `src/mainwindow.h/.cpp` | 小说／视频双入口、独立导航、保留原小说页面与对象、模块忙状态 |
| `src/videoapitypes.h/.cpp` | 纯数据模型、查询身份、参数构造、分类／列表／搜索／详情／游客响应严格解析 |
| `src/videostore.h/.cpp` | 视频独立文件、查询快照、收藏、排序、DPAPI 会话与锁定／原子写入 |
| `src/videoclient.h/.cpp` | 按需游客会话、限定来源 HTTPS、DNS 地址检查、取消／超时／响应大小限制 |
| `src/videocatalogcontroller.h/.cpp` | 分类与搜索独立状态、分页去重、迟到响应拦截、本机收藏与显式详情请求 |
| `src/videocover.h/.cpp` | 本地封面解码／转换、缩略图及有界磁盘／内存缓存；不含网络下载器 |
| `src/videolistmodel.h/.cpp` | `QAbstractListModel` 与绘制式卡片 delegate，不为每行创建 QWidget |
| `src/videocatalogwidget.h/.cpp` | 分类与搜索、本机收藏、会话与连接三页及元数据详情侧栏 |
| `CMakeLists.txt` | `novdecoder_video` 库、UI 集成与新增测试目标 |
| `tests/tst_videoapi.cpp` | 协议与封面转换合成测试 |
| `tests/tst_videocatalog.cpp` | 存储、会话、网络边界、控制器、模型与 UI 行为测试 |
| `tests/tst_mainwindow.cpp` | 新增模块切换、原工作区状态及任务互斥相关测试 |

### 4.1 界面与状态

- 启动默认进入小说工作区；切换模块不重建原 `BookDownloadWidget` 或其 controller。
- 视频有“分类与搜索”“本机收藏”“会话与连接”三页；分类结果与搜索结果使用独立快照。
- 启动、切换模块、进入收藏、选择条目只读取本机资料，不自动请求网络详情或封面。
- 加载、搜索、刷新分类、获取详情及初始化会话均由显式操作触发；首次访问需同意本次启动期间的按需游客访问。
- 模块切换不取消后台请求；小说原有下载／转换互斥保持独立，不把视频忙状态错误套到小说导航上。
- 详情只展示元数据，使用纯文本控件，不加入播放链接、播放器或媒体下载入口。

### 4.2 分页逻辑与最后一次修正

核心位置：`src/videocatalogcontroller.cpp:224` 的 `receive()`。

- 按条目 ID 去重，分类与搜索各自维护 `query/page/items/hasMore/complete`。
- 只有空结果页确认末页；短页本身不代表数据完整。
- 重复页没有新增条目、去重后达到 10000 条、或达到第 10000 页时停止继续加载，但标为“未确认完整”。
- 最后修正移除了按“已有数量 + 原始页数量”预判超限的条件，因为重复项不应消耗新增容量。
- 对应回归用例为 `VideoCatalogTest::duplicateItemsDoNotConsumeCapacity()`：已有 9998 条，收到两个重复 ID 和一个新 ID 后为 9999 条，仍可继续加载。
- 取消后或状态变更后的迟到响应通过请求 ID 与 generation 检查丢弃。

### 4.3 视频网络与游客会话边界

核心位置：`src/videoclient.cpp`。

- 当前源码只允许精确 HTTPS 主机 `0826.aiyejikou0001.com`、`0826.aiyejikou0002.com`；默认使用前者，不自动发现新域或切换来源。
- 范围内请求为 `/api/user/registeruuid`、`/api/vod/type`、`/api/vod/list`、`/api/vod/clever`、`/api/vod/info`。本轮文档核验没有重新调用这些业务接口。
- 使用固定随机 UUID 游客身份，不采集硬件标识、不登录账号；token 通过同源 `x-token` 发送。
- DNS 返回地址必须全部为允许的公开地址，连接使用选定地址并保留原主机的 TLS 验证名与 Host。
- 保留证书验证，TLS 1.2 或更新，不跟随重定向；生产网络 manager 显式 `NoProxy`，不自动加载／保存 cookie，不复用自动认证或网络缓存。
- 单请求链超时 30 秒；普通 JSON 上限 32 MiB，游客登记响应上限 64 KiB；失败不自动循环重试。
- 登记前保存身份及已尝试标记，并持有登记锁；失败不自动换新 UUID。登记成功但安全保存失败时停止后续请求，须由用户主动重试保存，不重新登记。
- 清除游客 token 保留 UUID、已尝试标记、收藏、分类及缓存；损坏会话不会被静默删除或重新创建。

### 4.4 本机存储

`VideoStoragePaths::defaults()` 使用 Qt 标准路径，不要硬编码推测出的 Windows 用户目录：

- `dataRoot`：`QStandardPaths::AppLocalDataLocation`。
- `cacheRoot`：`QStandardPaths::CacheLocation` 下的 `video` 子目录。

文件布局：

| 位置 | 内容 |
| --- | --- |
| `dataRoot/video-categories-v1.json` | 原始分类 |
| `dataRoot/video-category-order-v1.json` | 本机分类顺序 |
| `dataRoot/video-favorites-v1.json` | 本机收藏展示资料 |
| `dataRoot/video-session-v1.dat` | 当前 Windows 用户 DPAPI 保护的游客会话 |
| `cacheRoot/list-<query-key>-v1.json` | 按查询身份隔离的分页快照 |
| `cacheRoot/thumbnails/<key>.png` | 本机缩略图 |

使用 `QLockFile` 和 `QSaveFile`，写入失败不直接覆盖旧资料；读取校验失败时保留原文件并报错。收藏变更只有持久化成功后才更新已提交的内存状态。本机排序保留父子关系，不允许跨父级移动子分类。

### 4.5 封面：本地能力完成，网络接入暂缓

- 已有普通图片识别、兼容封面转换与 PNG 缩略图缓存逻辑。
- 单份输入最多 8 MiB，限制尺寸和像素数量，拒绝动画，缩略图最多 360×240。
- 内存预算 64 MiB、磁盘预算 256 MiB；磁盘淘汰只处理自身合法缓存键命名的 PNG，不删除无关用户文件。
- **没有接入网络封面获取链。CDN 尚未核实，卡片没有本机封面时显示占位图，这是当前边界，不是已完成的在线封面功能。**
- 后续若接入 CDN，须先取得联网核实授权并确认精确来源、安全传输与凭证隔离，不能直接请求元数据里任意 `coverUrl`。

## 5. 构建环境与已有验证证据

本机环境：Windows、C++17、Qt 6.10.0 MinGW、MinGW 13.1、Ninja；代码依赖 Windows BCrypt 与 DPAPI，CMake 对非 Windows 平台直接报错。

两套已有目录为 `build` 和 `build/release`。**两者的 `CMAKE_BUILD_TYPE` 都是 `Release`，不能描述成一套 Debug、一套 Release。** 两者均启用 `BUILD_TESTING=ON`。

此前已执行两套构建，均以退出码 0 完成以下目标的编译与链接：

- `NovDecoder`
- `tst_novdecoder`
- `tst_mainwindow`
- `tst_videoapi`
- `tst_videocatalog`

验证边界：

| 项目 | 状态 |
| --- | --- |
| 两套目录编译／链接 | 已成功，含最终分页修正及相应用例 |
| 最终源码差异与 `git diff --check` | 此前已检查 |
| 测试目标编译 | 已成功 |
| 测试用例执行 | 未运行，不宣称通过 |
| 当前版本 GUI、在线业务链路验收 | 未执行 |
| 网络封面 | 未接入，CDN 核实待授权 |
| `dist` 部署／发布 | 未更新 |

已知非阻塞编译警告：`tests/tst_videocatalog.cpp:304` 使用 `QDateTime::fromSecsSinceEpoch(1000000000, Qt::UTC)`，Qt 6.10 提示该重载已弃用，建议改用 `QTimeZone` 重载。这里只记录，不为撰写文档顺带修改测试代码。

测试源码包含协议校验、游客会话保护、失败保存、并发登记锁、DNS／TLS／重定向／大小／超时边界、分页、迟到响应、收藏及 UI 状态等用例；这些是已有用例范围，不等于已验证结果。缓存淘汰用例会在临时目录构造超过 256 MiB 的测试文件，执行前应注意磁盘空间。

### 仅编译的复现命令

以下适用于本机 Bash 与已经配置好的两个构建目录，不会运行 EXE 或测试；本轮文档工作未重新执行编译。

```bash
export PATH="/d/OSApplication/Work/Code/Qt6/6.10.0/mingw_64/bin:/d/OSApplication/Work/Code/Qt6/Tools/mingw1310_64/bin:/d/OSApplication/Work/Code/Qt6/Tools/Ninja:$PATH"

"D:/OSApplication/Work/Code/Qt6/Tools/CMake_64/bin/cmake.exe" --build "E:/Res/Projects/Personally/Qt/NovDecoder/build" --target NovDecoder tst_novdecoder tst_mainwindow tst_videoapi tst_videocatalog --parallel 4

"D:/OSApplication/Work/Code/Qt6/Tools/CMake_64/bin/cmake.exe" --build "E:/Res/Projects/Personally/Qt/NovDecoder/build/release" --target NovDecoder tst_novdecoder tst_mainwindow tst_videoapi tst_videocatalog --parallel 4
```

更换电脑后需按实际 Qt、编译器与 Ninja 位置重新配置；不要将已有 CMake 缓存路径视为通用环境。

## 6. 建议的接续顺序

1. 先阅读本文和 `git status`／当前 diff，避免覆盖后续人工修改；本文编写后工作区会新增 `HANDOFF.md`。
2. 确认当前任务。若只是继续编码，可延续只编译约束；若需运行测试、GUI 或在线核实，应先取得明确授权。
3. 获准执行测试后，优先运行已有协议、目录及主窗口相关用例；失败先复现定位，不通过删测试或放宽安全检查掩盖问题。
4. 获准 GUI 验收后，重点检查模块切换保留状态、忙状态隔离、拒绝联网同意、纯离线收藏、分类／搜索分页、取消与过时响应，以及持久化失败提示。
5. 获准新的联网核实后，再处理真实业务链路和封面 CDN。当前域名与服务可用性不能仅凭已有源码当作持续有效的在线证据。
6. 只有用户要求时才更新 `dist`、打包、创建新提交或推送；当前 `b7aa105` 已在服务器分支，不需要因先前错误提示盲目重复推送。

这些是候选后续事项，不是本次已获准执行的任务清单。当前没有被复现且尚未修复的测试失败，因为测试尚未执行。

## 7. 可直接交给下一会话的提示词

```text
请接手 E:/Res/Projects/Personally/Qt/NovDecoder，先阅读 HANDOFF.md 并检查当前 Git 状态。
工作分支是 ui/fog-white-workbench，已核实的功能提交 b7aa105 在编写文档时与远端同步；不要沿用更早的“仍待推送”结论。
小说／视频双入口和视频目录、本机收藏、受保护游客会话已实现，两个现有 Release 构建目录编译成功，但测试、GUI 和当前版本在线流程尚未执行验收。网络封面因 CDN 未核实仍未接入。
继续遵守只编译、不运行程序或测试、不更新 dist 的既有约束，除非我另行明确授权。不要自动提交、推送、合并、公开凭证或触碰 local-only-before-sanitization 分支。
我接下来希望你完成的具体任务是：<在此填写>。
```
