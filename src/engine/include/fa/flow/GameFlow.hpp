#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fa/core/Result.hpp"
#include "fa/flow/FlowTypes.hpp"
#include "fa/save/SaveTypes.hpp"
#include "fa/world/Calendar.hpp"

namespace fa::save {
class ISaveService;
}

namespace fa::world {
class WorldCalendar;
}

namespace fa::economy {
class EconomyService;
}

namespace fa::narrative {
class NarrativeService;
}

namespace fa::flow {

/// \file
/// Application flow orchestration for M25.
///
/// `GameFlow` owns the state machine and the ordering of cross-module steps. It
/// borrows the save service and gameplay services, and calls composition-root
/// callbacks to build/apply saves and to reset the world. Day-end settlement is
/// synchronous: the clock skips, M24 settles shipping/stock, M23 resets its daily
/// caps, then the flow autosaves when the configured interval has elapsed.

/// \brief Minimal interface used by menus, debug commands and tests.
class IGameFlow {
public:
    virtual ~IGameFlow() = default;

    /// \brief Starts a fresh session in \p slot (main-menu/paused/dialogue states only).
    virtual core::Status startNewGame(std::string_view slot) = 0;
    /// \brief Restores \p slot into a new session.
    virtual core::Status loadGame(std::string_view slot) = 0;
    /// \brief Saves the active session to its own slot.
    virtual core::Status saveGame() = 0;
    /// \brief Requests or clears the pause overlay.
    virtual void requestPause(bool paused) = 0;
    /// \brief Advances one in-game day with settlement and autosave.
    virtual core::Status endDay() = 0;
    /// \brief Signals that the app should leave the frame loop.
    virtual void requestQuit() = 0;
    /// \brief Current state-machine state.
    [[nodiscard]] virtual AppState state() const noexcept = 0;
    /// \brief The live session (slot, day, dirty flag, counters).
    [[nodiscard]] virtual const GameSession& session() const noexcept = 0;
};

/// \brief Concrete M25 state machine and orchestration service.
class GameFlow final : public IGameFlow {
public:
    /// \brief Composition callback types. @{
    using NewGameHandler = std::function<core::Status()>;
    using SaveBuilder = std::function<core::Status(save::SaveContext&)>;
    using LoadHandler = std::function<core::Status(const save::SaveFile&)>;
    using PauseHandler = std::function<void(bool)>;
    /// @}
    /// \brief Runs after the clock advances and before economy/quest/autosave steps.
    using EndOfDayHandler = std::function<void()>;

    GameFlow() = default;
    /// \brief Wires the services the flow drives; any may be null and the matching
    /// step is skipped (a flow without a save service still runs the state machine).
    GameFlow(save::ISaveService* saves,
             world::WorldCalendar* calendar = nullptr,
             economy::EconomyService* economy = nullptr,
             narrative::NarrativeService* narrative = nullptr);

    /// \brief Service setters; safe to re-point during Boot or MainMenu. @{
    void setSaveService(save::ISaveService* saves) noexcept { m_saves = saves; }
    void setCalendar(world::WorldCalendar* calendar) noexcept { m_calendar = calendar; }
    void setEconomy(economy::EconomyService* economy) noexcept { m_economy = economy; }
    void setNarrative(narrative::NarrativeService* narrative) noexcept { m_narrative = narrative; }
    /// @}

    /// \brief Composition callbacks. `newGame` rebuilds the world for a fresh run;
    /// `saveBuilder`/`loadHandler` move it through M14; `pauseHandler` mirrors the
    /// pause into gameplay systems; `endOfDay` runs per-day gameplay steps. @{
    void setNewGameHandler(NewGameHandler handler) { m_newGame = std::move(handler); }
    void setSaveBuilder(SaveBuilder builder) { m_saveBuilder = std::move(builder); }
    void setLoadHandler(LoadHandler handler) { m_loadHandler = std::move(handler); }
    void setPauseHandler(PauseHandler handler) { m_pauseHandler = std::move(handler); }
    void setEndOfDayHandler(EndOfDayHandler handler) { m_endOfDay = std::move(handler); }
    /// @}

    /// \brief Interface methods; semantics documented on \ref IGameFlow. @{
    /// \brief Interface methods; semantics documented on \ref IGameFlow. @{
    core::Status startNewGame(std::string_view slot) override;
    core::Status loadGame(std::string_view slot) override;
    core::Status saveGame() override;
    /// @}
    /// @}
    /// \brief Saves the active session to an explicit manual slot.
    ///
    /// Used by the save-slot menu; the session's own slot is left unchanged.
    core::Status saveToSlot(std::string_view slot);
    /// \brief Pauses or resumes gameplay; the pause handler mirrors it into systems.
    void requestPause(bool paused) override;
    /// \brief Advances one day with settlement and interval autosave.
    core::Status endDay() override;
    /// \brief Requests the frame loop to exit (autosaves first when configured).
    void requestQuit() override;

