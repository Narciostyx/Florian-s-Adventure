# M07 Input

| 项 | 值 |
|---|---|
| 层 | L1 |
| 优先级 | P0 |
| 状态 | 已实现 |
| 依赖 | M00、M01、M02 |
| 被依赖 | M09、M16、M25、M26、M27 |
| 实现记录 | [M07 输入：实现说明、实测结果与遗留风险](../build/m07-input.md) |

## 1. 目的

把键盘、鼠标、手柄的原始事件转换为稳定的**动作状态**，让玩法只关心「移动/攻击/使用工具」，不关心具体按键。支持输入上下文与重绑定。

设计上的一句话约束：**玩法层只认识 `ActionId`，不认识 `Scancode`。** 这条约束是重绑定、按键提示、以及将来接入手柄时不必改玩法代码的全部原因。

## 2. 范围

**负责**

- 键盘/鼠标状态跟踪：按住、本帧按下、本帧释放。
- 动作映射：动作名 → 多个物理输入，多绑定按 OR 合成。
- 输入上下文栈：Gameplay、UI、Dialogue、Editor、TextInput；只有栈顶那一份 `ActionMap` 被查询。
- 轴输入：带符号强度（`scale`），四方向移动归一化。
- 鼠标位置（窗口像素 → 虚拟分辨率）、滚轮增量、文本输入。
- 绑定持久化（JSON），含宽松加载与按上下文合并。
- 默认键位（WASD/方向键/鼠标左键），只在动作尚无绑定时填充。

**不负责**

- 玩法键位语义（由 M16/M20 通过动作名使用）。
- UI 焦点/导航的具体行为（M09 消费动作，M07 只提供上下文）。
- 平台事件泵与 SDL 细节（M02；M07 只见 `PlatformEvent`）。
- 手柄后端（ADR-0002 决策 4：v1 不实现，但绑定格式与设备抽象已预留）。
- 宏/组合键/双击/长按（v1 不做，见 §9）。

## 3. 交付的文件

公共头 `src/engine/include/fa/input/`：

| 文件 | 内容 |
|---|---|
| `Action.hpp` | `ActionId`、`kInvalidActionId`、`actionIdFromName`、四个保留动作名/常量、`ActionState`、`ActionRegistry` |
| `Binding.hpp` | `BindingKind`、`Binding`、`isUsable` |
| `ActionMap.hpp` | `ActionMap`、`saveActionMapToJson` / `loadActionMapFromJson`、`saveAllActionMapsToJson` / `loadAllActionMapsFromJson` |
| `InputContext.hpp` | `InputContext`、`kInputContextCount`、`toString` / `inputContextFromName`、`InputContextStack` |
| `InputDevice.hpp` | `InputDeviceKind`、`IInputDevice`、`KeyboardDevice`、`MouseDevice` |
| `Input.hpp` | `IInput`（玩法唯一依赖面）与 `InputSystem`（完整实现） |

私有实现 `src/engine/input/src/`：`Action.cpp`、`Binding.cpp`、`ActionMap.cpp`、`InputContext.cpp`、`InputDevice.cpp`、`InputSystem.cpp`。
`fa_input.vcxproj` 显式列出源文件与公共头（VS2026 不再使用通配符）；新增实现文件时要同时改 `.vcxproj`。

