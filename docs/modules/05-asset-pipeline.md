# M05 Asset Pipeline

| 项 | 值 |
|---|---|
| 层 | L1 |
| 优先级 | P0 |
| 状态 | 已实现 |
| 依赖 | M00、M01、M02、M03 |
| 被依赖 | M06、M08、M10、M12、M14、M15、M26 |
| 实现记录 | [M05 资源管线：实现说明、实测结果与遗留风险](../build/m05-assets.md) |

## 1. 目的

为所有资源提供统一的稳定 ID、异步加载、缓存、引用计数句柄与重载，并把「拿不到资源怎么办」变成一条**确定的、可测的**规则。

玩法与渲染都不直接读文件，也不关心资源来自磁盘、pak 还是内存。资源缺失时游戏仍然能跑起来——这一点在 v1 尤其重要，因为**真实美术资源在所有模块完成之前都不存在**，全部由色块占位（见 [美术资源策略](../architecture/art-policy.md)）。

## 2. 范围

**负责**

- `AssetId`：稳定字符串 ID + FNV-1a 64 位哈希；名字才是身份，哈希只用于加速查找。
- `AssetCatalog`：id → 声明（种类、路径、占位尺寸、占位颜色）的映射表，由 JSON 载入。
- `AssetHandle<T>`：引用计数句柄，`AssetState` 可观测，槽位由管理器回收。
- 同步加载（`loadTexture` / `loadJson`）与异步请求（`requestTexture` + `update()`）。
- 占位降级：缺文件、无解码器、解码失败都降级为色块并**返回成功**。
- 帧线程 GPU 上传：解码在 worker，上传必须在帧线程，由 `update()` 完成。
- `reload()` 重载：同一槽位内替换内容 + 递增 `revision`，已有句柄继续有效。
- `AssetStats`：缓存快照，供 M27 调试面板与冒烟输出使用。

**不负责**

- OpenGL 纹理对象的创建/销毁（M06）——M05 只经 `ITextureUploader` 要一个不透明 token。
- 真实图片格式的解码（PNG/JPEG）——解码器是注入的 `IImageDecoder` 实现，v1 一个都没有。
- 内容语义与校验（M15）：JSON 文本原样交出，解析与合法性归内容模块。
- 存档文件（M14）。
- 资源编辑器 UI（M26）。
- 音频片段（M08）：v1 只缓存 `Texture` 与 `JsonText` 两种种类。

## 3. 交付的文件

### 公共头（`src/engine/include/fa/assets/`）

| 文件 | 内容 |
|---|---|
| `AssetError.hpp` | `asset_error` 3000–3007：`kNotInitialized`、`kUnknownAsset`、`kLoadFailed`、`kDecodeFailed`、`kCatalogParseFailed`、`kInvalidImage`、`kUnsupportedKind`、`kUploadFailed` |
| `AssetId.hpp` | `AssetId`：名字 + FNV-1a 64 哈希；`fromName`、`name`、`hash`、`isValid`、`==`/`!=`/`<`；`std::hash<AssetId>` 特化（空名 hash = 0） |
| `AssetHandle.hpp` | `AssetState`、`detail::AssetSlotBase`、`AssetHandle<T>`（引用计数、`state()`、`id()`、`useCount()`） |
| `ImageData.hpp` | `ImageFormat`（`Rgba8`、`R8`）+ `bytesPerPixel` + `toString`；`ImageData`；`makeSolidImage`、`makeCheckerboardImage`、`parseColorText`、`toColorText` |
| `AssetTypes.hpp` | `AssetKind`（`Texture`、`JsonText`）；`TextureAsset`、`JsonAsset`、`AssetStats` |
| `ITextureUploader.hpp` | `upload(image, debugName) -> std::uint32_t`（0 = 被拒）、`release(token)` |
| `IImageDecoder.hpp` | `supports(path)`、`decode(path, bytes) -> Result<ImageData>` |
| `AssetCatalog.hpp` | `AssetEntry`、`AssetCatalog`（`loadFromText`、`loadFromFile`、`find`、`entries`、`size`、`empty`、`clear`） |
| `AssetManager.hpp` | `AssetManagerDesc`、`IAssetManager`、`createAssetManager()` |

### 私有实现（`src/engine/assets/src/`）

