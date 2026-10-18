# M24 Economy / Crafting / Shops —— 构建记录 / 实测

对应模块文档：[24-economy-crafting-shops.md](../modules/24-economy-crafting-shops.md)

> **状态：已实现并三配置实测通过。** v1 提供钱包、商店买卖、每日库存、加工任务与
> 出货箱结算；交易先检查金币/容量/库存再修改状态。app 用玩家背包跑通
> “买种子 → 卖萝卜 → 做汤 → 出货”，摘要报告金币与事件。

## 1. 实际落地

新增工程 `src/engine/economy/fa_economy.vcxproj`（GUID `{C0DE001A-…001A}`），
引用 `fa_core`、`fa_content`、`fa_inventory`。`ShopDatabase.cpp` 使用
nlohmann/json，因此本工程打开 `/EHsc`。

| 文件 | 职责 |
|---|---|
| `fa/economy/EconomyTypes.hpp` | `ShopDef`/`ShopEntry`、`ProcessingJob`、经济事件/统计 |
| `fa/economy/ShopDatabase.hpp` + `src/ShopDatabase.cpp` | 商店 JSON 加载、查找、重复/字段校验 |
| `fa/economy/EconomyService.hpp` + `src/EconomyService.cpp` | 钱包、买卖、加工、出货、每日刷新 |
| `tests/economy.test.cpp` | `[economy]` 10 用例 / 90 断言 |

### 关键实现选择

- **配方来自 M15**：`EconomyService::startCraft()` 读取 `content::RecipeDef`
  （输入、输出、分钟、`machineKind`、季节掩码），不复制配方表。
- **商店是 M24 本地表**：M15 还没有商店文档，`ShopDatabase` 负责
  `shop.*`、`item.*` 前缀与价格/限购校验；后续可迁入 M15。
- **交易原子性**：买入先检查每日剩余、金币、背包空间，再 `add()` 物品、扣钱、
  减库存；卖出先检查数量与价格，再 `remove()` 并加钱。失败路径不改变任何状态。
- **价格**：买入 = 商店 `price × buyMultiplier`；卖出 = 商店挂牌价（没有则
  `ItemDef::sellPrice`）× `sellMultiplier`；出货箱 = `ItemDef::sellPrice × 0.5`。
- **每日库存**：`EconomyService` 用 `map<shop, vector<int>>` 保存剩余量；
  `onDayChanged()` 重置为 `daily_limit`（`-1` 表示无限）。
- **加工**：`startCraft()` 先检查季节/工作站/输入，再扣除输入并创建
  `ProcessingJob`；`update(minutes)` 推进到 `ready`；`collectCraft()` 检查输出
  空间后交付并删除任务。输入扣除中途失败会回滚已扣物品。
- **出货箱**：`ship()` 把物品移入 pending；`onDayChanged()` 按售价结算成金币并清空。
- **金币归属**：v1 由 `EconomyService` 持有 `int m_gold`，尚未写回
  `player::Wallet`；组装根可以双向同步。
- **app 集成**：
  - 内联 `kShopText`：`shop.general` 卖种子（限购 5）、萝卜、汤。
  - `initializeEconomy()` 在 M18 背包填充后运行：买 3 种子、卖 2 萝卜、
    做 1 份汤、出货 1 份汤并结算新一天。
  - HUD 右上角显示 `GOLD`；摘要新增 `m24:` 行，smoke OK 加入 `+M24`。

### 商店格式

```json
{
  "schema_version": 1,
  "shops": [{
    "id": "shop.general", "name": "General Store",
    "buy_multiplier": 1.0, "sell_multiplier": 0.5,
    "stock": [{"item": "item.turnip_seed", "price": 10, "daily_limit": 5}]
  }]
}
```

## 2. 测试

`[economy]` 覆盖：

- 商店 JSON 加载、查找、坏文档拒绝。
- 买入成功、金币不足、背包满、每日限购；失败时金币/背包不变。
- 每日库存重置。
- 卖出扣物品与金币、数量不足。
- 买入/卖出价格倍率。
- 加工扣输入、时间推进、任务 ready、取回产物。
- 缺输入、错工作站、错季节拒绝且不扣输入。
- 输出空间不足时任务保留。
- 出货 pending 与次日结算、金币到账。
- 事件计数与 `clear()`。

实测：

```text
fa_tests.exe "[economy]" → All tests passed (90 assertions in 10 test cases)
fa_tests.exe             → All tests passed (29791 assertions in 530 test cases)
fa_app.exe --frames 60   → m24: gold 170 (+60 settled), seed 102, turnip 1, soup 2,
                           buy ok, sell ok, craft ok, collect ok, ship ok;
                           1 bought / 1 sold / 1 crafted / 1 shipped,
                           30 spent / 100 earned, 8 event(s)
                         → M02+…+M23+M24+M26+M27 smoke test OK
```

## 3. 遇到的问题与结论

1. **M15 没有商店表。** 直接扩 M15 会牵动解析/校验/测试；v1 让 M24 自带
   `ShopDatabase`，只做前缀与价格校验，物品存在性仍由调用方保证。
2. **配方季节默认 `Any`。** 测试里把配方改成 `["spring"]` 才能验证冬季拒绝；
   这也说明季节经济需要内容作者显式写掩码。
3. **背包满时不能先扣钱。** 买入顺序是空间 → 加物品 → 扣钱；卖出是数量 →
   移除 → 加钱。失败路径不留下半成交状态。
4. **加工输入扣除要回滚。** `remove()` 逐个扣输入；中途失败时把已扣输入加回，
   避免任务创建失败却丢材料。
5. **产物空间在取回时检查。** 加工开始时只扣输入，不预留输出格；`collectCraft()`
   空间不足会保留任务，玩家清背包后再取。
6. **金币还没有接 `player::Wallet`。** M24 自己持有 `m_gold`；M25/M18 需要决定
   唯一钱包归属，避免 HUD、任务奖励、商店各存一份。
7. **`onDayChanged()` 目前由 app 手动调用。** M22 的 `DayChanged` 还没有自动路由；
   M25 应把日终结算、自动存档与出货结算排序。

## 4. 验证命令

```powershell
$env:VCPKG_ROOT='D:\CPPLibraries\vcpkg'
$msbuild='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
foreach ($cfg in @('Debug','Development','Release')) {
  & $msbuild FloriansAdventure.sln /t:Rebuild /p:Configuration=$cfg /p:Platform=x64 /m /nologo /v:m
  & ".\build\bin\x64\$cfg\fa_tests.exe"
  & ".\build\bin\x64\$cfg\fa_app.exe" --frames 60
}
```

## 5. 已知限制 / 后续

- 没有商店 UI/交易确认；当前只有服务与脚本演示。
- 没有按季节重建库存/价格；`onSeasonChanged` 尚未实现。
- 没有品质、供需、节日溢价、温室产物差异。
- 金币没有统一钱包；`player::Wallet` 尚未接入。
- 加工站是字符串 `machineKind`，没有设备实体、燃料、队列上限、多输出。
- 出货箱没有实体/容量；pending 只在服务内存里。
- 没有经济存档：金币、库存剩余、加工任务、出货 pending 都不能保存。
- `onDayChanged` 与 M22 的 `DayChanged` 尚未自动接线。
- M23 的任务金币奖励仍是事件，没有走 M24 钱包。
- 没有交易日志持久化、订单板、拍卖或成就挂钩。