工程 `src/engine/input/fa_input.vcxproj`（GUID `{C0DE0006-…0006}`）引用 `fa_core` 与 `fa_platform`。它**单独开启 `/EHsc`**（`<ExceptionHandling>Sync</ExceptionHandling>`），原因是 `nlohmann/json` 会拉入 `<chrono>`，见 [M07 坑 #2](../build/m07-input.md)。

同轮的辅助改动：`src/engine/include/fa/platform/Scancode.hpp` 与 `src/engine/platform/src/Scancode.cpp`（`Scancode` 枚举 + 名称表）、`PlatformEvent::scancode` 由裸整数改为 `Scancode`、`SdlPlatform.cpp` 里的 `toScancode()` 翻译表。

## 4. 核心概念与数据结构

### 动作身份：名字派生的 `ActionId`

```cpp
using ActionId = std::uint32_t;
inline constexpr ActionId kInvalidActionId = 0;
constexpr ActionId actionIdFromName(std::string_view name) noexcept;
```

`actionIdFromName` 是 **FNV-1a 64 折叠到 32 位**，`constexpr`。因此：

- 内容文件和玩法代码**不需要中心注册表**就能就同一动作达成一致，`kActionMoveLeft` 这类常量是 `inline constexpr`。
- 0 保留为「无动作」，空名字返回 0，非空名字若算出 0 折叠为 1，保证「有效名字永远不是 0」。
- **可以写进 JSON**：JSON 用动作**名字**做键，加载时重新 intern，不持久化数字。名字字符串的稳定性由内容作者保证，不由构建顺序保证。

`ActionRegistry` 只保留名字用于调试与重绑定界面，迭代顺序不作承诺。

### 动作状态

```cpp
struct ActionState {
    bool isDown = false;
    bool wasPressed = false;
    bool wasReleased = false;
    float value = 0.0f;
};
```

- 边沿直接来自**本帧轮询到的事件**，不是前后帧 diff。因此「同一帧按下又释放」会同时置 `wasPressed` 与 `wasReleased`，而 `isDown` 保持 false —— 这是 `diff` 实现拿不到的行为，也正是快速点击不丢的原因。
- `value` 是获胜绑定的带符号强度：`isDown` 乘上该绑定的 `scale`。
- 边沿只在当前逻辑帧有效，`endFrame()` 后清除。

### 绑定

```cpp
enum class BindingKind : unsigned char { None, Key, MouseButton, MouseWheel, GamepadButton, GamepadAxis };

struct Binding {
    BindingKind kind = BindingKind::None;
    fa::platform::Scancode scancode = fa::platform::Scancode::Unknown;
    fa::platform::MouseButton mouseButton = fa::platform::MouseButton::None;
    int gamepadControl = 0;
    float scale = 1.0f;
};
```

- `scale = -1` 表示「这个键反向推轴」，于是 `A/D` 两个绑定就能合成一个带符号横轴，不需要第二个动作。
- 文本形式（JSON 与调试输出共用一套）：`Key:A`、`Key:Left:-1`、`MouseButton:Left`、`MouseWheel:Left`、`GamepadButton:2`、`GamepadAxis:2:0.5`。`scale == 1.0f` 时省略第三段。
- `isUsable()` 判定「这个绑定永远不可能触发」，也就是 `kind == None` 或键/键位为 `Unknown`/`None`。重绑定 UI 用它拒绝装死键。
- `BindingKind::GamepadButton` / `GamepadAxis` 在 v1 **可解析、可保存、永不触发**：没有手柄设备会认领这两种 kind。这让今天写下的配置文件在接入手柄后直接生效（ADR-0002 决策 4）。

### 上下文即「各自一份 `ActionMap`」

`InputSystem` 持有 `std::vector<ActionMap> m_maps`，按 `InputContext` 索引；查询走 `activeMap()` = 栈顶那一份。

于是「打开菜单角色停止移动」不需要任何逐动作 gating：压入 `UI` 之后，Gameplay 的动作**根本不在被查询的那张表里**，物理键仍按着也不产生动作。这是本模块最重要的结构决策。

`InputContextStack` 的性质：

| 行为 | 语义 |
|---|---|
| 栈底 | 永远是 `Gameplay`，栈永不空 |
| `pop()` 于只剩一项时 | 不弹，重置为 `Gameplay` |
| `push(InputContext::Count)` | 忽略（非法值不入栈） |
| 重复 `push` | 允许；两个重叠面板各弹自己那一项 |
| `tryPop(context)` | 只在栈顶匹配时弹，返回是否弹出 |
| `contains(context)` | 任意深度匹配 |

### 设备抽象

```cpp
class IInputDevice {
public:
    virtual InputDeviceKind kind() const noexcept = 0;
    virtual KeyboardDevice* asKeyboard() noexcept { return nullptr; }
    virtual MouseDevice* asMouse() noexcept { return nullptr; }
    virtual bool handles(BindingKind bindingKind) const noexcept = 0;
    virtual void poll(std::span<const fa::platform::PlatformEvent> events) = 0;
    virtual void endFrame() noexcept = 0;
    virtual void reset() noexcept = 0;
    virtual bool held(const Binding&) const noexcept = 0;
    virtual bool pressed(const Binding&) const noexcept = 0;
    virtual bool released(const Binding&) const noexcept = 0;
    virtual float value(const Binding&) const noexcept = 0;
};
```

- `asKeyboard()` / `asMouse()` 取代向下转型。全局 `/GR-`（无 RTTI），`dynamic_cast` 不可用，而 `static_cast` 对第三方设备是 UB。默认返回 `nullptr` 意味着「不是我」。
- `handles(BindingKind)` 是「这个设备能不能产生这种输入」。不支持的种类返回 false，于是未支持的绑定**惰性失效**而不是恒真。
- `KeyboardDevice` / `MouseDevice` 是 v1 的两个实现。新增设备 = 实现接口 + `addDevice()`，玩法零改动。

### 鼠标变换

`MouseDevice::position()` 是**窗口像素**；`InputSystem::mousePosition()` 是**虚拟分辨率**，公式 `virtual = raw * scale + offset`，默认恒等。这样 1280×720 窗口与 320×180 像素画布之间的换算只发生在一个地方。

## 5. 公共接口

### `IInput`（玩法唯一依赖面）

```cpp
class IInput {
public:
    virtual bool isDown(ActionId action) const = 0;
    virtual bool wasPressed(ActionId action) const = 0;
    virtual bool wasReleased(ActionId action) const = 0;
    virtual float axis(ActionId action) const = 0;
    virtual fa::core::Vec2 moveAxis() const = 0;
    virtual fa::core::Vec2 mousePosition() const = 0;
    virtual void pushContext(InputContext context) = 0;
    virtual void popContext() = 0;
    virtual void endFrame() = 0;
};
```

M09/M16 只依赖这个接口。设备、JSON、SDL 都不出现在这里。

### `InputSystem`：帧顺序与查询

```cpp
void poll(std::span<const fa::platform::PlatformEvent> events);
ActionState actionState(ActionId action) const;   // 一次取齐四个字段
fa::core::Vec2 mousePositionRaw() const;          // 变换前的窗口像素
fa::core::Vec2 scroll() const;                    // 本帧滚轮格数 (h, v)
std::string_view textInput() const;               // 本帧 UTF-8 文本
void setMouseTransform(fa::core::Vec2 scale, fa::core::Vec2 offset = {0.0f, 0.0f});
```

**帧顺序是契约，顺序错了拿到的是上一帧或空**：

```text
poll(events)  →  玩法/UI 读动作  →  endFrame()
```

在 `poll()` 之前读，看到的是上一帧的残留；在 `endFrame()` 之后读，看到的是全空。

`moveAxis()` 的语义：`x = right - left`，`y = down - up`（**y 向下**），长度超过 1 才除以长度 —— 对角线不会更快。它只依赖 `MoveLeft/Right/Up/Down` 四个**保留动作名**，与项目绑了哪些键无关。

### 绑定与上下文

```cpp
bool bind(InputContext context, ActionId action, Binding binding);   // 不可用绑定返回 false
bool unbind(InputContext context, ActionId action, const Binding& binding);
void applyDefaultBindings();
void reset();                                                        // 清状态，保留绑定与上下文

ActionMap& mapFor(InputContext context);
ActionMap& activeMap();
const InputContextStack& contextStack() const;
```

`applyDefaultBindings()` 的规则：**动作已有绑定就完全不动它**（玩家/项目编辑优先），包括它剩下的默认键。默认表：

| 上下文 | 动作 | 默认绑定 |
|---|---|---|
| Gameplay | `MoveLeft` | `Key:A`、`Key:Left` |
| Gameplay | `MoveRight` | `Key:D`、`Key:Right` |
| Gameplay | `MoveUp` | `Key:W`、`Key:Up` |
| Gameplay | `MoveDown` | `Key:S`、`Key:Down` |
| Gameplay | `Interact` | `Key:E`、`MouseButton:Left` |
| Gameplay | `Cancel` | `Key:Escape` |
| UI | `Confirm` | `Key:Enter`、`Key:Space` |
| UI | `Cancel` | `Key:Escape` |

### 持久化

```cpp
std::string saveBindings() const;
fa::core::Status loadBindings(std::string_view json);
std::string saveContextToJson(InputContext context) const;
fa::core::Status loadContextFromJson(InputContext context, std::string_view json);
```

整份布局的形状：

```json
{
  "version": 1,
  "contexts": {
    "Gameplay": { "MoveLeft": ["Key:A", "Key:Left"], "Interact": ["Key:E", "MouseButton:Left"] },
    "UI": { "Confirm": ["Key:Enter"] }
  }
}
```

- **键是动作名字**，值是绑定文本数组。没有注册名字的动作跳过而不是写成裸数字。
- 空上下文省略，保持文件可读。
- **加载是合并而非重置**：文档里缺失的上下文保留原有绑定；`loadContextFromJson` 只动那一个上下文。
- **宽松**：单个绑定拼错只 Warn 并跳过，只有「不是 JSON / 不是 object / 缺 `contexts`」才返回错误。手改一个错字不该让玩家的整套键位归零。

## 6. 使用范式

### 玩法侧

```cpp
void PlayerController::update(const fa::input::IInput& input) {
    const fa::core::Vec2 axis = input.moveAxis();
    m_velocity = axis * kSpeed;

    if (input.wasPressed(fa::input::actionIdFromName("Interact"))) {
        m_pendingInteraction = true;
    }
}
```

### 打开菜单

```cpp
input.pushContext(fa::input::InputContext::UI);   // 角色立即停止响应移动
// ... 菜单期间只读 UI 的 Confirm/Cancel ...
input.tryPop(fa::input::InputContext::UI);
```

### 帧驱动（app 的接法）

`fa_app` 的 `SmokeSink` 就是标准样例：`onEvents()` 里 `poll(events)`，`onFixedUpdate()` 里读 `moveAxis()`，`onRender()` 里 `endFrame()`。`IFrameSink::onEvents` 每帧在固定步之前调用一次，正好对上契约。

### 重绑定

```cpp
const auto action = input.registerAction("Attack");
if (!input.unbind(InputContext::Gameplay, action, oldBinding)) { /* 原本就没绑 */ }
input.bind(InputContext::Gameplay, action, fa::input::Binding::fromKey(fa::platform::Scancode::J));
```

`bind()` 对 `BindingKind::None` 或 `Scancode::Unknown` 返回 `false`，重绑定界面据此提示「这个键不可用」而不是装一个死键。

## 7. 契约要点

| 契约 | 内容 |
|---|---|
| 帧顺序 | `poll` → 读 → `endFrame`，不可调换 |
| 边沿寿命 | `wasPressed`/`wasReleased` 只在本帧为真 |
| 自动重复 | `KeyDown` 且 `repeat == true` **不**产生新边沿，否则边沿动作会按 OS 重复率连发 |
| 同帧按下+释放 | 两个边沿同时为真，`isDown` 为假 |
| 失焦 | 立即 `reset()`：配对的 key-up 常常永远不到达，卡住的键比丢一次按下更糟 |
| 上下文 | 只有栈顶的 `ActionMap` 被查询；栈底永远是 `Gameplay` |
| 多绑定 | 布尔查询按 OR；`value` 取**绝对值最大**的那一项（保留符号） |
| 对角线 | `moveAxis()` 长度超过 1 才归一化 |
| 不可用绑定 | `bind()` 拒绝；解析时产生的 `None` 绑定惰性失效，不报错 |
| 动作 id | 由名字派生，跨运行/机器/编译器稳定；**数值不进存档**，存档只写名字 |
| 线程 | 单线程，全部 API 只在拥有线程调用；无锁、无原子 |
| 异常 | 模块本身不抛不捕；JSON 用 nlohmann 的非抛出重载 |

## 8. 验收

三配置（Debug / Development / Release × x64）全部通过：

| 项 | 结果 |
|---|---|
| 全量测试 | `All tests passed (16598 assertions in 112 test cases)` |
| `[input]` 过滤 | `All tests passed (444 assertions in 36 test cases)` |
| `fa_app --frames 6` | `M02+M03+M04+M07 smoke test OK`，exit 0 |
| `m07:` 行 | 三配置**逐字相同** |

模块文档设计版 §7 的验收点逐条对照：

| 验收点 | 状态 | 依据 |
|---|---|---|
| WASD 移动 | ✅ | `moveAxis()` 用例覆盖四个方向 + 对角归一化 |
| 鼠标点击交互 | ✅ | `Interact` 绑定 `MouseButton:Left` 的按下/边沿用例 |
| 打开菜单后角色停止移动 | ✅ | 上下文栈遮蔽用例（压 UI 后 Gameplay 动作全部为假） |
| 重绑定后生效 | ✅ | 重绑定用例 + `bind()` 拒绝不可用绑定 |
| 多槽持久化可用 | ✅ | JSON 往返、宽松加载、按上下文合并用例 |

## 9. 风险与开放问题

| 风险 | 影响 | 处理 |
|---|---|---|
| `ActionId` 是 32 位折叠哈希，理论会碰撞 | 两个动作名塌成同一 id | 动作名是**内容作者**输入，不做对抗性假设；碰撞需要同项目内两个名字同时撞上，概率可忽略 |
| 动作 id 跨项目不通用 | 换项目后 id 变化 | 契约规定只持久化名字；`ActionId` 是运行时身份，不是存档格式 |
| `MouseWheel` 不在 `handles()` 里 | 滚轮不能直接绑定成动作 | v1 故意：滚轮是增量不是持续态，绑成 `isDown` 语义歧义。改走 `InputSystem::scroll()` |
| 无手柄后端 | v1 不支持手柄（ADR-0002 决策 4） | 绑定格式、`InputDeviceKind::Gamepad`、`handles()` 已预留；接入手柄只加一个设备类 |
| 无宏/组合键/长按/双击 | 复杂输入需玩法自己组合 | v1 不做；扩展点是绑定层，`IInput` 保持简单 |
| 文本输入上限 256 字节 | 粘贴超长文本被截断 | 够用于命名框；需要长文本的编辑器场景再调 |
| `applyDefaultBindings()` 无法区分「用户清空了」与「从没设过」 | 玩家把某动作解绑全部键后，下次启动会被重新填默认键 | 需要「显式解绑」标记时在存档里记空数组（`loadBindings` 合并语义已支持空表） |
| 无输入录制/回放 | 无法做确定性回放测试 | 结构上只需记录 `PlatformEvent` 序列；`poll()` 是唯一入口，接入成本低 |
