# M08 Audio —— 构建记录 / 交接计划

对应模块文档：[08-audio.md](../modules/08-audio.md)

> **状态：尚未编码。** 本文件是中断交接：新对话读完
> [计划完成情况](../plan-status.md) 后按本文件继续 M08。

## 1. 结论：后端选择

优先使用 **SDL3 自带音频 + M08 自研软件混音器**，不引入 SDL_mixer。

理由：

- SDL3 已经是 M02 的既有依赖（`vcpkg.json` 里的 `sdl3`），不新增第三方库，符合
  「不引入新依赖除非必要」的长期规约。
- SDL3 提供 `SDL_OpenAudioDeviceStream` + `SDL_AudioStreamCallback`，设备打开、格式转换
  和音频线程都由 SDL 管理；混音、总线、声像、循环与淘汰规则仍由 M08 实现。
- SDL_mixer 会带来第二套音频依赖与格式/解码约束，v1 只有 2D PCM 音效，收益不足。

需要新增一个 ADR（`docs/adr/0005-audio-backend.md`），记录「SDL3 audio + 自研混音、
不引入 SDL_mixer」以及备选方案。

### SDL3 API 事实（已在本机头文件确认）

- 头文件：`vcpkg_installed/x64-windows-static-md/x64-windows-static-md/include/SDL3/SDL_audio.h`
- `SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, callback, userdata)`
  返回 `SDL_AudioStream*`；`SDL_DestroyAudioStream()` 关闭。
- 回调类型：`void (SDLCALL *SDL_AudioStreamCallback)(void* userdata, SDL_AudioStream* stream,
  int additional_amount, int total_amount)`；在回调里生成/推入 PCM。
- 常用格式：`SDL_AUDIO_S16`（16-bit 有符号，交错）；`SDL_AUDIO_F32` 也可。
- `SDL_ResumeAudioStreamDevice(stream)` 开始播放；`SDL_PauseAudioStreamDevice()` 暂停。

## 2. 跨模块改动

### M02 Platform

新增 `IAudioDevice` + SDL 实现（与 `IGlContext` 一样，初始化失败不致命）：

```cpp
namespace fa::platform {
using AudioCallback = void (*)(void* user, std::int16_t* output, int frames);

class IAudioDevice {
public:
    virtual ~IAudioDevice() = default;
    virtual core::Status initialize(int sampleRate, int channels, AudioCallback callback, void* user) = 0;
    virtual void shutdown() = 0;
    virtual bool isOpen() const noexcept = 0;
    virtual int sampleRate() const noexcept = 0;
    virtual int channels() const noexcept = 0;
};
}
```

- 文件：`src/engine/include/fa/platform/AudioDevice.hpp`、`src/engine/platform/src/SdlAudioDevice.cpp`。
- 固定 48 kHz、2 声道、`SDL_AUDIO_S16`；`initialize()` 打开 `SDL_OpenAudioDeviceStream`，
  回调里直接调用 M08 的 mixer（通过 `void* user`），把 `additional_amount` 字节的 PCM
  通过 `SDL_PutAudioStreamData` 推回 stream。
- 失败返回 `platform_error::kAudioInitFailed`（新增 2000 段错误码）；隐藏/CI 运行继续无音频。
- `fa_platform` 已经有 SDL3；不需要新链接库。

### M08 Audio

文件：

- `src/engine/include/fa/audio/AudioTypes.hpp`（`AudioBus`、`SoundDesc`、`SoundHandle`、`AudioStats`）
- `src/engine/include/fa/audio/AudioClip.hpp`（PCM16、采样率、声道、帧数）
- `src/engine/include/fa/audio/SpscQueue.hpp`（无锁单生产者/单消费者命令队列）
- `src/engine/include/fa/audio/AudioMixer.hpp`（纯软件混音，不依赖设备）
- `src/engine/include/fa/audio/AudioEngine.hpp`（实现 `IAudio`，连接 `IAudioDevice`）
- `src/engine/audio/src/*.cpp`
- `src/engine/audio/fa_audio.vcxproj`（GUID `{C0DE0010-0010-4F00-9A00-000000000010}`）
- `tests/audio.test.cpp`（`[audio]`）

API 草案：

