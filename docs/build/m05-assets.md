# M05 资源管线：实现说明、实测结果与遗留风险

本文件记录 M05（Asset Pipeline）的落地细节：真实接口形态、本机实测结果，
以及实现过程中踩到并修掉的坑。凡是「为什么这样写」的背景知识都集中在这里，
源码里只保留 Doxygen 注释。

## 实测环境

与 [M00 实测结果](m00-fixups.md) 相同：

| 项 | 值 |
|---|---|
| Visual Studio | Community 2026，18.10.12224.181 |
| MSVC 工具集 | 14.51.36231（`v145`） |
| MSBuild | `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe` |
| vcpkg | `D:\CPPLibraries\vcpkg\vcpkg.exe`（`VCPKG_ROOT` 需每次显式设置） |
| triplet | `x64-windows-static-md` |

## 实测结果

三套配置（Debug / Development / Release × x64）构建全部成功，**零 warning、零 error**。

```text
fa_tests.exe → All tests passed (17092 assertions in 215 test cases)   exit = 0
fa_tests.exe "[assets]"
             → All tests passed (312 assertions in 61 test cases)       exit = 0
fa_app.exe --frames 60
             → m05: 4 catalog entr(ies), 2 texture(s) cached, 2 placeholder(s),
                    1280 CPU byte(s), 2 live GPU texture(s), 0 failed
               M02+M03+M04+M05+M06+M07 smoke test OK                    exit = 0
```

`fa_tests` 的 215 个用例 = M01 的 22 个 + M02 的 8 个 + M04 的 20 个 + M03 的 26 个
+ M07 的 36 个 + M06 的 42 个 + M05 新增 61 个。三配置运行结果一致。

`m05:` 行在三配置下**逐字相同**，且连跑 3 次不变。

### 1280 字节是怎么来的

| 资产 | 尺寸 | 字节 |
|---|---|---|
| `tex.ground.grass` | 16×16 RGBA8 | 1024 |
| `tex.marker` | 8×8 RGBA8 | 256 |
| **合计** | | **1280** |

catalog 有 4 条，但只请求了 2 条——`tex.ground.soil` 与 `tex.actor.player`
从未被 `loadTexture` 触碰，因此 `textureCount` 是 2。这条数字同时验证了
「catalog 尺寸 → 占位像素 → 上传 → 统计」整条链路。

### 一个容易看错的地方

`fa_tests` 的输出里会出现这两行，它们**不是失败**：

```text
[error][events ] JobSystem: job 'always-fails' failed: job exploded
[warn ][asset  ] tex.grass: texture upload failed, keeping CPU pixels only
```

- 第一行是 M03 的用例故意提交一个会失败的任务，验证错误传播。
- 第二行是 M05 的用例故意让上传器返回 0，验证 `uploadFailed` 标记与「CPU 像素仍可用」。

两行都是**被断言的预期行为**，`All tests passed` 才是结论。

## 交付内容

### 公共头（`src/engine/include/fa/assets/`）

| 文件 | 内容 |
|---|---|
| `AssetError.hpp` | `asset_error` 3000–3007 |
| `AssetId.hpp` | `AssetId`、FNV-1a 64、`std::hash` 特化 |
| `AssetHandle.hpp` | `AssetState`、`detail::AssetSlotBase`、`AssetHandle<T>` |
| `ImageData.hpp` | `ImageFormat`、`ImageData`、占位与颜色文本辅助函数 |
| `AssetTypes.hpp` | `AssetKind`、`TextureAsset`、`JsonAsset`、`AssetStats` |
| `ITextureUploader.hpp` | 反向 GPU 桥 |
| `IImageDecoder.hpp` | 解码器接口；app 注册 stb_image 实现 |
| `AssetCatalog.hpp` | `AssetEntry`、`AssetCatalog` |
| `AssetManager.hpp` | `AssetManagerDesc`、`IAssetManager`、`createAssetManager()` |

### 私有实现（`src/engine/assets/src/`）

| 文件 | 内容 |
|---|---|
| `AssetId.cpp` | `fnv1a64` |
| `ImageData.cpp` | 图像构造、颜色文本互转、`channelToByte` |
| `AssetTypes.cpp` | 两张 `toString` 名字表 |
| `AssetCatalog.cpp` | nlohmann/json 非抛出解析 + 校验 |
| `AssetManager.cpp` | 槽位缓存、异步作业、帧线程上传、`reload`、`stats`、淘汰 |

### 辅助改动

