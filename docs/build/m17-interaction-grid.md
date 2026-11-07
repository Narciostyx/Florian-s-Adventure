# M17 交互与格子放置 —— 构建记录

对应模块文档：[17-interaction-grid-placement.md](../modules/17-interaction-grid-placement.md)

## 1. 实测环境

| 项 | 值 |
|---|---|
| MSBuild | `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe` |
| 平台工具集 | v145 / MSVC `14.51.36231` |
| vcpkg | `D:\CPPLibraries\vcpkg`，triplet `x64-windows-static-md` |
| 配置 | Debug / Development / Release × x64 |
| 新增工程 | `src/engine/interaction/fa_interaction.vcxproj` |

`fa_interaction` **不需要** `/EHsc`：它不 include nlohmann/json、`<chrono>` 或
`<unordered_map>`。它不引用 `fa_assets`、`fa_events`、`fa_input`、`fa_content`。

## 2. 实测结果

| 检查 | Debug | Development | Release |
|---|---|---|---|
| 全解决方案 `Rebuild` | exit 0，0 warning / 0 error | exit 0，0 warning / 0 error | exit 0，0 warning / 0 error |
| `fa_tests.exe` | 365 用例 / 18136 断言，全绿 | 同左 | 同左 |
| `fa_app.exe --frames 60` | exit 0 | exit 0 | exit 0 |

用例拆分（三种配置一致）：

| 过滤 | 用例 | 断言 |
|---|---|---|
| `[interaction]` | 15 | 184 |
| `~[interaction]` | 350 | 17952 |
| 全量 | 365 | 18136 |

`15 + 350 = 365`、`184 + 17952 = 18136`：`interaction.test.cpp` 的每个用例都带
`[interaction]`，没有漏标签。

## 3. 冒烟输出与对账

Debug：

```text
  m11: 1 scene depth, 3 system(s), 4 live entit(ies) (4 created / 0 destroyed) over 5 component type(s), 12 component(s), 923 extracted item(s)
       stack: 1 push(es), 0 pop(s), 0 replace(s), 0 pause(s), 0 resume(s)
       marker at (344, 243.599)
  m12: 40x23 map, 16 px tiles, 920 cell(s), 1 layer(s), 3 def(s), 920 non-empty, 143 solid, 16 tillable, 0 area(s), 0 portal(s)
       920 tile item(s) submitted, 0 culled, 0 empty, 0 without texture
  m13: 3 collider(s), 1 trigger, 109 move(s), 75 blocked, 2 cell(s) (worst bucket 2)
       109 step(s), 0 overlap quer(ies), 1 ray(s), 1 trigger begin(s) / 1 end(s), 0 event(s) last step
       nearest wall straight ahead: 4 px
  m17: target cell (19, 12), entity, 12.6 px, reachable; 1 tool use(s), 1 placement(s), 3 event(s), 1 occupied cell(s), preview valid -> invalid
M02+M03+M04+M05+M06+M07+M11+M12+M13+M17 smoke test OK
```

| 观测 | 来源 |
|---|---|
| 4 实体 | 标记 + 触发器 + 可交互板条箱 + 放置物 |
| 12 组件 | 标记 4（`Transform`/`Sprite`/`Collider`/`Velocity`）+ 触发器 2 + 板条箱 3（`Transform`/`Sprite`/`Interactable`）+ 放置物 3（`Transform`/`Sprite`/`Collider`） |
| 923 提取项 | 920 瓦片 + 标记 + 板条箱 + 放置物；触发器没有 `Sprite` |
| 932 quad | 923 个提取项 + 9 个调试图元（既有光标/调试开销 + M17 目标框） |
| 3 collider | 标记 + 触发器 + 放置物 |
| 2 宽相 cell | 标记/触发器/放置物在原 64 px 桶里合并；板条箱无 `Collider` |
| 143 实心格 | 与 M12 相同：外边框 122 + 墙列 21 |
| 16 可耕地 | 与 M12 相同：4×4 土壤补丁 |
| 920 提交 / 0 裁剪 | 与 M12 相同 |
| 目标 `(19,12)` | 板条箱所在格；标记中心到板条箱中心约 12.6 px |
| `1 tool use` | 冒烟注册的 `CountingToolHandler` 只接受 `Hoe` |
| `1 placement` | 19,11 空格；放置物实体作为 owner |
| `3 event(s)` | `InteractRequested` + `ToolUsed` + `PlacementRequested` |
| `1 occupied cell` | `PlacementGrid` 的单格占用 |
| `preview valid -> invalid` | 放置前合法；放置后同一格返回 `kOccupied` |

### Development / Release 会多跑几步

Development 实测 `109 move(s)`、Release `92 move(s)`；标记 x 始终是 344。
m17 的目标、距离、事件数与占用数在三种配置下一致。不要把固定步数逐字比对——
它由 M04 的墙钟累积驱动，和 M17 无关。

## 4. 语义决策表

