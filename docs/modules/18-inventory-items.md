# M18 Inventory & Items

| 项 | 值 |
|---|---|
| 层 | L3 |
| 优先级 | P1 |
| 状态 | 设计完成 |
| 依赖 | M00–M17 |
| 被依赖 | M19、M20、M24、M25、M26、M27 |

## 1. 目的

提供物品栈、背包容器、快捷栏、装备与容器间转移的通用模型。物品只由 `ContentId` 与数量描述，不携带运行时逻辑对象。

## 2. 范围

**负责**

- `ItemStack`：`ContentId` + 数量 + 可选实例数据（耐久、品质）。
- `Inventory`：固定槽位数组、堆叠上限、添加/移除/交换/拆分/合并。
- 容器类型：玩家背包、快捷栏、箱子、商店库存、掉落物。
- 转移规则：同物品自动合并、空格优先、Shift 快速转移、拖放。
- 快捷栏：选中槽位、切换、消耗。
- 装备：工具/武器槽、耐久、品质修正（预留）。
- 物品使用：消耗品、种子、放置物、工具。
- 事件：`ItemAdded`、`ItemRemoved`、`InventoryChanged`。
- 序列化：容器快照交给 M14。

**不负责**

- 物品定义（M15）。
- 工具效果（M17）。
- 商店定价/交易（M24）。
- 背包 UI 布局（M09/M26）。

## 3. 核心概念与数据结构

```cpp
struct ItemStack {
    ContentId id;
    int count = 0;
    int durability = -1; // -1 表示无耐久
    bool empty() const { return count <= 0 || id.value.empty(); }
};

class Inventory {
public:
    explicit Inventory(int slotCount, int defaultMaxStack = 99);
    Result<int> add(ItemStack stack);
    Result<ItemStack> remove(ContentId id, int count);
    bool swap(int from, int to);
    bool split(int from, int amount);
    const ItemStack& at(int slot) const;
};\n```

- 槽位索引稳定；UI 拖放只交换内容，不移动槽位。
- 添加时先填未满同类栈，再占空槽；返回剩余数量（0 表示全部放入）。
- 物品数量变化必须通过 `Inventory`，禁止外部直接改 `count`。

## 4. 公共接口草案

```cpp
namespace fa::inventory {

class IInventoryService {
public:
    virtual ~IInventoryService() = default;
    virtual Inventory* playerInventory() = 0;
    virtual Inventory* hotbar() = 0;
    virtual Result<void> transfer(Inventory& from, int fromSlot, Inventory& to, int amount) = 0;
    virtual Result<void> useItem(Inventory& inv, int slot, ecs::EntityId user) = 0;
    virtual int selectedHotbarSlot() const = 0;
};

} // namespace fa::inventory
```

## 5. 依赖关系

- 依赖 M01（Result）、M03（事件）、M11（使用者实体）、M15（物品定义）、M17（放置/使用请求）。
- 不依赖 M19/M20；物品效果通过事件或注册的 `IItemUseHandler` 分发。
- 存档由 M14 调用 `Inventory` 的快照接口，M18 不直接写文件。

## 6. 扩展点

- 新增容器类型（冰箱、邮寄箱）复用 `Inventory`。
- 新增实例数据（品质、附魔）扩展 `ItemStack` 的附加字段。
- 新增堆叠规则（按品质分栈）作为 `StackPolicy`。
- 新增自动整理/排序策略。

## 7. 实现阶段

P1，Phase C。先实现 `ItemStack`/`Inventory`、添加/移除/交换、快捷栏；再实现转移、装备、使用分发。验收：拾取 100 个作物正确堆叠；背包满时掉落物留在地上；快捷栏切换正确。

## 8. 风险与开放问题

- 耐久/品质会让“同 ID 不同栈”复杂化；v1 可先不做耐久。
- 背包容量与堆叠上限影响经济平衡，需要产品决策。
- 物品使用与工具使用的边界：种子既是物品又是工具，需要明确路由。
- 与 M24 的商店交易需要原子性（扣钱与给物品同时成功）。
