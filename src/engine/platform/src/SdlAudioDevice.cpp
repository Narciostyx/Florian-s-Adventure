#include "SdlAudioDevice.hpp"

#include <cstddef>
#include <string>

#include <SDL3/SDL.h>

#include <fa/platform/PlatformError.hpp>

namespace fa::platform::detail {
namespace {

/// Frames generated per SDL request chunk. 2048 frames cover a 42 ms block at
/// 48 kHz, which is the usual callback granularity on Windows.
constexpr int kScratchFrames = 2048;

}  // namespace

SdlAudioDevice::~SdlAudioDevice() {
    shutdown();
}

fa::core::Status SdlAudioDevice::initialize(int sampleRate, int channels, AudioCallback callback, void* user) {
    shutdown();

    if (sampleRate <= 0) {
        return fa::core::errorStatus(platform_error::kAudioInitFailed, "audio sample rate must be positive");
    }
    if (channels != 1 && channels != 2) {
        return fa::core::errorStatus(platform_error::kAudioInitFailed, "only mono and stereo audio are supported");
    }
    if (callback == nullptr) {
        return fa::core::errorStatus(platform_error::kAudioInitFailed, "audio callback is null");
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        return fa::core::errorStatus(platform_error::kAudioInitFailed,
                                     std::string("SDL_InitSubSystem(SDL_INIT_AUDIO) failed: ") + SDL_GetError());
    }

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = channels;
    spec.freq = sampleRate;

    m_stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &SdlAudioDevice::streamCallback, this);
    if (m_stream == nullptr) {
        return fa::core::errorStatus(platform_error::kAudioInitFailed,
                                     std::string("SDL_OpenAudioDeviceStream failed: ") + SDL_GetError());
    }

    m_callback = callback;
    m_user = user;
    m_sampleRate = sampleRate;
    m_channels = channels;
    m_scratch.assign(static_cast<std::size_t>(kScratchFrames) * static_cast<std::size_t>(channels), 0);

    if (!SDL_ResumeAudioStreamDevice(m_stream)) {
        SDL_DestroyAudioStream(m_stream);
        m_stream = nullptr;
        m_callback = nullptr;
        m_user = nullptr;
        m_sampleRate = 0;
        m_channels = 0;
        m_scratch.clear();
        return fa::core::errorStatus(platform_error::kAudioInitFailed,
                                     std::string("SDL_ResumeAudioStreamDevice failed: ") + SDL_GetError());
    }

    return fa::core::okStatus();
}

void SdlAudioDevice::shutdown() {
    if (m_stream != nullptr) {
        SDL_DestroyAudioStream(m_stream);
        m_stream = nullptr;
    }
    m_callback = nullptr;
    m_user = nullptr;
    m_sampleRate = 0;
    m_channels = 0;
    m_scratch.clear();
}

bool SdlAudioDevice::isOpen() const noexcept {
    return m_stream != nullptr;
}

int SdlAudioDevice::sampleRate() const noexcept {
    return m_sampleRate;
}

int SdlAudioDevice::channels() const noexcept {
    return m_channels;
}

void SDLCALL SdlAudioDevice::streamCallback(void* userdata,
                                            SDL_AudioStream* stream,
                                            int additionalAmount,
                                            int totalAmount) {
    static_cast<void>(totalAmount);
    if (userdata == nullptr || stream == nullptr) {
        return;
    }
    static_cast<SdlAudioDevice*>(userdata)->fillStream(stream, additionalAmount);
}

void SdlAudioDevice::fillStream(SDL_AudioStream* stream, int additionalAmount) {
    if (m_callback == nullptr || m_channels <= 0 || m_scratch.empty() || additionalAmount <= 0) {
        return;
    }

    const int bytesPerFrame = static_cast<int>(sizeof(std::int16_t)) * m_channels;
    int frames = additionalAmount / bytesPerFrame;
    while (frames > 0) {
        const int chunk = frames > kScratchFrames ? kScratchFrames : frames;
        m_callback(m_user, m_scratch.data(), chunk);
        static_cast<void>(SDL_PutAudioStreamData(stream, m_scratch.data(), chunk * bytesPerFrame));
        frames -= chunk;
    }
}

}  // namespace fa::platform::detail
