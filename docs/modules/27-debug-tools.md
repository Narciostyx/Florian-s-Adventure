# M27 Debug Tools

| 项 | 值 |
|---|---|
| 层 | L4 |
| 优先级 | P1 |
| 状态 | 已实现 |
| 依赖 | M01、M02、M06、M09、M11、M13 |
| 被依赖 | 无（叶子模块） |

> 实测与踩坑：[构建记录](../build/m27-debug-tools.md)

## 1. 目的

提供开发期可观测性与操控能力：控制台、CVar、统计覆盖层、日志窗口、实体检查器与物理调试绘制。仅在 Debug/Development 构建链接进 `fa_app`；Release 完全不引用 `fa_debug`。

## 2. 范围

**已实现**

- 控制台：命令注册、带引号的参数解析、历史、输出环、输入行编辑。
- 内置命令：`help`、`clear`、`history`、`echo`。
- 外挂命令：`stats`、`stats.show/hide/toggle`、`log.show/hide/toggle/clear`、
  `physics.show/hide/toggle`、`cvar.list/get/set`、`console.close`。
- CVar：bool/int/float/string 四种类型、变更回调、`name=value` 文本持久化。
- 统计快照：FPS、帧时间、固定步、实体/组件、绘制项/批次、资源、物理、音频、地图。
- 角色相机：当前控制实体的相机位置、视口尺寸、跟随目标与死区/平滑参数。
- AI 锁定目标：每个 agent 的状态、当前目标、可见性，以及 agent/目标的中心点。
- 日志窗口：捕获 `fa::core::log`，按最低级别过滤，环形容量，等级着色。
- 实体检查器：快照 `Transform`/`Sprite`/`Collider`，不持有组件指针。
- 物理调试：把 M13 的 `DebugShape` 列表转成 M06 的 `drawDebugRect` 调用。
- 覆盖层：通过 M09 `UiContext` 绘制统计面板、控制台与日志面板。
- 事件路由：F3 统计、F4 日志、F5 物理、反引号控制台；控制台打开时吞掉键盘/文本输入。
- app 命令：`teleport <x> <y>`、`give <item.id> <count>`、`entities`，以及 M14 接入后的
  `save [slot]`、`load [slot]`、`slots`、`delete <slot>`。

**未实现 / 后续**

- ImGui 或独立调试窗口；当前全部走 M09。
- 时间控制（暂停、单步、缩放）命令。
- 纹理查看器、线框模式、渲染目标调试。
- 生成敌人/杀死全部/存档/读档/切换地图等玩法命令。
- 命令自动补全、滚动、搜索、远程 TCP 控制。
- 录制/回放（输入 + 时间）。

## 3. 核心概念与数据结构

```cpp
struct ConsoleCommand {
    std::string name;
    std::string help;
    std::function<void(DebugConsole&, std::span<const std::string>)> handler;
};

enum class CVarType { Bool, Int, Float, String };

struct DebugCameraInfo {
    bool valid;
    core::Vec2 position, viewportSize;
    core::Rect visible;
    scene::EntityId followTarget;
    bool following;
    float deadzoneX, deadzoneY, smoothing;
};

struct DebugAiTargetInfo {
    scene::EntityId agent, target;
    bool hasTarget, targetVisible;
    std::string state;
    core::Vec2 agentPosition, targetPosition;
    bool hasAgentPosition, hasTargetPosition;
};

struct DebugStats {
    float fps, frameMs;
    std::uint64_t frameCount, fixedSteps, droppedSteps;
    double simulatedSeconds;
    std::size_t entities, componentTypes, components;
    std::size_t worldDrawItems, uiDrawItems;
    std::uint32_t drawCalls, quads, textureSwitches, liveTextures;
    std::size_t assetTextures, placeholderTextures, failedAssets;
    std::size_t physicsBodies, physicsCells;
    std::uint64_t physicsMoves, physicsBlocked;
    int audioVoices, audioSampleRate;
    std::uint64_t audioDropped;
    std::size_t mapTiles, mapSolidTiles;
    DebugCameraInfo camera;
    std::vector<DebugAiTargetInfo> aiTargets;
};
```

- 控制台输出是 `std::vector<std::string>` 的环形窗口（最多 256 行），历史最多 64 条。
- CVar 用 `std::vector<Entry>` 线性查找，避免 `std::unordered_map` 的额外异常路径。
- `DebugLogSink` 用 `std::atomic_flag` 自旋锁保护环形日志，日志线程不阻塞在互斥量上。
- `DebugStats` 由 composition root 填充；M27 不反向查询 ECS/资源/音频。
- 相机与 AI 目标都是**纯数据快照**：M27 不知道 `Camera2D` 或 `AiService` 的存在，
  只格式化 app 交来的坐标/实体 ID。
- 覆盖层文本最多列出 6 个 AI 目标；更多时显示 `+N more`，避免面板无限增长。
- F3 同时在世界里画 agent 方框、agent→target 连线和目标方框；
  目标可见为橙色，丢失视线为灰色。F5 的物理调试仍独立开关。

## 4. 公共接口

