/// \file
/// \brief Tests for M25: state transitions, new game/load, pause, day settlement,
/// autosave policy, death/respawn and quit handling.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "fa/economy/EconomyService.hpp"
#include "fa/flow/GameFlow.hpp"
#include "fa/flow/Sleep.hpp"
#include "fa/save/SaveError.hpp"
#include "fa/save/SaveService.hpp"
#include "fa/save/SaveTypes.hpp"
#include "fa/world/Calendar.hpp"

namespace {

using fa::core::Status;
using fa::economy::EconomyService;
using fa::flow::AppState;
namespace flow_error = fa::flow::flow_error;
using fa::flow::FlowEventKind;
using fa::flow::GameFlow;
using fa::save::ISaveService;
using fa::save::SaveContext;
using fa::save::SaveFile;
using fa::save::SaveMetadata;
using fa::world::WorldCalendar;

/// \brief In-memory save service so flow tests never touch the disk.
class FakeSaveService final : public ISaveService {
public:
    [[nodiscard]] Status save(std::string_view slot, const SaveContext& context) override {
        ++saveCalls;
        if (failSave) {
            return fa::core::errorStatus(fa::save::save_error::kIoFailure, "forced save failure");
        }
        slots[std::string(slot)] = context;
        return fa::core::okStatus();
    }

    [[nodiscard]] fa::core::Result<SaveFile> load(std::string_view slot) override {
        ++loadCalls;
        if (failLoad) {
            return fa::core::Result<SaveFile>::err(fa::save::save_error::kIoFailure, "forced load failure");
        }
        const auto found = slots.find(std::string(slot));
        if (found == slots.end()) {
            return fa::core::Result<SaveFile>::err(fa::save::save_error::kSlotNotFound, "slot is missing");
        }

        SaveFile file;
        file.meta.saveVersion = fa::save::kSaveVersion;
        file.meta.slotName = std::string(slot);
        file.meta.inGameDay = found->second.inGameDay;
        file.meta.playSeconds = found->second.playSeconds;
        file.world = found->second.world;
        file.player = found->second.player;
        file.inventories = found->second.inventories;
        file.quests = found->second.quests;
        file.economy = found->second.economy;
        return fa::core::Result<SaveFile>::ok(std::move(file));
    }

    void apply(const SaveFile&, SaveContext&) override {}

    [[nodiscard]] std::vector<SaveMetadata> listSlots() override {
        std::vector<SaveMetadata> result;
        for (const auto& [name, context] : slots) {
            SaveMetadata meta;
            meta.slotName = name;
            meta.inGameDay = context.inGameDay;
            result.push_back(std::move(meta));
        }
        return result;
    }

    [[nodiscard]] Status deleteSlot(std::string_view slot) override {
        slots.erase(std::string(slot));
        return fa::core::okStatus();
    }

    std::map<std::string, SaveContext> slots;
    int saveCalls = 0;
    int loadCalls = 0;
    bool failSave = false;
    bool failLoad = false;
};

SaveContext makeContext(int day, double seconds) {
    SaveContext context;
    context.inGameDay = day;
    context.playSeconds = seconds;
    context.world.setInt("probe", 7);
    return context;
}

}  // namespace

TEST_CASE("Game flow boots, starts a new game and validates transitions", "[flow]") {
    WorldCalendar calendar(20261018ULL);
    GameFlow flow(nullptr, &calendar);

    CHECK(flow.state() == AppState::Boot);
    CHECK(flow.stats().state == AppState::Boot);

    CHECK(flow.startNewGame("bad slot").error().code == flow_error::kInvalidSlot);
    CHECK(flow.state() == AppState::Boot);

    REQUIRE(flow.startNewGame("slot1").isOk());
    CHECK(flow.state() == AppState::Playing);
    CHECK(flow.session().slot == "slot1");
    CHECK(flow.dirty());
    CHECK_FALSE(calendar.paused());
    CHECK(flow.stats().newGames == 1);

    flow.update(1.5);
    CHECK(flow.session().playSeconds == Catch::Approx(1.5));

    CHECK(flow.startNewGame("slot2").error().code == flow_error::kAlreadyInGame);

    flow.requestPause(true);
    CHECK(flow.state() == AppState::Paused);
    CHECK(calendar.paused());
    CHECK(flow.stats().pauses == 1);

    flow.requestPause(false);
    CHECK(flow.state() == AppState::Playing);
    CHECK_FALSE(calendar.paused());
    CHECK(flow.stats().resumes == 1);

    REQUIRE(flow.enterDialogue().isOk());
    CHECK(flow.state() == AppState::Dialogue);
    CHECK(calendar.paused());
    CHECK(flow.stats().dialogues == 1);

    REQUIRE(flow.exitDialogue().isOk());
    CHECK(flow.state() == AppState::Playing);
    CHECK_FALSE(calendar.paused());

    REQUIRE(flow.requestGameOver("hp").isOk());
    CHECK(flow.state() == AppState::GameOver);
    CHECK(calendar.paused());
    CHECK(flow.stats().deaths == 1);

    REQUIRE(flow.respawn().isOk());
    CHECK(flow.state() == AppState::Playing);
    CHECK_FALSE(calendar.paused());
    CHECK(flow.stats().respawns == 1);

    REQUIRE(flow.enterMainMenu().isOk());
    CHECK(flow.state() == AppState::MainMenu);
    CHECK(flow.session().slot.empty());
    CHECK(flow.stats().menuReturns == 1);

    flow.requestQuit();
    CHECK(flow.state() == AppState::Quit);
    CHECK(flow.stats().quitRequests == 1);
    CHECK(flow.startNewGame("slot1").error().code == flow_error::kInvalidTransition);
}

