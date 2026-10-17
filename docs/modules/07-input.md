# M07 Input

| 项 | 值 |
|---|---|
| 层 | L1 |
| 优先级 | P0 |
| 状态 | 设计完成 |
| 依赖 | M00–M02 |
| 被依赖 | M09、M16、M25、M26、M27 |

## 1. 目的

把键盘、鼠标、手柄的原始事件转换为稳定的**动作状态**，让玩法只关心“移动/攻击/使用工具”，不关心具体按键。支持输入上下文与重绑定。

## 2. 范围

**负责**

- 键盘/鼠标/手柄状态跟踪：按下、按住、释放、本帧按下。
- 动作映射：动作名 → 多个物理输入；支持键盘/鼠标/手柄。
- 输入上下文栈：Gameplay、UI、Dialogue、Editor；栈顶消费，避免 UI 打开时角色移动。
- 轴输入：手柄摇杆死区、键盘数字轴归一化。
- 鼠标位置（窗口/虚拟分辨率）、滚轮、文本输入。
- 重绑定与配置持久化（JSON）。

**不负责**

- 玩法键位语义（由 M16/M20 通过动作名使用）。
- UI 焦点/导航（M09 消费动作）。
- 平台事件泵（M02）。

## 3. 核心概念与数据结构

```cpp
using ActionId = std::uint32_t;

struct ActionState {
    bool isDown;
    bool wasPressed;  // 本帧按下
    bool wasReleased; // 本帧释放
    float value;      // 数字/模拟轴
};

struct InputSnapshot {
    glm::vec2 moveAxis;
    glm::vec2 aimAxis;
    glm::vec2 mousePosition;
    std::span<const ActionState> actions;
};
```

- 动作名在启动时哈希为 `ActionId`；调试保留字符串。
- 同一动作可绑定多个输入；任一满足即触发（OR），轴取绝对值最大。
- `wasPressed`/`wasReleased` 只在当前逻辑帧有效，`endFrame` 后清除。

### 设备范围（ADR-0002 决策 4）

- v1 **仅实现键盘/鼠标**；不实现手柄 provider，不展示手柄提示，不保证手柄导航。
- 必须保留 `InputDevice` 抽象：`virtual void poll(const PlatformEvents&) = 0;`。
- 动作与物理按键分离：`ActionMap` 将 `Action` 映射到 `KeyCombo`；玩法层只读 `ActionState`。
- 新增设备仅需实现 `InputDevice` 并注册，不改玩法代码。
- 公开头不得暴露 SDL 手柄类型。

```cpp
class InputDevice {
public:
    virtual ~InputDevice() = default;
    virtual void poll(const PlatformEvents& events) = 0;
    virtual float axis(Axis axis) const = 0;
    virtual bool held(Action action) const = 0;
    virtual bool pressed(Action action) const = 0;
    virtual bool released(Action action) const = 0;
};
```

## 4. 公共接口草案

```cpp
namespace fa::input {

class IInput {
public:
    virtual ~IInput() = default;
    virtual void beginFrame() = 0;
    virtual void endFrame() = 0;
    virtual void pushContext(std::string_view name) = 0;
    virtual void popContext() = 0;
    virtual bool isDown(ActionId action) const = 0;
    virtual bool wasPressed(ActionId action) const = 0;
    virtual float axis(ActionId action) const = 0;
    virtual glm::vec2 moveAxis() const = 0;
    virtual glm::vec2 mousePosition() const = 0;
};

} // namespace fa::input
```

## 5. 依赖关系

- 依赖 M01（哈希/日志）与 M02（平台事件）。
- 不依赖 ECS/玩法；M16 读取 `IInput`，不反向依赖输入实现。
- M09 通过输入上下文接管 UI 导航；打开菜单时压入 `UI` 上下文。

## 6. 扩展点

- 新增设备：实现 `IInputDevice` 并注册；动作映射不变。
- 新增动作类型（双击、长按、组合键）：在映射层扩展，玩法接口保持简单。
- 输入录制/回放：记录 `InputSnapshot` 序列，用于测试与调试。
- 重绑定 UI：M09 提供界面，M07 提供 `rebind(action, device, code)`。

## 7. 实现阶段

P0，Phase A。先实现键盘 + 鼠标 + 动作映射 + 上下文栈。验收：WASD 移动、鼠标点击交互、打开菜单后角色停止移动、重绑定后生效。

## 8. 风险与开放问题

- 窗口失焦时按键释放事件可能丢失；失焦时清空所有按下状态。
- 手柄布局差异（Xbox/PS/Switch）；v1 以 Xbox 布局为默认，提供映射表。
- 文本输入与快捷键冲突；UI 文本框激活时压入 `TextInput` 上下文。
- 是否需要手柄支持进入 v1？架构已预留，产品决策待定。
