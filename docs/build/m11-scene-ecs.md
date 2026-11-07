# M11 场景与 ECS：实现说明、实测结果与遗留风险

本文件记录 M11（Scene / World & ECS）的落地细节：真实接口形态、本机实测结果，
以及实现过程中踩到并修掉的坑。凡是「为什么这样写」的背景知识都集中在这里，
源码里只保留 Doxygen 注释。

模块设计与契约见 [M11 Scene / World & ECS](../modules/11-scene-world-ecs.md)。

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

三套配置（Debug / Development / Release × x64）全部 `Rebuild` 成功，**零 warning、零 error**。

```text
fa_tests.exe → All tests passed (17378 assertions in 293 test cases)   exit = 0
fa_tests.exe "[scene]"
             → All tests passed (286 assertions in 78 test cases)      exit = 0
fa_tests.exe "~[scene]"
             → All tests passed (17092 assertions in 215 test cases)   exit = 0
fa_app.exe --frames 60
             → m11: 1 scene depth, 2 system(s), 921 live entit(ies)
                    (921 created / 0 destroyed) over 2 component type(s),
                    1842 component(s), 921 extracted item(s)
               stack: 1 push(es), 0 pop(s), 0 replace(s), 0 pause(s), 0 resume(s)
               920 ground tile(s), marker at (362.001, 224)
               M02+M03+M04+M05+M06+M07+M11 smoke test OK               exit = 0
```

> 后续的独立地图补丁让 app 在 `--play` 真正使用 `replace()`：农场/林地门口换图后
> 栈深仍为 1，`--play-test` 结束时的统计是
> `stack: 1 push(es), 0 pop(s), 5 replace(s), 0 pause(s), 0 resume(s)`；
> 跨场景可存活的状态放在组装根的 `GameState`，不放在被替换的场景里。

`[scene]` 与 `~[scene]` 的用例数和断言数相加正好等于全量（78 + 215 = 293，286 + 17092 = 17378），
说明新增用例没有漏打标签。

### 921 实体、1842 组件、926 quad 是怎么对上的

- `DemoScene::onEnter()` 建 40 × 23 = **920** 块地面，全部共享同一张纹理句柄，各自一块 `Transform` + 一块 `Sprite`。
- 再加 **1** 个标记实体，同样是 `Transform` + `Sprite`。合计 921 实体、921 × 2 = **1842** 组件。
- 提取出 **921** 个 `DrawItem`；`m06:` 那行的 926 个 quad = 921 个精灵 + 5 个调试图元（相机边界矩形与标记十字各若干）。
- 组件类型数是 **2**（`Transform`、`Sprite`），不是 1842——`registeredComponentTypes()` 数的是类型不是实例。

### marker 为什么从 (320,200) 走到 (362.001,224)

无头运行没有任何键盘输入，`input.moveAxis()` 恒为 `{0, 0}`，标记会一动不动，
那样「系统真的在跑」这件事在 CI 里就没有被验证。所以 app 在无输入时喂一个**脚本化漂移** `{0.35, 0.2}`：

```text
60 帧 × 0.0166667 s × 120 px/s × 0.35 = 42.000 px   →  320 + 42.000 = 362.000
60 帧 × 0.0166667 s × 120 px/s × 0.20 = 24.000 px   →  200 + 24.000 = 224.000
```

实测是 `(362.001, 224)`。那 `0.001` 的尾巴来自 `FrameContext` 里 `deltaSeconds` 的浮点累积，
不是逻辑错误——所以冒烟断言用容差比较，不做逐位相等。

### 统计必须在 `shutdownRenderer()` 之前快照

`main()` 里 `m11:` 那几行打印的数值来自 `stackStats()` / `worldStats()` / `markerPosition()`。
如果在 `shutdownRenderer()` 之后取，`stack.clear()` 已经把场景销毁，世界统计全部归零、
`demoScene` 也成了空指针。所以顺序固定为：

```text
renderFrame × 60
  → 快照 stats / marker / systemCount
  → shutdownRenderer()   （内部先 stack.clear()）
  → 打印
```

## 交付内容

### 公共头（`src/engine/include/fa/scene/`）

