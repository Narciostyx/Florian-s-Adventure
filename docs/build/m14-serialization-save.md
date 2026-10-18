# M14 Serialization & Save —— 构建记录 / 实测

对应模块文档：[14-serialization-save.md](../modules/14-serialization-save.md)

> **状态：已实现并三配置实测通过。** JSON 存档、槽位、`.bak` 恢复、迁移链与
> 组件 serializer 注册均已落地；app 每轮 smoke 执行一次 `autosave` 写入 + 读回。

## 1. 实际落地

新增工程 `src/engine/save/fa_save.vcxproj`（GUID `{C0DE0012-…0012}`），
只引用 `fa_core`、`fa_platform`、`fa_scene`。

| 文件 | 职责 |
|---|---|
| `fa/save/SaveArchive.hpp` + `src/SaveArchive.cpp` | JSON 值树封装；`nlohmann::json` 只出现在 cpp |
| `fa/save/SaveTypes.hpp` | `SaveMetadata` / `SaveContext` / `SaveFile` / `kSaveVersion` / `kSaveMagic` |
| `fa/save/SaveError.hpp` | 6100–6107 错误码 |
| `fa/save/SaveService.hpp` + `src/SaveService.cpp` | 槽位、原子写、`.bak` 轮转/恢复、迁移链 |
| `fa/save/ComponentSerializer.hpp` + `src/ComponentSerializer.cpp` | `IComponentSerializer` 与注册表 |
| `tests/save.test.cpp` | `[save]` 测试 |

### 关键实现选择

- **公共头不泄漏 nlohmann**：`SaveArchive` 用 PIMPL 持有 `nlohmann::json`，公共头只暴露
  `object/array/parse/child/set*/get*/dump`。代价是 `SaveArchive.cpp` 打开 `/EHsc`。
- **值树是深拷贝**：`SaveArchive` 的拷贝构造/赋值复制底层 JSON；`SaveContext` 可以按值
  传给 `SaveService::save`，调用方之后修改不会影响已写文件。
- **原子写 + 备份**：`IFileSystem` 没有 rename 接口，所以 `.bak` 轮转是
  “读旧文件 → `writeTextAtomic(.bak)`”；主文件继续用 `writeTextAtomic`（内部 `.tmp` + rename）。
- **恢复策略**：主文件读取/解析失败才尝试 `.bak`；成功时 `SaveFile::recoveredFromBackup=true`。
- **错误不吞**：`kUnsupportedVersion` / `kMigrationFailed` 不尝试备份（备份只会同样失败），
  且在没有可用备份时保留主文件的 `kParseFailed`/`kCorruptSave`，不再统一折叠成 `kCorruptSave`。
- **迁移**：`registerMigration(from, fn)` 按 `fromVersion` 链式执行；加载成功后
  `meta.saveVersion` 统一提升到当前版本，磁盘原始版本保留在顶层 `version`。
- **槽名校验**：1..64 的 `[A-Za-z0-9_-]`，避免 `../` 路径穿越。
- **组件扩展**：`IComponentSerializer` 由 composition root 注册；M14 不 include 任何玩法头。
  未注册的 section 会被保留但不消费，便于向前兼容。
- **app 集成**：保存到仓库根 `saves/`（`.gitignore` 已忽略），每轮 smoke 写
  `saves/autosave.json` 并读回摘要；M27 控制台注册 `save` / `load` / `slots` / `delete`。

### app 写入的内容

- `meta`：版本、引擎版本、槽名、UTC 时间戳、场景 `demo`、day 1、playSeconds。
- `world`：`entities` 数量、`marker` 位置。
- `player`：`stamina`。
- `inventories`：backpack/hotbar 数量与选中槽。
- `quests` / `economy`：预留为空 object，由后续玩法填充。

## 2. 测试

`tests/save.test.cpp` 覆盖：

