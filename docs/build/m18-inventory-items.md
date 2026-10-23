# M18 Inventory & Items —— 构建记录

对应模块文档：[18-inventory-items.md](../modules/18-inventory-items.md)

## 1. 实测环境

| 项 | 值 |
|---|---|
| MSBuild | `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe` |
| 平台工具集 | v145 / MSVC `14.51.36231` |
| vcpkg | `D:\CPPLibraries\vcpkg`，triplet `x64-windows-static-md` |
| 配置 | Debug / Development / Release × x64 |
| 新增工程 | `src/engine/inventory/fa_inventory.vcxproj`（GUID `{C0DE000F-…}`） |
| 新增测试 | `tests/inventory.test.cpp` |

`fa_inventory` **不需要** `/EHsc`：它不 include nlohmann/json、`<chrono>` 或
`<unordered_map>`。它只链接 `fa_core` 与 `fa_content`；`fa/scene/EntityId.hpp`
只是值类型头，不链接 `fa_scene`。

## 2. 实测结果

| 检查 | Debug | Development | Release |
|---|---|---|---|
| 全解决方案 `Rebuild` | exit 0，0 warning / 0 error | exit 0，0 warning / 0 error | exit 0，0 warning / 0 error |
| `fa_tests.exe` | 428 用例 / 18820 断言，全绿 | 同左 | 同左 |
| `fa_app.exe --frames 60` | exit 0 | exit 0 | exit 0 |

用例拆分（三种配置一致）：

| 过滤 | 用例 | 断言 |
|---|---|---|
| `[inventory]` | 22 | 288 |
| `~[inventory]` | 406 | 18532 |
| 全量 | 428 | 18820 |

`22 + 406 = 428`、`288 + 18532 = 18820`：`inventory.test.cpp` 的每个用例都带
`[inventory]`，没有漏标签。M18 之前是 406 用例 / 18532 断言，所以新增量正好
是 22 / 288。

## 3. 冒烟输出与对账

Debug（Development / Release 的 m18 行完全一致）：

```text
  m09: 95 glyph(s) in a 380x5 atlas, 87 UI item(s), 0 press(es), HUD on, slider 2.00
  m06: 4.1.0 Core Profile Context ... on AMD Radeon (TM) Graphics
       109 draw call(s), 1019 quad(s), 4076 vertex/vertices, 108 texture switch(es), 0 dropped, 7 live texture(s)
  m05: 5 catalog entr(ies), 4 texture(s) cached, 4 placeholder(s), 3328 CPU byte(s), 4 live GPU texture(s), 0 failed
  m11: 1 scene depth, 4 system(s), 4 live entit(ies) (4 created / 0 destroyed) over 10 component type(s), 17 component(s), 923 extracted item(s)
  m18: backpack 106 item(s) in 3 slot(s), hotbar 21 item(s), selected 0, tool item.hoe,
       soup uses 1, 21 inventory event(s), 1 use event(s)
M02+M03+M04+M05+M06+M07+M09+M11+M12+M13+M15+M16+M17+M18 smoke test OK
```

| 观测 | 来源 |
|---|---|
| backpack 106 | 99 种子（槽 0）+ 5 萝卜（槽 2）+ 2 汤（槽 3） |
| backpack 3 slots | 槽 1 的 21 种子与槽 4 的锄头已被移走 |
| hotbar 21 | 从背包槽 1 整体转移的 21 个种子 |
| selected 0 | 先选 0，再 `cycle(1)` 到 1，再 `cycle(-1)` 回 0 |
| tool `item.hoe` | 背包槽 4 `takeAt` 后 `equip(Tool)` |
| soup uses 1 | `ConsumableUseHandler` 接受 `category == "food"` 并消耗 1 |
| 21 inventory events | 背包 15 + 快捷栏 6（见下表） |
| 1 use event | 一次成功的 `ItemUseEventKind::Used` |
| 87 UI 项 | M09 原有 66 + 热键栏文本 21 个字形 |
| 1019 quad | 923 世界项 + 87 UI 项 + 9 个调试图元 |