| 文件 | 内容 |
|---|---|
| `SceneError.hpp` | 错误码 5000–5007 |
| `EntityId.hpp` | `EntityId`、`kPendingGeneration`、`isValid`/`isPending`、`std::hash` 特化、`toString` |
| `ComponentType.hpp` | `componentTypeIdOf<T>()`、`componentSetIdOf<Ts...>()`、`kInvalidComponentType` |
| `SparseSet.hpp` | 模板 `SparseSet<T>` |
| `World.hpp` | `WorldStats`、`detail::StorageBase`/`Storage<T>`、`World` |
| `Components.hpp` | `Transform`、`Sprite` |
| `CommandBuffer.hpp` | `CommandBufferStats`、`CommandKind`、四个命令、`CommandBuffer` |
| `System.hpp` | `ISystem`、`SystemPipelineStats`、`SystemPipeline` |
| `Scene.hpp` | `IScene`、`BasicScene` |
| `SceneStack.hpp` | `SceneStackStats`、`SceneStack` |
| `SpriteExtractor.hpp` | `SpriteExtractOptions`/`SpriteExtractStats`、`extractSprites`、`SpriteExtractSystem` |

### 私有实现（`src/engine/scene/src/`）

`EntityId.cpp`、`World.cpp`、`CommandBuffer.cpp`、`System.cpp`、`Scene.cpp`、`SceneStack.cpp`、`SpriteExtractor.cpp`。

### 测试

`tests/scene.test.cpp`：78 用例 / 286 断言，标签 `[scene][entity]`、`[sparse]`、`[component]`、
`[world]`、`[commands]`、`[pipeline]`、`[stack]`、`[extract]`、`[integrate]`。

### 辅助改动

| 文件 | 改动 |
|---|---|
| `src/engine/scene/fa_scene.vcxproj` | 新建静态库工程，GUID `{C0DE0008-…}` |
| `FloriansAdventure.sln` | 工程节点 + 6 行配置映射 |
| `src/app/fa_app.vcxproj`、`tests/fa_tests.vcxproj` | 各加一行 `ProjectReference` |
| `src/app/main.cpp` | 新增 `MoveMarkerSystem`、`DemoScene`、`SceneStack` 成员；`renderFrame()` 改为走提取管线 |

## 语义决策

设计草案里有几处与最终实现不同，这里逐个记录**为什么**改了。

| 议题 | 草案 | 实现 | 理由 |
|---|---|---|---|
| 命名空间 | `fa::ecs` | **`fa::scene`** | 模块名从 M01 起就是 `fa::core`/`fa::render`/`fa::assets` 这种「一层一个名字」，`fa::ecs` 会和 `fa::scene` 里的 `Scene`/`SceneStack` 混在一起，读代码时要记两套前缀 |
| 提取接口 | `onRenderExtract(const World&, render::IRenderer&)` | **`onRenderExtract(const World&, render::RenderQueue&)`** | 提取是**纯数据**转换。给 `IRenderer&` 会让所有提取相关用例都需要 GL 上下文；给 `RenderQueue&` 之后，`[extract]` 的 20 多个用例完全是 CPU 的 |
| 依赖 `fa_assets` | 是（草案 M00–M10） | **否** | `Sprite` 只需要一个 `render::TextureHandle`（POD）。绑定 `fa_assets` 会把资源系统的 `/EHsc`、nlohmann/json、异步加载全套拖进每个用 ECS 的模块。纹理保活交给加载方——asset manager 的生命周期长于任何场景 |
| 场景栈 | 未明确 | **只有栈顶更新，全部提取** | 暂停菜单要压在**冻结**的世界上。这是唯一能同时满足「菜单不冻结自己」和「底下的世界继续可见」的语义 |
| `replace` 的生命周期 | 未明确 | `onExit` + 新场景 `onEnter`，**不** `onResume` | `replace` 是换关卡，不是关菜单。新场景是全新的世界，没有「之前的自己」可恢复；补一个 `onResume` 只会让派生类去猜该在哪个钩子里初始化 |
| `pop()` 的所有权 | 返回 `unique_ptr` | 同 | 调用方在 `onExit()` 之后还能观察那个场景（测试需要）。`replace`/`clear`/析构则立即销毁 |
| 占位符句柄 | 未明确 | `{index++, kPendingGeneration}` | 复用 `EntityId` 而不是引入第二种句柄类型。`isValid()` 为 false 保证「误用会安静失败而不是撞到别的实体」 |
| `StorageBase` 的虚函数 | conventions §9 说「无虚函数」 | 保留虚函数 | §9 约束的是**组件**必须是纯数据。存储的类型擦除没法用模板做（`unordered_map<ComponentTypeId, 基类>` 是唯一能让 `remove`/`clear` 遍历所有类型的形式）。组件本身仍是纯数据 |
| 组件类型键 | 未明确 | `std::atomic<uint32_t>` + 函数局部 static | 沿用 M03 的 `/GR-`（关闭 RTTI）模式，不能用 `typeid` |
| `entityId` 填什么 | 未明确 | 实体索引（可被 `Sprite::entityOverride` 覆盖） | 让调试器能把一次 draw call 反查回实体，而不是得到「本次提取的第 N 项」这种没有信息量的数 |

