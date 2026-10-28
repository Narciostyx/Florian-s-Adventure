#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include <fa/audio/AudioClip.hpp>
#include <fa/audio/AudioMixer.hpp>
#include <fa/audio/AudioTypes.hpp>
#include <fa/audio/SpscQueue.hpp>
#include <fa/platform/AudioDevice.hpp>

namespace fa::audio {

/// \brief Main-thread facade that bridges commands to the device audio thread.
///
/// `play()` and friends only enqueue commands; the mixer is touched exclusively
/// by the device callback. Counters are published back through atomics so
/// `stats()` is safe to call on the main thread at any time.
class AudioEngine {
public:
    static constexpr int kSampleRate = 48000;
    static constexpr int kChannels = 2;
    static constexpr std::size_t kCommandCapacity = 256;

    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    /// \brief Opens \p device and starts accepting sounds.
    ///
    /// Failure leaves the engine shut down; callers may continue without audio.
    [[nodiscard]] core::Status initialize(platform::IAudioDevice& device);

    /// \brief Stops the device and drops every queued command.
    void shutdown();

    [[nodiscard]] bool isInitialized() const noexcept;

    /// \brief Queues a one-shot or looping voice.
    /// \return A non-zero handle, or zero when the engine is not running.
    [[nodiscard]] SoundHandle play(std::shared_ptr<const AudioClip> clip, const SoundDesc& desc);

    /// \brief Queues a stop; a positive fade starts a linear ramp instead.
    void stop(SoundHandle handle, float fadeSeconds = 0.0f);

    void setBusVolume(AudioBus bus, float volume);

    /// \brief Enables or disables distance attenuation.
    void setListener(core::Vec2 position, bool enabled = true);

    /// \brief Main-thread pump. A live device consumes commands in its callback;
    /// without a device this applies them locally so headless runs stay coherent.
    void update();

    /// \brief Thread-safe snapshot of the published counters.
    [[nodiscard]] AudioStats stats() const noexcept;

private:
    enum class CommandKind : std::uint8_t {
        Play,
        Stop,
        BusVolume,
        Listener,
    };

    struct Command {
        CommandKind kind = CommandKind::Stop;
        SoundHandle handle = kInvalidSoundHandle;
        AudioBus bus = AudioBus::SFX;
        float value = 0.0f;
        float fadeSeconds = 0.0f;
        bool enabled = false;
        core::Vec2 position{};
        SoundDesc desc{};
        std::shared_ptr<const AudioClip> clip;
    };

    static void audioCallback(void* user, std::int16_t* output, int frames);
    void render(std::int16_t* output, int frames);
    void drainCommands();
    void apply(Command& command);
    void publishStats();

    platform::IAudioDevice* m_device = nullptr;
    AudioMixer m_mixer;
    SpscQueue<Command, kCommandCapacity> m_commands;
    std::atomic<SoundHandle> m_nextHandle{1};
    std::atomic<bool> m_initialized{false};

    std::atomic<int> m_activeVoices{0};
    std::atomic<int> m_sampleRate{0};
    std::atomic<int> m_channels{0};
    std::atomic<std::uint64_t> m_played{0};
    std::atomic<std::uint64_t> m_stopped{0};
    std::atomic<std::uint64_t> m_finished{0};
    std::atomic<std::uint64_t> m_dropped{0};
    std::atomic<std::uint64_t> m_evicted{0};
    std::atomic<std::uint64_t> m_mixedFrames{0};
    std::atomic<float> m_masterVolume{1.0f};
};

}  // namespace fa::audio
