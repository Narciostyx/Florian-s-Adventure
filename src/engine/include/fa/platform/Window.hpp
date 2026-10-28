#pragma once

#include <string>
#include <string_view>

namespace fa::platform {

/// \brief Window creation parameters.
///
/// \ref width and \ref height are logical (DPI-independent) units; the drawable
/// pixel size is derived by multiplying them with IWindow::dpiScale().
struct WindowDesc {
    std::string title = "Florian's Adventure";
    int width = 640;
    int height = 360;
    /// \brief Minimum logical client size; zero disables the constraint.
    int minWidth = 0;
    int minHeight = 0;
    bool resizable = true;
    bool highDpi = true;
    bool fullscreen = false;
    bool hidden = false;
};

/// \brief A single OS window.
class IWindow {
public:
    virtual ~IWindow() = default;

    /// \brief Replaces the window caption; \p title is UTF-8.
    virtual void setTitle(std::string_view title) = 0;

    /// \brief Requests a new logical client size in DPI-independent units.
    virtual void resize(int width, int height) = 0;

    /// \brief Switches between windowed and borderless fullscreen.
    virtual void setFullscreen(bool enabled) = 0;

    /// \brief Drawable width in physical pixels.
    virtual int width() const = 0;

    /// \brief Drawable height in physical pixels.
    virtual int height() const = 0;

    /// \brief Ratio of physical pixels to logical units; never below 1.0 for a live window.
    virtual float dpiScale() const = 0;

    /// \brief Whether the window is currently fullscreen.
    virtual bool isFullscreen() const = 0;

    /// \brief Native window handle (HWND on Windows); reserved for the graphics backend.
    ///
    /// Returns nullptr when the window is not created. Ownership stays with the
    /// window, and nothing about the handle is ever stored by the engine.
    virtual void* nativeHandle() const = 0;
};

}  // namespace fa::platform
