# M27 Debug Tools —— 构建记录 / 实测

对应模块文档：[27-debug-tools.md](../modules/27-debug-tools.md)

> **状态：已实现并三配置实测通过。** Debug/Development 链接 `fa_debug`，
> Release 完全不引用它；`fa_tests` 在所有配置链接并测试该模块。

## 1. 实际落地

新增工程 `src/engine/debug/fa_debug.vcxproj`（GUID `{C0DE0011-…0011}`），
只引用 `fa_core`、`fa_platform`、`fa_render`、`fa_scene`、`fa_physics`、`fa_ui`。

| 文件 | 职责 |
|---|---|
| `fa/debug/DebugConsole.hpp` + `src/DebugConsole.cpp` | 命令注册、引号/转义解析、历史、输出环、输入行编辑、内置命令 |
| `fa/debug/DebugCVar.hpp` + `src/DebugCVar.cpp` | bool/int/float/string CVar、回调、`name=value` 保存/加载 |
| `fa/debug/DebugStats.hpp` + `src/DebugStats.cpp` | 统计快照与覆盖层文本格式化 |
| `fa/debug/DebugLog.hpp` + `src/DebugLog.cpp` | `ILogSink` 环形日志、级别过滤、自旋锁 |
| `fa/debug/DebugInspector.hpp` + `src/DebugInspector.cpp` | `Transform`/`Sprite`/`Collider` 实体快照 |
| `fa/debug/DebugDraw.hpp` + `src/DebugDraw.cpp` | M13 `DebugShape` → M06 `drawDebugRect` 适配 |
| `fa/debug/DebugOverlay.hpp` + `src/DebugOverlay.cpp` | 通过 M09 `UiContext` 绘制统计/控制台/日志面板 |
| `fa/debug/DebugTools.hpp` + `src/DebugTools.cpp` | 门面：命令、CVar、日志、可见性、事件路由 |
| `tests/debug.test.cpp` | `[debug]` 测试 |

### 关键实现选择

- **控制台解析**：`splitCommandLine` 支持双引号与 `\"`/`\\`；空引号参数也保留。
- **历史**：最多 64 条，连续重复命令不重复入栈；输入行支持上/下翻阅。
- **CVar**：`std::vector` 线性查找，避免 `<unordered_map>`；`save` 用
  `writeTextAtomic`，`load` 忽略注释、空行和未知名字。
- **日志**：`std::atomic_flag` 自旋锁保护 `std::vector<DebugLogLine>`；容量满时
  丢最旧一条并计入 `dropped()`。默认最低级别 `Info`。
- **统计**：`DebugStats` 由 app 每帧从 M05/M06/M11/M13/M08/M12 的公开 stats 填充；
  M27 不反向 include 这些模块的私有实现。
- **相机/AI 目标**：app 额外填入当前控制实体的相机（位置/视口/跟随目标/死区/平滑）
  与每个 `AiAgent` 的状态/目标/可见性/中心点；M27 只格式化与绘制。
  覆盖层文本最多列 6 个 AI 目标，其余显示 `+N more`。
- **AI 可视化**：F3 打开统计时，app 用 `drawDebugLine`/`drawDebugRect` 画
  agent 方框、agent→target 连线与目标方框；可见目标橙色、丢失视线灰色。
- **覆盖层**：app 已经 `ui->setOrigin(camera.visibleRect().xy)`，所以 M27 用虚拟坐标
  画面板；统计在右上、日志在中左、控制台在左下。
- **事件路由**：app 在 `input.poll()` 前把每个事件交给 `DebugTools::handleEvent`；
  被吞掉的事件不进入 M07。F3 统计、F4 日志、F5 物理、反引号控制台。
- **物理调试**：app 在 `renderer->endFrame()` 前调用
  `demoScene->physicsDebugShapes()` 与 `fa::debug::drawPhysicsShapes`。
- **Release 隔离**：`fa_app.vcxproj` 的 `fa_debug` ProjectReference 带
  `Condition="'$(Configuration)' != 'Release'"`；app 源码里的 include/成员/调用都由
  `FA_ENABLE_DEBUG_TOOLS` 守卫。`fa_tests` 无条件链接，保证 Release 也能跑模块测试。

### app 注册的命令

- `entities`：列出最多 16 个实体的 Transform/Sprite/Collider 摘要。
- `teleport <x> <y>`：直接移动 marker（下一次物理步仍由 M13 写位置）。
- `give <item.id> <count>`：调用 M18 背包 `add()`，输出剩余数量。
- `save [slot]` / `load [slot]` / `slots` / `delete <slot>`：M14 交付时补上的存档命令，
  默认槽为 `quicksave` / `autosave`。

## 2. 测试

`tests/debug.test.cpp` 覆盖：

