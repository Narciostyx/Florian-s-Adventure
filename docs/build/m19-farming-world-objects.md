# M19 Farming & World Objects —— 构建记录 / 实测

对应模块文档：[19-farming-world-objects.md](../modules/19-farming-world-objects.md)

> **状态：已实现并三配置实测通过。** 耕地、浇水、播种、按天生长、季节/温室、
> 收获/再生、杂物清理、世界对象占用与 M17 工具适配均已完成；app 用萝卜跑通
> 锄地 → 播种 → 浇水 → 成熟 → 收获，并证明冬季室外枯萎、温室存活。

## 1. 实际落地

新增工程 `src/engine/farming/fa_farming.vcxproj`（GUID `{C0DE0016-…0016}`），
引用 `fa_core`、`fa_content`、`fa_inventory`、`fa_interaction`、`fa_scene`、
`fa_tilemap`。因为公共头经过 M17 的 `ToolHandler.hpp` 间接引入
`scene::World`/`Tilemap`（内部使用 `std::unordered_map`），本工程单独打开
`/EHsc`。

| 文件 | 职责 |
|---|---|
| `fa/farming/FarmingTypes.hpp` | `FarmTile`、`CropState`、`FarmingDayContext`、事件、统计、`WorldObject`、`Debris`、错误码 7040 段 |
| `fa/farming/FarmingService.hpp` + `src/FarmingService.cpp` | 锄地/浇水/播种/收获/每日结算/季节与温室 |
| `fa/farming/FarmingWorld.hpp` + `src/FarmingWorld.cpp` | `WorldObjectLayer` 与 `DebrisLayer` 的单格占用 |
| `fa/farming/FarmingTools.hpp` + `src/FarmingTools.cpp` | M17 `IToolHandler` 适配：Hoe/WateringCan/Seed/Harvest/Axe/Pickaxe |
| `tests/farming.test.cpp` | `[farming]` 13 用例 / 219 断言 |

### 关键实现选择

- **按格存储，不进 ECS**：`FarmingService` 用 `std::vector<FarmTileEntry>` 保存
  耕地与作物；线性查找在 v1 的田地规模下足够，且没有 ECS 生命周期与句柄成本。
- **作物定义在播种时复制**：`CropState` 保存 `harvestItem`、`stageDays`、`seasons`、
  `regrow`/`regrowDays`，因此内容库热重载后已种下的作物仍按原规则生长。
- **每日结算顺序**：季节检查 → 浇水/降雨判定 → 生长 → 阶段刷新 → 干燥。
  室外作物在换季后死亡；温室格（`Tilemap::queryArea` 返回
  `AreaKind::Greenhouse`）跳过季节检查。
- **生长是浮点天数**：M22 的生长倍率 1.1/0.9/0.5 直接累加到 `growthDays`，
  阶段阈值来自 `CropDef::stageDays`；只有浇过水（或当天降雨）才累加。
- **收获原子性**：先检查 `Inventory::spaceFor()`，再 `add()`；空间不足时不改变
  作物状态。普通作物收获后保留耕地、清空作物；再生作物回退到
  `totalDays - regrowDays` 并重新进入不可收获状态。
- **种子消耗归工具适配器**：`FarmingService::plant()` 保持纯逻辑；
  `FarmingToolHandler` 在播种成功后从背包移除 1 个种子，并在没有选中种子/背包
  没有该种子时返回 `kNoSeedSelected`。
- **M17 扩展**：新增 `ToolKind::Harvest`，让收获走与锄地/浇水相同的分发路径。
  app 的通用记录处理器改为接受 `ToolKind::Sword`，避免与 M19 的 Hoe 处理器抢占。
- **世界对象/杂物**：`WorldObjectLayer` 只负责单格占用与对象标识；箱子存储、
  熔炉加工等属于 M24。`DebrisLayer` 保存产出，斧/镐通过工具适配器清理并入库。
- **app 集成**：
  - `DemoScene` 新增 `FarmingService`、种子/收获背包、`DebrisLayer` 和
    `FarmingToolHandler`；`InteractionService` 同时注册通用处理器与 M19 处理器。
  - 田地脚本在 (4,8) 完成 5 天生长并收获，随后在 (7,11) 温室格和 (6,9) 室外格
    验证冬季规则。
  - 渲染：耕地/浇水/作物用 `Weather` 之前的 `GroundDetail`/`Objects` 层色块 quad
    表示；成熟作物显示黄色，死亡作物显示灰褐色。
  - 摘要新增 `m19:` 行，smoke OK 加入 `+M19`。

