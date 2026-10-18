# M08 Audio —— 构建记录 / 实测

对应模块文档：[08-audio.md](../modules/08-audio.md) · 决策：[ADR-0005](../adr/0005-audio-backend.md)

> **状态：已实现并三配置实测通过。** 后端为 SDL3 `SDL_OpenAudioDeviceStream` +
> `SDL_AudioStreamCallback` + M08 自研软件混音。没有引入 SDL_mixer。

## 1. 实际落地

### M02 Platform

新增：

- `src/engine/include/fa/platform/AudioDevice.hpp`
  - `platform::AudioCallback = void (*)(void* user, std::int16_t* output, int frames)`
  - `platform::IAudioDevice`：`initialize` / `shutdown` / `isOpen` / `sampleRate` / `channels`
- `src/engine/platform/src/SdlAudioDevice.hpp/.cpp`
  - `initialize()` 先 `SDL_InitSubSystem(SDL_INIT_AUDIO)`，再
    `SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, callback, this)`。
  - 请求 `48 kHz`、`SDL_AUDIO_S16`、单/双声道；打开后设备默认暂停，必须
    `SDL_ResumeAudioStreamDevice()`。
  - SDL 回调参数 `additional_amount` 是**字节数**，不是帧数；实现按
    `additionalAmount / (sizeof(int16_t) * channels)` 换算，并按 2048 帧一块
    调用上层回调后 `SDL_PutAudioStreamData()`，回调内不分配。
- `IPlatform::audio()` 返回 `IAudioDevice*`；`SdlPlatform` 持有 `SdlAudioDevice`，
  `shutdown()` 先停音频流再 `SDL_Quit()`。
- `platform_error::kAudioInitFailed = 2010`。
- `tests/time.test.cpp` 的 `FakePlatform` 增加 `audio()` 空实现（新增纯虚函数）。

### M08 Audio

新增文件：

| 文件 | 职责 |
|---|---|
| `fa/audio/AudioTypes.hpp` | `AudioBus`、`SoundDesc`、`SoundHandle`、`AudioStats`、`kMaxVoices = 32` |
| `fa/audio/AudioClip.hpp` + `src/AudioClip.cpp` | PCM16、帧数/时长校验、`makeTone` / `makeSilence` |
| `fa/audio/SpscQueue.hpp` | 有界无锁单生产者/单消费者队列（move-only、满时计数） |
| `fa/audio/AudioMixer.hpp` + `src/AudioMixer.cpp` | 纯软件混音、总线、pan、循环、淡入淡出、pitch、32 voice 淘汰 |
| `fa/audio/AudioEngine.hpp` + `src/AudioEngine.cpp` | 命令桥、`IAudioDevice` 适配、原子统计发布 |
| `src/engine/audio/fa_audio.vcxproj` | GUID `{C0DE0010-...}`，只引用 `fa_core` + `fa_platform` |
| `tests/audio.test.cpp` | `[audio]` 12 用例 / 9808 断言 |

### 关键实现选择

- **主线程 → 音频线程**：`play` / `stop` / `setBusVolume` / `setListener` 只压
  `SpscQueue<Command, 256>`；音频回调先 drain 命令，再 `mix()`，最后把
  `AudioMixer::stats()` 写入 `AudioEngine` 的原子字段。
- **`stats()` 线程安全**：主线程只读原子快照；`update()` 在设备未打开时本地 drain
  命令，便于无设备测试。
- **句柄**：`AudioEngine` 在主线程预分配非零 `SoundHandle`，命令携带它进入音频线程；
  队列满时 `play` 返回 0。句柄在音频线程真正执行前就返回，因此被 voice 淘汰时
  调用方不会收到额外通知（统计里计数）。
- **pan**：线性、中心不衰减：`pan > 0` 时左增益 `1 - pan`，`pan < 0` 时右增益
  `1 + pan`，中心左右都是 1。
- **音高/重采样**：`step = clip.sampleRate / mixer.sampleRate * pitch`，逐帧线性插值。
- **循环与结束**：循环 voice 在越过 clip 长度时取模回绕；非循环 voice 在混完最后一帧
  后立即释放 `shared_ptr` 并计入 `finished`。
- **淡入/淡出**：淡入按 `framesPlayed / fadeFrames`；`stop(fade)` 记录
  `fadeOutStartGain`（当时的淡入增益），之后按 `startGain * (1 - progress)` 衰减，
  避免淡入与淡出重复相乘。
- **距离衰减**：仅当 `SoundDesc::spatial` 且 `setListener(..., true)` 时生效，
  `gain *= 256 / (256 + distance)`；默认关闭。
- **淘汰**：voice 满时先找 priority 最低、再找当前音量最低的 voice；新声音更高才替换，
  否则丢弃并计入 `dropped`。

### app 集成

`SmokeSink` 新增：

- `initializeAudio()`：`platform.audio()` → `AudioEngine::initialize()`；失败打印
  `audio init failed` 并继续（与 GL 一样非致命）。
