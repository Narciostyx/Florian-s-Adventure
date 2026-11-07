# M26 Editor Tools —— 构建记录 / 实测

对应模块文档：[26-editor-tools.md](../modules/26-editor-tools.md)

> **状态：已实现并三配置实测通过。** v1 提供游戏内瓦片编辑器：笔刷/矩形/填充/
> 擦除/取色、图层切换、命令差分撤销/重做、与运行时一致的 JSON 保存/加载；
> Debug/Development 启用，Release 不链接。

## 1. 实际落地

新增工程 `src/engine/editor/fa_editor.vcxproj`（GUID `{C0DE0018-…0018}`），
引用 `fa_core`、`fa_tilemap`、`fa_render`、`fa_ui`。

| 文件 | 职责 |
|---|---|
| `fa/editor/EditorTypes.hpp` | `EditorTool`、`TileChange`、`EditorCommand`、`EditorFrameInput`、`EditorStats` |
| `fa/editor/EditorHistory.hpp` + `src/EditorHistory.cpp` | 有界 undo/redo、redo 清空、脏标记、计数 |
| `fa/editor/MapEditor.hpp` + `src/MapEditor.cpp` | 笔刷/矩形/填充/擦除/取色、图层、预览、`mapText()` |
| `fa/editor/EditorOverlay.hpp` + `src/EditorOverlay.cpp` | 激活状态、鼠标路由、M09 状态面板 |
| `tests/editor.test.cpp` | `[editor]` 10 用例 / 310 断言 |

同时在 M12 增加：

| 文件 | 职责 |
|---|---|
| `fa/tilemap/TilemapWriter.hpp` + `src/TilemapWriter.cpp` | `saveTilemapToJson()`，与 `loadTilemapFromJson()` 对称 |

### 关键实现选择

- **命令差分**：`EditorCommand` 只保存被修改格子的 `before`/`after`，撤销一次只回写
  这些格子；50 步撤销不复制整张地图。
- **工具**：
  - Brush：单格写入；Erase 写入 `kEmptyTile`。
  - Rect：第一次点击记录起点，第二次点击填充并记录为一条命令；移动时 `previewCells()`
    返回矩形。
  - Fill：4 邻接洪水填充，只填充与种子相同的连续区域，单次上限 4096 格。
  - Picker：把当前格 tile id 设为笔刷，不产生命令。
- **有界历史**：默认容量 128；`record()` 清空 redo，超出容量丢弃最旧命令。
- **保存格式**：直接复用 M12 的新 writer，字段与 loader 完全对称；写文件仍由
  composition root 用 M02 的 `writeTextAtomic()` 完成，M26 不链接平台层。
- **编辑器与运行时同图**：`EditorOverlay` 借用 `DemoScene` 的 `Tilemap`，修改后
  下一帧 M12 提取就能看到，没有导入/烘焙步骤。地图数据现在来自
  `data/maps/*.json`（缺失回落内置布局），所以编辑器改的就是 JSON 载入的实时地图；
  `S` 仍写 `saves/editor-map.json`，把保存目标切到 `data/maps/*.json` 是后续
  可视化编辑会话的接线项。
- **面板**：M09 画右上角状态面板（工具/图层/笔刷/undo/redo/命令数/脏标记/map 尺寸），
  面板区域内的点击不会落到地图上。
- **快捷键**（由 app 路由）：F2 开关；1–5 选工具；`[`/`]` 切图层；Z/Y 撤销/重做；
  S/L 保存/加载；Esc 关闭。
- **构建边界**：`Directory.Build.props` 在 Debug/Development 定义
  `FA_ENABLE_EDITOR`；`fa_app.vcxproj` 只在非 Release 引用 `fa_editor`，app 代码
  全部用 `#if defined(FA_ENABLE_EDITOR)` 包住。Release 摘要打印
  `m26: disabled (release build)`，smoke OK 不含 `+M26`。
- **app 集成**：
  - `DemoScene::editableMap()` 暴露同一张地图；`EditorOverlay` 在场景 push 后创建，
    在 `stack.clear()` 前销毁。
  - 启动时保存 `saves/editor-map.json` 并用运行时 loader 读回，摘要报告
    `save ok, load ok`。
  - 鼠标悬停格画黄色框，矩形预览画蓝色框；UI 面板通过 `buildHud()` 提交。

## 2. 测试

`[editor]` 覆盖：

- 画笔/撤销/重做/脏标记/命令计数。
- 容量裁剪与新命令清空 redo。
- 50 次编辑全部撤销再全部重做，地图内容恢复。
- Rect 预览与一次命令填充。
- Fill 被墙隔开的区域不越过。
- Erase 与 Picker。
- Map JSON 往返：尺寸、tile 定义、图层、格子、area、portal、ambient。
- `MapEditor::mapText()` 能被运行时 loader 直接读取。
- Overlay 未激活不编辑、激活后点击落格、undo/redo、面板命中测试、图层夹取。
- 右键擦除/取消矩形。

实测：

```text
fa_tests.exe "[editor]" → All tests passed (310 assertions in 10 test cases)
fa_tests.exe            → All tests passed (29627 assertions in 510 test cases)
fa_app.exe --frames 60  → m26: off, tool brush, layer 0, brush 0,
                           undo 0 / redo 0, 0 command(s), dirty no,
                           map 40x23, save ok, load ok
                        → M02+…+M22+M26+M27 smoke test OK
fa_app.exe (Release)    → m26: disabled (release build)
                        → M02+…+M22 smoke test OK
```

## 3. 遇到的问题与结论

1. **M12 只有 loader，没有 writer。** 直接要求 M26 拼 JSON 会复制 schema；在 M12
   增加 `TilemapWriter` 后，编辑器和运行时共用同一份格式。
2. **矩形预览在起点也是 1×1。** 测试最初期待空预览；实现返回包含起点的单格矩形，
   这是更准确的“如果现在结束会填哪里”的预览，测试按此调整。
3. **空地图的 clamp 不能出现 hi < lo。** `clampedBounds()` 先检查
   `width/height > 0`，否则 `std::clamp(…, 0, -1)` 是未定义行为。
4. **Release 不能链接编辑器。** 只加 `FA_ENABLE_EDITOR` 宏不够；`fa_app.vcxproj`
   的 `ProjectReference` 也要加条件，否则 Release 仍会链接 `fa_editor`。
5. **面板点击不能穿透。** `EditorOverlay::overPanel()` 用视口坐标命中测试，
   `update()` 在面板内直接返回，避免拖面板时误画。
6. **保存目录可能不存在。** app 先 `createDirectories("saves")`，再用
   `writeTextAtomic()`；失败只记录 stats，不阻塞 smoke。
7. **加载不替换实时地图。** v1 的 L 键只校验文件能否被运行时 loader 读取；
   真正替换场景地图会丢失纹理绑定，留给后续编辑器会话。

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

- 没有对象/NPC/敌人/传送点放置面板；v1 只有瓦片编辑。
- 没有属性面板、内容检查器、动画预览、碰撞层可视化编辑。
- 没有自由相机/缩放；编辑器沿用运行时相机。
- 没有文本输入、键盘焦点链或滚动面板；M09 仍是鼠标即时模式。
- 没有自动瓦片重烘焙；编辑后按原 tile id 提取。
- 没有多步命令分组、保存前验证器或独立编辑器进程。
- L 键只验证加载，不替换实时地图。
