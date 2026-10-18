# M08 Audio

| 项 | 值 |
|---|---|
| 层 | L1 |
| 优先级 | P1 |
| 状态 | 已实现 |
| 依赖 | M01、M02 |
| 被依赖 | M20、M22、M23、M25 |

> 实测与踩坑：[构建记录](../build/m08-audio.md) · 后端决策：[ADR-0005](../adr/0005-audio-backend.md)

## 1. 目的

提供 2D 游戏所需的音效播放、循环、音量总线、声像、淡入淡出与简单距离衰减。音频线程只做混音，不访问 ECS/GL/文件系统。

## 2. 范围

**已实现**

- 音频设备：M02 暴露 `IAudioDevice`，`SdlAudioDevice` 用 `SDL_OpenAudioDeviceStream`
  打开默认播放设备并注册混音回调。
- 混音：软件混音到交错立体声 PCM16；固定 48 kHz，支持单/双声道输入。
- 声音实例：`play` / `stop`、循环、`fadeInSeconds`、`stop(fadeSeconds)`、pitch、pan。
- 总线：Master / SFX / Music / Ambience；线性音量相乘。
- 距离衰减：`SoundDesc::spatial` + `setListener()`，默认关闭。
- 32 voice 上限；超出按 priority 与当前音量淘汰或丢弃。
- 无锁 SPSC 命令队列；主线程与音频线程不共享可变混音状态。
- 线程安全统计快照 `AudioStats`。

**未实现 / 后续**

- 资源文件读取与解码（M05 的 `IAudioDecoder`）。
- 音乐流式播放；v1 整段 PCM 常驻。
- SDL 音频子系统初始化（归 M02）。
- 3D 空间音频、混响、DSP 图。
- 玩法语义（何时播放由 M16/M20/M22 决定）。

## 3. 核心概念与数据结构

```cpp
enum class AudioBus { Master, SFX, Music, Ambience, Count };
inline constexpr int kMaxVoices = 32;

struct SoundDesc {
    AudioBus bus = AudioBus::SFX;
    float volume = 1.0f;
    float pitch = 1.0f;
    float pan = 0.0f;          // -1 左, +1 右
    bool loop = false;
    int priority = 0;          // 越大越不容易被淘汰
    float fadeInSeconds = 0.0f;
    bool spatial = false;
    core::Vec2 position{};
};

using SoundHandle = std::uint32_t; // 0 无效
```

- `AudioClip`：`samples`（交错 PCM16）、`sampleRate`、`channels`；`isValid()` 要求非空、
  采样率 > 0、声道 > 0 且样本数是声道整数倍。
- `AudioMixer`：纯软件、非线程安全，由音频线程独占。
- `AudioEngine`：主线程门面；命令经 `SpscQueue<Command, 256>` 进入音频线程。
- `AudioStats`：activeVoices / played / stopped / finished / dropped / evicted /
  mixedFrames / sampleRate / channels / masterVolume。

## 4. 公共接口

```cpp
namespace fa::audio {

struct AudioClip {
    std::vector<std::int16_t> samples;
    int sampleRate = 48000;
    int channels = 2;
    bool isValid() const noexcept;
    int frameCount() const noexcept;
    double durationSeconds() const noexcept;
};

AudioClip makeTone(float frequencyHz, float durationSeconds, int sampleRate = 48000,
                   int channels = 2, float amplitude = 0.25f);
AudioClip makeSilence(float durationSeconds, int sampleRate = 48000, int channels = 2);

class AudioMixer {
public:
    explicit AudioMixer(int sampleRate = 48000, int channels = 2);
    void setFormat(int sampleRate, int channels) noexcept;
    SoundHandle add(std::shared_ptr<const AudioClip> clip, const SoundDesc& desc,
                    SoundHandle handle = kInvalidSoundHandle);
    bool stop(SoundHandle handle, float fadeSeconds = 0.0f);
    void setBusVolume(AudioBus bus, float volume) noexcept;
    float busVolume(AudioBus bus) const noexcept;
    void setListener(core::Vec2 position, bool enabled) noexcept;
    void mix(std::span<std::int16_t> output);
    void reset() noexcept;
    AudioStats stats() const noexcept;
    int activeVoices() const noexcept;
};

class AudioEngine {
public:
    core::Status initialize(platform::IAudioDevice& device);
    void shutdown();
    bool isInitialized() const noexcept;
    SoundHandle play(std::shared_ptr<const AudioClip> clip, const SoundDesc& desc);
    void stop(SoundHandle handle, float fadeSeconds = 0.0f);
    void setBusVolume(AudioBus bus, float volume);
    void setListener(core::Vec2 position, bool enabled = true);
    void update();
    AudioStats stats() const noexcept;
};

}  // namespace fa::audio
```

M02 侧：

```cpp
using AudioCallback = void (*)(void* user, std::int16_t* output, int frames);

class IAudioDevice {
public:
    virtual core::Status initialize(int sampleRate, int channels,
                                    AudioCallback callback, void* user) = 0;
    virtual void shutdown() = 0;
    virtual bool isOpen() const noexcept = 0;
    virtual int sampleRate() const noexcept = 0;
    virtual int channels() const noexcept = 0;
};
```

## 5. 依赖关系

- 依赖 M01（`Result`/`Vec2`/数学）、M02（`IAudioDevice`）。
- **不依赖** M05/M06/M09/M11：`AudioClip` 由 M08 定义，v1 没有解码器；听者位置由调用方传入。
- **不依赖** `fa_assets`、`fa_scene`、`fa_render`；`fa_audio.vcxproj` 只引用 `fa_core` 与
  `fa_platform`，因此不需要 `/EHsc`。
- 音频线程与主线程只通过命令队列与原子统计交互；`AudioClip` 在播放期间由
  `shared_ptr<const AudioClip>` 保活。

## 6. 扩展点

- 新增总线（Voice、UI）在枚举与混音器配置中注册。
- 新增效果：低通、混响 send；作为总线级 DSP 插入，不在 voice 里堆特例。
- 新增解码格式：由 M05 loader 输出统一 PCM16，再构造 `AudioClip`。
- 音乐流式播放：新增 `MusicStream`，按块向混音器提供 PCM；不要把文件读取放进音频线程。
- 可替换后端（WASAPI / miniaudio），只需实现 `IAudioDevice`。
- voice 淘汰/命令丢弃如需玩法反馈，可增加事件回调，但不要阻塞音频线程。

## 7. 验收

- 三配置（Debug / Development / Release × x64）构建零 warning。
- `fa_tests.exe "[audio]"` → 12 用例 / 9808 断言全绿。
- `fa_app.exe --frames 60` → exit 0，摘要打印 `m08:` 行与 `+M08`。
- 实测细节见 [构建记录](../build/m08-audio.md)。

## 8. 风险与开放问题

- 没有解码器与音乐流式播放；`AudioClip` 全量常驻。
- 固定 48 kHz / 1–2 声道；其他设备格式依赖 SDL 流转换。
- `play()` 返回句柄早于音频线程执行；句柄可能最终被淘汰，调用方只能看统计。
- 命令队列满时丢弃并计数；音频线程不做阻塞等待。
- 统计是最近一次回调的快照，不是逐样本精确值。
- 距离衰减是简单反比，不是物理正确的 3D 衰减。
- 没有总线级 DSP、低通或混响；M22 的环境音需要后续扩展。