## 2. 测试

`[farming]` 覆盖：

- 只锄可耕地；重复锄、草地、越界拒绝。
- 浇水要求已耕地；每日结算后干燥。
- 种子解析、重复播种、未知种子、季节限制、温室绕过。
- 无水不生长；浇水逐日推进阶段；成熟判定。
- 降雨自动浇水与生长倍率。
- 收获入包、作物清空、耕地保留、重复收获失败。
- 再生作物收获后回退并再次成熟。
- 换季室外枯萎、温室存活、枯死作物不可收获。
- 世界对象单格占用/移除；杂物单次清理与产出。
- M17 工具适配器：Hoe/WateringCan/Seed/Harvest/Axe/Pickaxe、种子扣除、
  缺失种子错误与统计。

实测：

```text
fa_tests.exe "[farming]" → All tests passed (238 assertions in 14 test cases)
fa_tests.exe             → All tests passed (29216 assertions in 489 test cases)
fa_app.exe --frames 60   → m19: 3 tilled, 2 crop(s), 0 harvestable, 1 dead;
                           3 till(s)/7 water(s)/3 plant(s)/1 harvest(s), 1 item(s),
                           stage 3 (ready), greenhouse ok, winter wilt ok, seed 2, 19 event(s)
                         → M02+…+M18+M19+M20+M22+M27 smoke test OK
```

## 3. 遇到的问题与结论

1. **`Result<T>` 没有隐式转换。** `Status`/`ItemStack` 不能直接 `return`；
   收获路径改用 `Result<ItemStack>::ok(...)` / `::err(...)`。
2. **M17 处理器会抢占。** 原 app 的记录处理器接受 Hoe；M19 注册 Hoe 后两者冲突。
   记录处理器改为 Sword，M19 处理器独占农业工具。
3. **季节类型有两套。** M22 用 `world::Season`，M15 用位掩码 `content::Season`；
   `FarmingDayContext` 只接收后者，转换留在组装根，M19 不依赖 M22。
4. **温室必须是每格属性。** 锄地时把 `queryArea` 的结果写入 `FarmTile`，
   后续每日结算不再需要地图；同一块田里的不同温室区域可以独立工作。
5. **生长倍率不能取整。** 0.5 倍如果按天取整会永远不生长；`growthDays` 用
   `float`，阶段仍按整数阈值判断。
6. **收获前必须检查空间。** `Inventory::add()` 可能部分入库；先
   `spaceFor()` 可以保证作物状态与物品数量一致。
7. **`/EHsc` 是编译期结论。** `fa_farming` 公共头经 M17 间接包含
   `scene::World`，触发 C4530；工程级打开 `ExceptionHandling=Sync`。

## 4. 验证命令

```powershell
$env:VCPKG_ROOT='D:\CPPLibraries\vcpkg'
$msbuild='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
foreach ($cfg in @('Debug','Development','Release')) {
  & $msbuild FloriansAdventure.sln /t:Rebuild /p:Configuration=$cfg /p:Platform=x64 /m /nologo /v:m
  & ".\build\bin\x64\$cfg\fa_tests.exe"
  & ".\build\bin\x64\$cfg\fa_app.exe" --frames 60
  & ".\build\bin\x64\$cfg\fa_app.exe" --play-test
}
```

### 玩法验证补丁

- `FarmingToolHandler` 的背包从引用改为指针，并新增 `setInventory()`；组装根在
  `--play` 模式把它切到玩家快捷栏，种子扣费与收获入包才走玩家的真实容器。
- app 新增 `--play`（跳过脚本演示）与 `--play-test`（无头耕地→播种→浇水→5 天→收获）。
- `--play-test` 三配置均通过；测试新增“可重新绑定背包”用例。

## 5. 已知限制 / 后续

- 没有 M19 存档序列化：`FarmingService` 还不能写入/读取 `SaveArchive`。
- 箱子存储、熔炉/工作台加工、洒水器行为属于 M24；v1 只有占用层。
- 没有动物、品质、产量表、多次收获次数与作物行为策略。
- app 的作物/耕地是色块 quad，不是 `CropDef::stageSprites` 的真实阶段精灵。
- 杂物清理已有图层与工具路径，但 app 脚本没有放杂物；单元测试覆盖了该路径。
- 季节长度、再生天数、生长倍率仍是内容/产品参数。
- 每日结算是内存操作；崩溃一致性依赖调用方在结算后再存档。
