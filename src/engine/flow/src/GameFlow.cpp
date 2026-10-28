#include "fa/flow/GameFlow.hpp"

#include <algorithm>
#include <utility>

#include "fa/economy/EconomyService.hpp"
#include "fa/narrative/NarrativeService.hpp"
#include "fa/save/SaveService.hpp"
#include "fa/world/Calendar.hpp"

namespace fa::flow {

std::string_view toString(AppState state) noexcept {
    switch (state) {
        case AppState::Boot:
            return "boot";
        case AppState::MainMenu:
            return "main-menu";
        case AppState::Loading:
            return "loading";
        case AppState::Playing:
            return "playing";
        case AppState::Paused:
            return "paused";
        case AppState::Dialogue:
            return "dialogue";
        case AppState::GameOver:
            return "game-over";
        case AppState::Quit:
            return "quit";
        case AppState::Count:
            break;
    }
    return "unknown";
}

std::string_view toString(FlowEventKind kind) noexcept {
    switch (kind) {
        case FlowEventKind::StateChanged:
            return "state-changed";
        case FlowEventKind::NewGame:
            return "new-game";
        case FlowEventKind::GameLoaded:
            return "game-loaded";
        case FlowEventKind::LoadFailed:
            return "load-failed";
        case FlowEventKind::SaveFailed:
            return "save-failed";
        case FlowEventKind::Saved:
            return "saved";
        case FlowEventKind::Autosaved:
            return "autosaved";
        case FlowEventKind::DayEnded:
            return "day-ended";
        case FlowEventKind::GameOver:
            return "game-over";
        case FlowEventKind::Respawned:
            return "respawned";
        case FlowEventKind::MenuReturned:
            return "menu-returned";
        case FlowEventKind::QuitRequested:
            return "quit-requested";
        case FlowEventKind::Count:
            break;
    }
    return "unknown";
}

GameFlow::GameFlow(save::ISaveService* saves,
                   world::WorldCalendar* calendar,
                   economy::EconomyService* economy,
                   narrative::NarrativeService* narrative)
    : m_saves(saves), m_calendar(calendar), m_economy(economy), m_narrative(narrative) {
    m_session.autosaveIntervalDays = 1;
}

void GameFlow::setAutosaveIntervalDays(int days) noexcept {
    m_session.autosaveIntervalDays = std::max(1, days);
}

bool GameFlow::isValidSlotName(std::string_view slot) noexcept {
    if (slot.empty() || slot.size() > 64u) {
        return false;
    }
    for (const char value : slot) {
        const bool valid = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
                           (value >= '0' && value <= '9') || value == '_' || value == '-';
        if (!valid) {
            return false;
        }
    }
    return true;
}

core::Status GameFlow::startNewGame(std::string_view slot) {
    if (m_state == AppState::Playing || m_state == AppState::Paused || m_state == AppState::Dialogue) {
        return core::errorStatus(flow_error::kAlreadyInGame, "a game session is already active");
    }
    if (!isValidSlotName(slot)) {
        return core::errorStatus(flow_error::kInvalidSlot, "invalid save slot name");
    }

    const int interval = std::max(1, m_session.autosaveIntervalDays);
    if (const core::Status loading = transition(AppState::Loading, "new game"); loading.isError()) {
        return loading;
    }

    m_session = GameSession{};
    m_session.slot.assign(slot);
    m_session.dirty = true;
    m_session.autosaveIntervalDays = interval;
    m_day = 1;
    m_daysSinceAutosave = 0;

    if (m_newGame) {
        const core::Status setup = m_newGame();
        if (setup.isError()) {
            static_cast<void>(transition(AppState::MainMenu, "new game setup failed"));
            m_session = GameSession{};
            m_session.autosaveIntervalDays = interval;
            return core::errorStatus(flow_error::kNewGameFailed, setup.error().message);
        }
    }

    const core::Status playing = transition(AppState::Playing, "new game");
    if (playing.isError()) {
        return playing;
    }
    ++m_totals.newGames;
    record(FlowEventKind::NewGame, slot, currentDay());
    return core::okStatus();
}

core::Status GameFlow::loadGame(std::string_view slot) {
    if (m_saves == nullptr) {
        return core::errorStatus(flow_error::kSaveUnavailable, "save service is not available");
    }
    if (!isValidSlotName(slot)) {
        return core::errorStatus(flow_error::kInvalidSlot, "invalid save slot name");
    }
    if (m_state == AppState::Playing || m_state == AppState::Paused || m_state == AppState::Dialogue) {
        return core::errorStatus(flow_error::kAlreadyInGame, "return to the main menu before loading");
    }

    const int interval = std::max(1, m_session.autosaveIntervalDays);
    if (const core::Status loading = transition(AppState::Loading, "load game"); loading.isError()) {
        return loading;
    }

    const core::Result<save::SaveFile> file = m_saves->load(slot);
    if (file.isError()) {
        ++m_totals.loadFailures;
        record(FlowEventKind::LoadFailed, slot, currentDay(), file.error().message);
        static_cast<void>(transition(AppState::MainMenu, "load failed"));
        m_session = GameSession{};
        m_session.autosaveIntervalDays = interval;
        return core::Status::err(file.error());
    }

    if (m_loadHandler) {
        const core::Status applied = m_loadHandler(file.value());
        if (applied.isError()) {
            ++m_totals.loadFailures;
            record(FlowEventKind::LoadFailed, slot, currentDay(), applied.error().message);
            static_cast<void>(transition(AppState::MainMenu, "load apply failed"));
            m_session = GameSession{};
            m_session.autosaveIntervalDays = interval;
            return core::errorStatus(flow_error::kLoadHandlerFailed, applied.error().message);
        }
    }

    m_session = GameSession{};
    m_session.slot.assign(slot);
    m_session.playSeconds = file.value().meta.playSeconds;
    m_session.dirty = false;
    m_session.autosaveIntervalDays = interval;
    if (file.value().meta.inGameDay > 0) {
        m_day = file.value().meta.inGameDay;
    }
    m_daysSinceAutosave = 0;

    const core::Status playing = transition(AppState::Playing, "load game");
    if (playing.isError()) {
        return playing;
    }
    ++m_totals.loads;
    record(FlowEventKind::GameLoaded, slot, currentDay());
    return core::okStatus();
}

core::Status GameFlow::saveGame() {
    if (m_saves == nullptr) {
        return core::errorStatus(flow_error::kSaveUnavailable, "save service is not available");
    }
    if (m_session.slot.empty()) {
        return core::errorStatus(flow_error::kNoSession, "no active game session");
    }
    if (m_state != AppState::Playing && m_state != AppState::Paused && m_state != AppState::Dialogue) {
        return core::errorStatus(flow_error::kNotPlaying, "saving is only available during play");
    }
    return saveToSlot(m_session.slot, false);
}

core::Status GameFlow::saveToSlot(std::string_view slot) {
    if (m_state != AppState::Playing && m_state != AppState::Paused && m_state != AppState::Dialogue) {
        return core::errorStatus(flow_error::kNotPlaying, "saving is only available during play");
    }
    return saveToSlot(slot, false);
}

core::Status GameFlow::saveToSlot(std::string_view slot, bool autosave) {
    if (m_saves == nullptr) {
        return core::errorStatus(flow_error::kSaveUnavailable, "save service is not available");
    }
    if (!isValidSlotName(slot)) {
        return core::errorStatus(flow_error::kInvalidSlot, "invalid save slot name");
    }
    if (!m_saveBuilder) {
        return core::errorStatus(flow_error::kSaveFailed, "no save context builder is registered");
    }

    save::SaveContext context;
    const core::Status built = m_saveBuilder(context);
    if (built.isError()) {
        ++m_totals.saveFailures;
        record(FlowEventKind::SaveFailed, slot, currentDay(), built.error().message);
        return core::errorStatus(flow_error::kSaveFailed, built.error().message);
    }
    context.slotName.assign(slot);

    const core::Status saved = m_saves->save(slot, context);
    if (saved.isError()) {
        ++m_totals.saveFailures;
        record(FlowEventKind::SaveFailed, slot, currentDay(), saved.error().message);
        return core::errorStatus(flow_error::kSaveFailed, saved.error().message);
    }

    m_session.dirty = false;
    m_daysSinceAutosave = 0;
    if (autosave) {
        ++m_totals.autosaves;
        record(FlowEventKind::Autosaved, slot, currentDay());
    } else {
        ++m_totals.saves;
        record(FlowEventKind::Saved, slot, currentDay());
    }
    return core::okStatus();
}

void GameFlow::requestPause(bool paused) {
    if (paused) {
        if (m_state != AppState::Playing) {
            return;
        }
        if (transition(AppState::Paused, "pause").isOk()) {
            ++m_totals.pauses;
        }
        return;
    }

    if (m_state != AppState::Paused) {
        return;
    }
    if (transition(AppState::Playing, "resume").isOk()) {
        ++m_totals.resumes;
    }
}

core::Status GameFlow::endDay() {
    return settleDay(true);
}

core::Status GameFlow::onDayChanged() {
    return settleDay(false);
}

core::Status GameFlow::settleDay(bool skipClock) {
    if (m_state != AppState::Playing && m_state != AppState::Paused && m_state != AppState::Dialogue) {
        return core::errorStatus(flow_error::kNotPlaying, "day settlement needs an active session");
    }

    if (skipClock) {
        if (m_calendar != nullptr) {
            m_calendar->skipToNextDay();
            if (m_dayStartHour != world::kStartHour) {
                // Sleep resumes later than the calendar's default start; restore the
                // clock before the day handlers and autosave so the save agrees.
                m_calendar->restore(m_calendar->date(), m_dayStartHour, m_calendar->today(), m_calendar->tomorrow());
            }
        } else {
            ++m_day;
        }
    }
    m_day = currentDay();

    // M19 farming growth runs after the clock has rolled the new date/weather,
    // and before M24/M23/autosave so the save contains the grown state.
    if (m_endOfDay) {
        m_endOfDay();
    }

    if (m_economy != nullptr) {
        static_cast<void>(m_economy->onDayChanged());
    }
    if (m_narrative != nullptr) {
        m_narrative->onDayChanged();
    }

    m_session.dirty = true;
    ++m_daysSinceAutosave;
    ++m_totals.dayEnds;
    record(FlowEventKind::DayEnded, m_session.slot, currentDay());

    const core::Status resumed = transition(AppState::Playing, "day end");
    if (resumed.isError()) {
        return resumed;
    }

    if (m_autosaveEnabled && m_daysSinceAutosave >= m_session.autosaveIntervalDays) {
        const core::Status saved = saveToSlot(kAutosaveSlot, true);
        if (saved.isError()) {
            return saved;
        }
    }
    return core::okStatus();
}

core::Status GameFlow::enterMainMenu() {
    if (m_state == AppState::MainMenu) {
        return core::okStatus();
    }
    if (!canTransition(m_state, AppState::MainMenu)) {
        return core::errorStatus(flow_error::kInvalidTransition, "cannot return to the main menu now");
    }

    const bool fromBoot = m_state == AppState::Boot;
    const int interval = std::max(1, m_session.autosaveIntervalDays);
    const core::Status moved = transition(AppState::MainMenu, "main menu");
    if (moved.isError()) {
        return moved;
    }

    m_session = GameSession{};
    m_session.autosaveIntervalDays = interval;
    m_day = 1;
    m_daysSinceAutosave = 0;
    if (!fromBoot) {
        ++m_totals.menuReturns;
        record(FlowEventKind::MenuReturned);
    }
    return core::okStatus();
}

core::Status GameFlow::enterDialogue() {
    if (m_state != AppState::Playing) {
        return core::errorStatus(flow_error::kNotPlaying, "dialogue needs a playing session");
    }
    const core::Status moved = transition(AppState::Dialogue, "dialogue");
    if (moved.isOk()) {
        ++m_totals.dialogues;
    }
    return moved;
}

core::Status GameFlow::exitDialogue() {
    if (m_state != AppState::Dialogue) {
        return core::errorStatus(flow_error::kNotPlaying, "no dialogue is active");
    }
    return transition(AppState::Playing, "dialogue end");
}

core::Status GameFlow::requestGameOver(std::string_view reason) {
    if (m_state != AppState::Playing && m_state != AppState::Paused && m_state != AppState::Dialogue) {
        return core::errorStatus(flow_error::kNotPlaying, "game over needs an active session");
    }
    const core::Status moved = transition(AppState::GameOver, reason);
    if (moved.isOk()) {
        ++m_totals.deaths;
        record(FlowEventKind::GameOver, m_session.slot, currentDay(), reason);
    }
    return moved;
}

core::Status GameFlow::respawn() {
    if (m_state != AppState::GameOver) {
        return core::errorStatus(flow_error::kNotPlaying, "respawn needs the game over state");
    }
    const core::Status moved = transition(AppState::Playing, "respawn");
    if (moved.isOk()) {
        ++m_totals.respawns;
        m_session.dirty = true;
        record(FlowEventKind::Respawned, m_session.slot, currentDay());
    }
    return moved;
}

void GameFlow::update(double realDeltaSeconds) {
    if (m_state == AppState::Playing && realDeltaSeconds > 0.0) {
        m_session.playSeconds += realDeltaSeconds;
    }
}

void GameFlow::requestQuit() {
    if (m_state == AppState::Quit) {
        return;
    }
    if (m_autosaveOnQuit && m_session.dirty && !m_session.slot.empty() && m_saves != nullptr) {
        static_cast<void>(saveToSlot(kAutosaveSlot, true));
    }
    if (transition(AppState::Quit, "quit").isOk()) {
        ++m_totals.quitRequests;
        record(FlowEventKind::QuitRequested, m_session.slot, currentDay());
    }
}

FlowStats GameFlow::stats() const {
    FlowStats result = m_totals;
    result.state = m_state;
    result.slot = m_session.slot;
    result.dirty = m_session.dirty;
    result.playSeconds = m_session.playSeconds;
    result.day = currentDay();
    result.daysSinceAutosave = m_daysSinceAutosave;
    result.autosaveIntervalDays = m_session.autosaveIntervalDays;
    result.autosaveEnabled = m_autosaveEnabled;
    result.events = m_events.size();
    return result;
}

void GameFlow::reset() {
    m_state = AppState::Boot;
    m_session = GameSession{};
    m_day = 1;
    m_daysSinceAutosave = 0;
    m_events.clear();
    m_totals = FlowStats{};
    applyPauseState(m_state);
}

int GameFlow::currentDay() const noexcept {
    return m_calendar != nullptr ? m_calendar->date().day : m_day;
}

core::Status GameFlow::transition(AppState next, std::string_view message) {
    if (next == m_state) {
        return core::okStatus();
    }
    if (!canTransition(m_state, next)) {
        return core::errorStatus(flow_error::kInvalidTransition,
                                 std::string("cannot transition from ") + std::string(toString(m_state)) + " to " +
                                     std::string(toString(next)));
    }

    const AppState from = m_state;
    m_state = next;
    applyPauseState(next);
    ++m_totals.transitions;

    FlowEvent event;
    event.kind = FlowEventKind::StateChanged;
    event.from = from;
    event.to = next;
    event.day = currentDay();
    event.message.assign(message);
    m_events.push_back(std::move(event));
    return core::okStatus();
}

bool GameFlow::canTransition(AppState from, AppState to) const noexcept {
    if (from == to || from == AppState::Quit) {
        return false;
    }
    if (to == AppState::Quit) {
        return true;
    }
    switch (to) {
        case AppState::MainMenu:
            return from == AppState::Boot || from == AppState::Loading || from == AppState::Playing ||
                   from == AppState::Paused || from == AppState::Dialogue || from == AppState::GameOver;
        case AppState::Loading:
            return from == AppState::Boot || from == AppState::MainMenu || from == AppState::GameOver;
        case AppState::Playing:
            return from == AppState::Loading || from == AppState::Paused || from == AppState::Dialogue ||
                   from == AppState::GameOver;
        case AppState::Paused:
            return from == AppState::Playing;
        case AppState::Dialogue:
            return from == AppState::Playing;
        case AppState::GameOver:
            return from == AppState::Playing || from == AppState::Paused || from == AppState::Dialogue;
        case AppState::Boot:
        case AppState::Count:
            return false;
    }
    return false;
}

void GameFlow::applyPauseState(AppState state) {
    const bool paused = state != AppState::Playing;
    if (m_calendar != nullptr) {
        m_calendar->setPaused(paused);
    }
    if (m_pauseHandler) {
        m_pauseHandler(paused);
    }
}

void GameFlow::record(FlowEventKind kind, std::string_view slot, int day, std::string_view message) {
    FlowEvent event;
    event.kind = kind;
    event.from = m_state;
    event.to = m_state;
    event.slot.assign(slot);
    event.day = day;
    event.message.assign(message);
    m_events.push_back(std::move(event));
}

}  // namespace fa::flow