| # | 决策 | 选择 | 理由 |
|---|---|---|---|
| 1 | 命名空间 | `fa::interaction` | 与模块名一致；不与 `scene`/`tilemap` 冲突 |
| 2 | 内容 ID | 不透明 `std::uint32_t payload` | M15 未实现；M17 不该解释内容 ID，处理器才是解释方 |
| 3 | 实体交互 | `Interactable` 纯数据组件 | 与 `Transform` 锚点一致；不引入接口/虚函数到 ECS |
| 4 | 放置占用 | 独立 `PlacementGrid` | 放置占用与地形碰撞是两种权威；分开后可独立测试 |
| 5 | 放置预览 | `PlacementPreview` 纯数据 | 不依赖 `IRenderer`；app 用 `drawDebugRect` 画 |
| 6 | 工具行为 | `IToolHandler` 注册表 | M17 不做锄头/种子规则；M19/M20 注册即可 |
| 7 | 事件 | 记录在 `events()` | 与 M13 的触发器一致；由组装根决定是否发 M03 |
| 8 | `canInteract` | 接收 actor | 没有 actor 就无法重算距离/视线；只信 `reachable` 会过期 |
| 9 | 视线 | 中间格阻挡，目标格放行 | 允许镐子/斧头目标就是墙；对角墙角检查防穿缝 |
| 10 | 放置 owner | 必须存活 | `remove(owner)` 依赖 owner；服务不创建实体 |
| 11 | 多格占地 | `size` 向上取整成格 | 24×16 = 2×1；旋转用交换宽高表达 |
| 12 | 错误码 | 7000–7012 | Gameplay 段的前 13 个码 |

## 5. 踩坑

1. **`AreaKind` 忘了命名空间。** `InteractionTypes.hpp` 在 `fa::interaction` 里直接写
   `std::optional<AreaKind>`，而 `AreaKind` 在 `fa::tilemap`。编译器报
   C2065/C2923/C2955；改成 `std::optional<tilemap::AreaKind>`。

2. **`canInteract` 最初没有 actor 参数。** 草案接口是
   `canInteract(target, tool)`，实现时才发现无法重算距离与视线，只能信任
   `GridTarget::reachable`。改为 `canInteract(world, actor, target, tool)`，
   内部复用 `validateInteraction`，测试也能验证“墙后目标不可交互”。

3. **`cellsAlong` 对 NaN/负数不能直接 cast。** 先写的是
   `ceil(size / tileSize)` 再 `static_cast<int>`；负数和 NaN 会产生未定义结果。
   改成 `if (!(size > 0.0f) || tileSize <= 0) return 0;`，并把结果上限夹到 4096。

4. **放置校验的顺序影响错误码。** 测试最初在 `(8,8)` 期望 `kNotOccupied`，
   但那里先触发距离检查得到 `kOutOfRange`。要么把 maxDistance 调大，要么换近处
   的格子；最终换到 actor 所在格 `(1,1)` 测空占用。

5. **网格边界要同时看地图与占用网格。** 只检查 `PlacementGrid::contains` 会允许
   一个比地图更小的 grid 越界；只检查 map 会允许 grid 越界。两边都用
   `right/bottom` 的 `long long` 比较。

6. **`remove` 的计数不能减错。** 只对真正匹配 owner 的格子减 `m_occupied`；
   用 `removed` 计数并在最后统一减，避免重复移除同一格时下溢。

7. **`World::each` 需要 const 重载。** `pickTarget` 接收 `const World&`，
   M11 的 `each` 已经有 const 版本；如果只用非 const 版本会编译失败。

8. **实体优先级要有确定性。** 只比 priority 时，同格同优先级的两个实体顺序取决于
   `each` 的槽位顺序；补上距离与 `EntityId` 的 tie-break。

9. **调试图元会改变 m06 的 quad 数。** M17 新增目标框后，冒烟从 926 变成 932；
   这不是批量/排序退化，是调试绘制数量变化。验收时看提取项数，不要只看 quad。

10. **成员声明顺序。** `InteractionService` 持有 `Tilemap&` 与 `PlacementGrid*`，
    必须声明在它们之后；`CountingToolHandler` 必须比 service 活得久。`onExit`
    不需要清 service，但成员顺序仍是正确性的前提。

11. **不需要 `/EHsc`。** 本模块没有 JSON/chrono/unordered_map；不要因为
    “玩法模块”就顺手打开异常。

## 6. 复现命令

```powershell
$env:VCPKG_ROOT = 'D:\CPPLibraries\vcpkg'
$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'

foreach ($cfg in @('Debug','Development','Release')) {
    & $msbuild FloriansAdventure.sln /t:Rebuild /p:Configuration=$cfg /p:Platform=x64 /m /nologo /v:m
    & ".\build\bin\x64\$cfg\fa_tests.exe"
    & ".\build\bin\x64\$cfg\fa_app.exe" --frames 60
}

# 只跑交互
& ".\build\bin\x64\Debug\fa_tests.exe" "[interaction]"
```

单编新工程：

```powershell
& $msbuild 'src\engine\interaction\fa_interaction.vcxproj' /t:Rebuild `
    /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir="$($PWD.Path)\" /m /nologo /v:m
```

## 7. 遗留风险

1. `payload` 是临时内容 ID；M15 落地后要统一替换。
2. 放置占用与实体碰撞仍是两套数据；M19 必须在放置后创建碰撞体。
3. 不支持任意旋转，只支持交换宽高的轴对齐占地。
4. 放置不检查已有实体/碰撞体，只检查地图可走与占用网格。
5. 移除按 owner 清空所有格子，不通知其他系统。
6. 视线是单条中心线 + 对角墙角检查，不模拟 actor 体积。
7. `canInteract` 每次重算距离/视线，不适合逐帧扫大量目标。
8. `Interactable` 不随 `Transform::scale/rotation` 变换；像素风只允许 90° 旋转。
9. “使用工具时是否暂停移动”仍是 M16 的手感决策。
