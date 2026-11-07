# M13 物理与碰撞 —— 构建记录

对应模块文档：[13-physics-collision.md](../modules/13-physics-collision.md)

## 1. 实测环境

| 项 | 值 |
|---|---|
| MSBuild | `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe` |
| 平台工具集 | v145 / MSVC `14.51.36231` |
| vcpkg | `D:\CPPLibraries\vcpkg`，triplet `x64-windows-static-md` |
| 配置 | Debug / Development / Release × x64 |
| 提交 | `37d6b74` |

## 2. 实测结果

| 检查 | Debug | Development | Release |
|---|---|---|---|
| 全解决方案 `Rebuild` | exit 0，0 warning / 0 error | exit 0，0 warning / 0 error | exit 0，0 warning / 0 error |
| `fa_tests.exe` | 333 用例 / 17677 断言，全绿 | 同左 | 同左 |
| `fa_app.exe --frames 60` | exit 0 | exit 0 | exit 0 |

用例拆分（三种配置一致）：

| 过滤 | 用例 | 断言 |
|---|---|---|
| `[physics]` | 40 | 299 |
| `~[physics]` | 293 | 17378 |
| 全量 | 333 | 17677 |

`40 + 293 = 333`、`299 + 17378 = 17677`：两个标签正好把测试集切开，说明 `physics.test.cpp` 里的每个用例都带 `[physics]`，没有漏标签。

## 3. 冒烟输出与对账

Debug / Release：

```text
       920 ground tile(s), 143 solid, marker at (344, 224)
  m13: 2 collider(s), 1 trigger, 60 move(s), 26 blocked, 1 cell(s) (worst bucket 2)
       60 step(s), 0 overlap quer(ies), 1 ray(s), 1 trigger begin(s) / 1 end(s), 0 event(s) last step
       nearest wall straight ahead: 4 px
M02+M03+M04+M05+M06+M07+M11+M13 smoke test OK
```

| 观测 | 来源 |
|---|---|
| 922 实体 | 920 地面 + 1 标记 + 1 触发器 |
| 1846 组件 | 920 × 2（`Transform`+`Sprite`）+ 标记 4（`Transform`/`Sprite`/`Collider`/`Velocity`）+ 触发器 2（`Transform`/`Collider`） |
| 921 提取项 | 触发器没有 `Sprite`，不产生绘制项 |
| 926 quad | 921 + 5 个调试图元（光标框、光标线、2 个调试矩形等在 M06 侧的固定开销） |
| 143 实心格 | 墙列 23 + 外边框 122 − 两处重叠（墙列首尾格已在边框里） |
| 标记 x=344 | 墙列 22 的左边界 = `22 × 16 = 352`；标记宽 8 → `352 − 8 = 344` |
| 标记 y=224 | 起始 200 + `0.2 × 120 × 1.000s = 24` |
| `blocked = 26` | 34 步走到位，之后 26 步持续顶墙 |
| `1 begin / 1 end` | 标记 x 从 320 到 344，穿过 330..338 的触发体积后离开 |
| `probe = 4 px` | 射线起点 `344 + 4 = 348`，墙在 352 |
| `worst bucket 2` | 标记与触发器落在同一个 64 px cell |

### Development 会多跑一步

同一份代码，Development 有时打印 `61 step(s)`、标记 `(344, 224.4)`、`blocked = 27`。重复运行四次实测：一次 60 步、三次 61 步。

原因是 M04 的固定步由墙钟累积驱动，某一帧稍慢就会补一步，**与物理无关**。证据是 x 始终是 344（被墙夹死，完全稳定），只有 y 每多一步多 `0.2 × 120 / 60 = 0.4`。

因此**不要再声称"三配置输出逐字相同"**——至少对含有速度积分的冒烟场景不成立。要断言确定性，用 `Identical worlds produce identical motion` 用例（同一 world 重跑两次，逐位相等），而不是比对跨进程的帧数。

## 4. 语义决策表