```cpp
namespace fa::debug {

struct ConsoleCommand { /* name, help, handler */ };
class DebugConsole {
public:
    bool registerCommand(ConsoleCommand command);
    bool execute(std::string_view line);
    void print(std::string_view line);
    void clear();
    const std::vector<std::string>& lines() const;
    const std::vector<std::string>& history() const;
    void setOpen(bool open);
    bool open() const;
    const std::string& input() const;
    void setInput(std::string text);
    void appendInput(std::string_view text);
    void backspace();
    bool submitInput();
    void historyPrevious();
    void historyNext();
};

class CVarRegistry {
public:
    bool registerBool/registerInt/registerFloat/registerString(...);
    const CVarValue* get(std::string_view name) const;
    bool set(std::string_view name, std::string_view value);
    std::vector<CVarInfo> list() const;
    core::Status save(platform::IFileSystem&, std::string_view path) const;
    core::Status load(platform::IFileSystem&, std::string_view path);
};

class DebugLogSink final : public core::ILogSink {
    void write(core::LogLevel, core::LogCategory, std::string_view) override;
    std::vector<DebugLogLine> snapshot() const;
    void clear();
    void setMinimumLevel(core::LogLevel);
};

class DebugOverlay {
    void draw(ui::UiContext&, core::Rect viewport, const DebugStats&, const DebugConsole&,
              const DebugLogSink&, const DebugOverlayState&) const;
};

class DebugTools {
public:
    DebugConsole& console();
    CVarRegistry& cvars();
    DebugLogSink& log();
    void setStats(const DebugStats&);
    void setStatsVisible(bool); bool statsVisible() const; void toggleStats();
    void setLogVisible(bool);   bool logVisible() const;   void toggleLog();
    void setPhysicsVisible(bool); bool physicsVisible() const; void togglePhysics();
    void setConsoleOpen(bool);  bool consoleOpen() const;
    bool handleEvent(const platform::PlatformEvent&);
    void draw(ui::UiContext&, core::Rect viewport) const;
    bool registerCommand(ConsoleCommand);
    void attachLogSink();
    void detachLogSink();
};

void drawPhysicsShapes(render::IRenderer&, std::span<const physics::DebugShape> shapes);

}  // namespace fa::debug
```

## 5. 依赖关系

- 依赖 M01（字符串/数学/日志/`Result`）、M02（`PlatformEvent`、`IFileSystem`）、
  M06（`UiContext` 需要的绘制类型、`IRenderer::drawDebugRect`）、M09（`UiContext`）、
  M11（`World` 快照）、M13（`DebugShape`）。
- **不依赖任何玩法模块**；`teleport`/`give`/`entities` 与存档命令由 composition root 注册，命令实现只调用公共接口。
- `fa_debug` 因为 `DebugInspector.cpp` include `fa/scene/World.hpp`（`<unordered_map>`）而打开 `/EHsc`。
- `fa_app` 只在 `Configuration != Release` 时引用 `fa_debug`；`fa_tests` 在所有配置链接它，
  因此 Release 仍能测试模块，但发布可执行文件不包含调试代码。

## 6. 扩展点

- 新增命令：`DebugTools::registerCommand`，命令拿到 `DebugConsole&` 与参数 span。
- 新增 CVar：注册时给默认值、帮助文本与变更回调；保存/加载自动包含。
- 新增面板：在 `DebugOverlay::draw` 里追加，或直接调用 `UiContext`。
- 新增可视化：参考 `drawPhysicsShapes`，把纯数据列表适配到 M06。
- 新增时间控制：由 app 注册命令并调用 `time::ITimeService`，M27 不直接拥有时间服务。
- 新增远程控制/ImGui：隔离在 M27/M26，不污染公共头。

## 7. 验收

- `fa_tests.exe "[debug]"` → 122 断言 / 12 用例全绿（含既有 `[debug]` 用例）。
- 全量测试 → 543 用例 / 30281 断言全绿。
- Debug/Development `fa_app.exe --frames 60` → `m27: 28 console command(s)`（含 `time.addDays`/`time.set`），smoke 行含 `+M27`。
- Release `fa_app.exe --frames 60` → `m27: disabled (release build)`，smoke 行不含 `+M27`。
- 三配置构建零 warning。

## 8. 风险与开放问题

- 调试代码泄漏到发布构建：已用条件 `ProjectReference` 隔离，但 `fa_tests` 仍链接它。
- 控制台只支持 ASCII；CJK 输入仍需 M09 多字节字体与文本输入。
- 控制台与玩法输入冲突：打开时吞掉键盘/文本，鼠标事件仍会传给 M07。
- 日志自旋锁在极端高频日志下会烧 CPU；日志量应是开发期可接受范围。
- `DebugStats` 只是快照，可能落后一帧；不要用它做玩法判定。
- AI 目标文本最多 6 行（多出的只显示数量）；agent/目标坐标也是上一帧快照。
- 实体检查器最多 64 条，且只展示 Transform/Sprite/Collider；无组件编辑/销毁。
- 覆盖层面板使用固定像素矩形，640×360 以外的分辨率需要重新排布。
- 物理调试每帧重建 `DebugShape` 列表；实体多时只在需要时开启 F5。
- 没有 ImGui、时间控制、纹理查看器与远程控制。
