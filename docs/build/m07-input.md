# M07 输入：实现说明、实测结果与遗留风险

本文件记录 M07（Input）的落地细节：真实接口形态、本机实测结果，
以及实现过程中踩到并修掉的坑。凡是「为什么这样写」的背景知识都集中在这里，
源码里只保留 Doxygen 注释。

## 实测环境

与 [M00 实测结果](m00-fixups.md) 相同：

| 项 | 值 |
|---|---|
| Visual Studio | Community 2026，18.10.12224.181 |
| MSVC 工具集 | 14.51.36231（`v145`） |
| MSBuild | `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe` |
| vcpkg | `D:\CPPLibraries\vcpkg\vcpkg.exe`（`VCPKG_ROOT` 需每次显式设置） |
| triplet | `x64-windows-static-md` |

## 实测结果

三套配置（Debug / Development / Release × x64）Rebuild 全部成功，**零 warning、零 error**。

```text
fa_tests.exe → All tests passed (16598 assertions in 112 test cases)   exit = 0
fa_tests.exe "[input]"
             → All tests passed (444 assertions in 36 test cases)       exit = 0
fa_app.exe --frames 6
             → m07: 0 event(s) forwarded, context Gameplay, moving 0 step(s),
                    axis (0.00, 0.00), mouse (0, 0)
               M02+M03+M04+M07 smoke test OK                           exit = 0
```

`fa_tests` 的 112 个用例 = M01 的 22 个 + M02 的 8 个 + M04 的 20 个 + M03 的 26 个 + M07 新增 36 个。
三配置运行结果一致。

`m07:` 行在三配置下**逐字相同**，且连跑 3 次不变：

```text
m07: 0 event(s) forwarded, context Gameplay, moving 0 step(s), axis (0.00, 0.00), mouse (0, 0)
```

隐藏窗口的无头运行本来就没有真实键盘/鼠标事件，所以 `0 event(s)`、轴为 0、鼠标为 `(0, 0)`
都是**预期值**而不是「没接上」——这条行验证的是接线正确（事件确实流过了 `poll()`，
上下文确实是 `Gameplay`，读取路径不崩），真实输入行为由 `[input]` 的 36 个用例覆盖。

压力测试：M07 是纯单线程、无并发，跑 3 次 × 3 配置的 `fa_app` + 3 次全量 `fa_tests`，0 次失败。

## 交付内容

### 公共头（`src/engine/include/fa/input/`）

| 文件 | 内容 |
|---|---|
| `Action.hpp` | `ActionId`、`kInvalidActionId`、`actionIdFromName`、四个保留动作名/常量、`ActionState`、`ActionRegistry` |
| `Binding.hpp` | `BindingKind`、`Binding`、`isUsable` |
| `ActionMap.hpp` | `ActionMap`、按上下文/整份布局的 JSON 存取 |
| `InputContext.hpp` | `InputContext`、`kInputContextCount`、名称往返、`InputContextStack` |
| `InputDevice.hpp` | `InputDeviceKind`、`IInputDevice`、`KeyboardDevice`、`MouseDevice` |
| `Input.hpp` | `IInput`（玩法唯一依赖面）与 `InputSystem` |

### 私有实现

| 文件 | 内容 |
|---|---|
| `src/engine/input/src/Action.cpp` | `ActionRegistry` intern / 反查 |
| `src/engine/input/src/Binding.cpp` | `operator==`、工厂、`toString` / `fromString`、`isUsable` |
| `src/engine/input/src/ActionMap.cpp` | 绑定增删、单上下文与整份布局的 JSON |
| `src/engine/input/src/InputContext.cpp` | 名称表、上下文栈 |
| `src/engine/input/src/InputDevice.cpp` | 键盘/鼠标设备 |
| `src/engine/input/src/InputSystem.cpp` | 动作求值、上下文推入弹出、默认键位、reset、存取 |
| `src/engine/platform/src/Scancode.cpp` | `Scancode` 名称表 + 双向转换 |

### 辅助改动