| 文件 | 内容 |
|---|---|
| `AssetId.cpp` | 构造函数、`fromName`、`fnv1a64` |
| `ImageData.cpp` | 图像构造与颜色文本互转；`channelToByte`（clamp01 × 255 + 0.5）、`hexDigit` |
| `AssetTypes.cpp` | `toString(AssetKind)`、`toString(AssetState)` |
| `AssetCatalog.cpp` | nlohmann/json **非抛出**解析；重名 / 缺字段 / 尺寸校验 |
| `AssetManager.cpp` | 槽位缓存、异步作业、帧线程上传、`reload`、`stats`、淘汰 |

### 工程改动

| 文件 | 改动 |
|---|---|
| `src/engine/assets/fa_assets.vcxproj` | `ExceptionHandling=Sync`（`/EHsc`）；新增 `fa_events` 引用；`ClCompile` 已是通配符，新增源文件无需改工程 |
| `tests/fa_tests.vcxproj` | 新增 `fa_assets` 引用 |
| `src/app/fa_app.vcxproj` | M00 骨架期已引用 `fa_assets`，本轮无需改动 |

`fa_assets` 只引用 `fa_core`、`fa_platform`、`fa_events`，**不引用 `fa_render`**（见 §5 的反向桥）。

### 删除

`src/engine/assets/src/AssetRegistry.cpp` 是 M00 骨架期的占位实现，自己定义了一套与
`AssetManager.hpp` 同名的类型，两者编进同一个静态库必然重定义。全仓库无人引用它，已整份删除。
这与 M06 删 `Renderer.cpp`、M07 删 `InputSystem.cpp` 是同一类操作：**骨架期占位实现一旦有了正式版本，就在同一批改动里删掉**。

## 4. 核心概念与数据结构

### `AssetId`

```cpp
class AssetId {
public:
    AssetId() = default;                       // 无效 id
    explicit AssetId(std::string name);
    static AssetId fromName(std::string_view name);

    const std::string& name() const noexcept;
    std::uint64_t hash() const noexcept;
    bool isValid() const noexcept;             // !name().empty()
};
```

- id 由**内容声明**（catalog）给出，不从文件路径推导；路径只是 catalog 的一个字段。
- 相等比较只看 `name`。`hash` 存在只是为了让 `unordered_map` 不必每帧算字符串哈希；
  哈希碰撞由相等比较兜住，不构成正确性风险。
- 空名的 `hash()` 为 0，`isValid()` 为 `false`。

### `AssetHandle<T>` 与 `AssetState`

```cpp
enum class AssetState { Unloaded, Loading, Ready, Failed, Count };

template <typename T>
class AssetHandle {
public:
    AssetHandle() = default;                   // 空句柄
    // 拷贝 +1，移动转移，析构 -1
    T* get() const noexcept;                   // 非 Ready 时为 null
    T* operator->() const noexcept;
    explicit operator bool() const noexcept;
    void reset() noexcept;
    void swap(AssetHandle&) noexcept;
    bool isValid() const noexcept;
    AssetState state() const noexcept;
    AssetId id() const noexcept;
    int useCount() const noexcept;

private:
    explicit AssetHandle(detail::AssetSlotBase* slot) noexcept;
    friend class AssetManager;                 // 只有管理器能造句柄
};
```

| 状态 | 含义 | `get()` |
|---|---|---|
| `Unloaded` | 空句柄，或所在槽位的管理器已销毁 | `nullptr` |
| `Loading` | 后台作业持有它，尚未完成 | `nullptr` |
| `Ready` | 可用；**占位也算 Ready** | 非空 |
| `Failed` | 上次尝试失败；只能靠 `reload()` 脱离 | `nullptr` |

- 句柄**不能**从裸指针构造，只能由管理器发出——引用计数因此无法被外部打乱。
- `Loading` 期间 `get()` 返回 null 是刻意的：调用方不该拿到半写入的对象。
- 管理器析构后，仍在外部存活的句柄读到 `Unloaded`（槽位带 `m_retired` 标记），不会解引用已释放内存。

### `ImageData` 与占位图

```cpp
enum class ImageFormat : std::uint8_t { Rgba8, R8, Count };

struct ImageData {
    int width = 0;
    int height = 0;
    ImageFormat format = ImageFormat::Rgba8;
    std::vector<std::uint8_t> pixels;

    std::size_t rowPitch() const noexcept;      // width * bytesPerPixel
    std::size_t requiredBytes() const noexcept;
    bool isValid() const noexcept;
};
```

