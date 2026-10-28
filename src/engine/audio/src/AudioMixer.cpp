#include "fa/audio/AudioMixer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace fa::audio {
namespace {

constexpr float kReferenceDistance = 256.0f;

[[nodiscard]] float toPcm(float value) noexcept {
    const float clamped = core::clampf(value, -1.0f, 1.0f);
    return clamped * 32767.0f;
}

}  // namespace

AudioMixer::AudioMixer(int sampleRate, int channels) {
    m_busVolumes.fill(1.0f);
    setFormat(sampleRate, channels);
}

void AudioMixer::setFormat(int sampleRate, int channels) noexcept {
    m_sampleRate = sampleRate > 0 ? sampleRate : 48000;
    m_channels = channels == 1 ? 1 : 2;
}

SoundHandle AudioMixer::add(std::shared_ptr<const AudioClip> clip, const SoundDesc& desc, SoundHandle handle) {
    if (!clip || !clip->isValid()) {
        ++m_dropped;
        return kInvalidSoundHandle;
    }

    SoundDesc clean = desc;
    clean.volume = clampVolume(desc.volume);
    clean.pan = core::clampf(desc.pan, -1.0f, 1.0f);
    clean.pitch = core::clampf(desc.pitch, 0.01f, 16.0f);
    clean.fadeInSeconds = (std::isfinite(desc.fadeInSeconds) && desc.fadeInSeconds > 0.0f) ? desc.fadeInSeconds : 0.0f;
    if (clean.bus == AudioBus::Count) {
        clean.bus = AudioBus::SFX;
    }

    const int index = allocateVoice(clean.priority, clean.volume);
    if (index < 0) {
        ++m_dropped;
        return kInvalidSoundHandle;
    }

    if (handle == kInvalidSoundHandle) {
        handle = m_nextHandle++;
        if (handle == kInvalidSoundHandle) {
            handle = m_nextHandle++;
        }
    }

    Voice& voice = m_voices[static_cast<std::size_t>(index)];
    voice.clip = std::move(clip);
    voice.desc = clean;
    voice.handle = handle;
    voice.positionFrames = 0.0;
    voice.step = static_cast<double>(voice.clip->sampleRate) / static_cast<double>(m_sampleRate) *
                 static_cast<double>(clean.pitch);
    voice.framesPlayed = 0;
    voice.fadeOutFrames = 0;
    voice.fadeOutElapsed = 0;
    voice.fadeOutStartGain = 1.0f;
    voice.active = true;
    voice.fadingOut = false;
    ++m_played;
    return handle;
}

bool AudioMixer::stop(SoundHandle handle, float fadeSeconds) {
    const int index = findVoice(handle);
    if (index < 0) {
        return false;
    }

    Voice& voice = m_voices[static_cast<std::size_t>(index)];
    ++m_stopped;
    if (!std::isfinite(fadeSeconds) || fadeSeconds <= 0.0f) {
        deactivate(index);
        return true;
    }

    voice.fadingOut = true;
    voice.fadeOutStartGain = currentFadeIn(voice);
    voice.fadeOutFrames = static_cast<std::uint64_t>(fadeSeconds * static_cast<float>(m_sampleRate));
    if (voice.fadeOutFrames == 0) {
        voice.fadeOutFrames = 1;
    }
    voice.fadeOutElapsed = 0;
    return true;
}

void AudioMixer::setBusVolume(AudioBus bus, float volume) noexcept {
    const std::size_t index = busIndex(bus);
    if (index < kAudioBusCount) {
        m_busVolumes[index] = clampVolume(volume);
    }
}

float AudioMixer::busVolume(AudioBus bus) const noexcept {
    const std::size_t index = busIndex(bus);
    return index < kAudioBusCount ? m_busVolumes[index] : 0.0f;
}

void AudioMixer::setListener(core::Vec2 position, bool enabled) noexcept {
    m_listener = position;
    m_listenerEnabled = enabled;
}