| 文件 | 改动 |
|---|---|
| `src/engine/include/fa/platform/Scancode.hpp` | 新增：类型化 `Scancode` 枚举 |
| `src/engine/include/fa/platform/PlatformEvent.hpp` | `scancode` 由 `std::int32_t` 改为 `Scancode`；新增 `toString(MouseButton)` / `mouseButtonFromName` |
| `src/engine/platform/src/PlatformEvent.cpp` | 上述两个函数的实现 |
| `src/engine/platform/src/SdlPlatform.cpp` | 新增 `toScancode(SDL_Scancode)` 全表 switch（约 120 行） |
| `src/engine/input/fa_input.vcxproj` | 单独开启 `/EHsc`（见坑 #2） |
| `tests/fa_tests.vcxproj` | 引用 `fa_input` |
| `src/app/main.cpp` | `SmokeSink` 集成 `InputSystem`，输出 `m07:` 行 |

`fa_app.vcxproj` 在 M00 骨架期就已经引用了 `fa_input`，本轮无需改动。
`fa_input` 只引用 `fa_core` + `fa_platform`，**不引用任何上层模块**。

### 删除

`src/engine/input/src/InputSystem.cpp` 在 M00 骨架期提交过一个占位实现
（`int supportedDeviceCount()`），全仓库无人引用，已被正式实现整份取代。

## 语义决策

| 决策 | 选择 | 原因 |
|---|---|---|
| 动作身份 | `ActionId = uint32_t`，由名字 FNV-1a-64 折叠到 32 位 | 内容与代码无需中心注册表就能对齐；`constexpr` 让保留动作可以 `inline constexpr` |
| 动作 id 持久化 | **禁止**，JSON 只存名字 | 折叠值虽稳定，但名字是作者真正写的那个东西，改实现不该动存档 |
| 0 值 | 保留为无效；空名 → 0；非空名算出 0 → 折叠为 1 | 保证「有效名字永远不是 0」，`id == 0` 可以无条件当作未绑定 |
| 上下文的作用方式 | 每个上下文一份 `ActionMap`，只查栈顶 | 「打开菜单角色停止」变成零成本的天然结果，不需要逐动作 gating |
| 上下文栈底 | 永远 `Gameplay`，栈永不空 | 消除「当前没有 map」这一整类空指针/空查询分支 |
| `pop()` 于只剩一项 | 不弹，重置为 `Gameplay` | 面板配错对次数的容错；崩掉比弹错更糟 |
| 重复 `push` | 允许 | 两个重叠面板各弹自己的那一项，`tryPop(context)` 保证不误伤 |
| 多绑定的布尔语义 | OR | 一个动作多个键是常态 |
| 多绑定的轴语义 | 取**绝对值最大**的 `value`，保留符号 | 「W=+1 / S=-1」合成单个带符号轴，而不是相加后互相抵消 |
| 绑定方向 | `scale` 乘子，`-1` 反向 | 正反两个绑定不需要两个动作 |
| `moveAxis()` | `x = right - left`，`y = down - up`，长度 > 1 才归一化 | y 向下与屏幕坐标系一致；归一化让对角线不变快 |
| 边沿来源 | 本帧轮询到的事件，**不是**前后帧 diff | 「同帧按下+释放」两个边沿都能报出来，快速点击不丢 |
| 自动重复 | `repeat == true` 的 `KeyDown` 不产生新边沿 | 否则边沿动作按 OS 重复率连发 |
| 失焦 | 立即 `reset()` | 配对的 key-up 经常永远不到达，卡住的键比丢一次按下更糟 |
| 设备下行转型 | `asKeyboard()` / `asMouse()` 虚函数 | `/GR-` 无 RTTI，`dynamic_cast` 不可用；`static_cast` 对第三方设备是 UB |
| 不支持的种类 | 设备 `handles()` 返回 false，绑定惰性失效 | 未接手柄时手柄绑定「不触发」而不是「恒触发」或报错 |
| 滚轮 | **不**进 `handles()`，走 `scroll()` | 滚轮是增量不是持续态，当作 `isDown` 语义歧义 |
| 默认键位 | 只在动作**尚无任何绑定**时填充 | 玩家/项目编辑优先 |
| JSON 加载 | 宽松：单条绑定拼错只 Warn 跳过 | 手改一个错字不该清空整套键位 |
| 整份加载 | **合并**语义：文档缺失的上下文保留原绑定 | 部分文件是补丁而不是重置 |
| 动作名未注册时保存 | 跳过该动作 | 序列化成裸数字对作者毫无意义 |
| 鼠标坐标 | 设备存窗口像素，`InputSystem` 输出虚拟分辨率 | 缩放换算只发生在一处，UI 与玩法拿到同一坐标系 |