TEST_CASE("New game setup failure returns to the main menu", "[flow]") {
    GameFlow flow;
    flow.setNewGameHandler([] { return fa::core::errorStatus(9999, "world setup failed"); });

    const Status result = flow.startNewGame("slot1");
    CHECK(result.error().code == flow_error::kNewGameFailed);
    CHECK(flow.state() == AppState::MainMenu);
    CHECK(flow.session().slot.empty());
    CHECK(flow.stats().newGames == 0);

    bool sawFailure = false;
    for (const auto& event : flow.events()) {
        if (event.kind == FlowEventKind::StateChanged && event.to == AppState::MainMenu) {
            sawFailure = event.message == "new game setup failed";
        }
    }
    CHECK(sawFailure);
}

TEST_CASE("Saving and loading use the builder and apply handler", "[flow]") {
    FakeSaveService saves;
    GameFlow flow(&saves);

    flow.setSaveBuilder([](SaveContext& context) {
        context = makeContext(4, 12.5);
        return fa::core::okStatus();
    });

    bool applied = false;
    flow.setLoadHandler([&applied](const SaveFile& file) {
        int probe = 0;
        applied = file.world.getInt("probe", probe) && probe == 7;
        return fa::core::okStatus();
    });

    REQUIRE(flow.startNewGame("slot1").isOk());
    flow.update(3.0);
    REQUIRE(flow.saveGame().isOk());
    CHECK_FALSE(flow.dirty());
    CHECK(flow.stats().saves == 1);
    CHECK(saves.saveCalls == 1);
    REQUIRE(saves.slots.count("slot1") == 1);
    CHECK(saves.slots.at("slot1").slotName == "slot1");
    CHECK(saves.slots.at("slot1").inGameDay == 4);

    flow.markDirty();
    saves.failSave = true;
    CHECK(flow.saveGame().error().code == flow_error::kSaveFailed);
    CHECK(flow.dirty());
    CHECK(flow.stats().saveFailures == 1);
    saves.failSave = false;

    REQUIRE(flow.enterMainMenu().isOk());
    REQUIRE(flow.loadGame("slot1").isOk());
    CHECK(flow.state() == AppState::Playing);
    CHECK(flow.session().slot == "slot1");
    CHECK(flow.session().playSeconds == Catch::Approx(12.5));
    CHECK_FALSE(flow.dirty());
    CHECK(flow.stats().loads == 1);
    CHECK(applied);

    // GameOver may load directly; this is the death panel's "Load autosave" path.
    REQUIRE(flow.requestGameOver("hp").isOk());
    CHECK(flow.state() == AppState::GameOver);
    REQUIRE(flow.loadGame("slot1").isOk());
    CHECK(flow.state() == AppState::Playing);
    CHECK(flow.stats().loads == 2);

    REQUIRE(flow.enterMainMenu().isOk());
    saves.failLoad = true;
    CHECK(flow.loadGame("slot1").error().code == fa::save::save_error::kIoFailure);
    CHECK(flow.state() == AppState::MainMenu);
    CHECK(flow.stats().loadFailures == 1);
    saves.failLoad = false;

    flow.setLoadHandler([](const SaveFile&) { return fa::core::errorStatus(9998, "apply failed"); });
    CHECK(flow.loadGame("slot1").error().code == flow_error::kLoadHandlerFailed);
    CHECK(flow.state() == AppState::MainMenu);
    CHECK(flow.stats().loadFailures == 2);
}

