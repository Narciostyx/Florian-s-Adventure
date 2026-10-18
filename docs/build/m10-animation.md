# M10 Animation —— 构建记录 / 实测

对应模块文档：[10-animation.md](../modules/10-animation.md)

> **状态：已实现并三配置实测通过。** 帧序列、循环、事件、暂停/速度、四向选择与
> 状态机已落地；app 用一个 4 帧草地动画实体验证固定步推进与提取。

## 1. 实际落地

新增工程 `src/engine/anim/fa_anim.vcxproj`（GUID `{C0DE0013-…0013}`），
只引用 `fa_core` 与 `fa_render`。

| 文件 | 职责 |
|---|---|
| `fa/anim/AnimationClip.hpp` + `src/AnimationClip.cpp` | `Direction`、`AnimationFrame`、`AnimationClip`、校验/总时长 |
| `fa/anim/Animator.hpp` | `AnimationEvent`、`Animator` 纯数据组件 |
| `fa/anim/AnimationService.hpp` + `src/AnimationService.cpp` | 播放、停止、跨帧推进、事件、循环/完成 |
| `fa/anim/AnimationStateMachine.hpp` + `src/AnimationStateMachine.cpp` | 命名状态、方向变体、条件转换、优先级 |
| `tests/anim.test.cpp` | `[anim]` 测试 |

### 关键实现选择

- **不依赖 M05/M11**：`AnimationClip` 用裸 `render::TextureHandle`，纹理保活归加载方；
  `Animator` 是纯数据，ECS 调度由 composition root / M11 负责。`fa_anim` 不需要 `/EHsc`。
- **首帧事件**：`play()` 不直接产生输出；`Animator::eventPending` 让第一次 `update()`
  触发 frame 0 的事件，避免事件在 `play()` 后立刻被下一次 update 的 `events.clear()` 吃掉。
- **跨帧事件**：`update()` 用 while 循环消耗时间；每进入一帧触发一次事件。大 delta 也
  不会跳过中间帧的事件。
- **完成语义**：非循环在最后一帧停留，`finished=true`、`playing=false`；循环回绕到 frame 0。
- **速度/暂停**：`time += delta * speed`；`speed <= 0` 只保留首帧事件，不推进。
- **方向变体**：`DirectionalClips::forDirection` 缺省回退到 `down`，再回退到任意可用方向。
- **状态机**：转换按优先级选最高；`setState` 与状态/方向变化都会强制重启动画。
- **app 集成**：`DemoScene` 新增一个无碰撞的 4 帧草地实体；每固定步更新
  `Sprite::source`，纹理取 `AnimationClip::texture`。事件计数与最后事件进入摘要。

### app 动画内容

- 4 帧，每帧 0.12s，循环。
- 源矩形是占位草地纹理的四个 8×8 象限。
- frame 0 与 frame 2 有 `step` 事件；60 帧内约 1.5 个循环，实测 5 次事件。

## 2. 测试

`tests/anim.test.cpp` 覆盖：

- `AnimationClip`：空/零时长非法、总时长、`directionFromVector` 主导轴与 fallback。
- `AnimationService`：逐帧推进、首帧事件、跨帧事件、非循环完成与最后一帧。
- 循环：0.25s 跨两帧并回绕；time 保留；speed 2 推进；speed 0 暂停；stop 清事件。
- 状态机：idle/walk 条件转换、方向变体（up/left fallback）、重复/空状态拒绝、
  无效转换拒绝、`setState`、方向 fallback。

实测（三配置一致）：

```text
fa_tests.exe "[anim]" → All tests passed (59 assertions in 4 test cases)
fa_tests.exe          → All tests passed (28885 assertions in 462 test cases)
fa_app.exe --frames 60 → m10: 60 update(s), frame 1/4, 5 event(s), last step@0, looping
                       → ...+M09+M10+M11+... smoke test OK
```

加入动画实体后 `m11` 从 4 个实体变为 5 个，`m14` 摘要是 5 entities；这是预期变化。
M20 之后又加入两个 combat dummy，当前为 7 entities。

## 3. 遇到的问题与结论

1. **`updateAnimation` 最初放在 `DemoScene` 的 private 区。**
   `SmokeSink::onFixedUpdate` 无法访问。加了一个 public `advanceAnimation` 包装，
   保持内部更新逻辑私有。
2. **首帧事件会被下一次 update 的清空吃掉。**
   如果 `play()` 直接 push 事件，随后 `update()` 开头 `events.clear()` 就丢掉。
   用 `eventPending` 在第一次 update 补发，事件生命周期统一为“update 后读取”。
3. **大 delta 可能跳过事件帧。**
   while 循环逐个进入帧并触发事件；同时加 `frames.size()*64+64` 的迭代上限，避免
   异常 speed 下的死循环。
4. **非循环结束不能把 frameIndex 留在 size()。**
   结束后把 `frameIndex` 固定到最后一帧，`currentFrame` 仍可返回它；否则提取会拿到空指针。
5. **方向切换的相位。**
   v1 选择方向变化即重启 clip；这在走/跑切换时会有轻微跳变，但比跨 clip 保留错误相位更可预测。
6. **`fa_anim` 不需要 `/EHsc`。**
   只用 `<vector>/<string>/<memory>/<functional>` 与 render 类型，没有 JSON、`<chrono>`、
   `<unordered_map>`。
7. **显式工程列表。**
   `anim.test.cpp` 和 `fa_anim.vcxproj` 必须手动加入 `fa_tests`/`fa_app`/`.sln`；这是
   M08 去掉通配符后的固定流程。
8. **app 的动画实体无碰撞。**
   只加 `Transform` + `Sprite`，不加入物理宽相；因此不会改变 M13 的碰撞体数量，只改变
   M11 的实体/组件计数与提取项数量。

## 4. 验证命令

```powershell
$env:VCPKG_ROOT='D:\CPPLibraries\vcpkg'
$msbuild='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
foreach ($cfg in @('Debug','Development','Release')) {
  & $msbuild FloriansAdventure.sln /t:Rebuild /p:Configuration=$cfg /p:Platform=x64 /m /nologo /v:w
  & ".\build\bin\x64\$cfg\fa_tests.exe"
  & ".\build\bin\x64\$cfg\fa_app.exe" --frames 60
}
```

## 5. 已知限制 / 后续

- 没有混合树/上下半身/武器叠加；需要多 `Animator` 通道。
- 方向切换会重启 clip；8 向动画未实现。
- 事件是字符串，没有参数；攻击判定窗口需要结构化事件。
- 没有 clip 资源格式；真实动画数据由 M05 loader 或 M26 生成。
- `std::shared_ptr<const AnimationClip>` 的拷贝有原子引用计数成本；不在每帧热路径之外。
- 状态机条件 `std::function` 注册时分配一次，更新时只调用。
