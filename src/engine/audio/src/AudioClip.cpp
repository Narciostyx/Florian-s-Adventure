#include "fa/audio/AudioClip.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>

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

namespace {

[[nodiscard]] std::uint16_t readU16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
}

[[nodiscard]] std::uint32_t readU32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

[[nodiscard]] core::Result<AudioClip> wavError(std::string_view message) {
    return core::Result<AudioClip>::err(core::makeError(core::error_code::kInvalidArgument, std::string{message}));
}

}  // namespace

core::Result<AudioClip> decodeWav(std::span<const std::uint8_t> bytes, int targetRate) {
    // RIFF....WAVE then a flat chunk walk; the header is 12 bytes minimum.
    if (bytes.size() < 12 || targetRate <= 0) {
        return wavError("wav buffer is too small");
    }
    if (bytes[0] != 'R' || bytes[1] != 'I' || bytes[2] != 'F' || bytes[3] != 'F' || bytes[8] != 'W' ||
        bytes[9] != 'A' || bytes[10] != 'V' || bytes[11] != 'E') {
        return wavError("not a RIFF/WAVE buffer");
    }

    std::uint16_t format = 0;
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t bitsPerSample = 0;
    std::span<const std::uint8_t> data{};
    bool sawFormat = false;

    std::size_t cursor = 12;
    while (cursor + 8 <= bytes.size()) {
        const std::uint32_t chunkSize = readU32(&bytes[cursor + 4]);
        const std::size_t payload = cursor + 8;
        // Guard against a corrupt size running off the buffer.
        if (chunkSize > bytes.size() - payload) {
            return wavError("wav chunk exceeds the buffer");
        }
        const std::span<const std::uint8_t> chunk(bytes.data() + payload, chunkSize);
        if (bytes[cursor] == 'f' && bytes[cursor + 1] == 'm' && bytes[cursor + 2] == 't' && bytes[cursor + 3] == ' ' &&
            chunk.size() >= 16) {
            format = readU16(&chunk[0]);
            channels = readU16(&chunk[2]);
            sampleRate = readU32(&chunk[4]);
            bitsPerSample = readU16(&chunk[14]);
            sawFormat = true;
        } else if (bytes[cursor] == 'd' && bytes[cursor + 1] == 'a' && bytes[cursor + 2] == 't' &&
                   bytes[cursor + 3] == 'a') {
            data = chunk;
        }
        // Chunks pad to an even byte; step past payload plus pad.
        cursor = payload + chunkSize + (chunkSize % 2u);
    }

    if (!sawFormat || data.empty()) {
        return wavError("wav is missing fmt or data");
    }
    if (format != 1) {
        return wavError("wav format must be PCM16/PCM8");
    }
    if (channels != 1 && channels != 2) {
        return wavError("wav needs 1 or 2 channels");
    }
    if (bitsPerSample != 8 && bitsPerSample != 16) {
        return wavError("wav needs 8 or 16 bit samples");
    }
    if (sampleRate == 0) {
        return wavError("wav sample rate is zero");
    }

    const std::size_t bytesPerFrame = static_cast<std::size_t>(channels) * (bitsPerSample / 8u);
    const std::size_t sourceFrames = data.size() / bytesPerFrame;
    if (sourceFrames == 0) {
        return wavError("wav data has no complete frames");
    }

    // Decode to float frames first, then resample once.
    const auto frameAt = [&](std::size_t frame) -> std::array<float, 2> {
        std::array<float, 2> out{0.0f, 0.0f};
        const std::size_t base = frame * bytesPerFrame;
        for (int c = 0; c < channels; ++c) {
            float value = 0.0f;
            if (bitsPerSample == 16) {
                const std::int16_t raw =
                    static_cast<std::int16_t>(readU16(&data[base + static_cast<std::size_t>(c) * 2u]));
                value = static_cast<float>(raw) / 32768.0f;
            } else {
                // PCM8 is unsigned with 128 as zero.
                const int raw = data[base + static_cast<std::size_t>(c)];
                value = (static_cast<float>(raw) - 128.0f) / 128.0f;
            }
            out[static_cast<std::size_t>(c)] = core::clampf(value, -1.0f, 1.0f);
        }
        return out;
    };

    AudioClip clip;
    clip.sampleRate = targetRate;
    clip.channels = channels;
    const std::size_t frames = static_cast<std::size_t>(
        static_cast<double>(sourceFrames) * static_cast<double>(targetRate) / static_cast<double>(sampleRate));
    clip.samples.resize(frames * static_cast<std::size_t>(channels));
    const double step = static_cast<double>(sourceFrames - 1) / static_cast<double>(frames > 1 ? frames - 1 : 1);
    for (std::size_t out = 0; out < frames; ++out) {
        const double position = std::min(step * static_cast<double>(out), static_cast<double>(sourceFrames - 1));
        const std::size_t first = static_cast<std::size_t>(position);
        const std::size_t second = std::min(first + 1u, sourceFrames - 1u);
        const float mix = static_cast<float>(position - static_cast<double>(first));
        const auto a = frameAt(first);
        const auto b = frameAt(second);
        for (int c = 0; c < channels; ++c) {
            const float value = a[static_cast<std::size_t>(c)] +
                                mix * (b[static_cast<std::size_t>(c)] - a[static_cast<std::size_t>(c)]);
            clip.samples[out * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)] =
                static_cast<std::int16_t>(core::clampf(value, -1.0f, 1.0f) * 32767.0f);
        }
    }
    if (!clip.isValid()) {
        return wavError("wav decode produced an unusable clip");
    }
    return core::Result<AudioClip>::ok(std::move(clip));
}

}  // namespace fa::audio
