# M27 Debug Tools

| 项 | 值 |
|---|---|
| 层 | L4 |
| 优先级 | P1 |
| 状态 | 设计完成 |
| 依赖 | M00–M25 |
| 被依赖 | 无（叶子模块） |

## 1. 目的

提供开发期可观测性与操控能力：控制台、统计覆盖层、实体检查器、物理调试绘制、时间控制与快捷命令。仅在 Debug/Development 构建启用。

## 2. 范围

**负责**

- 控制台：命令注册、参数解析、历史、输出窗口（M09）。
- CVar：运行时配置（显示、音量、玩法开关），支持持久化。
- 性能覆盖层：FPS、帧时间、固定步数、实体数、绘制项、批次数、内存/资源计数。
- 实体检查器：搜索实体、查看/编辑组件、销毁/复制。
- 物理调试：碰撞盒、瓦片网格、射线、空间哈希格子。
- 渲染调试：线框、批次统计、纹理查看。
- 时间控制：暂停、单步、时间缩放、跳转时刻/日期。
- 便捷命令：传送、给予物品、生成敌人、杀死全部、存档/读档、切换地图。
- 日志窗口：按级别/分类过滤，错误高亮。

**不负责**

- 发布构建中的任何功能。
- 内容编辑（M26）。
- 作弊防护（单机游戏不设防）。

### 战斗与存档调试（ADR-0002）

- 显示耐力条数值、闪避冷却、无敌窗口剩余时间。
- 可强制触发自动存档、可查看当前存档槽状态与上次存档游戏时间。
- 可切换季节/时间以验证季节限制与温室逻辑。
- 所有调试入口仅在 Debug/Development 构建启用。

## 3. 核心概念与数据结构

```cpp
struct ConsoleCommand {
    std::string name;
    std::string help;
    std::function<void(std::span<const std::string>)> fn;
};

struct DebugStats {
    float fps;
    float frameMs;
    int fixedSteps;
    int entityCount;
    int drawItems;
    int batchCount;
    std::size_t textureCount;
};
```

- 命令注册在启动时完成；命令实现只调用公共接口。
- CVar 有类型（bool/int/float/string）与回调，变更立即生效。
- 调试覆盖层使用 M06 的即时绘制与 M09 文本。

## 4. 公共接口草案

```cpp
namespace fa::debug {

class IDebugTools {
public:
    virtual ~IDebugTools() = default;
    virtual void registerCommand(ConsoleCommand cmd) = 0;
    virtual void execute(std::string_view line) = 0;
    virtual void update(ecs::World& world, const time::FrameContext& time) = 0;
    virtual void draw(ui::IUi& ui, render::IRenderer& renderer, const DebugStats& stats) = 0;
    virtual bool visible() const = 0;
    virtual void setVisible(bool visible) = 0;
};

} // namespace fa::debug
```

## 5. 依赖关系

- 依赖 M04（时间控制）、M06（绘制）、M07（快捷键）、M09（UI）、M11（实体）、M13（物理调试）、M25（流程命令）。
- 被 composition root 在开发构建中注册；不进入发布构建。
- 不反向依赖玩法模块内部；需要玩法数据时通过公共查询接口。

## 6. 扩展点

- 新增命令分组与自动补全。
- 新增可视化面板（AI 状态、任务进度、经济流水）。
- 新增录制/回放（输入 + 时间）。
- 新增远程控制（TCP/ImGui 独立窗口）用于调试主机。

## 7. 实现阶段

P1，Phase F（部分可提前到 Phase B）。先实现控制台、FPS/实体统计、物理调试绘制；再实现检查器与时间控制。验收：可用命令传送/给物品/生成敌人；能定位穿墙与碰撞盒错位。

## 8. 风险与开放问题

- 调试代码泄漏到发布构建会带来性能与安全问题；用编译开关与独立目标隔离。
- 命令直接修改状态可能绕过规则导致误判；明确“调试命令不受玩法约束”。
- 控制台与文本输入上下文的冲突需处理。
- 是否引入 ImGui？若引入，需隔离在 M27/M26，不污染公共头。
