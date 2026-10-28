#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include <fa/core/Result.hpp>
#include <fa/platform/AudioDevice.hpp>
#include <fa/platform/FileSystem.hpp>
#include <fa/platform/GlContext.hpp>
#include <fa/platform/PlatformEvent.hpp>
#include <fa/platform/Window.hpp>

namespace fa::platform {

/// \brief Severity of a modal message box.
enum class MessageBoxKind : std::uint8_t {
    Information,
    Warning,
    Error,
};

/// \brief Owns the platform backend: window, event pump, file system and clock.
class IPlatform {
public:
    virtual ~IPlatform() = default;

    /// \brief Initializes the backend and creates the window described by \p desc.
    ///
    /// Safe to call again: a second call reuses the already created window and
    /// only reports errors that prevent the window from existing.
    virtual fa::core::Status initialize(const WindowDesc& desc) = 0;

    /// \brief Destroys the window and releases the backend.
    ///
    /// Safe to call more than once, and called automatically on destruction.
    virtual void shutdown() = 0;

    /// \brief Appends every event received since the previous call to \p outEvents.
    ///
    /// The vector is never cleared by the implementation.
    ///
    /// \return false once the user has asked to quit; once false is returned it
    /// stays false until shutdown() resets the backend.
    virtual bool pumpEvents(std::vector<PlatformEvent>& outEvents) = 0;

    /// \brief Whether a quit request has been observed by pumpEvents().
    virtual bool quitRequested() const = 0;

    /// \brief The window created by initialize().
    virtual IWindow& window() = 0;

    /// \brief The file system implementation owned by this backend.
    virtual IFileSystem& files() = 0;

    /// \brief The OpenGL context bound to the window, or nullptr when this
    /// backend cannot provide one (a headless or non-graphics build).
    ///
    /// The returned pointer stays valid until the backend is destroyed; the
    /// context inside it is created and destroyed by the caller.
    virtual IGlContext* glContext() = 0;

    /// \brief The audio output device owned by this backend, or nullptr when the
    /// backend cannot provide one.
    ///
    /// The pointer stays valid until the backend is destroyed. Opening the
    /// device is the caller's job and may fail without failing the platform.
    virtual IAudioDevice* audio() = 0;

    /// \brief Monotonic clock reading in seconds.
    ///
    /// Unaffected by wall-clock changes, and returns 0.0 while the backend is not
    /// initialized.
    virtual double monotonicSeconds() const = 0;

    /// \brief Suspends the calling thread for \p seconds.
    ///
    /// Lives here so frame pacing never pulls `<thread>` — and therefore exception
    /// unwinding — into modules that would otherwise stay exception-free. Values
    /// that are zero, negative or not finite return immediately.
    virtual void sleepSeconds(double seconds) = 0;

    /// \brief Shows a modal message box, or falls back to stderr when that fails.
    virtual fa::core::Status showMessageBox(MessageBoxKind kind, std::string_view title, std::string_view message) = 0;
};

/// \brief Creates the platform backend selected at build time.
std::unique_ptr<IPlatform> createPlatform();

}  // namespace fa::platform