辅助函数：

| 函数 | 用途 |
|---|---|
| `makeSolidImage(w, h, color, format = Rgba8)` | 纯色块，最快路径 |
| `makeCheckerboardImage(w, h, a, b, cellSize)` | 双色棋盘；用来让两张占位纹理在画面上可区分 |
| `parseColorText("#rrggbb" / "#rrggbbaa")` | catalog 的颜色写法；内容作者不写浮点 |
| `toColorText(color)` | 反函数，输出 `#rrggbbaa` |

`tileSize = 0`（或不填 `tile`）→ 纯色块；`tileSize > 0` → 以该边长的棋盘。
颜色通道转字节用 `clamp01(c) * 255 + 0.5`（四舍五入到最近字节），所以 `1.0 → 255`、
`0.5 → 128`，`parseColorText` / `toColorText` 往返不漂移。

### `AssetEntry` 与 `AssetCatalog`

```cpp
struct AssetEntry {
    AssetId id;
    AssetKind kind = AssetKind::Texture;
    std::string path;                          // 相对 rootPath；空 = 纯占位
    int width = 0;                             // 占位尺寸，path 为空时必需
    int height = 0;
    core::Color color{1.0f, 0.0f, 1.0f, 1.0f}; // 主色；默认洋红 = 显眼的「缺资源」
    core::Color altColor{1.0f, 0.0f, 1.0f, 1.0f};
    int tileSize = 0;                          // 棋盘格边长；<= 0 = 纯色
};
```

`AssetCatalog` 是**独立的** id→文件表，不挂在缓存里：换一份 catalog 就是换一整套内容，
这是后续内容包 / mod 的挂钩点，而不是资源系统的改动。

v1 现用的 catalog（`src/app/main.cpp` 的 `kCatalogText`，四条全是 path-less 色块）：

```json
{
  "assets": [
    {"id": "tex.ground.grass", "kind": "texture", "size": [16, 16], "tile": 8,
     "color": "#4a8250", "altColor": "#5c8a42"},
    {"id": "tex.ground.soil", "kind": "texture", "size": [16, 16], "tile": 4,
     "color": "#6b4a2f", "altColor": "#7d5a3a"},
    {"id": "tex.actor.player", "kind": "texture", "size": [16, 24],
     "color": "#e8c39e", "altColor": "#c79a74"},
    {"id": "tex.marker", "kind": "texture", "size": [8, 8], "color": "#ffffff"}
  ]
}
```

校验规则（任一不满足 → `kCatalogParseFailed`，错误信息带出错条目下标）：

- `id` 非空且不重复；`kind` 必须是 `texture` 或 `json`；
- `kind = texture` 且 `path` 为空时，`size` 必须给出两个正整数；
- `kind = json` 必须给 `path`（JSON 没有占位，见 §7）；
- 颜色字符串必须能解析出 4 个通道。

### `AssetStats`

```cpp
struct AssetStats {
    std::size_t textureCount = 0;
    std::size_t jsonCount = 0;
    std::size_t placeholderCount = 0;   // 用占位的纹理数
    std::size_t loadingCount = 0;
    std::size_t failedCount = 0;
    std::size_t evictableCount = 0;     // 引用计数归零但仍活着
    std::size_t cpuBytes = 0;           // 所有缓存图像占的 CPU 字节
    std::size_t liveGpuTextures = 0;    // 上传接受数 - 释放数
};
```

全部字段是 `std::size_t`，打印用 `%zu`。

## 5. 公共接口

### `IAssetManager`（唯一门面）

```cpp
std::unique_ptr<IAssetManager> createAssetManager(const AssetManagerDesc& desc);

class IAssetManager {
public:
    virtual Result<AssetHandle<TextureAsset>> loadTexture(const AssetId& id) = 0;
    virtual AssetHandle<TextureAsset>        requestTexture(const AssetId& id) = 0;
    virtual Result<AssetHandle<JsonAsset>>   loadJson(const AssetId& id) = 0;

    virtual Status reload(const AssetId& id) = 0;

    virtual void update() = 0;                 // 帧线程泵
    virtual void purgeUnreferenced() = 0;
    virtual void releaseGpuTextures() = 0;
    virtual void releaseAll() = 0;

    virtual AssetStats stats() const = 0;
    virtual const AssetCatalog& catalog() const = 0;
};
```

