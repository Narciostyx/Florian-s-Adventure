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

    virtual core::Status startNewGame(std::string_view slot) = 0;
    virtual core::Status loadGame(std::string_view slot) = 0;
    virtual core::Status saveGame() = 0;
    virtual void requestPause(bool paused) = 0;
    virtual core::Status endDay() = 0;
    virtual void requestQuit() = 0;
    [[nodiscard]] virtual AppState state() const noexcept = 0;
    [[nodiscard]] virtual const GameSession& session() const noexcept = 0;
};

/// \brief Concrete M25 state machine and orchestration service.
class GameFlow final : public IGameFlow {
public:
    using NewGameHandler = std::function<core::Status()>;
    using SaveBuilder = std::function<core::Status(save::SaveContext&)>;
    using LoadHandler = std::function<core::Status(const save::SaveFile&)>;
    using PauseHandler = std::function<void(bool)>;
    /// \brief Runs after the clock advances and before economy/quest/autosave steps.
    using EndOfDayHandler = std::function<void()>;

    GameFlow() = default;
    GameFlow(save::ISaveService* saves,
             world::WorldCalendar* calendar = nullptr,
             economy::EconomyService* economy = nullptr,
             narrative::NarrativeService* narrative = nullptr);

    void setSaveService(save::ISaveService* saves) noexcept { m_saves = saves; }
    void setCalendar(world::WorldCalendar* calendar) noexcept { m_calendar = calendar; }
    void setEconomy(economy::EconomyService* economy) noexcept { m_economy = economy; }
    void setNarrative(narrative::NarrativeService* narrative) noexcept { m_narrative = narrative; }

    void setNewGameHandler(NewGameHandler handler) { m_newGame = std::move(handler); }
    void setSaveBuilder(SaveBuilder builder) { m_saveBuilder = std::move(builder); }
    void setLoadHandler(LoadHandler handler) { m_loadHandler = std::move(handler); }
    void setPauseHandler(PauseHandler handler) { m_pauseHandler = std::move(handler); }
    void setEndOfDayHandler(EndOfDayHandler handler) { m_endOfDay = std::move(handler); }

    core::Status startNewGame(std::string_view slot) override;
    core::Status loadGame(std::string_view slot) override;
    core::Status saveGame() override;
    /// \brief Saves the active session to an explicit manual slot.
    ///
    /// Used by the save-slot menu; the session's own slot is left unchanged.
    core::Status saveToSlot(std::string_view slot);
    void requestPause(bool paused) override;
    core::Status endDay() override;
    void requestQuit() override;

    [[nodiscard]] AppState state() const noexcept override { return m_state; }
    [[nodiscard]] const GameSession& session() const noexcept override { return m_session; }

    /// \brief Applies an externally observed day change without skipping the clock.
    core::Status onDayChanged();

    /// \brief Leaves the current session and shows the main menu.
    core::Status enterMainMenu();

    /// \brief Opens/closes a dialogue overlay; gameplay and the clock pause.
    core::Status enterDialogue();
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

    void setAutosaveEnabled(bool enabled) noexcept { m_autosaveEnabled = enabled; }
    [[nodiscard]] bool autosaveEnabled() const noexcept { return m_autosaveEnabled; }
    void setAutosaveIntervalDays(int days) noexcept;
    [[nodiscard]] int autosaveIntervalDays() const noexcept { return m_session.autosaveIntervalDays; }
    void setAutosaveOnQuit(bool enabled) noexcept { m_autosaveOnQuit = enabled; }
    [[nodiscard]] bool autosaveOnQuit() const noexcept { return m_autosaveOnQuit; }

    [[nodiscard]] int daysSinceAutosave() const noexcept { return m_daysSinceAutosave; }
    [[nodiscard]] bool dirty() const noexcept { return m_session.dirty; }

    [[nodiscard]] std::span<const FlowEvent> events() const noexcept { return m_events; }
    void clearEvents() noexcept { m_events.clear(); }
    [[nodiscard]] FlowStats stats() const;

    /// \brief Returns to `Boot` and clears session/events/statistics.
    void reset();

    /// \brief Letters, digits, `_` and `-`, length 1..64 (same rule as M14).
    [[nodiscard]] static bool isValidSlotName(std::string_view slot) noexcept;

private:
    [[nodiscard]] core::Status transition(AppState next, std::string_view message = {});
    [[nodiscard]] bool canTransition(AppState from, AppState to) const noexcept;
    void applyPauseState(AppState state);
    [[nodiscard]] core::Status saveToSlot(std::string_view slot, bool autosave);
    [[nodiscard]] core::Status settleDay(bool skipClock);
    [[nodiscard]] int currentDay() const noexcept;

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
