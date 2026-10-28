#pragma once

#include <cstdint>
#include <vector>

namespace fa::audio {

/// \brief Resident PCM16 audio. Samples are interleaved by channel.
///
/// v1 has no decoder: clips are generated in code or produced by a later M05
/// decoder. The mixer keeps a `shared_ptr<const AudioClip>` alive while a voice
/// references it, so callers may drop their copy after `play()` returns.
struct AudioClip {
    std::vector<std::int16_t> samples;
    int sampleRate = 48000;
    int channels = 2;

    /// \brief Whether the PCM layout is usable by the mixer.
    [[nodiscard]] bool isValid() const noexcept;

    /// \brief Frames (samples per channel) in the clip.
    [[nodiscard]] int frameCount() const noexcept;

    /// \brief Playback duration in seconds.
    [[nodiscard]] double durationSeconds() const noexcept;
};

/// \brief Generates a sine tone with a short de-click ramp.
///
/// \param amplitude Linear peak amplitude in `[0, 1]`.
[[nodiscard]] AudioClip
makeTone(float frequencyHz, float durationSeconds, int sampleRate = 48000, int channels = 2, float amplitude = 0.25f);

/// \brief Generates a silent clip of the requested duration.
[[nodiscard]] AudioClip makeSilence(float durationSeconds, int sampleRate = 48000, int channels = 2);

}  // namespace fa::audio