| 方法 | 阻塞 | 失败通道 |
|---|---|---|
| `loadTexture` | 是（调用线程） | `Result`：只有「id 未声明」「占位尺寸非法」「上传被拒」是错误 |
| `requestTexture` | 否 | **无**——返回空句柄 + 一行 error 日志 |
| `loadJson` | 是 | `Result`：无占位，读不到就是错 |
| `reload` | 是 | `Status` |

`requestTexture()` 没有错误通道是刻意取舍：它是每帧都可能被调用的热路径，
调用方（渲染、动画）没有地方处理错误，也不该因为一个资源缺失就中断一帧。
真正的诊断信息在日志与 `stats()` 里。

### `AssetManagerDesc`

```cpp
struct AssetManagerDesc {
    platform::IFileSystem* fileSystem = nullptr;     // 有路径的资产必需
    const AssetCatalog* catalog = nullptr;           // 必需
    ITextureUploader* uploader = nullptr;            // 可选；为空则只留 CPU 像素
    events::JobSystem* jobs = nullptr;               // 可选；为空则同步加载
    std::span<IImageDecoder* const> decoders{};      // 按序尝试，第一个 supports() 的胜出
    std::string rootPath;                            // 与 AssetEntry::path 拼接
    bool retainCpuPixels = true;                     // 上传后是否保留像素（reload 需要）
    bool logEvents = true;                           // 测试里故意造失败时可关掉
};
```

全部是**借用指针**：composition root 比管理器活得久，为空的依赖等价于「关掉那个特性」。
`createAssetManager()` 在 catalog 或 fileSystem 为空时返回 null 并记一行日志。

`jobs == nullptr` 时 `requestTexture()` 退化为同步加载，句柄返回时已经是 `Ready` 或 `Failed`，
测试得以在没有任何线程的情况下覆盖同一条代码路径。

### `ITextureUploader`（打破循环依赖的反向桥）

```cpp
class ITextureUploader {
public:
    virtual std::uint32_t upload(const ImageData& image, std::string_view debugName) = 0;
    virtual void release(std::uint32_t token) = 0;
};
```

`fa_assets` 与 `fa_render` 互不依赖。GPU 对象由 M06 创建，M05 只经这个接口拿一个
`std::uint32_t` token（取值即 `fa::render::TextureHandle::value()`），由 composition root
在 app 里适配。这样两个模块之间没有编译期依赖，将来换后端只需要换 `ITextureUploader` 的实现。

0 表示「上传被拒」，此时 `TextureAsset::uploadFailed = true`，CPU 像素依然可用。

### `IImageDecoder`

```cpp
class IImageDecoder {
public:
    virtual bool supports(std::string_view path) const = 0;
    virtual Result<ImageData> decode(std::string_view path,
                                     std::span<const std::uint8_t> bytes) = 0;
};
```

v1 **没有任何真实解码器**。这是设计使然：美术资源全部是 catalog 里的色块，`path` 为空。
等真实 PNG 进来时注册一个解码器即可，`AssetManager` 不需要改。

## 6. 使用范式

### 帧契约

```text
每帧（在 update 阶段之后、渲染提交之前）：
    assets->update();          // 非阻塞：收作业结果、上传像素、淘汰无人引用的槽位

关停（在 renderer.shutdown() 之前）：
    handles.clear();           // 先放掉句柄
    assets->releaseAll();
    assets.reset();
    uploader.reset();          // 最后才让上传器失效
    renderer->shutdown();      // 再销毁 GL 上下文
```

`update()` **不阻塞**：它只看 `JobSystem::isComplete()`，没完成就下帧再看。
只有同步路径（`loadTexture` / `loadJson` / `reload`）才会 `wait()`。

### 申请一张纹理（异步，热路径）

```cpp
AssetHandle<TextureAsset> handle = assets->requestTexture(AssetId::fromName("tex.ground.grass"));
// handle.state() 可能是 Loading；本帧先用上一次的纹理，或者干脆不画
if (handle && handle->gpuTexture != 0) {
    render::TextureHandle tex{handle->gpuTexture};
    renderer->drawSprite(tex, position, size);
}
```

### 加载一份 JSON（同步，一次性）