## 踩到的坑

### 1. `static_assert` 的枚举边界算错（C2338）

`Scancode.cpp` 想用 `static_assert` 保证「枚举最后一项 + 1 == `Count`」，写成了：

```cpp
static_assert(static_cast<int>(Scancode::RightGui) + 1 == static_cast<int>(Scancode::Count) - 1);
```

`RightGui` 后面紧跟的就是 `Count`，所以正确关系是 `RightGui + 1 == Count`。
多减了一个 1，编译直接 C2338：

```text
error C2338: static assertion failed: Scancode 的最后一个值必须紧邻 Count
```

**修法**：去掉 `- 1`。这类错误的好处是它只在编译期失败一次，改完就永久钉住了枚举与名字表的对应关系
（同一文件里还有一条 `allNamesDistinct()` 的 constexpr 校验，防止两个枚举值共用同一个名字）。

### 2. `nlohmann/json` 会拉入 `<chrono>`，`/EHsc-` 下必炸（C4530）

M03 已经踩过 `<chrono>` 与 `/EHsc-` 的冲突，当时用 `cl /EHsc-` 探针得出的结论是
「nlohmann/json 在 `/EHsc-` 下可以编译」。**这个结论是错的**，本轮 `fa_input` 首次编译就失败：

```text
...\include\chrono(2387): error C4530: 使用了 C++ 异常处理程序，但未启用展开语义。
```

探针之所以过关，是因为它没有实例化到真正会展开 `try/catch` 的那条路径；
一旦按真实用法写出 `parse` / `dump` 调用，`<chrono>` 的 tzdb 段就被实例化，`/WX` 把 C4530 升级成错误。

**修法**：照 `fa_events` 的范式，给 `fa_input.vcxproj` 单独加
`<ExceptionHandling>Sync</ExceptionHandling>`，附注释指向本文件。

`Directory.Build.props` 的全局 `ExceptionHandling=false` **没有改动**：
`fa_input` 自己的代码从不 throw、从不 catch，JSON 全部走 nlohmann 的**非抛出重载**
（`parse(begin, end, nullptr, false)`），所以开 `/EHsc` 只是为了让 `<chrono>` 能过编译，
不是因为本模块要用异常。

> 结论修正：**凡是 include `nlohmann/json` 的工程都需要 `/EHsc`**。
> 这条已写进 [编码与内容规范](../architecture/conventions.md) §6。

### 3. `addDevice` 用 `static_cast` 做下行转型是 UB

`InputSystem` 需要「键盘/鼠标的强类型访问器」（`keyboard()` / `mouse()`），
最初写法是对 `IInputDevice*` 做 `static_cast<KeyboardDevice*>`：

```cpp
if (device->kind() == InputDeviceKind::Keyboard) {
    m_keyboard = static_cast<KeyboardDevice*>(device.get());   // 错
}
```

`kind()` 是**设备自己报的**值，一个第三方 `IInputDevice` 完全可以报 `Keyboard` 却不是
`KeyboardDevice`。那时 `static_cast` 是未定义行为，而 `/GR-` 下 `dynamic_cast` 根本编不过。

**修法**：在 `IInputDevice` 上加两个默认返回 `nullptr` 的虚函数：

```cpp
virtual KeyboardDevice* asKeyboard() noexcept { return nullptr; }
virtual MouseDevice* asMouse() noexcept { return nullptr; }
```

`KeyboardDevice` / `MouseDevice` 各自 override 返回 `this`。`addDevice` 改成
`if (auto* keyboard = device->asKeyboard(); keyboard != nullptr)`。
这样既不需要 RTTI，也不需要假设 `kind()` 诚实——报错的那个设备只会拿到 `nullptr`。

### 4. `string_view::data()` 配 `%s` 会越界读