    [[nodiscard]] AppState state() const noexcept override { return m_state; }
    [[nodiscard]] const GameSession& session() const noexcept override { return m_session; }

    /// \brief Applies an externally observed day change without skipping the clock.
    core::Status onDayChanged();

    /// \brief Leaves the current session and shows the main menu.
    core::Status enterMainMenu();

    /// \brief Opens/closes a dialogue overlay; gameplay and the clock pause.
    core::Status enterDialogue();
    /// \brief Leaves the dialogue overlay and resumes the previous state.
    core::Status exitDialogue();

    /// \brief Enters the death screen and pauses the clock.
    core::Status requestGameOver(std::string_view reason = {});
    /// \brief Leaves the death screen and resumes play.
    core::Status respawn();

    /// \brief Accumulates session play time while playing.
    void update(double realDeltaSeconds);

    /// \brief Marks the session as changed since the last successful save.
    void markDirty() noexcept { m_session.dirty = true; }

    /// \brief Hour the calendar resumes at after a sleep.
    ///
    /// Defaults to `world::kStartHour`; the play session sets it to
    /// `flow::kWakeHour` (08:00).
    void setDayStartHour(float hour) noexcept { m_dayStartHour = hour; }
    [[nodiscard]] float dayStartHour() const noexcept { return m_dayStartHour; }

    /// \brief Autosave switches and the day cadence. @{
    void setAutosaveEnabled(bool enabled) noexcept { m_autosaveEnabled = enabled; }
    [[nodiscard]] bool autosaveEnabled() const noexcept { return m_autosaveEnabled; }
    /// \brief Days between autosaves; clamped to at least 1.
    void setAutosaveIntervalDays(int days) noexcept;
    [[nodiscard]] int autosaveIntervalDays() const noexcept { return m_session.autosaveIntervalDays; }
    void setAutosaveOnQuit(bool enabled) noexcept { m_autosaveOnQuit = enabled; }
    [[nodiscard]] bool autosaveOnQuit() const noexcept { return m_autosaveOnQuit; }
    /// @}

    /// \brief Days elapsed since the last successful autosave.
    [[nodiscard]] int daysSinceAutosave() const noexcept { return m_daysSinceAutosave; }
    /// \brief Whether the session changed since the last successful save.
    [[nodiscard]] bool dirty() const noexcept { return m_session.dirty; }

    /// \brief Event ring for menus, logs and tests. @{
    [[nodiscard]] std::span<const FlowEvent> events() const noexcept { return m_events; }
    void clearEvents() noexcept { m_events.clear(); }
    /// \brief Cumulative counters (new games, loads, saves, autosaves, transitions).
    [[nodiscard]] FlowStats stats() const;
    /// @}

    /// \brief Returns to `Boot` and clears session/events/statistics.
    void reset();

    /// \brief Letters, digits, `_` and `-`, length 1..64; delegates to the save
    /// service, which owns the grammar because it builds the file path.
    [[nodiscard]] static bool isValidSlotName(std::string_view slot) noexcept;

private:
    /// \brief Moves to \p next when the transition table allows it, recording the
    /// event and applying pause side effects.
    [[nodiscard]] core::Status transition(AppState next, std::string_view message = {});
    /// \brief Pure transition-table lookup.
    [[nodiscard]] bool canTransition(AppState from, AppState to) const noexcept;
    /// \brief Notifies the pause handler when entering or leaving a paused state.
    void applyPauseState(AppState state);
    /// \brief Shared manual/autosave body: build context, write, update counters.
    [[nodiscard]] core::Status saveToSlot(std::string_view slot, bool autosave);
    /// \brief Runs one day boundary; \p skipClock false advances the calendar.
    [[nodiscard]] core::Status settleDay(bool skipClock);
    /// \brief Today's number from the calendar, or the session day without one.
    [[nodiscard]] int currentDay() const noexcept;

    /// \brief Appends one event to the ring (bounded).
    void record(FlowEventKind kind, std::string_view slot = {}, int day = 0, std::string_view message = {});

    save::ISaveService* m_saves = nullptr;
    world::WorldCalendar* m_calendar = nullptr;
    economy::EconomyService* m_economy = nullptr;
    narrative::NarrativeService* m_narrative = nullptr;

    NewGameHandler m_newGame;
    SaveBuilder m_saveBuilder;
    LoadHandler m_loadHandler;
    PauseHandler m_pauseHandler;
    EndOfDayHandler m_endOfDay;

    AppState m_state = AppState::Boot;
    GameSession m_session;
    int m_day = 1;
    int m_daysSinceAutosave = 0;
    float m_dayStartHour = world::kStartHour;
    bool m_autosaveEnabled = true;
    bool m_autosaveOnQuit = false;
    std::vector<FlowEvent> m_events;
    FlowStats m_totals;
};

}  // namespace fa::flow
