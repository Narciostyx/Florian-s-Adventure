#include "SdlGlContext.hpp"

#include <string>

#include <SDL3/SDL.h>

#include <fa/platform/PlatformError.hpp>

namespace fa::platform::detail {

SdlGlContext::SdlGlContext(SdlWindow& window) : m_window(window) {}

SdlGlContext::~SdlGlContext() {
    destroyContext();
}

fa::core::Status SdlGlContext::createContext(const GlContextDesc& desc) {
    destroyContext();

    SDL_Window* window = m_window.raw();
    if (window == nullptr) {
        return fa::core::errorStatus(platform_error::kGlContextCreationFailed,
                                     "cannot create an OpenGL context before the window exists");
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, desc.majorVersion);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, desc.minorVersion);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        desc.coreProfile ? SDL_GL_CONTEXT_PROFILE_CORE : SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, desc.debugContext ? SDL_GL_CONTEXT_DEBUG_FLAG : 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, desc.depthBuffer ? 24 : 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, desc.stencilBuffer ? 8 : 0);
    // Pixel art is upscaled with nearest sampling; multisampling would blur it
    // along every sprite edge and cannot be undone later in the pipeline.
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);

    m_context = SDL_GL_CreateContext(window);
    if (m_context == nullptr) {
        return fa::core::errorStatus(platform_error::kGlContextCreationFailed,
                                     std::string("SDL_GL_CreateContext failed: ") + SDL_GetError());
    }
    m_contextWindow = window;

    if (!SDL_GL_MakeCurrent(window, m_context)) {
        const std::string message = std::string("SDL_GL_MakeCurrent failed: ") + SDL_GetError();
        destroyContext();
        return fa::core::errorStatus(platform_error::kGlContextCreationFailed, message);
    }

    setSwapInterval(desc.vsync ? 1 : 0);
    return fa::core::okStatus();
}

void SdlGlContext::destroyContext() {
    if (m_context != nullptr) {
        if (m_contextWindow != nullptr) {
            SDL_GL_MakeCurrent(m_contextWindow, nullptr);
        }
        SDL_GL_DestroyContext(m_context);
        m_context = nullptr;
    }
    m_contextWindow = nullptr;
    m_swapInterval = 0;
}

bool SdlGlContext::hasContext() const {
    return m_context != nullptr;
}

bool SdlGlContext::makeCurrent() {
    if (m_context == nullptr || m_contextWindow == nullptr) {
        return false;
    }
    return SDL_GL_MakeCurrent(m_contextWindow, m_context);
}

void SdlGlContext::swapBuffers() {
    if (m_context != nullptr && m_contextWindow != nullptr) {
        SDL_GL_SwapWindow(m_contextWindow);
    }
}

bool SdlGlContext::setSwapInterval(int interval) {
    if (m_context == nullptr) {
        return false;
    }
    if (!SDL_GL_SetSwapInterval(interval)) {
        return false;
    }
    m_swapInterval = interval;
    return true;
}

int SdlGlContext::swapInterval() const {
    return m_swapInterval;
}

void* SdlGlContext::procAddress(std::string_view name) {
    if (m_context == nullptr || name.empty()) {
        return nullptr;
    }
    const std::string terminated(name);
    return reinterpret_cast<void*>(SDL_GL_GetProcAddress(terminated.c_str()));
}

}  // namespace fa::platform::detail