void AudioMixer::mix(std::span<std::int16_t> output) {
    std::fill(output.begin(), output.end(), static_cast<std::int16_t>(0));
    if (m_channels <= 0) {
        return;
    }

    const std::size_t frames = output.size() / static_cast<std::size_t>(m_channels);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        float mixLeft = 0.0f;
        float mixRight = 0.0f;

        for (int index = 0; index < kMaxVoices; ++index) {
            Voice& voice = m_voices[static_cast<std::size_t>(index)];
            if (!voice.active || !voice.clip) {
                continue;
            }

            const AudioClip& clip = *voice.clip;
            const int frameCount = clip.frameCount();
            if (frameCount <= 0) {
                deactivate(index);
                ++m_finished;
                continue;
            }

            if (!voice.desc.loop && voice.positionFrames >= static_cast<double>(frameCount)) {
                deactivate(index);
                ++m_finished;
                continue;
            }
            if (voice.desc.loop) {
                while (voice.positionFrames >= static_cast<double>(frameCount)) {
                    voice.positionFrames -= static_cast<double>(frameCount);
                }
            }

            const int first = static_cast<int>(voice.positionFrames);
            int second = first + 1;
            if (second >= frameCount) {
                second = voice.desc.loop ? 0 : first;
            }
            const float fraction = static_cast<float>(voice.positionFrames - static_cast<double>(first));

            float sampleLeft = 0.0f;
            float sampleRight = 0.0f;
            if (clip.channels == 1) {
                const float a = static_cast<float>(clip.samples[static_cast<std::size_t>(first)]) / 32768.0f;
                const float b = static_cast<float>(clip.samples[static_cast<std::size_t>(second)]) / 32768.0f;
                const float sample = a + (b - a) * fraction;
                sampleLeft = sample;
                sampleRight = sample;
            } else {
                const std::size_t channels = static_cast<std::size_t>(clip.channels);
                const std::size_t aIndex = static_cast<std::size_t>(first) * channels;
                const std::size_t bIndex = static_cast<std::size_t>(second) * channels;
                const float leftA = static_cast<float>(clip.samples[aIndex]) / 32768.0f;
                const float leftB = static_cast<float>(clip.samples[bIndex]) / 32768.0f;
                sampleLeft = leftA + (leftB - leftA) * fraction;
                const float rightA = static_cast<float>(clip.samples[aIndex + 1u]) / 32768.0f;
                const float rightB = static_cast<float>(clip.samples[bIndex + 1u]) / 32768.0f;
                sampleRight = rightA + (rightB - rightA) * fraction;
            }

            float gain = effectiveGain(voice);
            if (voice.fadingOut) {
                const float progress =
                    static_cast<float>(voice.fadeOutElapsed) / static_cast<float>(voice.fadeOutFrames);
                gain *= voice.fadeOutStartGain * (1.0f - progress);
            } else {
                gain *= currentFadeIn(voice);
            }

            const float pan = voice.desc.pan;
            const float leftPan = pan > 0.0f ? 1.0f - pan : 1.0f;
            const float rightPan = pan < 0.0f ? 1.0f + pan : 1.0f;
            mixLeft += sampleLeft * gain * leftPan;
            mixRight += sampleRight * gain * rightPan;

            voice.positionFrames += voice.step;
            ++voice.framesPlayed;

            if (voice.fadingOut) {
                ++voice.fadeOutElapsed;
                if (voice.fadeOutElapsed >= voice.fadeOutFrames) {
                    deactivate(index);
                }
            } else if (!voice.desc.loop && voice.positionFrames >= static_cast<double>(frameCount)) {
                deactivate(index);
                ++m_finished;
            }
        }

        if (m_channels == 1) {
            output[frame] = static_cast<std::int16_t>(toPcm((mixLeft + mixRight) * 0.5f));
        } else {
            output[frame * 2u] = static_cast<std::int16_t>(toPcm(mixLeft));
            output[frame * 2u + 1u] = static_cast<std::int16_t>(toPcm(mixRight));
        }
    }

    m_mixedFrames += static_cast<std::uint64_t>(frames);
}