## 踩到的坑

### 1. `FA_ASSERT` 是两参数宏（C4003 + C2660 连锁）

写成 `FA_ASSERT(condition && "message")` 会先报 C4003（宏实参不足），
然后带着残缺的实参展开出一串 C2660。正确写法是两个参数：

```cpp
FA_ASSERT(world.isAlive(id), "World::add requires a live entity");
```

这是本项目最容易犯的错——Catch2 的 `REQUIRE` 习惯让人以为断言只有一个参数。

### 2. `[[nodiscard]]` 虚函数裸调用（C4834 → C2220）

`ISystem::name()` 和 `CommandBuffer::apply()` 都标了 `[[nodiscard]]`。
在系统里只想跑一遍不关心返回值时，裸调用 `commands.apply(world);` 会报 C4834，
而 C4834 在 `/WX` 下连带 C2220（「警告被视为错误」）让编译整个失败。

```cpp
static_cast<void>(commands.apply(world));
```

### 3. `DrawItem::rotation` 的单位是弧度

`Transform::rotation` 与 `DrawItem::rotation` 都写「弧度」，但 `Sprite` 的 `scale`、`size` 是像素。
混起来写时很容易在某处 `* 180.0f / 3.14159f` 多除一次。提取时**原样搬运**，不做任何角度换算。

### 4. `RenderQueue` 的真实形态

草案里猜的几个名字都对不上，实测确认：

| 成员 | 真实签名 |
|---|---|
| `submit` | `void submit(const DrawItem&)` |
| `items` | `std::span<const DrawItem> items() const` |
| `sort` | `void sort()`（**稳定**排序） |
| `reserve` | `void reserve(std::size_t count)` |
| `clear` / `size` | 存在 |
| `IRenderer::submitAll` | `void submitAll(std::span<const DrawItem>)`（确实存在） |

`sort()` 是稳定排序：同键条目保持提交顺序。所以「同一图层同一深度键的多个精灵」的绘制顺序
取决于**谁先被提取**，而提取顺序又取决于 `SparseSet` 的密集数组顺序——这就是为什么
`SpriteExtractor` 那条「提取者不排序」的契约很重要。

### 5. `DrawItem::source` 的空矩形表示整张纹理

`source = {0,0,0,0}` 不是「画一个 0×0 的矩形」，而是「用整张纹理」。
`Sprite` 的默认值沿用了这个约定。如果真想要 0 尺寸（比如缩放到不可见），
必须靠 `scale` 表达，不能靠空的 `source`。

`depthKey(sortY)` 会把 `sortY` 四舍五入到整像素——所以 `sortYBias` 给 `0.4` 和 `0.6` 可能得到同一个深度键。

### 6. `StrongId` 有默认构造，`EntityId` 得自己写 `std::hash`

`core::StrongId<Tag>` 已经有 `std::hash` 偏特化，但 `EntityId` 是独立结构体（因为它是
`index` + `generation` 两个字段，不是单个 id 值），所以必须自己写**全特化**：

```cpp
template <>
struct hash<fa::scene::EntityId> { std::size_t operator()(...) const noexcept; };
```

写偏特化（`hash<EntityId>` 少写命名空间）会得到一个「看起来编译通过、实际用不到」的定义。

### 7. 新建 `.vcxproj` 必须手动转 LF 且去 BOM

`create` 工具写出来的工程文件是 CRLF + 无 BOM 混合状态，`git diff` 会显示整份文件被改写。
新建工程后固定做两步：

