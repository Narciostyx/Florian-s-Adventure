# M12 瓦片地图 —— 构建记录

对应模块文档：[12-tilemap-and-world.md](../modules/12-tilemap-and-world.md)

## 1. 实测环境

| 项 | 值 |
|---|---|
| MSBuild | `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe` |
| 平台工具集 | v145 / MSVC `14.51.36231` |
| vcpkg | `D:\CPPLibraries\vcpkg`，triplet `x64-windows-static-md` |
| 配置 | Debug / Development / Release × x64 |
| 新增工程 | `src/engine/tilemap/fa_tilemap.vcxproj` |

`fa_tilemap` 只打开 `/EHsc`，原因是 `TilemapLoader.cpp` include 了
`nlohmann/json.hpp`；模块自己的代码不 throw/catch。它不引用 `fa_assets`，
瓦片纹理只存裸 `render::TextureHandle`。

## 2. 实测结果

| 检查 | Debug | Development | Release |
|---|---|---|---|
| 全解决方案 `Rebuild` | exit 0，0 warning / 0 error | exit 0，0 warning / 0 error | exit 0，0 warning / 0 error |
| `fa_tests.exe` | 350 用例 / 17952 断言，全绿 | 同左 | 同左 |
| `fa_app.exe --frames 60` | exit 0 | exit 0 | exit 0 |

用例拆分（三种配置一致）：

| 过滤 | 用例 | 断言 |
|---|---|---|
| `[tilemap]` | 17 | 275 |
| `~[tilemap]` | 333 | 17677 |
| 全量 | 350 | 17952 |

`17 + 333 = 350`、`275 + 17677 = 17952`：`tilemap.test.cpp` 的每个用例都带
`[tilemap]`，没有漏标签。

## 3. 冒烟输出与对账

Debug：

```text
  m11: 1 scene depth, 3 system(s), 2 live entit(ies) (2 created / 0 destroyed) over 4 component type(s), 6 component(s), 921 extracted item(s)
       stack: 1 push(es), 0 pop(s), 0 replace(s), 0 pause(s), 0 resume(s)
       marker at (344, 224)
  m12: 40x23 map, 16 px tiles, 920 cell(s), 1 layer(s), 3 def(s), 920 non-empty, 143 solid, 16 tillable, 0 area(s), 0 portal(s)
       920 tile item(s) submitted, 0 culled, 0 empty, 0 without texture
  m13: 2 collider(s), 1 trigger, 60 move(s), 26 blocked, 1 cell(s) (worst bucket 2)
       60 step(s), 0 overlap quer(ies), 1 ray(s), 1 trigger begin(s) / 1 end(s), 0 event(s) last step
       nearest wall straight ahead: 4 px
M02+M03+M04+M05+M06+M07+M11+M12+M13 smoke test OK
```

| 观测 | 来源 |
|---|---|
| 2 实体 | 标记 + 触发器；地面不再由 920 个实体组成 |
| 6 组件 | 标记 `Transform`/`Sprite`/`Collider`/`Velocity` 4 个 + 触发器 2 个 |
| 921 提取项 | 920 个瓦片 + 1 个标记；触发器没有 `Sprite` |
| 926 quad | 921 + 5 个调试图元（与 M11/M13 交付时相同） |
| 143 实心格 | 外边框 122 + 墙列 22 行中 21 行非边框 |
| 16 可耕地 | 4×4 土壤补丁（列 4–7、行 8–11） |
| 920 提交 / 0 裁剪 | 40×23 地图全部落在 640×360 视口内；边缘行只部分可见但仍被提交 |
| 5 catalog / 4 cached | 目录含 grass/wall/soil/player/marker，`player` 目前未加载 |
| 4 live texture | grass、wall、soil、marker |
| 标记 x=344 | 墙列 22 左边界 `22 × 16 = 352`，标记宽 8 → `352 − 8 = 344` |
| `blocked = 26` | 34 步走到位，之后 26 步持续顶墙 |

### Development / Release 会多跑几步

