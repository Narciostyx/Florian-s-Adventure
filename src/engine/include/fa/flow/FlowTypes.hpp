#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace fa::flow {

/// \file
/// Value types for M25: application states, session data, events and statistics.
///
/// The flow layer only orchestrates other modules. It never owns gameplay rules,
/// save formatting or UI widgets; the composition root supplies the callbacks that
/// build and apply saves, reset the world and react to pause changes.

namespace flow_error {

/// \brief M25 owns the 7400 block.
inline constexpr int kNotInitialized = 7400;
inline constexpr int kInvalidTransition = 7401;
inline constexpr int kNoSession = 7402;
inline constexpr int kSaveUnavailable = 7403;
inline constexpr int kLoadFailed = 7404;
inline constexpr int kNewGameFailed = 7405;
inline constexpr int kSaveFailed = 7406;
inline constexpr int kLoadHandlerFailed = 7407;
inline constexpr int kInvalidSlot = 7408;
inline constexpr int kNotPlaying = 7409;
inline constexpr int kAlreadyInGame = 7410;

}  // namespace flow_error

/// \brief Automatic slot; manual slots are `slot1` through `slot3`.
inline constexpr std::string_view kAutosaveSlot = "autosave";
/// \brief Number of manual slots exposed by the main menu.
inline constexpr int kManualSlotCount = 3;

/// \brief 1-based manual slot name, e.g. `slot1`; out-of-range indices still format.
[[nodiscard]] inline std::string manualSlotName(int index) {
    return "slot" + std::to_string(index);
}

/// \brief Application-level state. Values are stable for logs and tests.
enum class AppState : std::uint8_t {
    Boot = 0,
    MainMenu,
    Loading,
    Playing,
    Paused,
    Dialogue,
    GameOver,
    Quit,
    Count,
};

/// \brief Stable name for logs, HUD and tests.
[[nodiscard]] std::string_view toString(AppState state) noexcept;

/// \brief One save session. `dirty` tracks changes since the last successful save.
struct GameSession {
    std::string slot;
    double playSeconds = 0.0;
    bool dirty = false;
    int autosaveIntervalDays = 1;
};

/// \brief What one recorded flow transition was about.
enum class FlowEventKind : std::uint8_t {
    StateChanged = 0,
    NewGame,
    GameLoaded,
    LoadFailed,
    SaveFailed,
    Saved,
    Autosaved,
    DayEnded,
    GameOver,
    Respawned,
    MenuReturned,
    QuitRequested,
    Count,
};

/// \brief Stable name for logs and tests.
[[nodiscard]] std::string_view toString(FlowEventKind kind) noexcept;

/// \brief One recorded flow transition or outcome.
struct FlowEvent {
    FlowEventKind kind = FlowEventKind::StateChanged;
    AppState from = AppState::Boot;
    AppState to = AppState::Boot;
    std::string slot;
    int day = 0;
    std::string message;
};

/// \brief Counters for the smoke summary and M27.
struct FlowStats {
    AppState state = AppState::Boot;
    std::string slot;
    bool dirty = false;
    double playSeconds = 0.0;
    int day = 1;
    int daysSinceAutosave = 0;
    int autosaveIntervalDays = 1;
    bool autosaveEnabled = true;
    std::uint64_t transitions = 0;
    std::uint64_t newGames = 0;
    std::uint64_t loads = 0;
    std::uint64_t loadFailures = 0;
    std::uint64_t saves = 0;
    std::uint64_t autosaves = 0;
    std::uint64_t saveFailures = 0;
    std::uint64_t dayEnds = 0;
    std::uint64_t pauses = 0;
    std::uint64_t resumes = 0;
    std::uint64_t dialogues = 0;
    std::uint64_t deaths = 0;
    std::uint64_t respawns = 0;
    std::uint64_t menuReturns = 0;
    std::uint64_t quitRequests = 0;
    std::size_t events = 0;
};

/// \brief True for states where gameplay simulation should run.
[[nodiscard]] constexpr bool isGameplayState(AppState state) noexcept {
    return state == AppState::Playing;
}

}  // namespace fa::flow