```powershell
# LF 化
(Get-Content $p -Raw) -replace "`r`n", "`n" | Set-Content $p -NoNewline -Encoding utf8
# 去 BOM
[IO.File]::WriteAllText($p, [IO.File]::ReadAllText($p), (New-Object System.Text.UTF8Encoding $false))
```

仓库里已有的 `.vcxproj`/`.sln` 全是 LF，新文件必须跟上，否则以后每次 rebase 都有冲突。

### 8. `World::each` 需要 const 与非 const 两个重载

最初的实现只有非 const 版，结果 `SpriteExtractor` 完全编不过：

```cpp
SpriteExtractStats extractSprites(const World& world, render::RenderQueue& queue, ...);
//                                        ^^^^^ 拿的是 const World&
```

const 版必须用 `const detail::Storage<T>*` 去查，返回 `const SparseSet<T>*`，回调拿到 `const Ts&...`。
两版的模板体几乎一样但不能共享——`std::apply` 里的指针类型从 `Ts*` 变成 `const Ts*`。

### 9. 占位符与真实句柄的区别只在 `generation`

`CommandBuffer::create()` 返回 `{index = m_nextPlaceholder++, generation = kPendingGeneration}`。
这个 `index` 会从 0 开始数，而 `EntityId` 里 `index == 0` 是**恒不存在的槽位**——
所以第一个占位符的 `index` 是 0，恰好落在哨兵上，`isValid()` 天然为 false。
这不是巧合而是有意的：即使调用方搞错了 `generation`，`index == 0` 也能兜住。

`apply()` 之后 `m_resolved` 保留到下一次 `clear()`，所以 `resolve()` 在 `apply()` 返回后仍然有效。

### 10. `World::create` 的代际必须防溢出

槽位复用时 `generation += 1`。跑满 2^32 次之后会绕回 0 或 `kPendingGeneration`，
那两个值都被保留给「无效」和「占位」，绕过去会让旧句柄「复活」：

```cpp
Slot& slot = m_entities[index];
slot.generation += 1;
if (slot.generation == 0 || slot.generation == EntityId::kPendingGeneration) {
    slot.generation = 1;
}
```

### 11. 测试不能在被 `SceneStack` 析构之后读场景对象

这是本轮唯一一类**逻辑正确但测试写错**的失败。最初的写法是：

```cpp
CountingScene* raw = new CountingScene("a");
stack.push(std::unique_ptr<IScene>(raw));
stack.clear();
REQUIRE(raw->enterCount == 1);   // ← 读已释放内存，实测拿到 -572662307
```

`clear()` 在 `onExit()` 返回后立即销毁 `unique_ptr` 里的场景，`raw` 就是悬空指针。
第一次跑测试时这类断言有 3 个失败，读到的 `-572662307`（`0xDDDDDDDD` 的 MSVC 调试填充）。

修法是**观察存活的外部对象**：给 `CountingScene` 加一个可选的 `LifecycleCounter*`，
断言改读那个计数器：

```cpp
LifecycleCounter counter;
stack.push(std::make_unique<CountingScene>("a", &counter));
stack.clear();
REQUIRE(counter.exits == 1);      // 计数器在栈外，仍然存活
```

这条对以后的场景测试是通用教训：**`onExit()` 里能做的事只有「对外部对象产生副作用」**，
测试也必须按这个假设写。

### 12. `SceneStackStats` / `WorldStats` 的字段名

初版按直觉写了 `registeredComponentTypes`、`destroyedEntities`，全部 C2039。真实字段是：

| 结构 | 字段 |
|---|---|
| `SceneStackStats` | `depth`、`pushes`、`pops`、`replacements`、`pauses`、`resumes` |
| `WorldStats` | `liveEntities`、`entitySlots`、`recycledSlots`、`componentTypes`、`components`、`createdTotal`、`destroyedTotal` |
| `SystemPipelineStats` | `systems`、`fixedRuns`、`variableRuns`、`extractRuns`、`commandsApplied`、`commandsSkipped` |
| `CommandBufferStats` | `applied`、`skipped`、`created`、`destroyed`、`componentsAdded`、`componentsRemoved` |
| `SpriteExtractStats` | `candidates`、`submitted`、`skippedNoTexture` |

注意 `World` 的**方法**叫 `registeredComponentTypes()`，而 `WorldStats` 的**字段**叫 `componentTypes`——
两者名字不一样，很容易照抄错。

### 13. 统计快照必须在 `shutdownRenderer()` 之前

见上文「统计必须在 shutdownRenderer 之前快照」。表现是打印出来全是 0 且 `marker at (0,0)`，
看起来像「世界根本没建成」，实际是快照取晚了。

### 14. 无头冒烟没有任何键盘输入

`fa_app --frames 60` 在 CI 里跑，没有窗口焦点、没有按键，`input.moveAxis()` 恒 `{0,0}`。
如果标记只跟着输入走，那么「固定步 → 系统 → 提取」这条链路在 CI 里**一次都不会被执行到非平凡路径**。

所以 `SmokeSink::onFixedUpdate` 在无输入时喂脚本化漂移：

```cpp
const fa::core::Vec2 scripted = (axis.x == 0.0f && axis.y == 0.0f)
                                    ? fa::core::Vec2{0.35f, 0.2f}
                                    : axis;
