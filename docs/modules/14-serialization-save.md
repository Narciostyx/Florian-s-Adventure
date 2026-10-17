# M14 Serialization & Save

| 项 | 值 |
|---|---|
| 层 | L2 |
| 优先级 | P1 |
| 状态 | 设计完成 |
| 依赖 | M00–M13 |
| 被依赖 | M15、M18、M19、M22、M23、M24、M25 |

## 1. 目的

提供版本化的世界快照序列化、存档槽管理、原子写入、迁移与损坏恢复。存档语义与文件格式分离，玩法模块只声明“哪些组件要存”。

## 2. 范围

**负责**

- 存档结构：元数据（版本、时间戳、游戏内日期、游玩时长、截图可选）。
- 世界快照：持久化实体、组件、地图差异、玩家状态、库存、任务、经济。
- 组件序列化注册：`ComponentSerializer<T>`，按组件类型名读写。
- 存档槽：多槽位、自动存档、快速存档；槽位索引文件。
- 原子写入：写入 `.tmp` → flush → rename；保留上一份备份。
- 版本迁移：`saveVersion` + 迁移函数链；不兼容时明确报错。
- 校验：文件头魔数、版本、可选 CRC；损坏时回退到备份。
- 设置/配置读写（与存档分离）。

**不负责**

- 内容定义（M15）。
- 玩家/世界的业务规则（各玩法模块）。
- 云存档/跨设备同步（非目标）。
- 反作弊/加密（非目标，v1 明文 JSON）。

### 存档槽与自动存档（ADR-0002 决策 3）

- 槽位模型：3 个手动槽（`slot_1..3`）+ 1 个自动槽（`autosave`）。
- 自动存档触发：**游戏时间每 12 小时**（一天 24 小时，即每日 06:00 与 18:00 附近）。
- 仅在安全状态存档：非战斗、非过场、非 UI 模态阻塞时。
- 写入策略：先写 `.tmp` → `fsync` → 原子重命名为正式文件，并把旧文件轮转为 `.bak`。
- 存档内含 `saveVersion`；读取时执行迁移链，失败则该槽标为不可读并保留原文。
- 存档头保存 `playTimeSeconds`、游戏日期/时间/季节，供读档界面展示。

## 3. 核心概念与数据结构

```cpp
struct SaveMetadata {
    int saveVersion;
    std::string gameVersion;
    std::string slotName;
    std::int64_t timestampUtc;
    std::string sceneId;
    int inGameDay;
    double playSeconds;
};

struct SaveFile {
    SaveMetadata meta;
    nlohmann::json world;     // 实体 + 组件
    nlohmann::json player;
    nlohmann::json inventories;
    nlohmann::json quests;
    nlohmann::json economy;
};
```

- 组件以类型名字符串为键，避免依赖 C++ 类型索引。
- 未知组件在读取时保留并警告，便于向前兼容。
- 存档只保存差异与持久对象；地图静态数据由地图 ID 引用重建。

## 4. 公共接口草案

```cpp
namespace fa::save {

class ISaveService {
public:
    virtual ~ISaveService() = default;
    virtual Result<void> save(std::string_view slot, const SaveContext& ctx) = 0;
    virtual Result<SaveFile> load(std::string_view slot) = 0;
    virtual Result<void> apply(const SaveFile& file, SaveContext& ctx) = 0;
    virtual std::vector<SaveMetadata> listSlots() const = 0;
    virtual Result<void> deleteSlot(std::string_view slot) = 0;
};

} // namespace fa::save
```

## 5. 依赖关系

- 依赖 M01（Result）、M02（文件系统）、M11（`World`）、M12（地图 ID）、M15（内容 ID 校验）。
- 玩法模块提供 `ComponentSerializer<T>` 注册；M14 不 include 具体玩法模块头，注册在 composition root 完成。
- 不依赖 M06/M09；截图存档作为可选扩展。

## 6. 扩展点

- 新增持久组件：注册 serializer，无需改 M14。
- 新增格式（二进制/压缩）：实现 `ISaveFormat`，槽位管理不变。
- 新增迁移：`migrate_v1_to_v2(json&)` 链式注册。
- 自动存档策略：按天数/事件触发，由 M25 调度。

## 7. 实现阶段

P1，Phase C。先实现单槽 JSON 存档/读档、原子写入；再多槽、备份、迁移、校验。验收：耕地→播种→浇水→收获后存档，重启读档状态一致；故意损坏文件能回退备份。

## 8. 风险与开放问题

- JSON 体积随实体数增长；v1 可接受，后期可压缩或分块。
- 组件版本与存档版本双重演进容易混乱；统一用 `saveVersion` + 每组件 `schemaVersion`。
- 读档时资源尚未加载完成；需在场景进入后延迟应用，或使用 ID 引用。
- 单槽/多槽/自动存档频率为产品决策，架构已支持多槽。
