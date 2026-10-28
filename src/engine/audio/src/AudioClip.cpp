#include "fa/audio/AudioClip.hpp"

#include <cmath>
#include <cstddef>

#include <fa/core/Math.hpp>

namespace fa::audio {

bool AudioClip::isValid() const noexcept {
    if (sampleRate <= 0 || channels <= 0 || samples.empty()) {
        return false;
    }
    return samples.size() % static_cast<std::size_t>(channels) == 0;
}

int AudioClip::frameCount() const noexcept {
    if (channels <= 0) {
        return 0;
    }
    return static_cast<int>(samples.size() / static_cast<std::size_t>(channels));
}

double AudioClip::durationSeconds() const noexcept {
    if (sampleRate <= 0) {
        return 0.0;
    }
    return static_cast<double>(frameCount()) / static_cast<double>(sampleRate);
}

namespace {

[[nodiscard]] std::size_t frameCountFor(float durationSeconds, int sampleRate) {
    if (!std::isfinite(durationSeconds) || durationSeconds <= 0.0f || sampleRate <= 0) {
        return 0u;
    }
    return static_cast<std::size_t>(durationSeconds * static_cast<float>(sampleRate));
}

}  // namespace

AudioClip makeTone(float frequencyHz, float durationSeconds, int sampleRate, int channels, float amplitude) {
    AudioClip clip;
    clip.sampleRate = sampleRate;
    clip.channels = channels;
    if (sampleRate <= 0 || (channels != 1 && channels != 2) || !std::isfinite(frequencyHz)) {
        return clip;
    }

    const std::size_t frames = frameCountFor(durationSeconds, sampleRate);
    if (frames == 0u) {
        return clip;
    }

    const float peak = core::clamp01(amplitude);
    const double twoPi = 2.0 * static_cast<double>(core::kPi);
    const double radiansPerFrame = twoPi * static_cast<double>(frequencyHz) / static_cast<double>(sampleRate);

    /// 5 ms ramp at each end removes the click a hard start/stop would create.
    const std::size_t rampFrames = static_cast<std::size_t>(0.005f * static_cast<float>(sampleRate));
    const std::size_t safeRamp = rampFrames < frames / 2u ? rampFrames : frames / 2u;

    clip.samples.resize(frames * static_cast<std::size_t>(channels));
    for (std::size_t frame = 0; frame < frames; ++frame) {
        float envelope = 1.0f;
        if (safeRamp > 0u) {
            if (frame < safeRamp) {
                envelope = static_cast<float>(frame) / static_cast<float>(safeRamp);
            } else if (frame + safeRamp >= frames) {
                envelope = static_cast<float>(frames - 1u - frame) / static_cast<float>(safeRamp);
            }
        }
        const float sample =
            static_cast<float>(std::sin(radiansPerFrame * static_cast<double>(frame))) * peak * envelope;
        const std::int16_t pcm = static_cast<std::int16_t>(core::clampf(sample, -1.0f, 1.0f) * 32767.0f);
        for (int channel = 0; channel < channels; ++channel) {
            clip.samples[frame * static_cast<std::size_t>(channels) + static_cast<std::size_t>(channel)] = pcm;
        }
    }
    return clip;
}

AudioClip makeSilence(float durationSeconds, int sampleRate, int channels) {
    AudioClip clip;
    clip.sampleRate = sampleRate;
    clip.channels = channels;
    if (sampleRate <= 0 || (channels != 1 && channels != 2)) {
        return clip;
    }
    const std::size_t frames = frameCountFor(durationSeconds, sampleRate);
    clip.samples.assign(frames * static_cast<std::size_t>(channels), 0);
    return clip;
}

}  // namespace fa::audio
