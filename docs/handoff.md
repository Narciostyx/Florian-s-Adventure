# 会话交接：当前状态与下一步

> 面向「继续本项目工作的会话」。本文件在 `docs/` 下，而 `docs/` 已被 `.gitignore` 排除，
> 只存在于本机工作树；仓库里可见的对应说明是 [README](../README.md) 的「运行与验证」一节。
> 最近更新：2026-10-30。

## 1. 一句话状态

28/28 模块实现完毕；**真实美术 + 全中文界面 + CI 首次跑通 + 玩法模式拆分**已落地；
地图编辑器现在能放置 **NPC 与传送区域**并一次保存地图 JSON + `entities.json`；
正式开局是无脚本内容的占位农场，等待玩法设计填充。

## 2. 本轮确定的决策（不要重新讨论）

| 决策 | 内容 |
|---|---|
| 模式划分 | `--play`/`--debug` 是玩家模式，**默认带窗口**、运行到关闭；`--play-test`/`--frames`/`--soak`/`--stress` 是自动化模式，**默认隐藏**（CI 无 GPU 才不会硬退出）；`--window` 可对任意模式强制开窗口 |
| `--debug` 语义 | 正式游戏 + 调试叠加（F3 统计面板、物理调试、AI 目标连线）+ 日志级别 `Debug` |
| 内容归属 | 三张演示地图（`data/maps/farm.json`、`farm-scripted.json`、`forest.json`）与 `data/entities.json` 的脚本化实体（Elder/任务/商人/出货箱/床/荆棘/林地敌人）**只归自动化模式**；`--play`/`--debug` 读 `data/maps/play/`，且 spawn 表为空 |
| 日志落盘 | 只有玩家模式写 `data/log/Log_yyyyMMdd-HHmmss.txt`；控制台输出不变（M02 的 stderr sink 始终在）；每次启动新建一个文件；`data/log/` 已加入 `.gitignore` |
| 界面语言 | 面向玩家的字符串一律中文；标识符（id/枚举名/日志类别/控制台命令/存档字段）保持英文；游戏名 `Florian's Adventure` 作为专有名词保留英文 |
| 文档入库 | `docs/` 不入库：已 `git rm -r --cached docs`（81 个文件，磁盘保留）并在 `.gitignore` 加 `/docs/` |
| 编辑器可放置内容 | 「对象」scope 放置 `npc`/`bed`/`shipping`/`merchant`/`enemy`/`hazard`（写回 `data/entities.json`），「传送门」scope 点两格生成 `MapPortal`（写回地图 JSON）；**保存同时写两个文件，且只重写当前 `map` 的行**。NPC 不再硬编码：`entities.json` 里 `kind: "npc"` + `dialogue` 决定某张地图有没有可对话对象 |

## 3. 关键实现落点

| 位置 | 作用 |
|---|---|
| `src/app/main.cpp` `Options` / `parseOptions` | 模式解析与 `isPlayerMode()`（窗口默认值的唯一来源） |
| `src/app/main.cpp` `SmokeSink::usesDemoContent()` + `makeDemoScene()` | 地图文件与 spawn 表按模式二选一；`kNoSpawns` 是玩家模式的空表 |
| `src/app/LogToFile.{hpp,cpp}` | `RunLogFileSink`（M02 `ILogSink`）+ `runLogFilePath()`；缓冲后整体重写文件，因为 `IFileSystem` 没有 append |
| `src/app/main.cpp` `main()` 里 `platform->initialize()` 之后 | 创建 run log、`setLogLevel(Debug)`（仅 `--debug`） |
| `src/app/main.cpp` `SmokeSink::onVariableUpdate` | 每 60 次可变步 `fa::core::flushLogs()`，避免每帧重写文件 |
| `src/app/main.cpp` `initializeDebug()` | `--debug` 下 `toggleStats()` + `setPhysicsVisible(true)` |
| `data/maps/play/farm.json` | 占位世界：40×23 全草地 + 演示农场原有形状的 16 格可耕土，无 area/portal/objects |
| `src/app/main.cpp` `EntitySpawn.map` / `DemoScene::findSpawns` / `makeDemoScene` 的 `mapId` | 一张共享 spawn 表按地图分作用域：`farm` / `forest` / `play/farm`；`map` 留空 = 所有地图（兼容旧行） |
| `src/app/main.cpp` `buildEditorPlacements` / `editorPlacementKinds` / `saveEntitySpawns` | 编辑器与 `entities.json` 之间的双向映射；kind 决定写哪些字段 |
| `src/app/main.cpp` `DemoScene::buildNarrative` | 只有存在 `npc` 行时才创建可对话对象；对话图取自该行 `dialogue` |
| `src/engine/editor/` `EditorScope` / `MapPlacement` / `PlacementKind` | 编辑器的三种 scope 与「内容由组装根供给」的解耦点：编辑器不认识 NPC/床的语义，只认 kind 字符串与标签 |
| `Tilemap::mutablePortals/mutableAreas/removePortal/removeArea` | 编辑器需要原地改/删区域与传送点（M12 原先只有 add） |
| `tools/build_font_atlas.py` | 扫描 `src/`、`data/` 的非 ASCII 字符 → 烘焙 `assets/fonts/ui_atlas.{png,json}` |
| `src/engine/include/fa/render/RenderTypes.hpp` `drawsBefore` | **UI 层只比层、不比深度**（即时模式按提交顺序），否则不透明按钮会盖掉自己的标签 |
| `src/engine/ui/src/Text.cpp` | UTF-8 `nextCodepoint`、`fitToCodepoints`（取代按字节的 `substr`）、逐字形 `bearingY` |

