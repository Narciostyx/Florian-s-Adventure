# M15 内容数据库 —— 构建记录

对应模块文档：[15-content-database.md](../modules/15-content-database.md)

## 1. 实测环境

| 项 | 值 |
|---|---|
| MSBuild | `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe` |
| 平台工具集 | v145 / MSVC `14.51.36231` |
| vcpkg | `D:\CPPLibraries\vcpkg`，triplet `x64-windows-static-md` |
| 配置 | Debug / Development / Release × x64 |
| 新增工程 | `src/engine/content/fa_content.vcxproj` |

`fa_content` 打开 `/EHsc`，因为 `ContentDatabase.cpp` 使用 nlohmann/json；
模块自身不 throw/catch。它只引用 `fa_core` 与 `fa_assets`，不引用 M11/M12。

## 2. 实测结果

| 检查 | Debug | Development | Release |
|---|---|---|---|
| 全解决方案 `Rebuild` | exit 0，0 warning / 0 error | exit 0，0 warning / 0 error | exit 0，0 warning / 0 error |
| `fa_tests.exe` | 390 用例 / 18421 断言，全绿 | 同左 | 同左 |
| `fa_app.exe --frames 60` | exit 0 | exit 0 | exit 0 |

用例拆分（三种配置一致）：

| 过滤 | 用例 | 断言 |
|---|---|---|
| `[content]` | 11 | 178 |
| `~[content]` | 379 | 18243 |
| 全量 | 390 | 18421 |

`11 + 379 = 390`、`178 + 18243 = 18421`：`content.test.cpp` 的每个用例都带
`[content]`，没有漏标签。

## 3. 冒烟输出与对账

Debug：

```text
  m15: 3 item(s), 1 crop(s), 1 recipe(s), 1 enemy(ies), 1 loot table(s), 0 error(s), schema 1, content v1, turnip stack 99
  m16: move, facing east, speed 90.0, stamina 100.0, 61 update(s), 61 move / 0 idle, 0 dodge(s), 0 action(s)
  m17: target cell (19, 12), entity, 12.6 px, reachable; 1 tool use(s), 1 placement(s), 3 event(s), 1 occupied cell(s), preview valid -> invalid
M02+M03+M04+M05+M06+M07+M11+M12+M13+M15+M16+M17 smoke test OK
```

| 观测 | 来源 |
|---|---|
| 3 物品 | `item.turnip_seed` / `item.turnip` / `item.turnip_soup` |
| 1 作物 | `crop.turnip`，3 个阶段，spring/fall |
| 1 配方 | `recipe.turnip_soup`，2 个 turnip → 1 个汤 |
| 1 敌人 | `enemy.slime`，掉落表 `loot.slime` |
| 1 掉落表 | `loot.slime`，0.5 概率掉种子 |
| 0 错误 | 所有引用都能解析 |
| schema 1 / content v1 | 一次成功加载 |
| turnip stack 99 | 按字符串查询 `item.turnip` |

## 4. 语义决策表

| # | 决策 | 选择 | 理由 |
|---|---|---|---|
| 1 | 加载输入 | JSON 文本，不是路径 | 文件归 M05；无平台也能测试 |
| 2 | 定义类型 | Item/Crop/Recipe/Enemy/LootTable | 先覆盖 P0 物品/作物/配方与战斗掉落 |
| 3 | ID | 命名空间字符串 + FNV 哈希 | 可读、稳定、查找便宜 |
| 4 | 前缀 | 加载期强制 `item.`/`crop.`/... | 内容错误尽早暴露 |
| 5 | 重复 ID | 报错，不覆盖 | 多文件合并时静默覆盖最难查 |
| 6 | 校验错误 | 收集全部，再返回 summary | 一次修完，不用逐个重启 |
| 7 | 失败加载 | 保留已解析数据，不推进版本 | 支持修好后重试；不是事务回滚 |
| 8 | `extra` | 不实现 | 高频字段强类型；弱类型 map 留到确有需要 |
| 9 | 瓦片定义 | 不重复建 | M12 的 `TileDef` 已存在 |
| 10 | 季节 | 位掩码 | 作物/配方/温室判定共用 |
| 11 | 资源校验 | 只校验 `AssetId` 语法 | 需要 catalog 才能查声明与否 |
| 12 | 错误码 | 6000–6008 | Save/Content 段的前 9 个 |
| 13 | 玩法工具映射 | `ItemDef::toolKind` 可选字符串 | M15 不依赖 M17 `ToolKind`；组装根在使用点转换，未知值在加载期报 `kInvalidField` |

