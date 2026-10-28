/// \file
/// \brief Tests for the app-level GameConfig defaults, overrides and clamping.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/GameConfig.hpp"

TEST_CASE("GameConfig defaults cover window, UI and gameplay", "[config]") {
    const fa::app::GameConfig config = fa::app::GameConfig::defaults();
    CHECK(config.window.width == 1280);
    CHECK(config.window.height == 720);
    CHECK(config.window.minWidth == 1280);
    CHECK(config.window.minHeight == 720);
    CHECK(config.window.resizable);
    CHECK(config.ui.scale == Catch::Approx(1.25f));
    CHECK(config.paths.data == "data");
    CHECK(config.paths.assets == "assets");
    CHECK(config.gameplay.wakeHour == Catch::Approx(8.0f));
    CHECK(config.gameplay.fullSleepHours == Catch::Approx(8.0f));
    CHECK(config.gameplay.hazardDamage == 5);
    CHECK(config.gameplay.autosaveDays == 1);
    CHECK(config.stamina.max == Catch::Approx(100.0f));
    CHECK(config.stamina.regenPerSecond == Catch::Approx(20.0f));
    CHECK(config.stamina.runDrainPerSecond == Catch::Approx(10.0f));
    CHECK(config.stamina.dodgeCost == Catch::Approx(25.0f));
    CHECK(config.stamina.attackCost == Catch::Approx(4.0f));
    CHECK(config.stamina.toolCost == Catch::Approx(3.0f));
}

TEST_CASE("GameConfig overrides individual fields and keeps the rest", "[config]") {
    constexpr std::string_view text = R"({
      "window": {"width": 1600, "height": 900, "min_width": 1280, "min_height": 720},
      "ui": {"scale": 1.5},
      "paths": {"data": "content", "assets": "art"},
      "gameplay": {"wake_hour": 7.0, "hazard_damage": 9},
      "stamina": {"run_drain_per_second": 7.5, "tool_cost": 1.0, "max": 80.0}
    })";

    const fa::core::Result<fa::app::GameConfig> parsed = fa::app::GameConfig::loadFromText(text);
    REQUIRE(parsed.isOk());
    CHECK(parsed.value().window.width == 1600);
    CHECK(parsed.value().window.height == 900);
    CHECK(parsed.value().ui.scale == Catch::Approx(1.5f));
    CHECK(parsed.value().paths.data == "content");
    CHECK(parsed.value().paths.assets == "art");
    CHECK(parsed.value().gameplay.wakeHour == Catch::Approx(7.0f));
    CHECK(parsed.value().gameplay.hazardDamage == 9);
    CHECK(parsed.value().gameplay.fullSleepHours == Catch::Approx(8.0f));
    CHECK(parsed.value().paths.saves == "saves");
    CHECK(parsed.value().stamina.max == Catch::Approx(80.0f));
    CHECK(parsed.value().stamina.runDrainPerSecond == Catch::Approx(7.5f));
    CHECK(parsed.value().stamina.toolCost == Catch::Approx(1.0f));
    CHECK(parsed.value().stamina.attackCost == Catch::Approx(4.0f));
}

TEST_CASE("GameConfig clamps unsafe values", "[config]") {
    constexpr std::string_view text = R"({
      "window": {"width": 10, "height": 10, "min_width": -5, "min_height": -5},
      "ui": {"scale": 99.0},
      "gameplay": {"wake_hour": 99.0, "full_sleep_hours": 0.0,
                   "hazard_damage": -3, "hazard_interval": 0.0,
                   "death_gold_penalty": 2.0, "autosave_days": 0},
      "stamina": {"max": 0.0, "regen_per_second": -1.0,
                  "run_drain_per_second": -2.0, "dodge_cost": 999.0,
                  "attack_cost": 999.0, "tool_cost": 999.0}
    })";

    const fa::core::Result<fa::app::GameConfig> parsed = fa::app::GameConfig::loadFromText(text);
    REQUIRE(parsed.isOk());
    CHECK(parsed.value().window.width >= 320);
    CHECK(parsed.value().window.height >= 180);
    CHECK(parsed.value().window.minWidth == 0);
    CHECK(parsed.value().window.minHeight == 0);
    CHECK(parsed.value().ui.scale == Catch::Approx(3.0f));
    CHECK(parsed.value().gameplay.wakeHour == Catch::Approx(23.0f));
    CHECK(parsed.value().gameplay.fullSleepHours == Catch::Approx(1.0f));
    CHECK(parsed.value().gameplay.hazardDamage == 0);
    CHECK(parsed.value().gameplay.hazardInterval == Catch::Approx(0.1f));
    CHECK(parsed.value().gameplay.deathGoldPenalty == Catch::Approx(1.0f));
    CHECK(parsed.value().gameplay.autosaveDays == 1);
    CHECK(parsed.value().stamina.max == Catch::Approx(1.0f));
    CHECK(parsed.value().stamina.regenPerSecond == Catch::Approx(0.0f));
    CHECK(parsed.value().stamina.runDrainPerSecond == Catch::Approx(0.0f));
    CHECK(parsed.value().stamina.dodgeCost == Catch::Approx(1.0f));
    CHECK(parsed.value().stamina.attackCost == Catch::Approx(1.0f));
    CHECK(parsed.value().stamina.toolCost == Catch::Approx(1.0f));
}

TEST_CASE("GameConfig rejects malformed JSON and non-object roots", "[config]") {
    CHECK(fa::app::GameConfig::loadFromText("{broken").isError());
    CHECK(fa::app::GameConfig::loadFromText("[1,2,3]").isError());
}