- `SaveArchive`：bool/int/int64/float/double/string/Vec2/Rect 往返、`keys()`、非法 JSON。
- 值树：array `push`/`at`、深拷贝独立性。
- `SaveService`：单槽 save/load、header/section 值、`recoveredFromBackup=false`。
- 备份：连续保存两次生成 `.bak`；破坏主文件后从 `.bak` 恢复 day/playSeconds。
- 槽位列表：两个正常槽 + 一个损坏文件；损坏项 `readable=false`；删除与非法槽名。
- 版本：高版本拒绝；v1→v2 迁移执行；缺少迁移时报 `kMigrationFailed`。
- `ComponentSerializerRegistry`：注册、重复拒绝、saveAll/loadAll。

实测（三配置一致）：

```text
fa_tests.exe "[save]" → All tests passed (82 assertions in 7 test cases)
fa_tests.exe          → All tests passed (28826 assertions in 458 test cases)
fa_app.exe --frames 60 → m14: slot autosave, v1, scene demo, day 1, 1.00s,
                          5 entities, marker (344, 242.792), stamina 100.0,
                          1 slot(s), backup no
                      (M20 加入两个 combat dummy 后当前为 7 entities)
                       → ...+M13+M14+M15+... smoke test OK
```

`backup no` 表示本次从主文件读取成功；只有主文件损坏时才会显示 `backup used`。

## 3. 遇到的问题与结论

1. **通配符展开后忘了把 `save.test.cpp` 加入 `.vcxproj`。**
   `fa_tests` 使用显式 `ClCompile` 列表；新测试文件必须同时登记。与 M27 的
   `debug.test.cpp` 相同，这是 M08 去掉通配符后的固定流程。
2. **`SaveService::load` 最初把解析错误统一折叠成 `kCorruptSave`。**
   高版本存档与缺少迁移都返回 6104，测试才能发现。修法：先保存主文件的
   `primaryResult`；`kUnsupportedVersion`/`kMigrationFailed` 直接返回，其他错误在
   备份也失败时返回主文件的原始错误。
3. **`Result<SaveArchive>` 的测试写法。**
   `std::move(parsed).value()` 没有 rvalue 重载；改用 `std::move(parsed).take()`，
   并把 `parsed` 从 `const` 改成可变。
4. **迁移后 `meta.saveVersion` 仍是旧值。**
   顶层 `version` 已提升，但 metadata 没有。统一在 `parseSave` 里把
   `file.meta.saveVersion = m_currentVersion`，避免 UI 显示旧版本。
5. **`IFileSystem` 没有 rename。**
   `.bak` 轮转只能“读旧文件 + 原子写 `.bak`”；主文件仍由 `writeTextAtomic` 保证原子替换。
6. **`nlohmann::json` 不能进公共头。**
   `SaveArchive` 用 PIMPL；`fa_save` 单独 `/EHsc`，其他 include `fa/save/...` 的模块
   不会被 nlohmann 的异常设置连坐。
7. **`SaveArchive::dump()` 的非法 UTF-8。**
   使用 `error_handler_t::replace`，避免 `dump` 抛 `type_error`。
8. **app 的保存目录。**
   使用根目录 `saves/`，`.gitignore` 已有 `/saves/`；不要写进 `build/`，否则
   `build/bin` 的清理规则不会覆盖它。
9. **M27 与 M14 的接线顺序。**
   `initializeSave()` 必须在 `initializeDebug()` 之前，控制台命令才能捕获已存在的
   `saveService`；命令内部仍检查空指针，避免 Release/无平台时崩溃。

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

- 没有 World/Inventory/Quest/Economy 的现成 serializer；app 只写摘要字段。
- 没有地图差异、截图、CRC、压缩、加密与云存档。
- `.bak` 只保留一份；连续两次写坏仍可能丢失。
- 自动存档的 12 游戏小时调度与“安全状态”判定归 M25。
- 读档应用时机由 M25 决定；资源未加载时不要直接应用实体引用。
- `SaveArchive` 是深拷贝值树，大存档会有分配；不在每帧热路径。