### 21 个 inventory 事件的拆分

| 操作 | 背包 | 快捷栏 |
|---|---|---|
| 加 120 种子 | 3（Added×2 + Changed） | — |
| 加 5 萝卜 | 2 | — |
| 加 3 汤 | 2 | — |
| 加 1 锄头 | 2 | — |
| 移 21 种子到快捷栏 | 2（Removed + Changed） | 2（Added + Changed） |
| 移 1 汤到快捷栏 | 2 | 2 |
| 取锄头装备 | 2 | — |
| 使用 1 汤 | — | 2（Removed + Changed） |
| 合计 | **15** | **6** |

## 4. 语义决策表

| # | 决策 | 选择 | 理由 |
|---|---|---|---|
| 1 | 命名空间 | `fa::inventory` | 与模块名一致 |
| 2 | 物品描述 | `ItemStack{ContentId, count, durability}` | 不存 `ItemDef*`，内容热重载不会悬空 |
| 3 | 堆叠上限 | 绑定内容时读 `ItemDef::maxStack`，否则默认值 | M15 是唯一物品定义来源，测试可脱离内容库 |
| 4 | 添加语义 | 先合并同类，再占空槽，返回剩余 | 拾取 100 个作物可以跨槽；背包满时把剩余留在地上 |
| 5 | 移除语义 | 数量不足时原子失败 | 避免“扣了一半”的中间状态 |
| 6 | 耐久分栈 | 同 id + 同耐久才合并 | 为后续耐久系统留出正确边界 |
| 7 | 事件 | 记录在 `events()`，含聚合 `Changed` | 与 M13/M17 一致，由组装根决定是否发 M03 |
| 8 | 快捷栏 | `Hotbar` 拥有一个 `Inventory` + 选中索引 | 内容与选中状态可分别存档 |
| 9 | 装备 | 固定 Tool/Weapon 两槽，数量必须为 1 | v1 不做品质/耐久扣减；类别由上层过滤 |
| 10 | 使用分发 | `IItemUseHandler` 注册表，首个匹配者胜出 | M18 不认识食物/种子/工具的具体效果 |
| 11 | 消耗责任 | handler 自己 `removeAt` | 种子/工具/放置物的消耗时机不同，服务保持中立 |
| 12 | 玩家容器 | 具体 `PlayerInventory` 聚合，不引入 `IInventoryService` | 目前只有一个实现，避免过早抽象 |
| 13 | 转移 | 先算目标容量再移动，满目标移动 0 | 不做“移一半再回滚” |
| 14 | `/EHsc` | 不开启 | 无 JSON/chrono/unordered_map |

### play 食物使用（2026-10-23）

- `F` 调用 `ItemUseService::use(hotbar, selected, player)`；`ConsumableUseHandler`
  负责 `removeAt(slot, 1)`，M18 仍不解释“食物效果”。
- app 在成功后读取 `ItemDef::healAmount`/`staminaRestore` 并调用
  `DemoScene::setPlayerHealth`/`setPlayerStamina`；汤为 +20 HP / +30 体力。
- play 初始快捷栏为锄头/水壶/种子/汤；汤也可通过商店 `COOK SOUP` 获得。
- `--play-test` 校验 `F` 后汤数量 -1、生命 50→70、体力 50→80。

## 5. 踩坑

1. **`ItemStack::empty()` 把“数量 ≤ 0”和“id 无效”合并了。** `setSlot(0, {invalidId, 1})`
   最初被当成空栈直接清槽；测试期望 `kInvalidStack`。修复为：`count <= 0` 才清槽，
   正数量 + 无效 id 报错。`restore()` 同样改为先判 `count <= 0`，再校验 id。
