#pragma once

#include <fa/platform/Window.hpp>

struct SDL_Window;

namespace fa::platform::detail {

/// \brief IWindow backed by an SDL3 window.
///
/// Window flags are fixed at creation time; every later mutation goes through
/// the IWindow methods.
class SdlWindow final : public IWindow {
public:
    SdlWindow() = default;
    ~SdlWindow() override;

    SdlWindow(const SdlWindow&) = delete;
    SdlWindow& operator=(const SdlWindow&) = delete;

    /// \brief Creates the native window, replacing any window held before.
    /// \return false when the platform refused to create it.
    bool create(const WindowDesc& desc);

    /// \brief Destroys the native window if one exists.
    void destroy();

    void setTitle(std::string_view title) override;
    void resize(int width, int height) override;
    void setFullscreen(bool enabled) override;
    int width() const override;
    int height() const override;
    float dpiScale() const override;
    bool isFullscreen() const override;
    void* nativeHandle() const override;

    /// \brief The underlying handle, or nullptr when the window is not created.
    SDL_Window* raw() const noexcept { return m_window; }

private:
    SDL_Window* m_window = nullptr;
};

}  // namespace fa::platform::detail
