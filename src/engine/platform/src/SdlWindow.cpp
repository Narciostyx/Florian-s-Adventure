#include "SdlWindow.hpp"

#include <string>

#include <SDL3/SDL.h>

namespace fa::platform::detail {
namespace {

SDL_WindowFlags buildFlags(const WindowDesc& desc) {
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL;
    if (desc.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (desc.highDpi) {
        flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    }
    if (desc.fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }
    if (desc.hidden) {
        flags |= SDL_WINDOW_HIDDEN;
    }
    return flags;
}

}  // namespace

SdlWindow::~SdlWindow() {
    destroy();
}

bool SdlWindow::create(const WindowDesc& desc) {
    destroy();

    const std::string title = desc.title.empty() ? std::string("Florian's Adventure") : desc.title;
    m_window = SDL_CreateWindow(title.c_str(), desc.width, desc.height, buildFlags(desc));
    if (m_window != nullptr && desc.minWidth > 0 && desc.minHeight > 0) {
        static_cast<void>(SDL_SetWindowMinimumSize(m_window, desc.minWidth, desc.minHeight));
    }
    return m_window != nullptr;
}

void SdlWindow::destroy() {
    if (m_window != nullptr) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
}

void SdlWindow::setTitle(std::string_view title) {
    if (m_window == nullptr) {
        return;
    }
    const std::string text(title);
    SDL_SetWindowTitle(m_window, text.c_str());
}

void SdlWindow::resize(int width, int height) {
    if (m_window != nullptr) {
        SDL_SetWindowSize(m_window, width, height);
    }
}

void SdlWindow::setFullscreen(bool enabled) {
    if (m_window != nullptr) {
        SDL_SetWindowFullscreen(m_window, enabled);
    }
}

int SdlWindow::width() const {
    int pixels = 0;
    int ignoredHeight = 0;
    if (m_window == nullptr || !SDL_GetWindowSizeInPixels(m_window, &pixels, &ignoredHeight)) {
        return 0;
    }
    return pixels;
}

int SdlWindow::height() const {
    int ignoredWidth = 0;
    int pixels = 0;
    if (m_window == nullptr || !SDL_GetWindowSizeInPixels(m_window, &ignoredWidth, &pixels)) {
        return 0;
    }
    return pixels;
}

float SdlWindow::dpiScale() const {
    if (m_window == nullptr) {
        return 1.0f;
    }
    const float density = SDL_GetWindowPixelDensity(m_window);
    return density > 0.0f ? density : 1.0f;
}

bool SdlWindow::isFullscreen() const {
    if (m_window == nullptr) {
        return false;
    }
    return (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN) != 0;
}

void* SdlWindow::nativeHandle() const {
    if (m_window == nullptr) {
        return nullptr;
    }
    return SDL_GetPointerProperty(SDL_GetWindowProperties(m_window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
}

}  // namespace fa::platform::detail
