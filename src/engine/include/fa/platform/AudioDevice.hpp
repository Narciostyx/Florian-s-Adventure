#pragma once

#include <cstdint>

#include <fa/core/Result.hpp>

namespace fa::platform {

/// \brief Mixer callback invoked on the platform's audio thread.
///
/// \param user Opaque pointer passed to IAudioDevice::initialize().
/// \param output Interleaved signed 16-bit PCM; every frame must be written.
/// \param frames Number of frames (samples per channel) to generate.
using AudioCallback = void (*)(void* user, std::int16_t* output, int frames);

/// \brief Audio output device owned by the platform backend.
///
/// The device owns format conversion and the audio thread. Clients only supply
/// a callback that fills interleaved PCM; they never touch SDL or WASAPI here.
class IAudioDevice {
public:
    virtual ~IAudioDevice() = default;

    /// \brief Opens the default playback device and starts the callback stream.
    ///
    /// Supported formats are mono or stereo signed 16-bit PCM. When this fails,
    /// the caller may continue without audio.
    virtual fa::core::Status initialize(int sampleRate, int channels, AudioCallback callback, void* user) = 0;

    /// \brief Stops the stream and releases the device. Safe to call repeatedly.
    virtual void shutdown() = 0;

    /// \brief Whether the device stream is currently open.
    [[nodiscard]] virtual bool isOpen() const noexcept = 0;

    /// \brief Sample rate the callback must generate, or 0 when closed.
    [[nodiscard]] virtual int sampleRate() const noexcept = 0;

    /// \brief Channel count the callback must generate, or 0 when closed.
    [[nodiscard]] virtual int channels() const noexcept = 0;
};

}  // namespace fa::platform
