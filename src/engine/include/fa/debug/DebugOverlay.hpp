#pragma once

#include <fa/core/Math.hpp>

namespace fa::ui {
class UiContext;
}

namespace fa::debug {

struct DebugStats;
class DebugConsole;
class DebugLogSink;

/// \brief Which optional panels the overlay draws.
struct DebugOverlayState {
    bool statsVisible = true;
    bool logVisible = false;
};

/// \brief Draws the stats, console and log panels through M09.
///
/// The overlay owns no font or texture: the caller supplies a `UiContext` whose
/// origin is already set to the visible viewport, exactly like the normal HUD.
class DebugOverlay {
public:
    void draw(ui::UiContext& ui,
              core::Rect viewport,
              const DebugStats& stats,
              const DebugConsole& console,
              const DebugLogSink& log,
              const DebugOverlayState& state) const;
};

}  // namespace fa::debug