`logf` 用 `vsnprintf` 的 `%s` 打印输入上下文名，代码是：

```cpp
logf(LogCategory::Input, LogLevel::Debug, "context %s", toString(context).data());   // 错
```

`toString(InputContext)` 返回 `std::string_view`，指向枚举静态表里的字面量时分幸运地有 NUL，
但 `string_view` **从不承诺** NUL 结尾。一旦换成非字面量的来源就会读过界。

**修法**：全部改成精度限定的 `%.*s`：

```cpp
const std::string_view name = toString(context);
logf(LogCategory::Input, LogLevel::Debug, "context %.*s", static_cast<int>(name.size()), name.data());
```

这条规约在 M03 期间已经写进 `conventions.md`，本轮又踩了一次——凡是 `string_view`
进 `printf` 族，一律 `%.*s`。

### 5. `applyDefaultBindings()` 的「已绑定就跳过」判断放错了层级（真 bug）

默认绑定表里一个动作有多行（`MoveLeft` → `A`、`Left`），守卫写成了「逐行检查该动作是否已有绑定」：

```cpp
for (const auto& entry : defaults) {
    if (!map.bindings(action).empty()) {
        continue;                      // 第二行永远命中这里
    }
    map.bind(action, entry.binding);
}
```

于是**每个动作只装进第一个默认键**，第二个全被丢掉。表现是一串看似无关的失败：

| 失败断言 | 真实原因 |
|---|---|
| `isDown(kActionMoveLeft)` 按下 `A` 后为 false | 只有 `Left` 装上了，`A` 没装上 |
| `bindings(MoveUp).size() == 2` 实际 1 | 同上 |
| `unbind(Gameplay, Interact, MouseButton:Left)` 返回 false | `Interact` 只装了 `E` |

**修法**：守卫必须按**动作**而不是按行生效——本轮 `defaults[]` 内首次见到某动作时
判断一次「它在我开始之前是否已有绑定」，然后这次调用内后续同动作的行一律照装：

```cpp
std::vector<std::pair<InputContext, ActionId>> seeded;
for (const auto& entry : defaults) {
    const auto key = std::pair{entry.context, action};
    const bool seen = std::find(seeded.begin(), seeded.end(), key) != seeded.end();
    if (!seen) {
        if (!mapFor(entry.context).bindings(action).empty()) {
            continue;                    // 玩家/项目已经绑过，整组都不碰
        }
        seeded.push_back(key);
    }
    mapFor(entry.context).bind(action, entry.binding);
}
```

第一次修的时候把条件写成了 `if (known || !empty) continue;`（`known` 是「本次调用已 seed 过」），
逻辑正好反了——变成「seed 之后的行全部跳过」，测试结果与修复前**一模一样**。
这个反复本身值得记录：**症状完全没变时，先怀疑修复没生效，再看二进制时间戳**。

教训：这类「守卫放在循环里但语义是批量的」缺陷，单看代码很像对的，
必须靠「一个动作两个默认键」的用例才能抓到。默认绑定表天然就是多行同动作，所以用例必须断言
`size() == 2` 而不是 `> 0`。

### 6. 测试辅助函数想「按值返回 `InputSystem`」编不过

`InputSystem` 持有 `unique_ptr` 设备并显式 `delete` 了拷贝构造。按 C++ 规则，
一旦用户声明了拷贝构造（哪怕是 `= delete`），移动构造就**不会**被隐式生成。
所以 `InputSystem makeSystem() { InputSystem s; ...; return s; }` 在返回具名局部变量时
需要移动构造，直接编译失败。

**修法**：辅助函数改成接收引用、就地配置：

```cpp
void configure(InputSystem& input) {
    input.addDevice(std::make_unique<KeyboardDevice>());
    input.addDevice(std::make_unique<MouseDevice>());
    input.applyDefaultBindings();
}
```

顺带修掉同批的两处：`bind()` 返回 `void`，`REQUIRE(map.bind(...), true)` 这种写法编译不过；
`Binding{}`（`BindingKind::None`）必须被 `bind()` 拒绝，用例一度写了三行互相矛盾的断言
（两行 `CHECK` + 一行 `CHECK_FALSE`），已收敛成单行 `CHECK_FALSE`。

