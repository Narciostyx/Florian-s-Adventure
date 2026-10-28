#pragma once

#include <cstddef>
#include <cstdint>

#include <fa/core/Math.hpp>

namespace fa::audio {

/// \brief Mixing buses. Master multiplies every other bus.
enum class AudioBus : std::uint8_t {
    Master = 0,
    SFX,
    Music,
    Ambience,
    Count,
};

inline constexpr std::size_t kAudioBusCount = static_cast<std::size_t>(AudioBus::Count);

/// \brief Hard voice limit. New sounds either evict a lower-priority voice or drop.
inline constexpr int kMaxVoices = 32;

/// \brief Per-play sound parameters.
///
/// `position` and `spatial` are only used when the listener is enabled. v1 keeps
/// distance attenuation simple and off by default; pan is always applied.
struct SoundDesc {
    AudioBus bus = AudioBus::SFX;
    float volume = 1.0f;
    float pitch = 1.0f;
    /// -1 is hard left, 0 is center, +1 is hard right.
    float pan = 0.0f;
    bool loop = false;
    /// Higher values survive voice-limit eviction.
    int priority = 0;
    float fadeInSeconds = 0.0f;
    bool spatial = false;
    core::Vec2 position{};
};

/// \brief Opaque voice handle. Zero is never a valid handle.
using SoundHandle = std::uint32_t;
inline constexpr SoundHandle kInvalidSoundHandle = 0;

/// \brief Counters for the smoke summary and debug overlay.
struct AudioStats {
    int activeVoices = 0;
    int maxVoices = kMaxVoices;
    int sampleRate = 0;
    int channels = 0;
    std::uint64_t played = 0;
    std::uint64_t stopped = 0;
    std::uint64_t finished = 0;
    std::uint64_t dropped = 0;
    std::uint64_t evicted = 0;
    std::uint64_t mixedFrames = 0;
    float masterVolume = 1.0f;
};

}  // namespace fa::audio
