#pragma once

#include <string_view>

#include <fa/core/Result.hpp>

namespace fa::platform {

/// \brief Properties requested for a new OpenGL context.
///
/// Requested values are hints: the driver may hand back a different set of
/// buffers, which is why the renderer queries what it actually got instead of
/// trusting this struct after creation.
struct GlContextDesc {
    int majorVersion = 4;
    int minorVersion = 1;
    /// Core profile has no fixed-function pipeline and no client-side arrays.
    bool coreProfile = true;
    /// Depth and stencil buffers cost VRAM and are unused by a 2D renderer, so
    /// they are off unless a later pass asks for them.
    bool depthBuffer = false;
    bool stencilBuffer = false;
    /// Requests vertical sync. Ignored by drivers that refuse it.
    bool vsync = true;
    /// Asks the driver for a debug context; useful in Debug builds only.
    bool debugContext = false;
};

/// \brief Platform-owned OpenGL context bound to the platform window.
///
/// The only SDL_GL_* user in the engine. It exists so that nothing below the
/// renderer has to know that SDL owns the window, and so that a non-SDL backend
/// only has to implement this interface. Access it through
/// IPlatform::glContext(), which returns nullptr when the backend was built
/// without graphics support.
///
/// Lifetime: the context is bound to the window that existed when
/// createContext() succeeded, so destroy the context before the window closes
/// (IPlatform::shutdown() already does that).
class IGlContext {
public:
    virtual ~IGlContext() = default;

    /// \brief Creates a context for the current window, replacing any previous one.
    ///
    /// Fails when the window does not exist or the driver refuses the request.
    virtual fa::core::Status createContext(const GlContextDesc& desc) = 0;

    /// \brief Releases the context. Safe to call when there is none.
    virtual void destroyContext() = 0;

    /// \brief Whether a context is currently alive.
    virtual bool hasContext() const = 0;

    /// \brief Binds the context to the calling thread for drawing.
    ///
    /// A fresh context is already current on the thread that created it; call
    /// this again after any other context was made current.
    virtual bool makeCurrent() = 0;

    /// \brief Presents the back buffer and handles buffer swapping.
    ///
    /// Does nothing when there is no context.
    virtual void swapBuffers() = 0;

    /// \brief Sets the swap interval: 0 disables vsync, 1 enables it, -1 uses
    /// "adaptive" sync where the driver supports it.
    ///
    /// Returns false when the request was refused; the previous interval stays.
    virtual bool setSwapInterval(int interval) = 0;

    /// \brief The swap interval currently in effect.
    virtual int swapInterval() const = 0;

    /// \brief Resolves an OpenGL entry point by name.
    ///
    /// Covers both the driver's extension mechanism and the 1.1 entry points
    /// that only exist in opengl32.dll - the renderer must not care about the
    /// difference. Returns nullptr when the function is unavailable.
    virtual void* procAddress(std::string_view name) = 0;
};

}  // namespace fa::platform