## 复现命令

```powershell
$env:VCPKG_ROOT = 'D:\CPPLibraries\vcpkg'
.\scripts\build.ps1 -Configuration Development -NoInstall
.\build\bin\x64\Development\fa_tests.exe
.\build\bin\x64\Development\fa_tests.exe "[input]"
.\build\bin\x64\Development\fa_app.exe --frames 6
```

三配置逐字比对 `m07:` 行：

```powershell
foreach ($cfg in 'Debug', 'Development', 'Release') {
  .\build\bin\x64\$cfg\fa_app.exe --frames 6 |
    Select-String 'm07:'
}
```

`InputSystem` 不在解决方案里单独可跑，`fa_input` 是静态库；验证入口只有 `fa_tests` 与 `fa_app`。

## 全新克隆验证

针对提交 `5957b64`（`feat(input): implement M07 input system`）做了一次全新克隆：

```powershell
git clone --no-hardlinks . "$env:TEMP\fa-clone-m07"
cd "$env:TEMP\fa-clone-m07"
$env:VCPKG_ROOT = 'D:\CPPLibraries\vcpkg'
.\scripts\build.ps1 -Configuration Development      # 含空 vcpkg_installed 的 manifest 安装
.\build\bin\x64\Development\fa_tests.exe
.\build\bin\x64\Development\fa_app.exe --frames 60
```

```text
fa_tests.exe → All tests passed (16598 assertions in 112 test cases)   exit = 0
fa_app.exe   → ran 60 frame(s) in 1.007s
                 fixed steps: 60, dropped: 0
                 m03: 60 tick event(s) (last tick 60), 60 upload event(s) worth 1831
                 m07: 0 event(s) forwarded, context Gameplay, moving 0 step(s),
                      axis (0.00, 0.00), mouse (0, 0)
               M02+M03+M04+M07 smoke test OK                            exit = 0
```

新增的 `Scancode.hpp` / `Scancode.cpp` 与整个 `input/` 目录在克隆里都正常参与构建，
说明没有文件被 `.gitignore` 误伤（M02 曾因 `/build/` 把 `build/*.props` 一起忽略而踩过这个坑）。

> 注：克隆到 `%TEMP%` 下会额外出现 `MSB8029`（中间目录位于临时目录），
> 这是存放位置的副作用，与本仓库配置无关；正常路径下无此警告。

## 遗留风险

| 风险 | 影响 | 处理 |
|---|---|---|
| `ActionId` 是 32 位折叠哈希 | 两个动作名理论上会碰撞 | 动作名是作者内容，不做对抗性假设；契约禁止持久化 id |
| 动作 id 跨项目不通用 | 换项目后同一动作的 id 变化 | 存档只写名字，加载时重新 intern |
| `MouseWheel` 不能绑成动作 | 滚轮只能走 `scroll()` | v1 故意；需要「滚轮切换快捷栏」时在 M09 直接读 `scroll()` |
| 无手柄后端 | v1 不支持手柄（ADR-0002 决策 4） | 绑定格式、`InputDeviceKind::Gamepad`、`handles()` 已预留 |
| 无宏/组合键/长按/双击 | 复杂输入要玩法自己组合 | 扩展点在绑定层；`IInput` 保持窄接口 |
| 文本输入上限 256 字节 | 超长粘贴被截断 | 够用于命名框；需要长文本时再调 |
| `applyDefaultBindings()` 无法区分「用户清空了」与「从没设过」 | 玩家解绑全部键后，下次启动会被重新填默认键 | 让存档写空数组即可（合并语义已支持空表）；若确实需要，加显式「已初始化」标记 |
| 无输入录制/回放 | 无法做确定性回放测试 | `poll()` 是唯一入口，记录 `PlatformEvent` 序列即可接入 |
| `InputSystem` 非拷贝非移动 | 无法按值返回，测试里只能就地配置 | 有意为之（拥有设备所有权）；需要移动语义时再显式补 `= default` |
| 上下文数量固定为 5 | 新增上下文要改枚举并重编译 | 5 个覆盖 v1；真要动态增删得改成字符串键，代价不划算 |