```cpp
enum class AudioBus { Master, SFX, Music, Ambience, Count };
struct SoundDesc { AudioBus bus; float volume; float pitch; float pan; bool loop; };
using SoundHandle = std::uint32_t; // 0 无效

class AudioMixer {
public:
    SoundHandle add(std::shared_ptr<const AudioClip> clip, const SoundDesc& desc);
    void stop(SoundHandle handle, float fadeSeconds = 0.0f);
    void setBusVolume(AudioBus bus, float volume);
    void setListener(core::Vec2 position, bool enabled);
    void mix(std::span<std::int16_t> output); // frames = samples / channels
    AudioStats stats() const;
};

class AudioEngine {
public:
    core::Status initialize(platform::IAudioDevice& device);
    void shutdown();
    SoundHandle play(std::shared_ptr<const AudioClip> clip, const SoundDesc& desc);
    void stop(SoundHandle, float fadeSeconds = 0.0f);
    void setBusVolume(AudioBus, float);
    void setListener(core::Vec2);
    void update(); // 主线程回收/提交
};
```

- 32 voice 上限；超出按 `priority`（`SoundDesc` 可加字段）与当前音量淘汰。
- 总线音量相乘；Master 为 0 时静音。
- 声像：线性 pan；距离衰减 v1 默认关闭，`setListener` 只在启用时生效。
- 循环与淡入淡出；`stop` 的 fade 在混音器里用每块增益斜坡实现。
- 音频线程只读命令队列 + 混音；回调内不分配、不加锁等待。`AudioClip` 用
  `shared_ptr<const AudioClip>` 保活；voice 结束在音频线程释放引用（原子引用计数）。
- 音乐流式播放 v1 不做；整段 PCM 常驻，文档标注后续扩展。

### M05 / 资源

v1 **不新增解码器**：app 用代码生成一段 16-bit PCM 正弦/方波作为 `AudioClip`，验证
播放/循环/音量/声像/淡出。真实 WAV/OGG 解码与 `AudioClip` 资源类型留给 M05 的
`IAudioDecoder`（与 `IImageDecoder` 对称），在文档中登记为后续工作。

## 3. 测试计划（headless，不打开设备）

- `AudioClip` 帧数/声道校验；空 clip 拒绝。
- `AudioMixer::mix` 输出静音（无 voice）与单 voice 的采样值/峰值。
- 音量、Master/SFX 总线、静音、pan 左右声道差异。
- 循环：mix 超过 clip 长度后仍输出；非循环 voice 在结束后自动移除。
- `stop` 即时与 fade 后移除；voice 上限淘汰最低优先级/音量。
- `SpscQueue` 单线程顺序、容量满时丢弃并计数、move-only 类型。
- `AudioEngine` 用 fake `IAudioDevice` 验证 initialize/shutdown/play/stop/update；
  设备失败返回错误但 `shutdown()` 安全。
- 不测试真实 SDL 设备（CI 无声卡）；app 冒烟单独尝试初始化。

## 4. app 集成

- `SmokeSink` 增加 `std::unique_ptr<fa::audio::AudioEngine>`、`AudioClip`、`AudioStats`。
- 初始化：`platform.audio()`（M02 新增）返回 `IAudioDevice&`；失败则打印
  `audio unavailable` 并继续（与 GL 一样非致命）。
- 生成 0.25 s 440 Hz 正弦 + 0.1 s 静音，播放一次 one-shot、一个循环，设置
  Master/SFX 音量，播放 2 帧后 stop，输出统计。
- 摘要增加 `m08: voices, played, stopped, dropped, sample rate, channels`；
  smoke OK 行加 `+M08`（按模块号插在 M07 后）。
- HUD 可加一行 `AUDIO 48K STEREO`（可选，非必需）。

## 5. 验证

```powershell
$env:VCPKG_ROOT='D:\CPPLibraries\vcpkg'
$msbuild='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
foreach ($cfg in @('Debug','Development','Release')) {
  & $msbuild FloriansAdventure.sln /t:Rebuild /p:Configuration=$cfg /p:Platform=x64 /m /nologo /v:m
  & ".\build\bin\x64\$cfg\fa_tests.exe"
  & ".\build\bin\x64\$cfg\fa_app.exe" --frames 60
}
```

完成后回写：`docs/modules/08-audio.md`、`docs/build/m08-audio.md`（把本计划替换为实测）、
`docs/index.md`、`docs/plan-status.md`、`docs/architecture/module-map.md` /
`dependency-rules.md`、`docs/adr/0005-audio-backend.md`。

## 6. 已确认

用户已选择 **SDL3 自带音频 + M08 自研软件混音**，**不引入 SDL_mixer**。
实现顺序：先写 ADR-0005，再按上面的文件顺序实现；`fa_audio` 的 ProjectReference 只加
`fa_core` 与 `fa_platform`（`AudioClip` 由 M08 定义，不依赖 M05）。
