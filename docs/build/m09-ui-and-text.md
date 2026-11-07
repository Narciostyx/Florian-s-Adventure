# M09 UI & Text —— 构建记录

对应模块文档：[09-ui-and-text.md](../modules/09-ui-and-text.md)

## 1. 实测环境

| 项 | 值 |
|---|---|
| MSBuild | `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe` |
| 平台工具集 | v145 / MSVC `14.51.36231` |
| vcpkg | `D:\CPPLibraries\vcpkg`，triplet `x64-windows-static-md` |
| 配置 | Debug / Development / Release × x64 |
| 新增工程 | `src/engine/ui/fa_ui.vcxproj`（GUID `{C0DE000E-…}`） |
| 新增测试 | `tests/ui.test.cpp` |

`fa_ui` **不需要** `/EHsc`：它不 include nlohmann/json、`<chrono>` 或 `<unordered_map>`。
它只引用 `fa_core` 与 `fa_render`，不引用 `fa_assets`、`fa_input`、`fa_scene`、`fa_events`。

## 2. 实测结果

| 检查 | Debug | Development | Release |
|---|---|---|---|
| 全解决方案 `Rebuild` | exit 0，0 warning / 0 error | exit 0，0 warning / 0 error | exit 0，0 warning / 0 error |
| `fa_tests.exe` | 406 用例 / 18532 断言，全绿 | 同左 | 同左 |
| `fa_app.exe --frames 60` | exit 0 | exit 0 | exit 0 |

用例拆分（三种配置一致）：

| 过滤 | 用例 | 断言 |
|---|---|---|
| `[ui]` | 16 | 111 |
| `~[ui]` | 390 | 18421 |
| 全量 | 406 | 18532 |

`16 + 390 = 406`、`111 + 18421 = 18532`：`ui.test.cpp` 的每个用例都带 `[ui]`，没有漏标签。
M09 之前是 390 用例 / 18421 断言，所以新增量正好是 16 / 111。

## 3. 冒烟输出与对账

Debug（Development / Release 的 m09 行完全一致）：

```text
  m09: 95 glyph(s) in a 380x5 atlas, 66 UI item(s), 0 press(es), HUD on, slider 2.00
  m06: 4.1.0 Core Profile Context ... on AMD Radeon (TM) Graphics ...
       107 draw call(s), 998 quad(s), 3992 vertex/vertices, 106 texture switch(es), 0 dropped, 7 live texture(s)
  m05: 5 catalog entr(ies), 4 texture(s) cached, 4 placeholder(s), 3328 CPU byte(s), 4 live GPU texture(s), 0 failed
  m11: 1 scene depth, 4 system(s), 4 live entit(ies) (4 created / 0 destroyed) over 10 component type(s), 17 component(s), 923 extracted item(s)
  m12: 40x23 map, 16 px tiles, 920 cell(s), 1 layer(s), 3 def(s), 920 non-empty, 143 solid, 16 tillable, 0 area(s), 0 portal(s)
M02+M03+M04+M05+M06+M07+M09+M11+M12+M13+M15+M16+M17 smoke test OK
```

| 观测 | 来源 |
|---|---|
| 95 字形 | ASCII 32–126：`kLastCodepoint - kFirstCodepoint + 1` |
| 380×5 图集 | 95 格 × 4 px 前进量；高度 5 |
| 66 UI 项 | `UiContext::itemCount()` 每帧重建 |
| 0 次按下 | 冒烟是隐藏窗口，鼠标始终在 (0,0)，控件不会被点中 |
| HUD on | 复选框初值 `true`，无鼠标事件时保持不变 |
| slider 2.00 | 滑条初值；鼠标位于 (0,0)，不触发拖动 |
| 923 提取项 | 与 M17 相同：UI 不进 `stack.renderExtract()`，只追加到同一个队列 |
| 998 quad | 923 世界项 + 66 UI 项 + 9 个调试图元（光标框/线/交互目标框） |
| 7 live texture | 渲染器内部 1×1 白纹理 + 4 个 catalog 占位 + 字体图集 + UI 白纹理 |