```cpp
auto result = assets->loadJson(AssetId::fromName("data.items"));
if (result.isError()) {
    logf(Level::Error, "items: %.*s", static_cast<int>(result.error().message.size()),
         result.error().message.data());
    return result.error();
}
auto parsed = parseItems(result.value()->text);   // 解析归内容模块
```

### 占位降级：为什么「缺文件」不算错误

```text
catalog 有 entry，path 为空            → 用 color/altColor/tileSize 生成占位，Ready
catalog 有 entry，path 指向的文件缺失  → 占位 + Warn 日志，Ready
catalog 有 entry，没有解码器 supports  → 占位 + Warn 日志，Ready
catalog 有 entry，解码失败             → 占位 + Warn 日志，Ready
catalog 没有这个 id                    → kUnknownAsset，Failed
占位尺寸非法（width/height <= 0）       → kInvalidImage，Failed
上传器拒绝了图像                        → kUploadFailed，Failed
```

只有**最后三行**是错误。前四行全部 `Ready`，因为对 v1 来说「美术还没做」是常态而不是故障
——游戏必须在没有一张图的情况下跑通全部玩法。

### `reload()` 的语义

- 复用同一槽位，**不**产生新句柄：已发出去的句柄继续有效，内容是新的。
- 只有成功的 (re)load 才递增 `revision`，所以首次成功加载后是 `1`，重载一次变 `2`；
  失败的 `reload()` 保留旧内容并返回 `Status`。
- 消费方（动画剪辑、内容表）用 `revision` 判断「要不要重建派生数据」，
  比逐字节比较像素便宜得多。

### app 的接法

`src/app/main.cpp` 的 `AssetUploaderAdapter` 实现了 `ITextureUploader`，
内部调用 `IRenderer::createTexture`——这是两个模块唯一相遇的地方：

```cpp
class AssetUploaderAdapter final : public fa::assets::ITextureUploader {
public:
    explicit AssetUploaderAdapter(fa::render::IRenderer& renderer) noexcept : m_renderer(renderer) {}

    std::uint32_t upload(const fa::assets::ImageData& image, std::string_view debugName) override {
        fa::render::TextureDesc desc;
        desc.width = image.width;
        desc.height = image.height;
        desc.format = image.format == fa::assets::ImageFormat::R8 ? fa::render::TextureFormat::R8
                                                                 : fa::render::TextureFormat::Rgba8;
        desc.pixels = image.pixels.empty() ? nullptr : image.pixels.data();
        desc.rowPitchBytes = static_cast<int>(image.rowPitch());   // size_t → int
        desc.debugName = debugName;

        fa::render::TextureHandle handle;
        if (m_renderer.createTexture(desc, handle).isError()) {
            return 0;
        }
        m_live.push_back(handle);
        return handle.value();
    }

    void release(std::uint32_t token) override {
        for (std::size_t i = 0; i < m_live.size(); ++i) {
            if (m_live[i].value() == token) {
                m_renderer.destroyTexture(m_live[i]);
                m_live.erase(m_live.begin() + static_cast<std::ptrdiff_t>(i));
                return;
            }
        }
    }

private:
    fa::render::IRenderer& m_renderer;
    std::vector<fa::render::TextureHandle> m_live;
};
```

冒烟输出（三配置逐字相同）：

```text
m05: 4 catalog entr(ies), 2 texture(s) cached, 2 placeholder(s), 1280 CPU byte(s), 2 live GPU texture(s), 0 failed
```

4 条 catalog 声明里只请求了 2 张（`tex.ground.grass` 与 `tex.marker`）；
2 张都是占位（没有一条带 `path`）；1280 字节 = 16×16×4 + 8×8×4。

## 7. 契约要点

