# M08 Audio

| 项 | 值 |
|---|---|
| 层 | L1 |
| 优先级 | P1 |
| 状态 | 设计完成 |
| 依赖 | M00–M05 |
| 被依赖 | M20、M22、M23、M25 |

## 1. 目的

提供 2D 游戏所需的音效播放、音乐流式播放、音量总线与简单声像。音频线程只做混音，不访问 ECS/GL/文件系统。

## 2. 范围

**负责**

- 音频设备：通过 M02 暴露的 `IAudioDevice` 打开设备并注册混音回调。
- 混音：软件混音到交错立体声缓冲；支持 16-bit PCM。
- 声音实例：`playOneShot`、`playLoop`、`stop`、`fadeIn/fadeOut`。
- 总线：Master / SFX / Music / Ambience；线性音量与静音。
- 声像与距离衰减（简单线性/平方衰减，基于听者位置）。
- 音乐流式播放：分块解码，不整曲载入。
- 声音资源：`AudioClip`（短音效，PCM 常驻）与 `MusicStream`。

**不负责**

- 资源文件读取/解码（M05 的 loader）。
- SDL 音频子系统初始化（M02）。
- 3D 空间音频/混响/DSP 图（非目标）。
- 玩法语义（何时播放由 M16/M20/M22 决定）。

## 3. 核心概念与数据结构

```cpp
enum class AudioBus { Master, SFX, Music, Ambience };

struct SoundDesc {
    AudioBus bus = AudioBus::SFX;
    float volume = 1.0f;
    float pitch = 1.0f;
    float pan = 0.0f;      // -1 左, +1 右
    bool loop = false;
};

using SoundHandle = std::uint32_t; // 0 表示无效
```

- 混音回调运行在音频线程；它只读取预混音命令队列，不分配内存、不加锁等待主线程。
- 主线程通过无锁 SPSC 命令队列提交播放/停止/参数变更。
- 同时发声上限 32；超出时按优先级与音量淘汰。

## 4. 公共接口草案

```cpp
namespace fa::audio {

class IAudio {
public:
    virtual ~IAudio() = default;
    virtual Result<void> initialize(platform::IAudioDevice& device) = 0;
    virtual void shutdown() = 0;
    virtual SoundHandle play(const AudioClip& clip, const SoundDesc& desc) = 0;
    virtual void stop(SoundHandle handle, float fadeSeconds = 0.0f) = 0;
    virtual void setBusVolume(AudioBus bus, float volume) = 0;
    virtual void setListener(glm::vec2 position) = 0;
    virtual void update() = 0; // 主线程：提交命令、回收句柄
};

} // namespace fa::audio
```

## 5. 依赖关系

- 依赖 M01（错误/数学）、M02（`IAudioDevice`）、M05（`AudioClip` 资源）。
- 不依赖 M06/M09/M11；听者位置由调用方每帧设置。
- 音频线程与主线程只通过命令队列交互；`AudioClip` 在播放期间必须保持有效（句柄持有）。

## 6. 扩展点

- 新增总线（Voice、UI）在枚举与混音器配置中注册。
- 新增效果：低通、混响 send；作为总线级 DSP 插入。
- 新增解码格式：由 M05 loader 输出统一 PCM 格式。
- 可替换后端（SDL audio / WASAPI / miniaudio），实现 `IAudioDevice`。

## 7. 实现阶段

P1，Phase D/E。先实现设备打开、单音效播放、总线音量；再实现循环、淡入淡出、音乐流。验收：同时播放 16 个音效无爆音；音乐切换有淡入淡出；静音生效。

## 8. 风险与开放问题

- 音频线程安全问题是最主要风险；坚持命令队列，不做共享可变状态。
- 设备采样率与混音采样率不一致需要重采样；v1 固定 48kHz 输出，资源侧统一。
- 像素游戏是否需要声像/距离衰减？v1 保留简单实现，默认关闭距离衰减。
- 音乐流式解码需要 M05 支持分段读取；若成本高，v1 可整曲载入。