2. **`transferTo` 的第一段移动会把剩余放进目标的第二个空槽。** 目标已有 7 个同类物品、
   上限 10，移动 5 时先补 3 到满，再把 2 放进空槽；测试最初只检查第一个槽。
3. **`spaceFor` 对“不同物品”只计算空槽。** 同类未满栈只能被同 id + 同耐久的物品使用；
   测试最初把其他物品的部分栈也算进去，得到 8 而不是 5。
4. **`split` 的错误码取决于“有没有空槽”。** 槽里有 2 个、请求拆 1 个是合法数量，但
   三个槽都被占满时返回 `kInventoryFull`，不是 `kInvalidQuantity`；测试需要分开覆盖
   数量错误、空槽、无空位三种情况。
5. **`ItemUseService` 必须在 handler 之前复制 `ContentId`。** handler 消耗最后一件物品会
   把槽清空，之前保存的 `const ItemStack&` 的 id 也失效；复制 id 后再记录事件。
6. **`restore` 必须先全量校验再写槽。** 直接边校验边写会在失败时留下半份快照；现在
   先检查 size、id、maxStack，全部通过后才替换。
7. **`ItemStack` 的默认构造是有效状态。** `ContentId{}` 无效、count 0；`Result` 的错误
   路径要把“无效 id + 正数量”当成错误，而不是空槽。
8. **装备槽不检查类别。** v1 只要求数量 1；把食物放进 Tool 槽不会报错，M19/M20 需要在
   装备前按 `ItemDef::category`/tags 过滤。
9. **UI 项数会增加。** 热键栏文本 21 个字形让 m09 从 66 变 87、quad 从 998 变 1019；
   这是内容变化，不是批处理退化。
10. **不需要 `/EHsc`。** handler 注册表用 `std::vector` 线性查找，避免
    `<unordered_map>` 触发 C4530。
11. **工程 GUID 与 sln 配置块要一起加。** 只加 `.vcxproj`/`ProjectReference` 而漏掉
    `ProjectConfigurationPlatforms`，MSBuild 会跳过新工程。

## 6. 复现命令

```powershell
$env:VCPKG_ROOT = 'D:\CPPLibraries\vcpkg'
$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'

foreach ($cfg in @('Debug','Development','Release')) {
    & $msbuild FloriansAdventure.sln /t:Rebuild /p:Configuration=$cfg /p:Platform=x64 /m /nologo /v:m
    & ".\build\bin\x64\$cfg\fa_tests.exe"
    & ".\build\bin\x64\$cfg\fa_app.exe" --frames 60
}

# 只跑背包
& ".\build\bin\x64\Debug\fa_tests.exe" "[inventory]"
```

单编新工程：

```powershell
& $msbuild 'src\engine\inventory\fa_inventory.vcxproj' /t:Rebuild `
    /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir="$($PWD.Path)\" /m /nologo /v:m
```

## 7. 遗留风险

1. 耐久只参与分栈，不参与按实例移除；`remove(id, count)` 会合并不同耐久的计数。
2. 使用不自动消耗，handler 忘记 `removeAt` 会导致无限使用。
3. 商店扣钱 + 给物品的原子性留给 M24。
4. 无重量/容量/分类限制，任何物品可进任何容器。
5. 无自动整理/排序；槽位稳定是刻意设计。
6. 事件只记录不发布，M03 接线由组装根决定。
7. 无存档格式/版本迁移，`restore` 只校验槽数与 maxStack。
8. 非线程安全；容器只在主线程/单系统内使用。
9. `ContentDatabase` 生命周期由调用方保证，悬空会导致 `maxStackFor`/use 崩溃。
10. 装备槽不校验类别；Tool 槽可以放食物，需要上层过滤。
11. app 背包面板一页 8 个堆叠、每次移动 1 个，没有滚动/分类/拖拽。
12. `moveToHotbar`/`moveToBackpack` 经 `transferTo` 返回**移动数量**而不是剩余；
    成功判据是 `isOk() && value() > 0`。