| 文件 | 改动 |
|---|---|
| `src/engine/assets/fa_assets.vcxproj` | `ExceptionHandling=Sync`；新增 `fa_events` 引用 |
| `tests/fa_tests.vcxproj` | 引用 `fa_assets` |
| `src/app/main.cpp` | `data/catalog.json` + `StbImageDecoder` + `AssetUploaderAdapter` + `initializeAssets()` + `m05:` 行 |
| `src/engine/include/fa/render/RenderError.hpp` | `render_error` 5000–5005 → 4000–4005（见坑 #8） |

`fa_app.vcxproj` 在 M00 骨架期就已经引用了 `fa_assets`，本轮无需改动。
`fa_assets` 只引用 `fa_core` + `fa_platform` + `fa_events`，**不引用 `fa_render`**。

### 删除

`src/engine/assets/src/AssetRegistry.cpp` 是 M00 骨架期的占位实现，自己定义了一套
与 `AssetManager.hpp` 同名的类型，两者编进同一个静态库必然重定义。全仓库无人引用它，
已整份删除，由 `AssetManager.cpp` 取代。

## 语义决策

| 决策 | 选择 | 原因 |
|---|---|---|
| GPU 桥的方向 | `ITextureUploader` 反向接口，`fa_assets` 不认识 `fa_render` | 若 M05 依赖 M06，M06 又依赖 M05（要 `ImageData`）就成了循环；反向桥让两边编译期互不依赖 |
| token 类型 | `std::uint32_t` | 恰好等于 `render::TextureHandle::value()`，但类型上不泄漏渲染概念；换后端只需换适配器 |
| 缺文件的行为 | 降级为占位并**返回成功** | v1 期间根本没有美术文件，若这里是错误，所有模块的验收都得先造图 |
| JSON 缺失的行为 | 返回错误，**不**造占位 | 伪造一份 JSON 会掩盖内容 bug，并把错误推迟到更难查的地方 |
| `requestTexture()` 的错误通道 | 无（空句柄 + 日志） | 每帧都可能调用的热路径，调用方无处处理错误，也不该因一个资源中断一帧 |
| `update()` 阻塞性 | 非阻塞（只看 `isComplete()`） | 帧时间不能被资源加载绑架 |
| 上传的线程 | 只在帧线程（`update()` 内） | GL 上下文绑在帧线程，worker 上传是未定义行为 |
| `revision` 的递增时机 | **只有成功的 (re)load** | 失败的重载保留旧内容，此时「内容变了」是假话 |
| 句柄构造 | 私有 + `friend class AssetManager` | 外部无法伪造引用计数 |
| 槽位退役 | 管理器析构时置 `m_retired`，句柄读 `Unloaded` | 「管理器没了」比「段错误」好得多 |
| `retainCpuPixels` 默认值 | `true` | 热重载与 CPU 采样都需要原始像素；真实美术到位后可关 |
| `AssetManagerDesc` 的指针 | 全部借用，管理器不拥有 | composition root 本来就比管理器活得久；少一层生命周期纠缠 |
| `jobs == nullptr` 的行为 | 退化为同步加载 | 测试无需涉及线程即可覆盖同一条加载代码路径 |
| 空路径即占位 | `path` 为空 + 必然的 `size` | 让「还没有美术」成为一等公民，而不是特例分支 |
| catalog 与缓存分离 | `AssetCatalog` 独立于 `AssetManager` | 换一份 catalog = 换一整套内容，是内容包/mod 的挂钩点 |
| 冒烟只有 2 张纹理 | 只请求地面与标记 | 冒烟的目的是验证接线，不是覆盖 catalog 全部条目 |

## 踩到的坑

### 1. `AssetManager` 必须定义在 `fa::assets` 里（C2248 + 链接错）

`AssetHandle<T>` 的构造函数是私有的，只对 `friend class AssetManager;` 开放。
`friend` 声明的名字在**最近的封闭命名空间**里查找，也就是 `fa::assets::AssetManager`。
最初把实现类写在全局命名空间（或匿名空间）里，现象是：

```text
error C2248: "fa::assets::AssetHandle<T>::AssetHandle": 无法访问 private 成员
```

而且即便勉强绕过，链接时也会找不到那张 friend 声明的类。

**修法**：实现类必须一字不差地叫 `fa::assets::AssetManager`。
这条的教训是**`friend class X;` 里的 `X` 是一次名字声明，不是一个标签**——
写 friend 的那一刻就已经决定了实现类必须住在哪儿。

