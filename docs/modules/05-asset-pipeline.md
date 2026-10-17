# M05 Asset Pipeline

| 项 | 值 |
|---|---|
| 层 | L1 |
| 优先级 | P0 |
| 状态 | 设计完成 |
| 依赖 | M00–M04 |
| 被依赖 | M06、M08、M10、M12、M14、M15、M26 |

## 1. 目的

为所有资源提供统一的稳定 ID、异步加载、缓存、引用计数句柄与热重载。玩法与渲染不直接读文件，也不关心资源来自磁盘、pak 还是内存。

## 2. 范围

**负责**

- `AssetId`：稳定字符串 ID + 64 位哈希；调试保留原字符串。
- `AssetHandle<T>`：引用计数句柄；最后一个句柄释放后可延迟卸载。
- `IAssetLoader`：按扩展名/类型注册的加载器。
- 异步加载：通过 M03 Job 读取/解码，主线程完成 GPU 上传与对象构建。
- 缓存与失败占位：缺失资源返回占位对象并记录警告。
- 热重载（Debug/Development）：文件监控 + 重新加载 + 句柄内容替换。

**不负责**

- 内容语义与校验（M15）。
- OpenGL 纹理创建/销毁（M06）。
- 存档文件（M14）。
- 资源编辑器 UI（M26）。

## 3. 核心概念与数据结构

```cpp
struct AssetId {
    std::string name;   // item.turnip, tex.player
    std::uint64_t hash; // 稳定哈希
};

template <typename T>
class AssetHandle {
public:
    T* get() const;
    T* operator->() const;
    explicit operator bool() const;
    void reset();
};

enum class AssetState { Unloaded, Loading, Ready, Failed };
```

- `AssetId` 由内容/代码声明，不从文件路径推导；路径映射由 `AssetCatalog` 维护。
- `AssetHandle` 可复制；复制增加引用计数；禁止从裸指针构造。
- 加载失败返回 `Result<AssetHandle<T>>`；调用方可选择占位或中止。

## 4. 公共接口草案

```cpp
namespace fa::assets {

class IAssetManager {
public:
    virtual ~IAssetManager() = default;
    virtual Result<AssetHandle<Texture>> loadTexture(AssetId id) = 0;
    virtual Result<AssetHandle<JsonAsset>> loadJson(AssetId id) = 0;
    virtual Result<AssetHandle<AudioClip>> loadAudio(AssetId id) = 0;
    virtual void reload(AssetId id) = 0;
    virtual void update() = 0; // 主线程完成回调/上传
};

} // namespace fa::assets
```

## 5. 依赖关系

- 依赖 M01（Result/日志）、M02（文件系统）、M03（Job）、M04（帧更新）。
- 不依赖 M06；纹理上传通过注册的 `IAssetUploader` 接口由 M06 实现，避免 Assets ↔ Render 循环。
- 玩法只依赖 `AssetHandle`/`AssetId`，不依赖具体加载器。

## 6. 扩展点

- 新增资源类型：实现 `IAssetLoader<T>` 并注册扩展名。
- 新增来源：实现 `IFileSystem`（pak/内存/远程），M05 不感知。
- 加载优先级/取消：为 `load` 增加 `LoadPriority` 与 `CancellationToken`。
- 资源目录：`AssetCatalog` 可从 JSON 生成，支持 mod/额外内容包（后续）。

## 7. 实现阶段

P0，Phase A。先实现同步加载 + 缓存 + 句柄（纹理/JSON），再接入异步与热重载。测试：缓存命中、引用计数、失败占位、路径解析、重复加载。

## 8. 风险与开放问题

- 句柄生命周期跨场景时容易泄漏；M27 提供资源计数面板。
- 异步加载完成顺序不确定；玩法需处理“资源未就绪”的占位状态。
- 热重载与 GPU 资源替换需要主线程同步，可能造成一帧卡顿；可接受。
- 是否需要 pak 打包？v1 直接目录加载，pak 作为发布优化。