同一份代码，Development 实测 `95 move(s)`、标记 `(344, 237.999)`；
Release 实测 `85 move(s)`、标记 `(344, 233.999)`；另一次 Debug 在与测试并行运行时
得到 `94 move(s)`、标记 `(344, 237.599)`。x 始终是 344，y 的差异来自 M04 固定步由
墙钟累积驱动（某帧稍慢就补一步），与 M12/M13 无关。**不要**把“三配置输出逐字相同”
写进验收标准；要断言确定性，用单元测试里的固定 world，而不是比对进程间帧数。

## 4. 语义决策表

| # | 决策 | 选择 | 理由 |
|---|---|---|---|
| 1 | 命名空间 | `fa::tilemap` | `fa::world` 与 `scene::World` 混淆；日志分类本来叫 `Tilemap` |
| 2 | `Tilemap` 与 `ISolidTiles` 的关系 | 值类型 + `TilemapSolids` 适配器 | `ISolidTiles` 禁止拷贝/移动，派生会让 `Result<Tilemap>` 无法按值返回；适配器保持地图可移动/可存档 |
| 3 | 纹理依赖 | 不引用 `fa_assets` | `TileDef` 存裸句柄；JSON 的 `texture` 是名字，assembly root 调 `setTileTexture()` 绑定 |
| 4 | `solid` 与 `walkable` | 两个独立 bool | 水是“不可走但不阻挡”；只用一个 flag 无法表达 |
| 5 | 渲染接口 | `extractTiles` 产出 `DrawItem` | 无 GL 也能测；与 M11 的 `SpriteExtractor` 同构 |
| 6 | 视锥裁剪 | 默认关闭，`setViewBounds()` 显式打开 | 小地图/测试要全量；app 每帧传 `camera.visibleRect()` |
| 7 | 图层顺序 | 按 `zOrder` stable sort，平手按插入顺序 | 同一 `render::RenderLayer` 可以有多个地图图层；`entityId` 递增保证覆盖确定 |
| 8 | 自动瓦片 | 4 邻接 16 变体，烘焙到图层 | 运行时无额外查表；v1 复杂度可控 |
| 9 | 自动瓦片写入 | 先快照图层再算掩码 | 就地写入会让后处理的格子看到已被替换的邻居，结果依赖遍历顺序 |
| 10 | JSON 图层数据 | 扁平 row-major 数组 | 简单、无歧义；大图由编辑器/导入器生成 |
| 11 | 错误码 | 5013–5022 | 接在 Scene 5000–5007、Physics 5008–5012 之后 |
| 12 | 异常 | 仅本工程 `/EHsc` | nlohmann/json 需要；模块自身不抛 |

## 5. 踩坑

1. **`isWalkable` 最初写成了反向逻辑。** 先写的实现是
   `!isSolidCell() && !anyDefHas(&TileDef::walkable)`，而 `anyDefHas` 表示
   “存在可走定义”，于是“所有定义都可走”的格子反而被判成不可走。正确语义是
   “没有任何 `!walkable` 或 `solid` 定义”。用例 `Tilemap combines walkability
   and solidity across layers` 直接抓到了这个错误。

2. **`Result<Tilemap>` 要求 `Tilemap` 可移动，而 `ISolidTiles` 禁止移动。**
   初版打算让 `Tilemap` 直接继承 `physics::ISolidTiles`，但基类删除了拷贝/移动，
   `Result<Tilemap>::ok(std::move(map))` 无法编译。改为值类型 + `TilemapSolids`
   非 owning 适配器；物理系统只借用适配器。

3. **JSON 的 `origin` 一开始按 `TileCoord` 解析。** 写完又觉得“应该是世界像素”，
   于是留下了一段先解析成整数再无条件报错的死代码。最终补了 `readVec2Value`，
   按两个浮点数读取并调用 `setOrigin()`。这是“先写分支后改语义”留下的典型残渣。

4. **`nlohmann::json::get<int>()` 对超大无符号数会抛。** 虽然本工程开了 `/EHsc`，
   加载器仍按无异常路径写：先 `is_number_unsigned()` 再范围检查，再
   `is_number_integer()` + `long long` 范围检查；字符串/布尔/数组都先 `is_*()`。
   解析本身用 `parse(..., nullptr, false)`，失败返回 `is_discarded()`。

