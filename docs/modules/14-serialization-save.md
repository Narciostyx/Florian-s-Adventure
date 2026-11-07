# M14 Serialization & Save

| 项 | 值 |
|---|---|
| 层 | L2 |
| 优先级 | P1 |
| 状态 | 已实现 |
| 依赖 | M01、M02、M11 |
| 被依赖 | M15、M18、M19、M22、M23、M24、M25 |

> 实测与踩坑：[构建记录](../build/m14-serialization-save.md)

## 1. 目的

提供版本化的 JSON 存档、槽位管理、原子写入、`.bak` 恢复与迁移链。存档格式与玩法语义分离：M14 只提供 `SaveArchive` 值树与槽位服务，具体组件通过 `IComponentSerializer` 注册。

## 2. 范围

**已实现**

- `SaveArchive`：JSON 值树（object/array/标量/Vec2/Rect），不把 `nlohmann::json` 暴露到公共头。
- `SaveMetadata`：版本、引擎版本、槽名、UTC 时间戳、场景、游戏日、游玩时长、可读标记。
- `SaveContext` / `SaveFile`：world/player/inventories/quests/economy 五个 JSON 段。
- `SaveService`：`save` / `load` / `apply` / `listSlots` / `deleteSlot`。
- 原子写入：`IFileSystem::writeTextAtomic`（`.tmp` + rename）；写新文件前把旧文件轮转为 `.bak`。
- 损坏恢复：主文件缺失或解析失败时自动尝试 `.bak`，`SaveFile::recoveredFromBackup` 标记来源。
- 版本迁移：`registerMigration(fromVersion, fn)` 链式执行；版本高于当前值时明确报 `kUnsupportedVersion`。
- 槽位校验：1..64 的 `[A-Za-z0-9_-]`，拒绝路径穿越。
- `ComponentSerializerRegistry`：按名字注册、重复拒绝、`saveAll`/`loadAll`。
- 错误码段：`save_error` 6100–6107。

**未实现 / 后续**

- 地图差异、玩家/库存/任务/经济的现成序列化器；目前由 composition root 手动填充 `SaveArchive`。
- 截图、CRC、加密、压缩、云存档。
- 自动存档调度（每 12 游戏小时）由 M25 触发，M14 只提供槽位与写入。
- 读档时资源尚未加载完成的问题由 M25 在场景进入后应用。
- 二进制/压缩格式；`ISaveFormat` 抽象尚未抽出。

## 3. 核心概念与数据结构

```cpp
inline constexpr int kSaveVersion = 1;
inline constexpr std::string_view kSaveMagic = "FA_SAVE";

struct SaveMetadata {
    int saveVersion = kSaveVersion;
    std::string gameVersion;
    std::string slotName;
    std::int64_t timestampUtc = 0;
    std::string sceneId;
    int inGameDay = 0;
    double playSeconds = 0.0;
    bool readable = true;
};

struct SaveContext {
    std::string slotName;
    std::string sceneId;
    int inGameDay = 0;
    double playSeconds = 0.0;
    SaveArchive world, player, inventories, quests, economy;
};

struct SaveFile {
    SaveMetadata meta;
    SaveArchive world, player, inventories, quests, economy;
    bool recoveredFromBackup = false;
};
```

- `SaveArchive` 的默认构造是 JSON `null`；`object()` / `array()` 明确容器类型。
- 数值使用 `setInt` / `setInt64` / `setFloat` / `setDouble`；读取失败返回 false，不抛异常。
- 文件顶层为 `magic`、`version`、`meta` 与五个 section。
- 组件 section 以 serializer 名字为键；未知 section 在读取时保留但不消费。

## 4. 公共接口

