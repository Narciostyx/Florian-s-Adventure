# ADR-0005：音频后端使用 SDL3 流回调 + M08 自研软件混音

- 状态：Accepted
- 日期：2026-10-18
- 决策者：项目所有人（代行决策：架构负责人）
- 相关模块：M02、M08、M05、M20、M22、M23、M25
- 取代：无

## 背景

M08 需要一条能播放 2D 音效、循环、总线音量、声像与淡入淡出的最小音频链路。项目已经通过 M02 依赖 SDL3，v1 又只有短 PCM 音效，没有音乐流式解码需求。可选方案有三类：

1. 引入 SDL_mixer，直接使用它的声道/音乐 API。
2. 只用 SDL3 的设备与流回调，混音、总线、声像、循环与淘汰全部由 M08 实现。
3. 换成 miniaudio/WASAPI 等另一套后端。

## 决策

采用 **SDL3 `SDL_OpenAudioDeviceStream` + `SDL_AudioStreamCallback` + M08 自研软件混音**，不引入 SDL_mixer。

### 分层

- **M02** 新增 `fa::platform::IAudioDevice` 与 `SdlAudioDevice`。它是唯一接触 SDL 音频 API 的地方：打开默认播放设备、固定请求 48 kHz / 2 声道 / `SDL_AUDIO_S16`，并在 SDL 音频线程里调用回调。
- **M08** 新增 `AudioClip`、`AudioMixer`、`AudioEngine` 与 `SpscQueue`。混音器是纯软件实现，不依赖 SDL；引擎通过 `IAudioDevice` 借用设备，通过无锁 SPSC 队列接收主线程命令。
- **composition root（`fa_app`）** 负责调用 `platform.audio()`、初始化引擎、生成占位 PCM、设置总线音量并打印统计。

### 线程模型

- 主线程只调用 `play` / `stop` / `setBusVolume` / `setListener`，这些操作只把命令压入 `SpscQueue`。
- 音频线程是唯一消费命令、唯一访问 `AudioMixer` 的线程；回调内不分配、不加锁、不做文件或 ECS 访问。
- `AudioEngine::stats()` 读的是音频线程发布到 `std::atomic` 的快照，因此主线程随时可读。
- `AudioClip` 用 `std::shared_ptr<const AudioClip>` 保活；voice 结束时在音频线程释放引用。

### 格式与资源

- v1 固定输出 48 kHz、16-bit 有符号、交错立体声；声道数只支持 1/2。
- 资源侧统一为 PCM16；没有解码器。app 与测试用代码生成正弦/静音片段。
- 音乐流式播放、WAV/OGG 解码留给 M05 的 `IAudioDecoder` 后续扩展。

## 考虑过的其他方案

| 方案 | 优点 | 缺点 | 结论 |
|---|---|---|---|
| SDL_mixer | 现成的声道、音乐、格式转换 | 新增第二套音频依赖；v1 只有短 PCM，收益不足；混音规则受库约束 | 否决 |
| 只用 SDL3 设备回调 + 自研混音 | 不新增依赖；总线/声像/淘汰/淡入淡出完全可控；与现有窄接口风格一致 | 混音与线程安全要自己写；需要自己处理字节/帧换算 | **采纳** |
| miniaudio / WASAPI | 不依赖 SDL | 新增后端与平台分支；与 M02 已有的 SDL 生命周期不一致 | 否决 |
| 直接阻塞式 `SDL_QueueAudio` | 实现最短 | 主线程要持续喂数据，无法与帧循环解耦；没有音频线程回调 | 否决 |

## 后果

正面

- `vcpkg.json` 不新增依赖，构建时间与许可证面不变。
- 混音规则（32 voice、优先级淘汰、总线相乘、线性 pan、淡入淡出）都在 M08 的可测试代码里。
- M08 只引用 `fa_core` 与 `fa_platform`，不引入 `fa_assets`、ECS、GL 或 JSON，也不需要 `/EHsc`。
- 音频设备失败与 GL 失败一样是非致命的；隐藏/CI 运行继续完成 smoke。

负面

- 没有音乐流式播放与解码；整段 PCM 常驻内存。
- 固定 48 kHz / 立体声输出；设备采样率不一致时由 SDL 流的格式转换兜底，M08 自身不做重采样到任意设备格式。
- 命令队列满时新命令被丢弃并计数；`play()` 在命令真正被音频线程执行前就返回句柄，因此句柄可能对应一个最终被淘汰的 voice。
- 统计是“最近一次回调”的快照，不是逐样本精确计数。

需持续关注

- 若后续要流式音乐，优先在 M05 增加 `IAudioDecoder` / `MusicStream`，而不是把文件读取塞进音频线程。
- 若需要 3D/低通/混响，按总线插入 DSP，不要在 voice 里堆特例。
- `AudioClip` 的 `shared_ptr` 引用计数在音频线程递减；若未来换成自定义分配器，需要确认其线程安全。

## 验证方式

- `tests/audio.test.cpp` 覆盖：`AudioClip` 校验与生成、静音混音、单 voice 双声道、voice/bus/master 增益、pan、循环与非循环结束、即时 stop 与 fade、32 voice 淘汰、距离衰减、SPSC 顺序与溢出、fake `IAudioDevice` 下的 initialize/play/stop/update/shutdown。
- 三配置（Debug / Development / Release × x64）构建零 warning，`fa_tests.exe` 全绿，`fa_app.exe --frames 60` exit 0 且打印 `m08:` 行。

## 影响的文档

- `docs/reference/code-reference.md`
- `docs/reference/code-reference.md`
- `docs/architecture/module-map.md` / `dependency-rules.md`
- `docs/status/plan-status.md`