### 66 个 UI 项的拆分

| 控件 | 项数 |
|---|---|
| HUD 面板 | 1 |
| HUD 边框 | 4 |
| 标题 `FLORIAN'S ADVENTURE` | 18（19 字符，空格只前进不绘制） |
| `STAMINA` | 7 |
| 耐力条 | 2（轨道 + 填充） |
| `MARKER 344 275` | 12（14 字符，2 个空格） |
| `PAUSE` 按钮 | 10（1 底 + 4 边框 + 5 字形） |
| HUD 复选框 | 9（1 方框 + 4 边框 + 1 选中块 + 3 字形） |
| 滑条 | 3（轨道 + 填充 + 滑块） |
| 合计 | **66** |

### 纹理切换为什么变多

UI 项按 `sortY` 排序，同一行里白色四边形与字体图集交替出现，因此 `texture switch(es)` 从
纯世界的水平上升到 106。这是**排序语义的代价，不是批处理退化**：世界项仍是 923 个、UI 项
仍是 66 个。后续若切换成为热点，可以按控件分组排序、把所有纯色四边形聚到一组，或把字体
图集与白色像素合并到同一张纹理。

## 4. 语义决策表

| # | 决策 | 选择 | 理由 |
|---|---|---|---|
| 1 | 命名空间 | `fa::ui` | 与模块名一致 |
| 2 | 字体来源 | 内置 3×5 ASCII 位图 | 无美术文件也能显示文字；测试不需要 GL |
| 3 | 纹理所有权 | 调用方上传，`FontAtlas` 只存 `TextureHandle` | 与 M11/M12 的裸句柄约定一致，`fa_ui` 不认识 M05 |
| 4 | 绘制输出 | `RenderQueue` 纯数据 | 不需要 `IRenderer&`，测试无 GL |
| 5 | 图层 | 每字形/四边形 `RenderLayer::UI` | 渲染器排序后自然最后绘制 |
| 6 | 文字编码 | v1 逐字节 ASCII | 3×5 图集只有 ASCII；CJK 需要解码 + 动态图集 |
| 7 | 缩放 | 向下取整，最小 1 | 像素字体只允许整数缩放 |
| 8 | 对齐 | 用 advance 宽度 | 与排版光标一致；末字形右侧留 1 px 间距 |
| 9 | 未知字形 | 回退到 `?` | 比空白更容易发现内容问题 |
| 10 | `UiId` | FNV-1a 32 位，0 映射到 1 | 0 保留为“无活动控件” |
| 11 | 输入 | 每帧 `UiInputState` 快照 | UI 不依赖 M07；键盘/手柄字段已预留 |
| 12 | 点击语义 | 按下+在控件内释放才算 pressed | 与桌面 UI 一致；释放到外面会取消 |
| 13 | 滑条 | 活动 id 跨帧保持，按下/拖动/释放帧都应用位置并夹取 | 鼠标移出矩形后仍能拖到端点，释放位置不会丢 |
| 14 | 排序 | `sortY` = 绘制 y | 与 M06 既有排序键一致；重叠 UI 需显式顺序 |
| 15 | `/EHsc` | 不开启 | 无 JSON/chrono/unordered_map |

## 5. 踩坑

1. **`drawText` 在字体纹理无效时静默返回。** 第一版测试用 `buildBuiltinFont()` 直接绘制，
   期望有字形项却得到 0；必须先设置 `font.texture = TextureHandle{1}`。这是刻意的契约：
   GPU 句柄无效时不应提交垃圾绘制项。

2. **`glyphFor` 的 `defined` 描述“这个字形是否存在”，不是“输入的码点是否存在”。**
   未知码点会返回 `?` 的字形对象，所以 `unknown.defined == true`。最初测试断言 false，
   失败后改为比较 `source.x` 与 `?` 一致；真正未定义只发生在 `glyphs` 不完整的空图集上。