```cpp
namespace fa::save {

class SaveArchive {
public:
    static SaveArchive object();
    static SaveArchive array();
    static core::Result<SaveArchive> parse(std::string_view text);

    bool isNull/isObject/isArray() const;
    std::size_t size() const;
    bool has(std::string_view key) const;
    SaveArchive child(std::string_view key) const;
    std::vector<std::string> keys() const;
    SaveArchive at(std::size_t index) const;
    void push(SaveArchive value);
    void setChild(std::string_view key, SaveArchive value);

    void setBool/setInt/setInt64/setFloat/setDouble/setString/setVec2/setRect(...);
    bool getBool/getInt/getInt64/getFloat/getDouble/getString/getVec2/getRect(...) const;

    std::string dump() const;
};

class ISaveService {
public:
    virtual core::Status save(std::string_view slot, const SaveContext& context) = 0;
    virtual core::Result<SaveFile> load(std::string_view slot) = 0;
    virtual void apply(const SaveFile& file, SaveContext& context) = 0;
    virtual std::vector<SaveMetadata> listSlots() = 0;
    virtual core::Status deleteSlot(std::string_view slot) = 0;
};

class SaveService final : public ISaveService {
public:
    using Migration = std::function<core::Status(SaveArchive&)>;
    SaveService(platform::IFileSystem& files, std::string directory = "saves",
                int currentVersion = kSaveVersion);

    void registerMigration(int fromVersion, Migration migration);
    static bool isValidSlotName(std::string_view slot) noexcept;
    std::string slotPath(std::string_view slot) const;
    std::string backupPath(std::string_view slot) const;
    int currentVersion() const noexcept;
};

class IComponentSerializer {
public:
    virtual std::string_view name() const noexcept = 0;
    virtual void save(const scene::World& world, SaveArchive& out) const = 0;
    virtual void load(scene::World& world, const SaveArchive& in) const = 0;
};

class ComponentSerializerRegistry {
public:
    bool add(std::unique_ptr<IComponentSerializer> serializer);
    std::size_t size() const noexcept;
    const IComponentSerializer* at(std::size_t index) const noexcept;
    void saveAll(const scene::World& world, SaveArchive& out) const;
    void loadAll(scene::World& world, const SaveArchive& in) const;
};

}  // namespace fa::save
```

## 5. 依赖关系

- 依赖 M01（`Result`/`Vec2`/`Rect`/`engineVersion`）、M02（`IFileSystem` 原子写/目录/列表）、
  M11（`scene::World`，只用于 serializer 接口）。
- **不依赖 M05/M06/M09/M12/M15**：文件名与路径由调用方决定，地图/内容语义由调用方写入 `SaveArchive`。
- `SaveArchive.cpp` 是唯一 `nlohmann::json` 用户，公共头不泄漏第三方类型；`fa_save` 因此打开 `/EHsc`。
- 玩法模块通过 `IComponentSerializer` 注册，不反向依赖 `fa_save` 的具体实现。

## 6. 扩展点

- 新增持久组件：实现 `IComponentSerializer` 并在 composition root 注册。
- 新增存档段：直接 `SaveContext` 加字段并写 `SaveArchive`。
- 新增迁移：`registerMigration(1, fn)`，链式执行到当前版本。
- 新增格式：抽出 `ISaveFormat`，槽位/备份/迁移逻辑保持不变。
- 自动存档：M25 每 12 游戏小时调用 `save("autosave", ctx)`。
- 截图画布与 CRC：作为 metadata/文件头扩展，不改变 section 结构。

## 7. 验收

- `fa_tests.exe "[save]"` → 7 用例 / 82 断言全绿。
- 全量 → 458 用例 / 28826 断言全绿。
- 三配置 `fa_app.exe --frames 60` 都执行一次 `autosave` 写入 + 读回，打印 `m14:` 行。
- Debug/Development/Release 构建零 warning。

## 8. 风险与开放问题

- JSON 体积随实体数增长；v1 可接受，后期可压缩或分块。
- 没有现成的 World/Inventory serializer；app 只保存了少量摘要字段，真实玩法需补注册。
- `SaveArchive` 是深拷贝值树，大存档遍历会有分配；存档不在每帧热路径上。
- 迁移链只在加载时执行；`meta.saveVersion` 被统一提升到当前版本，原始磁盘版本只在文件 `version` 字段。
- `.bak` 只保留一份；连续两次损坏仍可能丢失。
- 没有 CRC；`writeTextAtomic` 保证写入原子性，但不检测磁盘位翻转。
- 读档应用时机由 M25 决定；资源未加载时不要直接应用实体引用。
- 自动存档的“安全状态”判定归 M25，M14 不判断战斗/过场。