| 契约 | 说明 |
|---|---|
| `AssetId` 的身份是名字 | 哈希只用于加速查找；不要依赖哈希值本身做持久化 |
| 句柄只能由管理器产生 | 构造函数私有 + `friend class AssetManager`；外部无法伪造引用计数 |
| 非 `Ready` 时 `get()` 返回 null | 调用方必须检查 `state()` 或 `operator bool`——`operator bool` 等价于「现在能用」 |
| 占位算 `Ready` | 用 `TextureAsset::placeholder` 区分「真资源」与「色块」 |
| `requestTexture()` 无错误通道 | 失败表现为空句柄 + 一行日志；热路径不为资源缺失中断 |
| `update()` 非阻塞 | 帧时间不被资源加载绑架；未完成的作业下帧再看 |
| 上传只在帧线程 | 解码在 worker，`upload()` 只由 `update()` 调用，GL 上下文因此始终正确 |
| JSON 没有占位 | 伪造一份 JSON 会掩盖内容 bug，还会把错误推迟到更难查的地方 |
| `reload()` 不换句柄 | 只换内容 + 递增 `revision`；失败的 reload 保留旧内容 |
| `AssetManagerDesc` 全部借用 | 管理器不拥有 fileSystem / catalog / uploader / jobs / decoders |
| `fa_assets` 不 include `fa_render` | GPU 只经 `ITextureUploader` 的整数 token 出现 |
| 公开面无 SDL 类型 | `fa_assets` 只前向声明 `platform::IFileSystem`，不 include 平台实现 |
| 销毁顺序 | 句柄 → `releaseAll()` → `assets.reset()` → `uploader.reset()` → `renderer.shutdown()` |
| `releaseAll()` 只在拆解时调用 | 它让外部残留句柄变成 `Unloaded`，运行期调用等于单方面撕毁契约 |

## 8. 验收

环境、踩坑与复现命令见 [M05 实现记录](../build/m05-assets.md)。

| 配置（x64） | 构建 | `fa_tests` | `fa_app --frames 60` |
|---|---|---|---|
| Debug | 零 warning | 215 用例 / 17092 断言 | exit 0 |
| Development | 零 warning | 215 用例 / 17092 断言 | exit 0 |
| Release | 零 warning | 215 用例 / 17092 断言 | exit 0 |

`[assets]` 单跑：**61 用例 / 312 断言**。

用例覆盖：id 哈希与相等、句柄引用计数与移动语义、`AssetState` 转移、catalog 解析与全部校验分支、
占位生成（纯色 / 棋盘 / 颜色文本往返）、同步加载、异步请求 + `update()` 完成、根路径拼接、
`reload` 的 revision 语义、失败槽位保持失败、`purgeUnreferenced`、`releaseGpuTextures`、
`releaseAll` 后旧句柄读 `Unloaded`、`stats` 各字段。

冒烟链路末行 `M02+M03+M04+M05+M06+M07 smoke test OK`；M05 接管纹理后 `m06:` 行仍是
`3 live texture(s)`（第 3 张来自渲染器自建的白纹理），说明上传 token 与渲染器句柄对得上。

## 9. 风险与开放问题

| 风险 | 影响 | 处理 |
|---|---|---|
| 没有任何真实解码器 | 只能画色块与程序化图像 | v1 有意；真实美术接入时注册 `IImageDecoder`，`AssetManager` 不改 |
| `retainCpuPixels = true` 是默认值 | 每张纹理常驻 `w*h*bpp` 字节 | 换真实美术后可关掉；`reload` 与 CPU 采样需要它 |
| 上传失败只留 `uploadFailed` 标记 | 调用方可能一直画不出东西 | `stats().liveGpuTextures` 与 `m05:` 行可观测；M27 面板可高亮 |
| 槽位是指针 | `releaseAll()` 后旧句柄读 `Unloaded`，但同槽位复用时语义要小心 | 契约限定 `releaseAll()` 只在拆解 composition root 时调用，其余靠引用计数 |
| 无加载优先级 / 取消 | 大量请求时 worker 顺序不可控 | `JobSystem` 是 FIFO；真实需要时加 `LoadPriority` 与 `CancellationToken` |
| 无异步 JSON | 大 JSON 会阻塞帧 | v1 内容表都很小；需要时把 `loadJson` 也走作业 |
| 无 pak / 打包 | 发布时是一堆散文件 | v1 直接目录加载；`IFileSystem` 已抽象好，pak 是发布优化 |
| `R8` 路径无实机用例 | 灰度图可能没被测到 | 枚举、`bytesPerPixel`、上传分支都在；接入真实资源时补一条用例 |
| 运行期换 catalog 语义未定义 | 换内容包后旧句柄仍指向旧槽位 | v1 不允许运行期换表；要支持得先定义「换表 = 全部失效」 |
| 无文件监听 | 改了文件不会自动重载 | 手动 `reload(id)`；watch 取决于 M02 是否暴露（v1 不做） |
