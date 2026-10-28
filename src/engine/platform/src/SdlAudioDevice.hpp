#pragma once

#include <cstdint>
#include <vector>

#include <SDL3/SDL_audio.h>

#include <fa/platform/AudioDevice.hpp>

namespace fa::platform::detail {

/// \brief IAudioDevice backed by SDL_OpenAudioDeviceStream().
///
/// SDL owns the device and the audio thread. The callback writes into a fixed
/// scratch buffer, so the audio thread performs no allocation after initialize().
class SdlAudioDevice final : public IAudioDevice {
public:
    SdlAudioDevice() = default;
    ~SdlAudioDevice() override;

    SdlAudioDevice(const SdlAudioDevice&) = delete;
    SdlAudioDevice& operator=(const SdlAudioDevice&) = delete;

    fa::core::Status initialize(int sampleRate, int channels, AudioCallback callback, void* user) override;
    void shutdown() override;
    [[nodiscard]] bool isOpen() const noexcept override;
    [[nodiscard]] int sampleRate() const noexcept override;
    [[nodiscard]] int channels() const noexcept override;

private:
    static void SDLCALL streamCallback(void* userdata, SDL_AudioStream* stream, int additionalAmount, int totalAmount);
    void fillStream(SDL_AudioStream* stream, int additionalAmount);

    SDL_AudioStream* m_stream = nullptr;
    AudioCallback m_callback = nullptr;
    void* m_user = nullptr;
    int m_sampleRate = 0;
    int m_channels = 0;
    std::vector<std::int16_t> m_scratch;
};

}  // namespace fa::platform::detail