5. **`skipEmpty` 选项是无效 API。** 空瓦片 id 为 0 且没有定义，无法绘制；
   保留这个开关只会误导调用方。删除选项，保留 `skippedEmpty` 统计。

6. **视锥裁剪不能把完全在外的视口 clamp 进地图。** 直接对
   `firstCell/lastCell` 做 clamp 时，视口在地图右侧外会把列号夹到最后一列，
   仍然提交一格。修法是先 `core::intersect(view, mapBounds)`，为空则直接返回，
   `culled` 记为全部可见图层格子数。

7. **测试里 `fillLayer` 之后 `setBorder(empty)` 并不会清空中心。** 3×3 地图的
   中心格仍在，`nonEmpty == 0` 断言失败。改成两步：先清边、断言中心仍填充；
   再整层清空、只画边框、断言中心为空。

8. **图层索引取决于添加顺序。** 提取测试先加 `detail` 再加 `ground`，所以
   `layerAt(0)` 是 `detail` 而不是 `ground`。测试里把“可见性”改到错误的索引会
   得到 32 个提交项而不是 16 个。

9. **自动瓦片必须对快照计算。** 3×3 基础块若就地烘焙，中心格会看到已经被改成
   变体的北邻，得到 mask 14 而不是 15。测试用 `variants[15] = 10`、
   `variants[14] = 11` 把两种结果区分开，确保实现使用快照。

10. **`TilemapSolids` 必须在 `m_map` 之后声明。** 成员初始化顺序是声明顺序；
    `m_solids{m_map}` 要求 `m_map` 已构造。`onExit()` 里先 `systems().clear()`，
    再清空 `m_physics`/`m_tileSystem` 指针，避免系统借用的成员先死。

11. **`stats()` 不是每帧接口。** 它遍历所有图层所有格子；冒烟和调试可以调用，
   热路径不应调用。`defFor` 也是线性查找，单地图定义数少时可接受。

12. **新 `.vcxproj` 保持 LF 无 BOM。** 与 M13 的坑 #12 相同；文件创建工具
    默认写出的换行/编码需要在提交前检查。

## 6. 复现命令

```powershell
$env:VCPKG_ROOT = 'D:\CPPLibraries\vcpkg'
$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'

# 全解决方案三配置
foreach ($cfg in @('Debug','Development','Release')) {
    & $msbuild FloriansAdventure.sln /t:Rebuild /p:Configuration=$cfg /p:Platform=x64 /m /nologo /v:m
}

# 测试（三配置）
foreach ($cfg in @('Debug','Development','Release')) {
    & ".\build\bin\x64\$cfg\fa_tests.exe"
}

# 只跑瓦片地图
& ".\build\bin\x64\Debug\fa_tests.exe" "[tilemap]"

# 冒烟
& ".\build\bin\x64\Debug\fa_app.exe" --frames 60
```

单编新工程时必须显式给 `SolutionDir`：

```powershell
& $msbuild 'src\engine\tilemap\fa_tilemap.vcxproj' /t:Rebuild `
    /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir="$($PWD.Path)\" /m /nologo /v:m
```

## 7. 遗留风险

与模块文档 §9 对应，这里只列“落地后立刻会碰到”的部分：

1. `defFor` 线性查找；瓦片定义上百时需要索引。
2. `stats()` 全量遍历，不能放进每帧热路径。
3. 大地图全量载入；128×128×3 层可接受，更大需要分块。
4. 自动瓦片编辑后必须重新烘焙。
5. 传送点只是数据；场景切换由 M25 消费。
6. JSON 图层是扁平数组，大图不适合手写；M26 已提供 `saveTilemapToJson()` 与编辑器。
7. 纹理名到句柄的绑定由 composition root 完成，地图不会自动加载图集。
8. 地图仍无昼夜/光照叠加；M22 消费 `MapAmbient` 后才能看到效果。
