#pragma once

#include <fa/platform/GlContext.hpp>

#include "SdlWindow.hpp"

struct SDL_Window;
/// SDL_GLContext is a typedef for a pointer to this opaque struct, so the tag is
/// enough to name the handle without pulling SDL into this header.
struct SDL_GLContextState;

namespace fa::platform::detail {

/// \brief IGlContext backed by SDL_GL_* on the window owned by SdlPlatform.
///
/// The window pointer is read on every call rather than cached, because the
/// platform may recreate its window (initialize() is documented as repeatable).
class SdlGlContext final : public IGlContext {
public:
    explicit SdlGlContext(SdlWindow& window);
    ~SdlGlContext() override;

    SdlGlContext(const SdlGlContext&) = delete;
    SdlGlContext& operator=(const SdlGlContext&) = delete;

    fa::core::Status createContext(const GlContextDesc& desc) override;
    void destroyContext() override;
    bool hasContext() const override;
    bool makeCurrent() override;
    void swapBuffers() override;
    bool setSwapInterval(int interval) override;
    int swapInterval() const override;
    void* procAddress(std::string_view name) override;

private:
    SdlWindow& m_window;
    SDL_Window* m_contextWindow = nullptr;
    SDL_GLContextState* m_context = nullptr;
    int m_swapInterval = 0;
};

}  // namespace fa::platform::detail
