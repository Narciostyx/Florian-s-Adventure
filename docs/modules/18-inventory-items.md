# M18 Inventory & Items

| 项 | 值 |
|---|---|
| 层 | L3 |
| 优先级 | P1 |
| 状态 | 已实现 |
| 依赖 | M01（`Result`）、M15（`ContentId`/`ItemDef`）；仅 include M11 的 `EntityId` 头 |
| 被依赖 | M19、M20、M24、M25、M26、M27 |

## 1. 目的

提供物品栈、固定槽位容器、快捷栏、装备与容器间转移的通用模型。物品只由
`ContentId` 与数量描述，不携带运行时逻辑对象；具体工具/食物效果由注册的
`IItemUseHandler` 实现，M18 只负责校验、分发和记录。

## 2. 范围

**已实现**

- `ItemStack`：`ContentId` + 数量 + 可选耐久；`empty()` 与相等比较。
- `Inventory`：固定槽位、按 `ItemDef::maxStack` 或默认上限堆叠、添加/移除/交换/
  拆分/合并/转移、`spaceFor`/`countOf`/`stats`。
- 添加规则：先填同类未满栈（同 id 且同耐久），再占空槽；返回剩余数量。
- 移除规则：按 id 跨槽移除；数量不足时**原子失败**，不改动任何槽。
- 事件：`Added`/`Removed`/`Merged`/`Swapped`/`Split`/`Restored`/`Changed`，
  记录在 `events()`，由组装根决定是否发布到 M03。
- 存档接口：`slots()` 快照、`setSlot()`、`restore(span)`；M18 不写文件。
- `Hotbar`：固定槽位 + 选中/循环/消耗选中项。
- `Equipment`：`Tool`/`Weapon` 两槽；每槽只接受数量 1 的物品。
- `IItemUseHandler` + `ItemUseService`：按 `ItemDef` 分发；带 `kSlotEmpty`/
  `kUnknownItem`/`kNoItemHandler` 校验与 Used/Failed 事件。
- `PlayerInventory`：背包 + 快捷栏 + 装备；`moveToHotbar`/`moveToBackpack`
  实现 Shift 快速转移（`amount < 0` 表示整堆）。
- 冒烟集成：120 种子堆叠、快速转移、装备锄头、切换快捷栏、使用一份汤。

**后续接入（本模块不负责）**

- 具体食物/种子/工具效果：M19/M20 注册 `IItemUseHandler`。
- 背包 UI 布局、拖放与滚动：M09/M26；M18 只提供数据与稳定槽位。
- 商店扣钱与给物品的原子事务：M24。
- 存档文件与版本迁移：M14；M18 只提供快照/恢复。
- 耐久扣减、品质/附魔、按品质分栈：后续 `StackPolicy` 扩展。
- 自动整理/排序、重量/容量限制。

## 3. 核心概念与数据结构

命名空间 `fa::inventory`；错误码 7019–7030。

```cpp
struct ItemStack {
    content::ContentId id;
    int count = 0;
    int durability = -1; // -1 = 无耐久；v1 只写 -1
    bool empty() const noexcept; // count <= 0 或 id 无效
};

enum class InventoryEvent::Kind {
    Added, Removed, Merged, Swapped, Split, Restored, Changed
};

struct InventoryEvent {
    Kind kind;
    int slot;       // -1 = 聚合 Changed
    int otherSlot;  // swap/split/merge 的第二槽
    content::ContentId id;
    int amount;
};

class Inventory {
    // slotCount 固定；槽位索引稳定，UI 拖放只交换内容
    core::Result<int> add(const ItemStack&);          // 返回剩余
    core::Result<int> addToSlot(int, const ItemStack&);
    core::Result<ItemStack> remove(ContentId, int);   // 不足则原子失败
    core::Result<ItemStack> removeAt(int, int);
    core::Result<ItemStack> takeAt(int);
    core::Status swap(int, int);
    core::Result<int> split(int from, int amount);    // 返回新槽
    core::Result<int> merge(int from, int to);        // 返回源剩余
    core::Result<int> transferTo(Inventory&, int, int);
    core::Status setSlot(int, const ItemStack&);
    core::Status restore(std::span<const ItemStack>);
};
```

- `maxStackFor(id)` 在绑定 `ContentDatabase` 时读 `ItemDef::maxStack`，否则用构造参数。
- `ContentDatabase*` 是**借用**指针，必须比 `Inventory` 活得久；可以后置 `setContentDatabase()`。
- `Hotbar` 内部拥有一个 `Inventory`，选中索引与内容分开，存档可分别快照。
- `Equipment` 固定两槽，不自动校验物品类别（类别策略属于 M19/M20/内容约定）。
- `ItemUseService` 持有 `const ContentDatabase&` 和一组**非 owning** handler 指针；
  handler 自己决定是否 `removeAt(slot, 1)` 消耗物品。
- `PlayerInventory` 只是三个容器的聚合，不引入抽象服务接口；需要接口时再加
  `IInventoryService`，避免 v1 过早抽象。

## 4. 公共接口