## 4. 新增中文文案后必须做的事

```powershell
python tools/build_font_atlas.py --font <fusion-pixel-10px-monospaced-zh_hans.ttf> --size 10
```

否则新汉字会画成 `?`。校验覆盖度：

```powershell
# 期望：missing: 0、blank renders: 0、汉字尺寸 (10, 9)
python <会话目录>/files/check_atlas.py
```

## 5. 验证清单（改完按顺序跑）

```powershell
$msbuild='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
& $msbuild FloriansAdventure.sln /t:Build /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo
& .\build\bin\x64\Debug\fa_tests.exe            # 期望 578 用例 / 30659 断言全绿
& python scripts\check-format.py                # 期望 format ok: 285 file(s)
& .\build\bin\x64\Debug\fa_app.exe --play-test  # 期望 play-test: OK + smoke test OK，且不产生 data/log
& .\build\bin\x64\Debug\fa_app.exe --frames 60  # 期望 smoke test OK
# 三配置都要零 warning（/W4 /WX）
foreach ($c in 'Debug','Development','Release') { & $msbuild FloriansAdventure.sln /t:Build /p:Configuration=$c /p:Platform=x64 /m /v:minimal /nologo }
```

`--play` / `--debug` 会开窗口且不会自己退出，验证时用 `--frames N` 限制帧数，或后台启动后
按 PID 结束。

## 6. 已知问题与未做

1. **主菜单的「地图编辑器」冷启动无效**：`openVisualEditor()` 需要 `demoScene`，没开过游戏时
   它是空的，点了没有任何反馈。绕法：先开始游戏 → `Esc` → 主菜单 → 编辑器。语义待定。
2. **占位农场仍走不通闭环**：没有床（复活点）与传送门，所以死亡后无处可回、进不了林地。
   但现在**可以用编辑器自己摆**：「对象」scope 放床，「传送门」scope 点两格拉出触发区并设
   目标地图，保存即写回 `data/maps/play/farm.json` 与 `data/entities.json`。注意
   `play/farm` 的传送门 `target_map` 目前只能指向 `farm`/`forest`（演示地图）。
3. **编辑器的「区域（`MapArea`，如温室）」scope 未做**；对象的 `texture` 也不能在编辑器里选；
   对象与传送门的增删**不进撤销栈**（历史只记录瓦片 diff），只把会话标为未保存。
4. **引擎 `Status` 文本仍是英文**，会通过 `"交互失败：" + status.error().message` 这类拼接进
   HUD；M27 开发者工具（F3/控制台/日志）按决定保持英文。
5. **`--play-test` 依赖演示地图与 `entities.json` 的行**：改 `data/maps/farm.json`、
   `forest.json` 或这些行会动到自检断言。`map` 字段留空 = 「所有地图」，是旧行兼容语义。
6. 像素级截图对比、128×128 地图基准、菜单键盘焦点链、真实音频素材、敌人招式与分帧动画未做。

## 7. Git 现状（截至本文更新时）

- 本地 `main` 与 `origin/main` **没有共同祖先**（两条独立历史：本地根 `5d4be5d`，
  远端根 `41aa082`）；远端历史上没有 `docs/`。
- 素材分支 `feat/sprite-assets` 已推送并开了 PR #1；它是用「临时 `GIT_INDEX_FILE` +
  `git rm -r --cached docs` + `git commit-tree -p origin/main`」构造的**无 docs 提交树**，
  不是直接推本地 main。CI 在该分支上已全绿。
- 本轮所有改动**尚未提交**：工作树含代码/资源改动，索引里另有 81 条 `docs/` 删除（本轮
  `git rm --cached` 产生）。提交前需要确认这些删除是否要一起进 commit。
- 推送走 SSH 显式 URL（`github.com:443` 在本机不可达，`github.com:22` 与 `api.github.com` 可达）。

## 8. 环境坑（省时间）

- **工作目录**：`config/`、`data/`、`assets/`、`saves/` 都相对 CWD 解析。VS 调试默认 CWD 是
  `src/app`，会让 `data/catalog.json` 读不到并**静默**回落到内联冒烟 catalog（前 5 条故意没有
  `path`），看起来就是「素材/文案没生效」。已用 `LocalDebuggerWorkingDirectory` +
  运行时 `adoptProjectRoot()` 双重修掉。
- 必须构建 `.sln` 而不是单个 `.vcxproj`（依赖 `$(SolutionDir)`）。
- 中文字体是 Fusion Pixel 10px monospaced（SIL OFL 1.1，许可在 `assets/fonts/OFL.txt`）；
  4 MB 源 TTF **不入库**。`stb_truetype` 的 hinting 未实现，不能用来在线烘焙像素字体。
- PowerShell 里 `python -c "..."` 带引号常被解析失败，改写成临时 `.py` 文件更稳。
- 用脚本重写源文件时注意行尾：仓库工作树是 CRLF（`core.autocrlf=true`），
  Python 写文件要显式 `newline="\r\n"`。