### 2. `JobSystem::wait()` / `submit()` 是 `[[nodiscard]]`（C4834 → C2220）

清理路径里写 `m_job.wait();` 只是为了「等它结束再析构」，返回值并不需要。
在 `/W4 /WX` 下：

```text
warning C4834: 丢弃具有 [[nodiscard]] 属性的函数的返回值
error C2220: 以下警告被视为错误
```

**修法**：`static_cast<void>(m_job.wait());`。这也是 M03/M04 里反复出现的同一个坑——
**`[[nodiscard]]` 的返回值不是可以随手丢掉的**，要么用，要么显式声明「我知道我不要」。

### 3. `nlohmann/json` 强制 `/EHsc`（C4530）

`AssetCatalog.cpp` 用 nlohmann/json 解析 catalog。全局开关是 `/EHsc-`，于是：

```text
warning C4530: 使用了 C++ 异常处理程序，但未启用展开语义。请指定 /EHsc
```

原因是该库间接 include `<chrono>`，其 tzdb 段含真实的 `try/catch`。
这条与 [M07 坑 #2](m07-input.md) 完全同源，**只要 include 图的某一处拉进 `<chrono>`
就必须开 `/EHsc`**，与「这个模块自己抛不抛异常」无关。

**修法**：`fa_assets.vcxproj` 设 `ExceptionHandling=Sync`。同时钉住一条：**M05 自己不抛异常**，
异常只可能在 nlohmann/json 内部产生，我们的代码一律以 `Result`/`Status` 返回。

### 4. `using` 声明不能作用于命名空间（C2871 一类的误用）

测试里想少写几层前缀，写了：

```cpp
using fa::assets::asset_error;        // 编译失败
```

`using` 声明只能引入**名字**，而 `asset_error` 是命名空间。

**修法**：用命名空间别名：

```cpp
namespace asset_error = fa::assets::asset_error;
```

顺带的好处是测试代码仍然保持 `asset_error::kUnknownAsset` 这种与生产代码一致的读法。

### 5. `std::map::insert(std::string)` 找不到重载（C2665）

测试替身 `FakeFileSystem` 想记「哪些目录被创建过」，最初写成 `std::map<std::string, int>`
再用 `m_directories.insert(name)`。`std::map::insert` 要的是 `value_type`（即 `pair<const K, V>`），
只给一个 key 会去匹配一堆不可行的重载：

```text
error C2665: "std::map<...>::insert": 没有重载函数可以转换所有参数类型
```

**修法**：改用 `std::set<std::string>`——本来也只需要「有没有这个目录」。
教训：**容器选型错了会以「找不到重载」的面目出现**，而不是以「你选错容器」的面目出现。

### 6. Catch2 断言里的链式比较会 `static_assert` 失败（C2338）

想写「b 严格位于 a 与 c 之间」：

```cpp
REQUIRE(a < b < c);          // 编译期 static_assert 失败
```

Catch2 把表达式按 `&&`/`||` 拆解，但**链式比较**在 C++ 里是
`(a < b) < c`，右边那个 `<` 作用于 `bool` 与整数，语义已经错了；Catch2 的
decomposer 会直接 `static_assert` 拦住。

**修法**：加括号写成两个独立断言，或 `REQUIRE((a < b && b < c));`。
这不只是「Catch2 挑剔」——链式比较在 C++ 里**本来就语义错误**，Catch2 只是把它暴露了出来。

### 7. `glm::vec4::operator==` 返回 `bvec4` 而不是 `bool`（C4800 一类）

`core::Color` 是 `glm::vec4`。`CHECK(colorA == colorB)` 在 `-W4 /WX` 下报错，
因为 GLM 的 `operator==` 返回逐分量的 `bvec4`（这样 `all()/any()` 才有意义），
而它不是可判定真假的值。

**修法**：测试里加一个逐分量比较的辅助函数：

```cpp
[[nodiscard]] bool sameColor(fa::core::Color lhs, fa::core::Color rhs) {
    return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
}
```

这条要记住的是**避免 `ASSERT` 里直接比较 GLM 向量**；浮点相等本来也该逐分量显式写。

### 8. `render_error` 的码段与 Ecs/Physics 撞车

`Error.hpp` 的预留区间写得很清楚：

```text
4000-4999  Render / Input / Audio / UI
5000-5999  Ecs / Physics
```

而 M06 初版把 `render_error` 写成了 **5000–5005**，正好占用了 Ecs/Physics 的段。
当时没抓到，因为还没有第二个模块用 5000 段；到 M05 整理错误码时才对齐出来。

**修法**：`RenderError.hpp` 改为 4000–4005，并同步
[06-rendering.md](../modules/06-rendering.md) 与 [m06-render.md](m06-render.md)。
全仓库没有把码值当字面量写死的地方（唯一命中是 `camera.setPosition(Vec2{5000, 5000})`），
所以改动是纯机械的。教训：**加新错误码时先回 `Error.hpp` 看一眼自己该在哪个段**，
而不是顺手接着上一个模块的号往下编。

### 9. `IPlatform` 没有 `createFileSystem()` 工厂（C2039）

app 集成时按「每样东西都有工厂」的直觉写了：

```cpp
fa::platform::createFileSystem();     // 不存在
```

实际形态是文件系统**属于平台对象**：`IPlatform::files()` 返回 `IFileSystem&`。

**修法**：`desc.fileSystem = &platform.files();`。
这与 M06 的坑 #3（`IPlatform` 加纯虚打断所有 fake）是同一类：
**接口形态要先去头文件确认，别按命名惯例推测**。

### 10. `ImageData::rowPitch()` 是 `size_t`，`TextureDesc::rowPitchBytes` 是 `int`（C4267）

```text
warning C4267: "初始化": 从 "size_t" 转换到 "int"，可能丢失数据
```

**修法**：`static_cast<int>(image.rowPitch())`。这里 cast 是安全的：
行距 = `width * bytesPerPixel`，而 `TextureDesc` 的宽度本身受 `kMaxTextureSize = 16384` 约束，
乘上最大 4 字节仍远小于 `INT_MAX`。但**cast 的地方要留一句解释**，否则下一个人
会以为这里只是「消警告」。

### 11. Release 下忙循环等不到 worker（用例时好时坏）

`requestTexture finishes in update()` 最初这样等后台作业：

```cpp
for (int i = 0; i < 2000 && !done; ++i) { assets->update(); }
```

Debug/Development 稳定通过，**Release 偶发失败**：优化后的 2000 次循环
比 worker 线程真正启动还快，`update()` 每次都看到「未完成」。

**修法**：改成时间截止 + 让出 CPU：

```cpp
const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
while (!done && std::chrono::steady_clock::now() < deadline) {
    assets->update();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
REQUIRE(done);
```

教训：**用「循环次数」当超时在跨配置下不可靠**，因为循环体耗时会随优化等级变化几个数量级。
凡是等异步结果的用例，都应该用**时间**而不是**次数**表达超时。

### 12. 首次加载后 `revision` 是 1，不是 2（用例断言错）

`failed slot stays failed` 用例里断言 `revision == 2`，实测 1。回头读头文件：

> `revision`: Bumped by every **successful** (re)load.

首次成功加载就把 `revision` 从 0 加到 1，所以它是 1。断言写 2 是我把「加载 + 重载」
两次算进去了，而那个用例只加载了一次。

**修法**：断言改 1u。这类偏差是好的信号：**用例写的是「我以为的语义」，跑一次就是实现给设计回话**。
如果反过来为了让用例过而去改实现，就会把「只有成功才递增」这条正确语义改坏。

### 13. `AssetCatalog` 解析用 `logEvents = false` 的测试要显式关闭日志

故意造失败的用例会刷出大片 `[error][asset]` 行，把真正的 `All tests passed` 埋掉。
`AssetManagerDesc::logEvents` 就是为此存在的。测试替身里记得显式设 `false`，
否则排查真失败时要先翻过几十行预期噪音。

## 复现命令

```powershell
$env:VCPKG_ROOT = 'D:\CPPLibraries\vcpkg'
.\scripts\build.ps1 -Configuration Development -NoInstall
.\build\bin\x64\Development\fa_tests.exe
.\build\bin\x64\Development\fa_tests.exe "[assets]"
.\build\bin\x64\Development\fa_app.exe --frames 60
.\build\bin\x64\Development\fa_app.exe --frames 60 --window
```

三配置逐字比对 `m05:` 行：

```powershell
foreach ($cfg in 'Debug', 'Development', 'Release') {
  .\build\bin\x64\$cfg\fa_app.exe --frames 60 | Select-String 'm05:'
}
```

若 `m05:` 显示 `2 live GPU texture(s)` 之外的数字，先看 `failed` 与 `placeholder(s)`：
`failed` 非 0 说明有 id 未声明或占位尺寸非法；`placeholder(s)` 少于 `texture(s) cached` 说明
某些条目走了真实文件路径（v1 不应发生）。

## 全新克隆验证

针对提交 `16b1a41`（`feat(assets): implement M05 asset pipeline with catalog-driven placeholders`）
做了一次全新克隆：

```powershell
git clone --local . --no-hardlinks "$env:TEMP\fa-clone-m05"
cd "$env:TEMP\fa-clone-m05"
$env:VCPKG_ROOT = 'D:\CPPLibraries\vcpkg'
.\scripts\build.ps1 -Configuration Development      # 含空 vcpkg_installed 的 manifest 安装
.\build\bin\x64\Development\fa_tests.exe
.\build\bin\x64\Development\fa_app.exe --frames 60
```

```text
fa_tests.exe → All tests passed (17092 assertions in 215 test cases)   exit = 0
fa_app.exe   → ran 60 frame(s)
                 average: 60.0 fps
                 m06: 4.1.0 Core Profile Context 23.19.260413 on AMD Radeon (TM) Graphics
                 m05: 4 catalog entr(ies), 2 texture(s) cached, 2 placeholder(s), 1280 CPU byte(s), 2 live GPU texture(s), 0 failed
               M02+M03+M04+M05+M06+M07 smoke test OK                    exit = 0
```

新增的 `include/fa/assets/`、`assets/src/` 与 `tests/assets.test.cpp` 在克隆里都正常参与构建，
说明没有文件被 `.gitignore` 误伤（M02 曾因 `/build/` 把 `build/*.props` 一起忽略而踩过这个坑）。

> 注：克隆到 `%TEMP%` 下会额外出现 `MSB8029`（中间目录位于临时目录），
> 这是存放位置的副作用，与本仓库配置无关；正常路径下无此警告。

## 图片接入增量

- catalog 移到 `data/catalog.json`，五条都带 `path`（`grass.png`/`soil.png`/`wall.png`/
  `player.png`/`marker.png`），`rootPath` 取 `config.paths.assets`。
- app 新增 `StbImageDecoder`（独立 TU，编译期屏蔽 stb 的 /W4 警告）并在
  `AssetManagerDesc::decoders` 注册；支持 PNG/JPG/TGA/BMP。
- 缺图、无解码器、解码失败仍由 M05 回落到 `color`/`altColor`/`tile`：
  - 删除整个 `assets/`：`m05: ... 4 placeholder(s)`，程序正常；
  - 只删 `grass.png`：`1 placeholder(s)`，其余纹理继续走 PNG。
- 正常运行时 `m05: ... 0 placeholder(s)`，说明 4 张实际使用的纹理都来自 PNG。

## 遗留风险

| 风险 | 影响 | 处理 |
|---|---|---|
| 解码器由宿主注册 | M05 自带无解码器；app 注册 stb_image（PNG/JPG/TGA/BMP） | 设计取舍；缺解码器仍回落占位 |
| `retainCpuPixels = true` 常驻内存 | 每张纹理占 `w*h*bpp` 字节 | 真实美术到位后可关掉；`reload` 与 CPU 采样需要它 |
| 上传失败只留 `uploadFailed` 标记 | 调用方可能一直画不出东西 | `stats().liveGpuTextures` 与 `m05:` 行可观测 |
| 槽位是指针 | `releaseAll()` 后旧句柄读 `Unloaded`，同槽位复用语义需小心 | 契约限定 `releaseAll()` 只在拆解时调用，其余靠引用计数 |
| 无加载优先级 / 取消 | 大量请求时 worker 顺序不可控 | `JobSystem` 是 FIFO；需要时加 `LoadPriority` / `CancellationToken` |
| 无异步 JSON | 大 JSON 阻塞帧 | v1 内容表很小；需要时把 `loadJson` 也走作业 |
| 无 pak / 打包 | 发布时是一堆散文件 | `IFileSystem` 已抽象好，pak 是发布优化 |
| `R8` 路径无实机用例 | 灰度图可能没被测到 | 枚举、`bytesPerPixel`、上传分支都在；接入真实资源时补用例 |
| 运行期换 catalog 语义未定义 | 换内容包后旧句柄仍指向旧槽位 | v1 不允许运行期换表；要支持得先定义「换表 = 全部失效」 |
| 无文件监听 | 改文件不会自动重载 | 手动 `reload(id)`；watch 取决于 M02 是否暴露（v1 不做） |
| `revision` 是 `uint64` 单调计数 | 理论上溢出后消费方会漏更新 | 每秒一次重载也要 5×10¹¹ 年；不做回绕处理 |