| # | 决策 | 选择 | 理由 |
|---|---|---|---|
| 1 | 命名空间 | `fa::physics` | 与草案一致 |
| 2 | 矩形类型 | 复用 `core::Rect` | ADR-0004；避免两套半开约定 |
| 3 | 盒子锚点 | `Transform::position` = 精灵左上角 | 与 `DrawItem::position`、`sortY` 同源 |
| 4 | 是否要 `IPhysicsWorld` | 不要，`PhysicsWorld` 是具体类，每次传 `const World&` | 不存 `World&` 就没有悬空引用；单一实现不值得虚接口 |
| 5 | 静态几何接口 | `ISolidTiles`（只有 `tileSize`/`isSolid`） | M12 未实现且不应被反向依赖 |
| 6 | 调试绘制 | 产出 `DebugShape` 列表 | 无 GL 也能测，`fa_physics` 不需要 `IRenderer` |
| 7 | 触发器事件 | 每步记录 `triggerEvents()`，不发布到 M03 | 去掉 `fa_events` 依赖，测试可直接断言序列 |
| 8 | 实体对实体 | 用步首宽相快照 | 静态几何精确优先；实体互推延后 |
| 9 | 瓦片何时阻挡 | 仅当移动者 `mask` 含 `World` | 给飞行单位留正当出口 |
| 10 | 子步长度 | 固定 4 px | 16 px 瓦片的 1/4，冲刺（192 px/s = 3.2 px/步）也只需一步 |
| 11 | 已重叠的阻挡物 | 忽略（放行） | 卡住必须能脱出，否则浮点误差变永久监禁 |
| 12 | `mask` 类型 | `CollisionMask`（uint32） | 掩码是集合而非单值 |
| 13 | 计数器与 `const` | `m_stats` 用 `mutable` | 查询是 `const`，但仍要计 `overlapQueries`/`rayQueries` |

## 5. 踩坑

1. **`std::unordered_map` 需要 `/EHsc`。** `SpatialHash` 用哈希表做桶，`<unordered_map>` 的头文件里有 try/catch 内部实现，`ExceptionHandling=false` 下报 C4530，`/WX` 把警告升级成 C2220。修法是在 `fa_physics.vcxproj` 里加 `<ExceptionHandling>Sync</ExceptionHandling>`。
   判断依据不是"有没有 `<chrono>`/`nlohmann/json`"，而是**这条 include 链里有没有异常相关的标准库实现**。`fa_physics` 的判据是 `<unordered_map>`。

2. **`limitAlongX` 在负方向的符号写反过一次。** 初版对 `delta < 0` 用 `right(blocker) - mover.x`，那是 `mover.x - right(blocker)` 的相反数，结果永远是负的、被当成"已穿透"从而放行——实体从右侧撞墙会直接穿过去。正确写法统一成"**移动方向的尖端**到**阻挡物的尾边**"：正方向 `blocker.x - right(mover)`，负方向 `mover.x - right(blocker)`。Y 轴同理。抓到这个 bug 的是"贴墙滑动"用例。

3. **`substeps` 对所有移动生效，不只是"高速"。** 测试里 `delta = {12.5, -3.0}` 期望 1 步，实际 4 步。4 px 上限与"是否高速"无关，任何超过 4 px 的位移都会被切分。文档里要写清"≤4 px 子步"而不是"高速才子步"。

4. **宽相会多捞，这是设计不是 bug。** 两个盒子都落在 cell (0,0) 时，查 cell (0,0) 会同时返回两个，即使其中一个的坐标在 32..96。测试最初断言 1 个，错在把宽相当成了窄相。

5. **`TileGrid::isSolid` 对界外返回 `outsideIsSolid`，默认 true。** 测试里 `REQUIRE_FALSE(isSolid(-1, 0))` 是错的；默认语义就是"地图外是实心的"，这正是有限地图有外墙的实现方式。

6. **半开区间会"吃掉"边界。** `Rect{15, 0, 2, 16}` 只覆盖 x 15..17、y 0..16，行范围是 `[0, 0]`——高度 16 正好停在格线上，够不到第 1 行。想测"跨两行"必须让高度越过 16。同理 `overlapsSolidTile` 对 `Rect{0,0,32,16}` 不认为碰到第 2 列。

7. **掩码判定是双向的，测试里只改了一边。** 让玩家挡住敌人，除了玩家 `mask` 加 `Enemy`，敌人的 `mask` 也必须含 `Player`，否则 `layersInteract` 仍然 false。这个规则在实现里是对的，但很容易在写测试/配内容时漏掉一边。