- 生成 `0.25 s 440 Hz` 正弦 + `0.1 s` 静音作为 one-shot，`0.5 s 220 Hz` 正弦作为
  loop；Master = 0.8、SFX = 0.7；one-shot 在渲染 2 帧后 `stop(..., 0.05 s)`。
- `onRender()` 每帧调用 `update()`；退出时先 `shutdownAudio()` 再关平台。
- 摘要新增 `m08: voices, played, stopped, dropped, sampleRate, channels, mixedFrames`；
  smoke OK 行插入 `+M08`（M07 之后）。

### 工程文件：去掉通配符

VS2026 打开解决方案时会提示「VC 项目不支持项目项定义中使用的通配符」。M08 交付时把
全部 18 个 `.vcxproj` 的 `src\*.cpp` / `*.cpp` / `..\include\fa\<module>\*.hpp` 展开成
显式文件列表，警告消失。**新增源文件或公共头时，要同时把它们加入对应的 `.vcxproj`**；
新增工程仍然只需要改 `.sln`。

## 2. 测试

`[audio]` 覆盖：

- `AudioClip`：空 clip、声道不整除、非法采样率、`makeTone` / `makeSilence` 的帧数。
- 混音：无 voice 静音、单 voice 双声道、voice/bus/master 增益、硬左/硬右 pan。
- 循环：2 帧 clip 循环 4 帧仍输出；非循环在结束后自动移除并计入 `finished`。
- stop：即时停止、fade 的线性下降与结束移除、重复 stop 返回 false。
- 32 voice：低优先级/低音量新声音被丢弃；高优先级新声音淘汰最低 voice。
- 距离衰减：listener 开启/关闭与远近差异。
- `SpscQueue`：顺序、容量、move-only、满时 `dropped()`。
- `AudioEngine` + fake `IAudioDevice`：initialize / play / pump / 总线静音 / stop /
  shutdown / 设备失败路径。

实测（三配置一致）：

```text
fa_tests.exe "[audio]" → All tests passed (9808 assertions in 12 test cases)
fa_tests.exe           → All tests passed (28628 assertions in 440 test cases)
fa_app.exe --frames 60 → m08: 1 voice(s), 2 played, 1 stopped, 0 dropped,
                         48000 Hz, 2 channel(s), <mixed frames>
                       → M02+M03+M04+M05+M06+M07+M08+M09+M11+M12+M13+M15+M16+M17+M18 smoke test OK
```

`mixed frames` 由墙钟与音频回调决定（本机实测 51360–52320），不要写成固定断言。

## 3. 遇到的问题与结论

1. **`SDL_audio.h` 不声明 `SDL_InitSubSystem` / `SDL_INIT_AUDIO`。**
   只 include `<SDL3/SDL_audio.h>` 会在 `SDL_INIT_AUDIO` 处报 C2065。解决：在
   `SdlAudioDevice.cpp` include `<SDL3/SDL.h>`；私有头只保留 `<SDL3/SDL_audio.h>`
   以获得 `SDL_AudioStream` 与 `SDLCALL`。
2. **`SDL_OpenAudioDeviceStream` 打开后是暂停状态。**
   必须调用 `SDL_ResumeAudioStreamDevice()`；否则命令与混音都正常，但设备不出声。
3. **`additional_amount` 是字节。**
   直接当帧数会每次生成 4 倍（立体声 16-bit）数据。实现里按
   `sizeof(int16_t) * channels` 换算并使用固定 scratch 分块。
4. **统计不能在主线程直接读 `AudioMixer`。**
   混音器只在音频线程被访问；`stats()` 改成读 `AudioEngine` 的原子快照。
5. **`update()` 的语义需要区分设备是否打开。**
   设备打开时命令由音频回调消费；未打开时才在主线程 drain，避免无设备测试里命令
   永远不生效。
6. **fade 测试最初把“剩余淡出帧”当成静音。**
   5 帧 ramp 检查只消耗了 10 帧淡出的一半，后 5 帧仍应线性下降；测试改为先验证
   剩余 ramp，再验证 voice 移除后的静音。
7. **新增 `IPlatform::audio()` 纯虚函数会打断测试替身。**
   `tests/time.test.cpp` 的 `FakePlatform` 补了 `audio()` 返回 `nullptr`。
8. **头文件依赖保持窄。**
   `fa_audio` 只 include `fa_core`、`fa_platform`；没有 JSON/`<chrono>`/
   `<unordered_map>`，所以不需要 `/EHsc`。SDL 类型只出现在 `fa_platform` 的私有
   `SdlAudioDevice.hpp` 中。

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

- 没有解码器：WAV/OGG 要等 M05 的 `IAudioDecoder`；`AudioClip` 常驻内存。
- 没有音乐流式播放：整曲 PCM 载入，长音乐要等 `MusicStream`。
- 固定 48 kHz / 1–2 声道；任意设备格式交给 SDL 流转换。
- 没有 3D、低通、混响、DSP 图；扩展点按总线插入。
- voice 淘汰/命令丢弃只有计数，没有回调通知。
- `stats()` 是最近一次音频回调的快照；游戏逻辑不要依赖逐样本精确值。