3. **文字按 UTF-8 字节遍历，不是按码点。** 用 `"\u4e2d"` 测试“未知字形”时得到 3 个 `?`，
   因为字符串是 3 个字节。v1 只支持 ASCII；测试改用单字节 `"\x7f"`，模块文档把 CJK
   列为后续工作。

4. **对齐用的是 advance，不是字形可视宽度。** `"A"` 在 scale 2 下 advance 为 8、可视宽
   为 6，居中位置因此是 `10 + (20 - 8) / 2 = 16`，不是 18。测试最初按可视宽度算错。

5. **渲染器内部还有一张 1×1 白纹理。** 冒烟显示 7 张 live texture，而 catalog 只加载了
   4 张、M09 又加了 2 张；第 7 张是 `GlRenderer::initialize()` 自建的白纹理，不是泄漏。

6. **UI 必须在 `renderQueue.sort()` / `submitAll()` 之前提交。** 最初沿用了 M17 的顺序，
   先把世界队列排好序交给渲染器，再画 UI，结果 UI 永远进不了这一帧。正确顺序是：
   `stack.renderExtract` → 计算光标 → `buildHud` → `sort` → `submitAll` → 调试图元。

7. **`UiContext` 持有 `const FontAtlas&`。** 字体必须先于 context 声明/销毁；app 里
   `FontAtlas uiFont` 声明在 `std::unique_ptr<UiContext> ui` 之前。字体对象被赋值
   （不是重新绑定）时引用仍然有效。

8. **边框厚度大于高度的一半时中间竖边高度为负。** `border()` 先算 `middleHeight`，只有
   大于 0 才画左右边；否则会提交负尺寸的 `DrawItem`（`quad()` 也会再挡一次）。

9. **窗口失焦可能收不到 mouse-up。** `beginFrame()` 在“既不按下也没有释放事件”时清空
   活动控件 id，避免按钮永远卡在 pressed 状态。

10. **不需要 `/EHsc`。** 不要因为它是“UI 模块”就顺手打开异常；判据仍是 include 图。

11. **工程 GUID 与 sln 配置块要一起加。** 只加 `.vcxproj` 和 `ProjectReference` 而漏掉
    `ProjectConfigurationPlatforms`，MSBuild 会跳过新工程或报配置不匹配。

## 6. 复现命令

```powershell
$env:VCPKG_ROOT = 'D:\CPPLibraries\vcpkg'
$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'

foreach ($cfg in @('Debug','Development','Release')) {
    & $msbuild FloriansAdventure.sln /t:Rebuild /p:Configuration=$cfg /p:Platform=x64 /m /nologo /v:m
    & ".\build\bin\x64\$cfg\fa_tests.exe"
    & ".\build\bin\x64\$cfg\fa_app.exe" --frames 60
}

# 只跑 UI
& ".\build\bin\x64\Debug\fa_tests.exe" "[ui]"
```

单编新工程：

```powershell
& $msbuild 'src\engine\ui\fa_ui.vcxproj' /t:Rebuild `
    /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir="$($PWD.Path)\" /m /nologo /v:m
```

## 7. 遗留风险

1. 文字只支持 ASCII；多字节 UTF-8 会拆成多个 `?`。
2. 内置 3×5 字体可读性有限，真实字体需要文件解析或动态图集。
3. 鼠标-only；键盘/手柄焦点链、Tab 顺序与文本输入未实现。
4. 没有布局组、裁剪与滚动区；复杂面板要手工算矩形。
5. UI 按 `sortY` 排序，重叠窗口的顺序由屏幕 y 决定，而不是提交顺序。
6. UI 激活不会自动切换输入上下文，玩法输入屏蔽要调用方自己做。
7. 主题是代码值，没有 JSON/皮肤加载。
8. UI 每帧重建全部绘制项；文字排版没有缓存，长文本/大量标签需要缓存。
9. 纹理切换随 UI 行数增长；需要时按控件分组排序或合并纹理。
10. UI 原点与鼠标映射使用的是未 snapped 的相机；冒烟的相机位置是整数，暂时看不出差异。
    相机改为像素对齐平滑跟随后，UI 应统一使用渲染器实际采用的 `camera.snapped()`。