8. **`world.each` 里不要直接调用 `move`。** `move` 会查宽相并写 `Transform`，而 `each` 的回调持有组件引用。`step` 因此先把有速度的实体 id 收集到 `vector`，再逐个移动。没有结构变更时严格来说允许嵌套，但"引用只在一次回调内有效"的约定很容易被违反。

9. **统计快照与查询的先后顺序。** 冒烟里先取 `physicsStats()` 再发射线，结果打印 `0 ray(s)`——射线是之后才发生的。把探测挪到快照之前，`rayQueries` 才是 1。凡是"先快照后产生副作用"的地方都要注意顺序。

10. **`PhysicsSystem` 借用 `TileGrid`，而 `TileGrid` 是派生类成员。** C++ 的销毁顺序是"派生成员先于基类子对象"，而 `SystemPipeline` 在基类 `BasicScene` 里——如果不管，`m_tiles` 会先死，系统里就留下悬空指针。虽然 `PhysicsWorld` 析构不碰地形，但这是靠运气。修法：`DemoScene::onExit()` 里先 `systems().clear()`。

11. **`static constexpr` 成员不能用在成员初始化列表里。** 类在 mem-initializer 里还不完整，`m_tiles(kColumns, kRows, ...)` 会报"未声明"。改用构造函数体内 `m_tiles.resize(...)`，此时类已完整。

12. **新建 `.vcxproj` 要转 LF + 去 BOM。** 文件创建工具写 CRLF；仓库里 `.sln`/`.vcxproj` 的历史约定是 LF。转换脚本：`[IO.File]::WriteAllText($p, ($t -replace "\r\n","\n"), (New-Object System.Text.UTF8Encoding $false))`。

13. **`m_stats` 必须是 `mutable`。** `overlap`/`raycast` 是 `const` 查询，但要累加计数器；不改 `mutable` 就只能去掉 `const`，那会让调用方无法在只读上下文里查询。

14. **`ISolidTiles` 需要 `columns()`/`rows()` 才能画瓦片调试框。** 只有 `isSolid` 的话，`debugDraw` 无法知道遍历范围。接口给的默认值是 0 = "无有限范围"，`debugDraw` 见到 0 就跳过瓦片。

15. **拼写事故：`PhysicsWorld::MoveResultAlias`。** 定义 `move` 时手滑写了不存在的限定名，编译期直接报错。记录在此只为提醒：这类错误在 IDE 里几乎不可见，只能靠编译器。

16. **Development 与 Debug/Release 的冒烟帧数不同。** 见 §3 末尾。不要把"逐字相同"当作验收标准写进文档。

## 6. 复现命令

```powershell
$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'

# 全解决方案三配置（Release 最慢，Development 最快）
foreach ($cfg in @('Debug','Development','Release')) {
    & $msbuild FloriansAdventure.sln /t:Rebuild /p:Configuration=$cfg /p:Platform=x64 /m /nologo /v:m
}

# 测试（三配置）
foreach ($cfg in @('Debug','Development','Release')) {
    & ".\build\bin\x64\$cfg\fa_tests.exe"
}

# 只跑物理
& ".\build\bin\x64\Debug\fa_tests.exe" "[physics]"

# 冒烟
& ".\build\bin\x64\Debug\fa_app.exe" --frames 60
```

单编某个 `vcxproj` 时必须显式给 `SolutionDir`（否则 `build\Project.props` 找不到）：

```powershell
& $msbuild 'src\engine\physics\fa_physics.vcxproj' /t:Rebuild `
    /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir="$($PWD.Path)\" /m /nologo /v:m
```

## 7. 遗留风险

与模块文档 §9 对应，这里只列"落地时立刻会碰到"的部分：

1. 实体互推未实现，一步内可能重叠一帧。
2. 生成点若在墙内，该实体会一直能穿那面墙。
3. 每次 `move` 有一次 `vector` 分配（`queryUnique`），100 实体可接受但可优化。
4. 触发器全量重算，触发器上百时需要增量。
5. `PlayerAttack`/`EnemyAttack` 层已定义但尚无使用者——M20 落地时要确认掩码成对配置。
6. `Velocity` 是否进存档未定（M14）。
7. 没有圆形碰撞体，斜向贴墙靠缩小盒子缓解卡角。