TEST_CASE("Explicit slot save writes the requested slot during play", "[flow]") {
    FakeSaveService saves;
    GameFlow flow(&saves);
    flow.setSaveBuilder([](SaveContext& context) {
        context.inGameDay = 3;
        return fa::core::okStatus();
    });

    REQUIRE(flow.startNewGame("slot1").isOk());
    REQUIRE(flow.saveToSlot("slot2").isOk());
    CHECK(saves.slots.count("slot2") == 1);
    CHECK(flow.session().slot == "slot1");

    flow.requestPause(true);
    REQUIRE(flow.saveToSlot("slot3").isOk());
    CHECK(saves.slots.count("slot3") == 1);

    REQUIRE(flow.enterMainMenu().isOk());
    CHECK(flow.saveToSlot("slot1").error().code == flow_error::kNotPlaying);
}

TEST_CASE("Save and load reject missing services, sessions and bad slots", "[flow]") {
    GameFlow empty;
    CHECK(empty.saveGame().error().code == flow_error::kSaveUnavailable);
    CHECK(empty.loadGame("slot1").error().code == flow_error::kSaveUnavailable);

    FakeSaveService saves;
    GameFlow flow(&saves);
    CHECK(flow.saveGame().error().code == flow_error::kNoSession);
    CHECK(flow.loadGame("bad slot").error().code == flow_error::kInvalidSlot);
    CHECK(flow.loadGame(std::string(65, 'a')).error().code == flow_error::kInvalidSlot);
    CHECK(flow.startNewGame("slot1").isOk());
    CHECK(flow.loadGame("slot2").error().code == flow_error::kAlreadyInGame);
}

TEST_CASE("Day end advances the calendar, settles economy and autosaves", "[flow]") {
    FakeSaveService saves;
    WorldCalendar calendar(20261018ULL);
    EconomyService economy;
    GameFlow flow(&saves, &calendar, &economy);

    flow.setAutosaveIntervalDays(1);
    flow.setSaveBuilder([&calendar](SaveContext& context) {
        context.inGameDay = calendar.date().day;
        return fa::core::okStatus();
    });

    REQUIRE(flow.startNewGame("slot1").isOk());
    REQUIRE(flow.endDay().isOk());
    CHECK(calendar.date().day == 2);
    CHECK(calendar.timeOfDay() == Catch::Approx(8.0f));
    CHECK(flow.state() == AppState::Playing);
    CHECK(flow.stats().dayEnds == 1);
    CHECK(flow.stats().daysSinceAutosave == 0);
    CHECK(flow.stats().autosaves == 1);
    CHECK(saves.saveCalls == 1);
    CHECK(saves.slots.count("autosave") == 1);
    CHECK(saves.slots.at("autosave").inGameDay == 2);
    CHECK_FALSE(flow.dirty());
    CHECK(economy.events().size() == 1);

    flow.setAutosaveIntervalDays(2);
    REQUIRE(flow.endDay().isOk());
    CHECK(calendar.date().day == 3);
    CHECK(flow.stats().daysSinceAutosave == 1);
    CHECK(flow.stats().autosaves == 1);

    REQUIRE(flow.endDay().isOk());
    CHECK(calendar.date().day == 4);
    CHECK(flow.stats().daysSinceAutosave == 0);
    CHECK(flow.stats().autosaves == 2);
    CHECK(saves.slots.at("autosave").inGameDay == 4);

    flow.setAutosaveEnabled(false);
    REQUIRE(flow.endDay().isOk());
    CHECK(calendar.date().day == 5);
    CHECK(flow.stats().autosaves == 2);

    flow.setAutosaveEnabled(true);
    flow.setAutosaveIntervalDays(1);
    REQUIRE(flow.onDayChanged().isOk());
    CHECK(calendar.date().day == 5);
    CHECK(flow.stats().autosaves == 3);

    REQUIRE(flow.enterMainMenu().isOk());
    CHECK(flow.endDay().error().code == flow_error::kNotPlaying);
}