void AudioMixer::reset() noexcept {
    for (Voice& voice : m_voices) {
        voice = Voice{};
    }
    m_busVolumes.fill(1.0f);
    m_listener = core::Vec2{};
    m_listenerEnabled = false;
    m_nextHandle = 1;
    m_played = 0;
    m_stopped = 0;
    m_finished = 0;
    m_dropped = 0;
    m_evicted = 0;
    m_mixedFrames = 0;
}

AudioStats AudioMixer::stats() const noexcept {
    AudioStats result;
    result.activeVoices = activeVoices();
    result.maxVoices = kMaxVoices;
    result.sampleRate = m_sampleRate;
    result.channels = m_channels;
    result.played = m_played;
    result.stopped = m_stopped;
    result.finished = m_finished;
    result.dropped = m_dropped;
    result.evicted = m_evicted;
    result.mixedFrames = m_mixedFrames;
    result.masterVolume = m_busVolumes[static_cast<std::size_t>(AudioBus::Master)];
    return result;
}

int AudioMixer::activeVoices() const noexcept {
    int count = 0;
    for (const Voice& voice : m_voices) {
        if (voice.active) {
            ++count;
        }
    }
    return count;
}

int AudioMixer::findVoice(SoundHandle handle) const noexcept {
    if (handle == kInvalidSoundHandle) {
        return -1;
    }
    for (int index = 0; index < kMaxVoices; ++index) {
        const Voice& voice = m_voices[static_cast<std::size_t>(index)];
        if (voice.active && voice.handle == handle) {
            return index;
        }
    }
    return -1;
}

int AudioMixer::allocateVoice(int priority, float volume) noexcept {
    for (int index = 0; index < kMaxVoices; ++index) {
        if (!m_voices[static_cast<std::size_t>(index)].active) {
            return index;
        }
    }

    int victim = -1;
    int victimPriority = 0;
    float victimVolume = 0.0f;
    for (int index = 0; index < kMaxVoices; ++index) {
        const Voice& voice = m_voices[static_cast<std::size_t>(index)];
        if (!voice.active) {
            continue;
        }
        const float audible = voice.desc.volume * effectiveGain(voice);
        if (victim < 0 || voice.desc.priority < victimPriority ||
            (voice.desc.priority == victimPriority && audible < victimVolume)) {
            victim = index;
            victimPriority = voice.desc.priority;
            victimVolume = audible;
        }
    }

    if (victim < 0) {
        return -1;
    }
    if (priority > victimPriority || (priority == victimPriority && volume > victimVolume)) {
        deactivate(victim);
        ++m_evicted;
        return victim;
    }
    return -1;
}

void AudioMixer::deactivate(int index) noexcept {
    if (index < 0 || index >= kMaxVoices) {
        return;
    }
    m_voices[static_cast<std::size_t>(index)] = Voice{};
}

float AudioMixer::effectiveGain(const Voice& voice) const noexcept {
    float gain = voice.desc.volume;
    const std::size_t index = busIndex(voice.desc.bus);
    if (index < kAudioBusCount) {
        gain *= m_busVolumes[index];
    }
    gain *= m_busVolumes[static_cast<std::size_t>(AudioBus::Master)];

    if (voice.desc.spatial && m_listenerEnabled) {
        const float dx = voice.desc.position.x - m_listener.x;
        const float dy = voice.desc.position.y - m_listener.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        gain *= kReferenceDistance / (kReferenceDistance + distance);
    }
    return gain;
}

float AudioMixer::currentFadeIn(const Voice& voice) const noexcept {
    if (voice.desc.fadeInSeconds <= 0.0f) {
        return 1.0f;
    }
    const double fadeFrames = static_cast<double>(voice.desc.fadeInSeconds) * static_cast<double>(m_sampleRate);
    if (fadeFrames <= 0.0) {
        return 1.0f;
    }
    const double played = static_cast<double>(voice.framesPlayed);
    return played < fadeFrames ? static_cast<float>(played / fadeFrames) : 1.0f;
}

float AudioMixer::clampVolume(float volume) noexcept {
    return core::clamp01(volume);
}

std::size_t AudioMixer::busIndex(AudioBus bus) noexcept {
    return static_cast<std::size_t>(bus);
}

}  // namespace fa::audio
