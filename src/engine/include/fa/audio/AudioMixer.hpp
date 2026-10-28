#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include <fa/audio/AudioClip.hpp>
#include <fa/audio/AudioTypes.hpp>
#include <fa/core/Math.hpp>

namespace fa::audio {

/// \brief Pure software mixer. Owned and driven by the audio thread.
///
/// Nothing here is thread-safe. `AudioEngine` is the only production caller and
/// invokes it from the device callback; tests invoke it directly.
class AudioMixer {
public:
    explicit AudioMixer(int sampleRate = 48000, int channels = 2);

    /// \brief Sets the output format. Only meaningful before voices are added.
    void setFormat(int sampleRate, int channels) noexcept;
    [[nodiscard]] int sampleRate() const noexcept { return m_sampleRate; }
    [[nodiscard]] int channels() const noexcept { return m_channels; }

    /// \brief Starts a voice. Returns zero when the clip is invalid or no voice
    /// could be freed.
    ///
    /// \param handle Explicit handle from AudioEngine; zero lets the mixer assign one.
    [[nodiscard]] SoundHandle
    add(std::shared_ptr<const AudioClip> clip, const SoundDesc& desc, SoundHandle handle = kInvalidSoundHandle);

    /// \brief Stops a voice immediately or starts a linear fade.
    [[nodiscard]] bool stop(SoundHandle handle, float fadeSeconds = 0.0f);

    void setBusVolume(AudioBus bus, float volume) noexcept;
    [[nodiscard]] float busVolume(AudioBus bus) const noexcept;

    /// \brief Enables distance attenuation for spatial voices.
    void setListener(core::Vec2 position, bool enabled) noexcept;

    /// \brief Fills interleaved PCM. `output.size()` must be a frame multiple.
    void mix(std::span<std::int16_t> output);

    /// \brief Stops every voice and resets counters.
    void reset() noexcept;

    [[nodiscard]] AudioStats stats() const noexcept;
    [[nodiscard]] int activeVoices() const noexcept;

private:
    struct Voice {
        std::shared_ptr<const AudioClip> clip;
        SoundDesc desc;
        SoundHandle handle = kInvalidSoundHandle;
        double positionFrames = 0.0;
        double step = 1.0;
        std::uint64_t framesPlayed = 0;
        std::uint64_t fadeOutFrames = 0;
        std::uint64_t fadeOutElapsed = 0;
        float fadeOutStartGain = 1.0f;
        bool active = false;
        bool fadingOut = false;
    };

    [[nodiscard]] int findVoice(SoundHandle handle) const noexcept;
    [[nodiscard]] int allocateVoice(int priority, float volume) noexcept;
    void deactivate(int index) noexcept;
    [[nodiscard]] float effectiveGain(const Voice& voice) const noexcept;
    [[nodiscard]] float currentFadeIn(const Voice& voice) const noexcept;
    [[nodiscard]] static float clampVolume(float volume) noexcept;
    [[nodiscard]] static std::size_t busIndex(AudioBus bus) noexcept;

    int m_sampleRate = 48000;
    int m_channels = 2;
    std::array<Voice, kMaxVoices> m_voices{};
    std::array<float, kAudioBusCount> m_busVolumes{};
    core::Vec2 m_listener{};
    bool m_listenerEnabled = false;
    SoundHandle m_nextHandle = 1;
    std::uint64_t m_played = 0;
    std::uint64_t m_stopped = 0;
    std::uint64_t m_finished = 0;
    std::uint64_t m_dropped = 0;
    std::uint64_t m_evicted = 0;
    std::uint64_t m_mixedFrames = 0;
};

}  // namespace fa::audio