- 命令行解析：引号、转义、空白；trim。
- 控制台：注册/重复拒绝、执行、未知命令、help、clear、历史去重。
- 输入编辑：`setInput`/`appendInput`/`backspace`/`submitInput`/上下列表。
- CVar：四种类型、回调、非法值、重复注册、`list`/`size`。
- CVar 持久化：内存 `IFileSystem` 的 save/load、注释/未知名字忽略。
- 日志：最低级别过滤、容量淘汰、`dropped()`、格式化、clear。
- 统计：`formatStats` 的关键行与数值；无 AI 时输出 `ai targets none`。
- 相机/AI：相机位置/视口/跟随目标/死区、agent 状态与 `-> target vis/hid` 文本。
- 检查器：Transform/Sprite/Collider 快照、无 Transform 实体被忽略、上限 0。
- 覆盖层：`UiContext` 队列确实收到面板/文字 item。
- 门面：F3/F4/F5/反引号切换、文本输入、Enter、Escape、控制台关闭后按键透传。
- CVar 命令：`cvar.set`/`cvar.get` 与非法值保持原值。

实测（三配置一致）：

```text
fa_tests.exe "[debug]" → All tests passed (122 assertions in 12 test cases)
fa_tests.exe           → All tests passed (30281 assertions in 543 test cases)
Debug/Development app  → m27: 28 console command(s), 0 cvar(s), camera ok, 1 ai target(s),
                         F3 stats / F4 log / ` console
                       → ...+M27 smoke test OK
Release app            → m27: disabled (release build)
                       → ... smoke test OK (no +M27)
```

`[debug]` 的 12 个用例包含 1 个既有的 `[debug]` 测试；M27 自己新增 11 个用例。

## 3. 遇到的问题与结论

1. **通配符展开后忘了把新测试文件加入 `.vcxproj`。**
   第一次运行 `fa_tests.exe "[debug]"` 只有 1 个用例 / 2 条断言，因为
   `tests/debug.test.cpp` 不在显式 `ClCompile` 列表里。修法是同时把新文件加入
   `tests/fa_tests.vcxproj`。这是 M08 去掉通配符后的固定流程。
2. **`registerCommand` 标了 `[[nodiscard]]`。**
   app 里三次调用触发 C4834（/WX 下报错）。用 `static_cast<void>(...)` 明确丢弃。
3. **`DebugInspector` 的 lambda 需要显式捕获 `world`。**
   `world.each` 的 lambda 只捕获了 `result`，调用 `world.tryGet` 时报 C3493；
   改成 `[&result, &world, maxEntities]`。
4. **`fa_debug` 需要 `/EHsc`。**
   `DebugInspector.cpp` include `fa/scene/World.hpp`，其中 `<unordered_map>` 会触发
   C4530 类问题。M27 是调试模块，直接打开 `ExceptionHandling=Sync`，不在头文件里
   拆 ECS 依赖。
5. **日志线程安全不用 `std::mutex`。**
   M27 要避免把 `<mutex>`/异常路径带进日志回调；用 `std::atomic_flag` 自旋锁，
   临界区只有一次 vector append/erase。
6. **Release 不能链接调试库。**
   只靠源码守卫不够，`fa_app.vcxproj` 的 ProjectReference 也要加
   `Condition="'$(Configuration)' != 'Release'"`；否则 Release 仍会把 `fa_debug.lib`
   链进可执行文件。`fa_tests` 则保持无条件链接，确保 Release 测试覆盖不降级。
7. **控制台与 M07 输入冲突。**
   app 不能先 `input.poll(events)` 再让控制台处理；必须在 poll 前过滤。实现里用
   `filteredEvents` 只转发未被吞掉的事件，quit/resize 仍从原始事件读取。
8. **覆盖层坐标。**
   `UiContext` 的 `panel`/`text` 接受的是虚拟坐标，调用方已经 `setOrigin`；不要
   再叠加 `viewport.x/y`，否则面板会偏移到世界坐标两倍处。
9. **物理调试不新增 M13→M06 依赖。**
   M13 继续只产出 `DebugShape`；M27 的 `drawPhysicsShapes` 是唯一适配点，app 只在
   F5 打开时调用。

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

- 控制台只支持 ASCII；CJK 依赖 M09 多字节字体与文本输入。
- 没有时间控制、纹理查看器、线框模式、生成敌人/存档/读档命令。
- 没有 ImGui、远程控制、录制/回放。
- 实体检查器只读，最多 64 条，只展示 Transform/Sprite/Collider。
- 覆盖层固定像素布局，非 640×360 需要重新排版。
- 日志自旋锁在高频日志下会烧 CPU；开发期可接受。
- `DebugStats` 落后一帧，不能作为玩法判定输入。
- AI 目标文本最多 6 行，多出的只显示数量；相机/AI 坐标也是上一帧快照。
- 目标连线只在 F3 统计可见时绘制；没有单独的“AI 调试”开关。
- 每帧重建 `aiTargets` 快照（调试路径，不在玩法热路径）。