demoScene->setMoveAxis(scripted);
```

这让冒烟测试真的在验证管线，而不是验证「零位移仍然是零位移」。

### 15. 用例里的无符号比较要用无符号字面量（C4389）

`REQUIRE(world.liveEntities() == 2)` 里 `2` 是 `int`，`liveEntities()` 返回 `std::size_t`，
在 `/W4` 下报 C4389。全部改成 `2u`：

```cpp
REQUIRE(world.liveEntities() == 2u);
```

### 16. 集成用例的实体索引假设会随实现变化

`[integrate]` 里最初断言「marker 的 `index` 是 2」（地面从 1 开始数），
但 `World` 的空闲表复用与哨兵槽位让这个数字不是稳定契约。
改成断言**语义**而不是**实现细节**：

```cpp
REQUIRE(scene.world().liveEntities() == 2u);   // 一个地面 + 一个 marker
REQUIRE(queue.size() == 1u);                   // 只提取出一个精灵
```

## 复现命令

```powershell
$env:VCPKG_ROOT = 'D:\CPPLibraries\vcpkg'
$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'

# 全解决方案（三配置都要跑）
& $msbuild FloriansAdventure.sln /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:m
& $msbuild FloriansAdventure.sln /t:Rebuild /p:Configuration=Development /p:Platform=x64 /m /nologo /v:m
& $msbuild FloriansAdventure.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m /nologo /v:m

# 全量 + M11 子集
& ".\build\bin\x64\Debug\fa_tests.exe"
& ".\build\bin\x64\Debug\fa_tests.exe" "[scene]"
& ".\build\bin\x64\Debug\fa_tests.exe" "~[scene]"

# 冒烟
& ".\build\bin\x64\Debug\fa_app.exe" --frames 60
```

单独编 `fa_scene` 时必须带 `/p:SolutionDir`，否则 `ProjectReference` 找不到同级工程：

```powershell
& $msbuild src\engine\scene\fa_scene.vcxproj /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir="$PWD\" /m /nologo /v:m
```

`fa_scene` **不需要** `/EHsc`：include 图里没有 `<chrono>`（那是 `fa_time` 的）也没有 nlohmann/json。

## 遗留风险

| 风险 | 说明 | 处理 |
|---|---|---|
| `Sprite` 不持 `assets::AssetHandle` | 只有裸 `TextureHandle`，纹理被 asset manager 卸载后句柄悬空 | 加载方（游戏侧）生命周期长于场景；`AssetHandle` 与 `Sprite` 的绑定留给 M05 的热重载完善，见模块文档 §9 |
| 无实体原型 / prefab | 建复杂实体要逐行 `add` | 等 M15 内容库驱动 |
| 无 archetype / chunk | 大世界缓存局部性差 | v1 是 1e3 量级；换 `SparseSet` 内部不破坏公共接口 |
| 系统注册顺序是隐式耦合 | 顺序错了难查 | composition root 集中注册；M27 应能列系统名 |
| 场景与存档的关系未定 | 存世界快照还是存场景描述 | 由 M14 决定，`World` 故意没有 `save`/`load` |
| 无场景过渡 | 切场景硬切 | 归 M25 |
| `[scene]` 没有跨配置压力测试 | 只验证了 1000 实体提取 | 大世界性能验证留给 M12 落地后 |

## 相关文档

- 模块设计与契约：[M11 Scene / World & ECS](../modules/11-scene-world-ecs.md)
- 架构与依赖规则：[模块依赖图](../architecture/module-map.md)、[依赖规则](../architecture/dependency-rules.md)
- 前序实现记录：[M00](m00-fixups.md)、[M05](m05-assets.md)
- 像素尺度与碰撞盒：[pixel-metrics.md](../architecture/pixel-metrics.md)
