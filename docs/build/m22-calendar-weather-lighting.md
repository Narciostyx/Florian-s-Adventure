# M22 Calendar / Weather / Lighting —— 构建记录 / 实测

对应模块文档：[22-calendar-weather-lighting.md](../modules/22-calendar-weather-lighting.md)

> **状态：已实现并三配置实测通过。** 日历/季节/时刻推进、按季节权重的天气、
> 天气玩法效果、环境光曲线与点光源采样均已落地；app 用快速时钟跑出小时变化，
> 并用 Weather 层绘制暗化层与两个占位点光。

## 1. 实际落地

新增工程 `src/engine/world/fa_world.vcxproj`（GUID `{C0DE0015-…0015}`），
**只引用 `fa_core`**。

| 文件 | 职责 |
|---|---|
| `fa/world/Calendar.hpp` + `src/Calendar.cpp` | `Season`/`Weather`/`GameDate`/`WorldClock`、`WorldCalendar` 推进、事件、天气权重、环境光 |
| `fa/world/Lighting.hpp` + `src/Lighting.cpp` | `AmbientLight`、`PointLight`、`LightSet`（添加/移除/采样） |
| `tests/world.test.cpp` | `[world]` 测试 |

### 关键实现选择

- **纯逻辑**：`WorldCalendar` 不读文件、不碰 GL/ECS；事件只进内部 `vector`，
  由组装根消费后再 `clearEvents()`。这样测试不需要平台或渲染上下文。
- **时间推进**：`advance(realSeconds)` 按
  `gameMinutes = realSeconds / realSecondsPerGameMinute` 换算；`timeOfDay`
  以 0–24 小时计，跨 24 时循环 `advanceDay()`。单次调用最多跨 64 天，防止
  异常大的 delta 卡死。
- **默认节奏**：`kDefaultRealSecondsPerGameMinute = 0.7`；`setRealSecondsPerGameMinute`
  夹在 `[0.001, 60]`，0 或负数不会导致除零。
- **日历**：1 季 28 天、4 季 1 年；跨季时 `SeasonChanged`，回到 Spring 时年份 +1。
- **天气**：每天开始时 `today = tomorrow`，再按新季节权重掷出新的 `tomorrow`。
  权重按季节不同：冬季偏雪、夏季偏晴/风暴、春秋多雨。随机源是 `core::Rng`，
  存档可保存 `state/increment` 以复现读档后的天气。
- **事件顺序**：`advanceDay()` 先发 `WeatherChanged`，再发 `DayChanged`，
  跨季时最后补 `SeasonChanged`；`skipToNextDay()` 另外补一个 `HourChanged`。
- **天气效果**：`autoWatersCrops()` 对 Rainy/Stormy 返回 true；作物生长倍率
  Sunny 1.0、Rainy 1.1、Stormy 0.9、Snowy 0.5。M22 只提供查询，不直接改 M19。
- **环境光**：06:00–08:00 与 18:00–20:00 线性过渡，08:00–18:00 全亮，
  夜间约 0.18；阴雨/风暴/雪按天气乘色。室内固定暖色 0.82，忽略昼夜曲线。
  `intensity` 是颜色亮度，供全屏暗化层使用。
- **点光源**：`LightSet` 句柄从 1 递增；`sample()` 用线性衰减
  `1 - distance / radius` 叠加并夹到 1，离开半径不贡献。v1 没有阴影/加法混合。
- **app 集成**：
  - `initializeCalendar()` 创建 `WorldCalendar(20261018)`，把节奏调成
    `0.01 s/game-minute`，让 60 帧 smoke 能看到小时变化；另加两个占位点光。
  - `onVariableUpdate()` 每帧 `advance(realDelta)`，随后统计 `events().size()` 并
    `clearEvents()`，避免内部事件 vector 随进程寿命增长。
  - `renderFrame()` 在 `RenderLayer::Weather` 上提交整屏黑色暗化 quad
    （alpha = `1 - ambientIntensity`，最多 0.85）和每个点光的半透明 quad；
    UI 层在 Weather 之后，所以 HUD 不会被压暗。
  - 摘要新增 `m22:` 行，smoke OK 行加入 `+M22`。

### 工程文件

`fa_world` 的源文件和公共头已显式列在 `.vcxproj` 中（VS2026 不接受通配符）。
`fa_tests.vcxproj` 增加 `world.test.cpp` 与 `fa_world` 引用；`fa_app.vcxproj`
增加 `fa_world` 引用。

## 2. 测试

`[world]` 覆盖：

- 初始日期/时刻/季节/天气 toString。
- 分钟/小时推进与 `HourChanged` 计数。
- 跨日、跨季、跨年，`DayChanged`/`SeasonChanged`/`WeatherChanged` 与统计。
- `skipToNextDay()` 回到 06:00。
- 雨天/风暴自动浇水与各天气生长倍率。
- 正午/夜晚/风暴环境光强度、室内固定值。
- 暂停、时间流速夹取、天气 RNG 状态保存/恢复。
- `LightSet` 添加/采样/移除/清空与句柄唯一性。

实测：

```text
fa_tests.exe "[world]" → All tests passed (122 assertions in 22 test cases)
fa_tests.exe           → All tests passed (28997 assertions in 476 test cases)
fa_app.exe --frames 60 → m22: year 1 spring day 1 07:39–07:40, sunny -> sunny,
                         ambient 0.91–0.92, auto-water no, growth x1.00,
                         0 day(s)/1 hour(s)/0 season(s)/0 weather change(s), 1 event(s)
                       → M02+…+M20+M22+M27 smoke test OK
```

## 3. 遇到的问题与结论

1. **设计里的 6:00–26:00 与 0–24 小时制冲突。** 实现采用 `0..24` 的
   `timeOfDay`，`hour()` 取模；06:00 仍是默认起点。26:00 的“强制睡眠”留给
   M25 通过 `skipToNextDay()` 表达，不在日历内部硬编码。
2. **跨日事件顺序必须固定。** 先 `WeatherChanged` 再 `DayChanged`，否则订阅
   `DayChanged` 的作物系统会读到旧的天气。跨季再补 `SeasonChanged`。
3. **天气随机性要能存档。** 只保存 `today/tomorrow` 不够，读档后下一次掷点会
   变化；因此额外暴露 `weatherState()/weatherIncrement()/restoreWeather()`。
4. **暗化层不能用“补色 + alpha”近似乘法。** 补色在低亮度时会把画面提亮；
   实际采用黑色 alpha 叠层，强度从 `ambientIntensity()` 推导，天气色只影响
   强度与 `ambientColor()`。
5. **点光必须排在暗化层之后。** 两者都在 `RenderLayer::Weather`，通过
   `sortY = -100000` 保证整屏暗化先画，点光再叠加。
6. **隐藏窗口仍要能跑。** 日历逻辑不依赖渲染；没有 GL 上下文时只跳过光照绘制，
   摘要和测试照常。

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

- 没有 26:00 强制睡眠/晕倒惩罚；需要 M25 决定规则。
- 天气只按季节权重，没有连续天气、天气预报准确率或区域差异。
- 环境光是近似暗化，不是真正的乘法光照；没有法线/阴影/彩色光通道。
- 点光没有遮挡与加法混合，只是半透明 quad。
- 天气效果只给查询，没有粒子/音效/室内判定；这些由 app、M08、M12 的室内外标记接。
- 季节长度、一天时长、睡眠惩罚仍是产品参数，需要后续平衡。
