# ADR-0004：Core 值类型形态与错误传递方式

- 状态：Accepted
- 日期：2026-10-17
- 决策者：项目所有人（代行决策：架构负责人）
- 相关模块：M01、M02、M05、M14、M15
- 取代：无

## 背景

M01 的实现阶段需要定下三件会被所有模块继承的事：

1. `Rect` 用 `glm::vec4` 还是独立结构体。
2. 错误如何传递（异常 / `std::expected` / 自研 `Result<T>`）。
3. `Result<T>` 存值用什么容器，能否安全承载 move-only 类型。

这三项一旦进入公共头文件，后续 27 个模块都会复制它的写法，改起来等于全局重构，因此必须在写出第一批头文件前固定。

## 决策

### 1. `Rect` 使用独立结构体，而非 `glm::vec4`

```cpp
struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};
```

- 提供 `makeRect` / `rectFromCenter` / `left` / `right` / `top` / `bottom` / `center` / `isEmpty` 自由函数。
- 几何语义固定为**半开区间** `[x, x+w) × [y, y+h)`：刚好接触的两条边不算重叠。
- `Vec2` 与 `Color` 继续使用 `glm` 类型（`glm::vec2` / `glm::vec4`）；只有 `Rect` 例外。

理由：`glm::vec4` 的分量名是 `x/y/z/w`，把 `w` 当宽度读、把 `z` 当高度读是长期的可读性负担，且 `Color` 也在用 `glm::vec4`，两者混用极易出现“把颜色当矩形传”的静默错误。独立结构体的代价只是与 glm 之间需要显式构造，这在少数需要传 GPU 的位置是显性的好事。

### 2. 错误用自研 `Result<T>`，不用异常

- 引擎全局关闭异常（`/EHs-c-`、`/GR-`），因此任何依赖异常的设计都不可用。
- C++20 没有 `std::expected`（C++23），不能直接用标准库。
- `Result<T>` 内部用 `std::optional<T>` 存值 + 独立 `Error` 成员，`[[nodiscard]]`。
- `value()` / `error()` 在状态不匹配时触发 `FA_ASSERT`，而不是抛异常或返回默认值。
- 提供 `take()`（`&&` 限定）以支持 move-only 载荷。
- 提供 `Status = Result<Unit>` 与 `okStatus()` / `errorStatus(code, message)` 作为无载荷的错误通道。

### 3. 错误码按模块分段，`Error` 自带消息

`error_code` 命名空间从 1000 起，每段 1000 个号：

| 段 | 归属 |
|---|---|
| 1000–1999 | Core（已用 1000–1008） |
| 2000–2999 | Platform |
| 3000–3999 | Asset |
| 4000–4999 | Save |

分段只约束起始值，具体常量由各模块在自己的头文件里定义。

### 4. 附带固定下来的小类型

- `StrongId<Tag, T = std::uint32_t>`：模板化强类型 ID，`isValid()` 以 0 为无效值，提供 `std::hash` 特化。
- `Seconds` / `Milliseconds` 两个显式单位结构体 + `Countdown`：禁止裸 `float` 表示时间在跨模块传递中丢单位。
- `Hash`：`constexpr` FNV-1a 64、`hashCombine`、`hashBytes`。
- `Rng`：PCG32，`state()` / `increment()` / `restore()` 保证存档可复现。

## 考虑过的其他方案

| 方案 | 优点 | 缺点 | 结论 |
|---|---|---|---|
| `Rect = glm::vec4` | 零转换、可直传 GPU | `z/w` 与 `h/w` 语义冲突，易与 `Color` 混用 | 否决 |
| `Rect` 用 `std::array<float,4>` | 无额外类型 | 丢失字段名，可读性更差 | 否决 |
| 启用异常 + `throw` | 代码短 | 引擎热路径与 SDL/GL 回调边界不希望有栈展开；`/EHsc` 会拖慢并放大二进制 | 否决 |
| `std::expected` | 标准库语义 | C++20 无此设施 | 否决（C++23 后可评估迁移） |
| `Result<T>` 用 `union` 存值 | 省一次构造 | move-only 与平凡性约束复杂，易 UB | 否决 |
| `Result<T>` 用 `std::optional<T>` + 独立 `Error` | 实现简单、move-only 安全、无 UB | 多一个 `std::string` 成员（错误态才有内容） | **采纳** |

## 后果

正面

- `Rect` 的字段名与几何语义在所有模块中一致，跨模块传参不会误读。
- 关闭异常后仍有一条统一的、可断言、可测试的错误通道。
- move-only 载荷（如文件句柄、上传缓冲）可以安全装进 `Result`。
- 时间单位在类型层面区分，避免“毫秒当秒”的经典 bug。

负面

- 与 glm 交互处需要显式构造 `Vec2{r.x, r.y}` 之类，比直接用 `vec4` 略啰嗦。
- `Result<T>` 的错误路径不带栈信息（无异常），定位依赖日志与断言消息。
- `value()` 依赖 `FA_ASSERT`，在 Release 下若断言被裁剪需靠调用方自觉检查 `isOk()`。因此所有 `Result` 返回函数标注 `[[nodiscard]]`。

需持续关注

- 若未来升级到 C++23 并希望用 `std::expected`，只需保持 `isOk()/value()/error()` 三个名字不变即可平滑替换。
- `Rect` 若日后需要直传 GPU，加一个 `toVec4()` 显式转换，而不是引入隐式转换。

## 验证方式

- `tests/core.test.cpp` 已覆盖：`Result` 的 ok/err/take/`valueOr`、`Rng` 确定性与存档往返、`Rect` 半开区间边界（接触不算重叠）、FNV-1a 参考向量、`StrongId` 比较与哈希、`Countdown` 计时、日志级别过滤与 sink 移除。
- 当前基线：22 个测试用例 / 14626 条断言全部通过。
- 新增 Core 类型必须同时补测试，不允许降级该基线。

## 影响的文档

- `docs/reference/code-reference.md`（已同步为实际 API）
- `docs/architecture/conventions.md`（错误处理与命名约定引用本 ADR）
- 任何定义错误码的模块文档（3000+ / 4000+ 段）
