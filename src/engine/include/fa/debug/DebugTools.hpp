#pragma once

#include <cstddef>
#include <string_view>
#include <utility>

#include <fa/core/Math.hpp>
#include <fa/debug/DebugCVar.hpp>
#include <fa/debug/DebugConsole.hpp>
#include <fa/debug/DebugLog.hpp>
#include <fa/debug/DebugOverlay.hpp>
#include <fa/debug/DebugStats.hpp>

namespace fa::platform {
struct PlatformEvent;
}

namespace fa::ui {
class UiContext;
}

namespace fa::debug {

/// \brief Facade that ties the console, CVars, log ring and overlay together.
///
/// The composition root owns one instance in Debug/Development builds. It routes
/// text/key events only while the console is open, so gameplay input is unaffected
/// when the overlay is hidden.
class DebugTools {
public:
    DebugTools();
    ~DebugTools();

    DebugTools(const DebugTools&) = delete;
    DebugTools& operator=(const DebugTools&) = delete;

    [[nodiscard]] DebugConsole& console() noexcept { return m_console; }
    [[nodiscard]] const DebugConsole& console() const noexcept { return m_console; }
    [[nodiscard]] CVarRegistry& cvars() noexcept { return m_cvars; }
    [[nodiscard]] const CVarRegistry& cvars() const noexcept { return m_cvars; }
    [[nodiscard]] DebugLogSink& log() noexcept { return m_logSink; }

    void setStats(const DebugStats& stats) { m_stats = stats; }
    [[nodiscard]] const DebugStats& stats() const noexcept { return m_stats; }

    void setStatsVisible(bool visible) noexcept { m_overlayState.statsVisible = visible; }
    [[nodiscard]] bool statsVisible() const noexcept { return m_overlayState.statsVisible; }
    void toggleStats() noexcept { m_overlayState.statsVisible = !m_overlayState.statsVisible; }

    void setLogVisible(bool visible) noexcept { m_overlayState.logVisible = visible; }
    [[nodiscard]] bool logVisible() const noexcept { return m_overlayState.logVisible; }
    void toggleLog() noexcept { m_overlayState.logVisible = !m_overlayState.logVisible; }

    void setPhysicsVisible(bool visible) noexcept { m_physicsVisible = visible; }
    [[nodiscard]] bool physicsVisible() const noexcept { return m_physicsVisible; }
    void togglePhysics() noexcept { m_physicsVisible = !m_physicsVisible; }

    /// \brief Opens/closes the console; opening prints the registered command list.
    void setConsoleOpen(bool open);
    /// \brief Whether the console currently owns input.
    [[nodiscard]] bool consoleOpen() const noexcept { return m_console.open(); }

    /// \brief Routes one platform event.
    /// \return true when the event was consumed and must not reach gameplay input.
    [[nodiscard]] bool handleEvent(const platform::PlatformEvent& event);

    /// \brief Draws the stats, console and log panels through M09.
    void draw(ui::UiContext& ui, core::Rect viewport) const;

    /// \brief Adds a console command to the embedded console.
    [[nodiscard]] bool registerCommand(ConsoleCommand command);
    /// \brief Hooks the sink into `core::addLogSink` / `removeLogSink`. @{
    void attachLogSink();
    void detachLogSink();
    /// @}

private:
    /// \brief Registers `stats`, `stats.*`, `log.*` and `cvar.*` commands.
    void registerBuiltins();

    DebugConsole m_console;
    CVarRegistry m_cvars;
    DebugLogSink m_logSink;
    /// \brief Bytes the KeyDown ASCII fallback appended last; undone when the real
    /// TextInput for the same key arrives (Chinese IME swallows TextInput, so the
    /// fallback keeps the console typable, while normal layouts dedupe to one char).
    std::size_t m_pendingFallback = 0;
    /// \brief True right after the grave toggle: swallows the backtick TextInput.
    bool m_awaitingOpenText = false;
    DebugOverlay m_overlay;
    DebugStats m_stats;
    DebugOverlayState m_overlayState;
    bool m_physicsVisible = false;
    bool m_logAttached = false;
};

}  // namespace fa::debug