## 5. 踩坑

1. **`ContentId` 的哈希不能对空字符串用 FNV。** 无效 ID 需要哈希 0，否则空 ID 会得到
   FNV offset basis，和“无 ID”语义不一致。构造函数只在非空时计算哈希。

2. **`Season::Any` 是 15，不是单独的枚举值。** `seasonAllows(mask, Any)` 直接用
   `mask & 15`，任何非零 mask 都匹配；写 `mask == Any` 会把 spring|summer 判错。

3. **`readSeasons` 要同时接受字符串和数组。** 单季作物写 `"spring"` 更自然，多季写数组；
   两条路径都要做非法名检查，不能只在数组分支里检查。

4. **`m_contentVersion` 最初忘了递增。** `loadDocument` 先写成直接 `return validate()`，
   结果成功加载后版本一直是 0。改为 validate 成功后再 `++m_contentVersion`。

5. **`validate()` 不能清空结构错误。** 最初 `validate()` 直接清空错误数组，会把
   `loadDocument` 刚收集的重复 ID/非法字段错误抹掉。改成三个数组：
   `m_parseErrors`、`m_referenceErrors`、`m_allErrors`；`validate()` 只替换引用部分。

6. **`std::span` 不能从 `std::map` 取连续视图。** 定义表用 `vector` 存值、`map` 存 id→index，
   查询 span 才是 O(1)；`map` 的节点不连续，不能直接当 span。

7. **指针只在加载完成后稳定。** 继续 `loadDocument` 可能让 `vector` 重分配；文档明确
   运行时实体应存 `ContentId`，不要长期存 `ItemDef*`。

8. **失败加载不是事务。** 解析到一半失败时，已经插入的定义会保留；这样第二次
   `loadDocument` 可以补上缺失引用（测试 `Content validation ... can be retried` 覆盖）。
   代价是发布多文件时要保证顺序与失败处理。

9. **默认值必须与文档一致。** `max_stack` 99、`sell_price` 0、`seasons` any、
   `required_minutes` 0、`amount` 1、敌人 100/0/60、掉落 1/1/1；少一个默认值就会让
   内容文件必须写全字段。

10. **`readIdField` 的 `starts_with` 用 string_view。** 传 `std::string` 也可以，但
    统一用 `std::string_view` 前缀，避免临时字符串。

11. **`nlohmann/json` 仍要求 `/EHsc`。** 即使只用 `parse(..., false)` 和 `is_*()`，
    头文件里的 tzdb/异常实现仍会触发 C4530；`fa_content` 单独打开。

## 6. 复现命令

```powershell
$env:VCPKG_ROOT = 'D:\CPPLibraries\vcpkg'
$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'

foreach ($cfg in @('Debug','Development','Release')) {
    & $msbuild FloriansAdventure.sln /t:Rebuild /p:Configuration=$cfg /p:Platform=x64 /m /nologo /v:m
    & ".\build\bin\x64\$cfg\fa_tests.exe"
    & ".\build\bin\x64\$cfg\fa_app.exe" --frames 60
}

& ".\build\bin\x64\Debug\fa_tests.exe" "[content]"
```

单编新工程：

```powershell
& $msbuild 'src\engine\content\fa_content.vcxproj' /t:Rebuild `
    /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir="$($PWD.Path)\" /m /nologo /v:m
```

## 7. 遗留风险

1. 全量加载，大内容包的内存/时间后续再优化。
2. 失败加载保留部分数据，不是事务回滚。
3. 返回的 `const Def*` 在后续加载/重载后可能失效。
4. 只校验 `AssetId` 语法，不校验 catalog 声明。
5. 没有 JSON Schema/本地化。
6. 瓦片定义仍在 M12；M15 没有第二套 tile 表。
7. 商店/对话/任务/动物行为表未实现。
8. 内容版本只是加载次数，不是内容哈希；跨存档兼容还要 M14 决定。