```cpp
// ItemStack.hpp
struct ItemStack { content::ContentId id; int count; int durability; ... };

// Inventory.hpp
class Inventory {
public:
    explicit Inventory(int slotCount, int defaultMaxStack = 99,
                       const content::ContentDatabase* content = nullptr);
    core::Result<int> add(const ItemStack& stack);
    core::Result<ItemStack> remove(const content::ContentId& id, int count);
    core::Result<ItemStack> removeAt(int slot, int count);
    core::Result<ItemStack> takeAt(int slot);
    core::Status swap(int from, int to);
    core::Result<int> split(int from, int amount);
    core::Result<int> merge(int from, int to);
    core::Result<int> transferTo(Inventory& to, int fromSlot, int amount);
    core::Status setSlot(int slot, const ItemStack& stack);
    core::Status restore(std::span<const ItemStack> slots);
    std::span<const InventoryEvent> events() const;
};

// Hotbar.hpp
class Hotbar {
public:
    bool select(int slot) noexcept;
    int cycle(int delta) noexcept;
    const ItemStack& selectedStack() const noexcept;
    core::Result<ItemStack> consumeSelected(int count = 1);
};

// Equipment.hpp
enum class EquipmentSlot : std::uint8_t { Tool, Weapon, Count };
core::Result<ItemStack> equip(EquipmentSlot, const ItemStack&);
core::Result<ItemStack> unequip(EquipmentSlot);

// ItemUseHandler.hpp / ItemUseService.hpp
class IItemUseHandler {
    virtual bool handles(const content::ItemDef&) const noexcept = 0;
    virtual core::Status use(ItemUseContext&) = 0;
};
class ItemUseService {
    void registerHandler(IItemUseHandler& handler);
    core::Status use(Inventory& inventory, int slot, scene::EntityId user);
};
```

冒烟中的 `scene::EntityId{1,1}` 是合成句柄：v1 的 use 分发不解析世界实体，
真实系统（M20/M23）可以在 handler 里用 `user` 查 `World`。

## 5. 依赖关系

- `fa_inventory` 只引用 `fa_core` 与 `fa_content`。
- 只 include `fa/scene/EntityId.hpp`（纯值头），**不链接 `fa_scene`**；不引用
  `fa_assets`/`fa_render`/`fa_input`/`fa_interaction`。
- **不需要 `/EHsc`**：没有 JSON、`<chrono>` 或 `<unordered_map>`；handler 注册表用
  `std::vector` 线性查找，避免把异常开关拖进来。
- 物品定义只存 `ContentId`，不存 `ItemDef*`：内容热重载会移动 `vector`，指针会失效。
- `ContentDatabase&`/`*` 都是借用；组装根必须保证内容库比容器和 `ItemUseService` 活得久。

## 6. 扩展点

- **新容器**：复用 `Inventory`；箱子/商店库存/掉落物只是不同槽位配置。
- **堆叠策略**：在 `sameKind()`/`maxStackFor()` 处扩展按品质、耐久、附魔分栈。
- **新物品效果**：实现 `IItemUseHandler` 并在组装根注册；M18 不改。
- **装备规则**：目前只要求数量 1；类别/等级/耐久扣减由 M19/M20 或内容策略决定。
- **事件发布**：`events()` 可直接映射到 M03；`Changed` 适合做 UI 脏标记。
- **存档**：M14 读取 `slots()` 与 `Hotbar::selected()`，写入 `restore()`。

## 7. 实现阶段

P1，Phase C。v1 已交付核心容器、堆叠/移除/交换/拆分/合并/转移、快捷栏、装备、
使用分发与玩家容器聚合。后续顺序：耐久/品质分栈 → 背包网格 UI（M09）→ 物品掉落
（M19）→ 商店原子交易（M24）→ 存档槽（M14）。

## 8. 风险与开放问题

1. **耐久只参与分栈，不参与移除**：`remove(id, count)` 会把不同耐久合并计数，返回的
   耐久取自第一个匹配槽；真正的耐久系统需要按实例拆分。
2. **`ItemStack::empty()` 同时表示 count ≤ 0 与 id 无效**：`setSlot`/`restore` 已单独
   拒绝“正数量 + 无效 id”，但调用方仍应避免构造这种栈。
3. **使用不自动消耗**：handler 必须自己 `removeAt`；忘记消耗会无限使用。
4. **商店交易不原子**：M24 需要自己保证扣钱与给物品同时成功，M18 不提供事务。
5. **无重量/容量/分类限制**：任何物品都能进任何容器。
6. **无自动整理/排序**：槽位稳定是刻意设计，排序是上层策略。
7. **事件只记录不发布**：M03 接线由组装根决定。
8. **无存档格式/版本**：`restore` 只校验槽数与堆叠上限。
9. **非线程安全**：容器只在主线程/单系统内使用。
10. **`ContentDatabase` 生命周期是调用方责任**：悬空指针会导致 `maxStackFor`/use 崩溃。
11. **装备不校验类别**：Tool 槽可以放食物；内容约定/上层系统需要在装备前过滤。