TEST_CASE("Configurable day start hour resumes the clock after the rollover", "[flow]") {
    FakeSaveService saves;
    WorldCalendar calendar(20261018ULL);
    GameFlow flow(&saves, &calendar);

    flow.setDayStartHour(9.0f);
    flow.setSaveBuilder([&calendar](SaveContext& context) {
        context.inGameDay = calendar.date().day;
        return fa::core::okStatus();
    });

    REQUIRE(flow.startNewGame("slot1").isOk());
    REQUIRE(flow.endDay().isOk());
    CHECK(calendar.date().day == 2);
    CHECK(calendar.timeOfDay() == Catch::Approx(9.0f));
    CHECK(calendar.hour() == 9);
    REQUIRE(saves.slots.count("autosave") == 1);
    CHECK(saves.slots.at("autosave").inGameDay == 2);

    // An earlier wake hour also has to move the clock backwards from the default.
    flow.setDayStartHour(7.0f);
    REQUIRE(flow.endDay().isOk());
    CHECK(calendar.timeOfDay() == Catch::Approx(7.0f));
    CHECK(calendar.hour() == 7);
}

TEST_CASE("End-of-day hook runs after the clock and before autosave", "[flow]") {
    FakeSaveService saves;
    WorldCalendar calendar(20261018ULL);
    GameFlow flow(&saves, &calendar);

    int calls = 0;
    int observedDay = 0;
    int observedHour = -1;
    flow.setAutosaveIntervalDays(1);
    flow.setEndOfDayHandler([&] {
        ++calls;
        observedDay = calendar.date().day;
        observedHour = calendar.hour();
    });
    flow.setSaveBuilder([&calendar, &observedDay](SaveContext& context) {
        context.inGameDay = calendar.date().day;
        context.world.setInt("hookDay", observedDay);
        return fa::core::okStatus();
    });

    REQUIRE(flow.startNewGame("slot1").isOk());
    REQUIRE(flow.endDay().isOk());

    CHECK(calls == 1);
    CHECK(observedDay == 2);
    CHECK(observedHour == 8);
    REQUIRE(saves.slots.count("autosave") == 1);
    int hookDay = 0;
    CHECK(saves.slots.at("autosave").world.getInt("hookDay", hookDay));
    CHECK(hookDay == 2);

    REQUIRE(flow.onDayChanged().isOk());
    CHECK(calls == 2);

    REQUIRE(flow.enterMainMenu().isOk());
    CHECK(flow.endDay().error().code == flow_error::kNotPlaying);
    CHECK(calls == 2);
}

TEST_CASE("Sleep wraps to the wake hour and scales stamina recovery", "[flow]") {
    using fa::flow::sleepHoursUntil;
    using fa::flow::sleepRecovery;

    CHECK(sleepHoursUntil(0.0f) == Catch::Approx(8.0f));
    CHECK(sleepHoursUntil(1.0f) == Catch::Approx(7.0f));
    CHECK(sleepHoursUntil(4.5f) == Catch::Approx(3.5f));
    CHECK(sleepHoursUntil(7.0f) == Catch::Approx(1.0f));
    CHECK(sleepHoursUntil(8.0f) == Catch::Approx(24.0f));
    CHECK(sleepHoursUntil(22.0f) == Catch::Approx(10.0f));
    CHECK(sleepHoursUntil(23.0f) == Catch::Approx(9.0f));

    CHECK(sleepRecovery(8.0f) == Catch::Approx(1.0f));
    CHECK(sleepRecovery(12.0f) == Catch::Approx(1.0f));
    CHECK(sleepRecovery(7.0f) == Catch::Approx(0.765625f));
    CHECK(sleepRecovery(4.0f) == Catch::Approx(0.25f));
    CHECK(sleepRecovery(1.0f) == Catch::Approx(0.015625f));
    CHECK(sleepRecovery(0.1f) == Catch::Approx(fa::flow::kMinSleepRecovery));
    CHECK(sleepRecovery(0.0f) == Catch::Approx(fa::flow::kMinSleepRecovery));
}

TEST_CASE("Autosave on quit writes the automatic slot", "[flow]") {
    FakeSaveService saves;
    GameFlow flow(&saves);
    flow.setAutosaveOnQuit(true);
    flow.setSaveBuilder([](SaveContext& context) {
        context.inGameDay = 9;
        return fa::core::okStatus();
    });

    REQUIRE(flow.startNewGame("slot1").isOk());
    flow.markDirty();
    flow.requestQuit();
    CHECK(flow.state() == AppState::Quit);
    CHECK(saves.slots.count("autosave") == 1);
    CHECK(flow.stats().autosaves == 1);
    CHECK(flow.stats().quitRequests == 1);

    flow.reset();
    CHECK(flow.state() == AppState::Boot);
    flow.setAutosaveOnQuit(false);
    REQUIRE(flow.startNewGame("slot2").isOk());
    flow.markDirty();
    flow.requestQuit();
    CHECK(flow.state() == AppState::Quit);
    CHECK(saves.saveCalls == 1);
    CHECK(flow.stats().autosaves == 0);
}
