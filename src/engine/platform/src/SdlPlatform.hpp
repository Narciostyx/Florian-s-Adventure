#pragma once

#include <vector>

#include <fa/platform/Platform.hpp>

#include "SdlWindow.hpp"
#include "StdFileSystem.hpp"

namespace fa::platform::detail {

/// \brief IPlatform backed by SDL3 windows, SDL3 events and the standard file system.
///
/// The instance owns the SDL global initialization: every SdlPlatform assumes it
/// is the only one driving SDL_Init/SDL_Quit, so at most one may be alive at a
/// time.
class SdlPlatform final : public IPlatform {
public:
    SdlPlatform() = default;
    ~SdlPlatform() override;

    SdlPlatform(const SdlPlatform&) = delete;
    SdlPlatform& operator=(const SdlPlatform&) = delete;

    fa::core::Status initialize(const WindowDesc& desc) override;
    void shutdown() override;
    bool pumpEvents(std::vector<PlatformEvent>& outEvents) override;
    bool quitRequested() const override;
    IWindow& window() override;
    IFileSystem& files() override;
    double monotonicSeconds() const override;
    fa::core::Status showMessageBox(MessageBoxKind kind, std::string_view title,
                                    std::string_view message) override;

private:
    SdlWindow m_window;
    StdFileSystem m_files;
    bool m_initialized = false;
    bool m_quitRequested = false;
};

}  // namespace fa::platform::detail
