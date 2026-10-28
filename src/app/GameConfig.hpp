#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include <fa/core/Result.hpp>
#include <fa/save/SaveArchive.hpp>

namespace fa::app {

/// \file
/// App-level configuration loaded from `config/game.json`.
///
/// Every field keeps a sane default, so a missing file, missing section or a
/// malformed value only logs a warning and leaves the rest of the game usable.
/// The gameplay block is intentionally generic so later schedule/content work can
/// add fields without touching the loader call sites.

struct WindowConfig {
    int width = 1280;
    int height = 720;
    int minWidth = 1280;
    int minHeight = 720;
    bool resizable = true;
    bool vsync = true;
    bool highDpi = true;
};

struct UiConfig {
    /// \brief Multiplier applied to the 640x360 UI baseline.
    float scale = 1.25f;
};

struct PathsConfig {
    std::string data = "data";
    std::string assets = "assets";
    std::string saves = "saves";
    std::string config = "config/game.json";
};

struct GameplayConfig {
    float wakeHour = 8.0f;
    float fullSleepHours = 8.0f;
    int hazardDamage = 5;
    float hazardInterval = 0.7f;
    float deathGoldPenalty = 0.10f;
    int autosaveDays = 1;
};

/// \brief Player stamina tuning; every value can be overridden from JSON.
struct StaminaConfig {
    float max = 100.0f;
    float regenPerSecond = 20.0f;
    float runDrainPerSecond = 10.0f;
    float dodgeCost = 25.0f;
    float attackCost = 4.0f;
    float toolCost = 3.0f;
};

struct GameConfig {
    WindowConfig window;
    UiConfig ui;
    PathsConfig paths;
    GameplayConfig gameplay;
    StaminaConfig stamina;

    [[nodiscard]] static GameConfig defaults() noexcept { return GameConfig{}; }

    /// \brief Parses a config document; absent fields keep their defaults.
    [[nodiscard]] static core::Result<GameConfig> loadFromText(std::string_view text) {
        core::Result<save::SaveArchive> parsed = save::SaveArchive::parse(text);
        if (parsed.isError()) {
            return core::Result<GameConfig>::err(parsed.error().code, parsed.error().message);
        }
        const save::SaveArchive root = std::move(parsed).take();
        if (!root.isObject()) {
            return core::Result<GameConfig>::err(core::error_code::kInvalidArgument, "config root must be an object");
        }

        GameConfig config;
        const save::SaveArchive window = root.child("window");
        static_cast<void>(window.getInt("width", config.window.width));
        static_cast<void>(window.getInt("height", config.window.height));
        static_cast<void>(window.getInt("min_width", config.window.minWidth));
        static_cast<void>(window.getInt("min_height", config.window.minHeight));
        static_cast<void>(window.getBool("resizable", config.window.resizable));
        static_cast<void>(window.getBool("vsync", config.window.vsync));
        static_cast<void>(window.getBool("high_dpi", config.window.highDpi));

        const save::SaveArchive ui = root.child("ui");
        static_cast<void>(ui.getFloat("scale", config.ui.scale));

        const save::SaveArchive paths = root.child("paths");
        static_cast<void>(paths.getString("data", config.paths.data));
        static_cast<void>(paths.getString("assets", config.paths.assets));
        static_cast<void>(paths.getString("saves", config.paths.saves));
        static_cast<void>(paths.getString("config", config.paths.config));

        const save::SaveArchive gameplay = root.child("gameplay");
        static_cast<void>(gameplay.getFloat("wake_hour", config.gameplay.wakeHour));
        static_cast<void>(gameplay.getFloat("full_sleep_hours", config.gameplay.fullSleepHours));
        static_cast<void>(gameplay.getInt("hazard_damage", config.gameplay.hazardDamage));
        static_cast<void>(gameplay.getFloat("hazard_interval", config.gameplay.hazardInterval));
        static_cast<void>(gameplay.getFloat("death_gold_penalty", config.gameplay.deathGoldPenalty));
        static_cast<void>(gameplay.getInt("autosave_days", config.gameplay.autosaveDays));

        const save::SaveArchive stamina = root.child("stamina");
        static_cast<void>(stamina.getFloat("max", config.stamina.max));
        static_cast<void>(stamina.getFloat("regen_per_second", config.stamina.regenPerSecond));
        static_cast<void>(stamina.getFloat("run_drain_per_second", config.stamina.runDrainPerSecond));
        static_cast<void>(stamina.getFloat("dodge_cost", config.stamina.dodgeCost));
        static_cast<void>(stamina.getFloat("attack_cost", config.stamina.attackCost));
        static_cast<void>(stamina.getFloat("tool_cost", config.stamina.toolCost));

        config.window.width = std::max(320, config.window.width);
        config.window.height = std::max(180, config.window.height);
        config.window.minWidth = std::max(0, config.window.minWidth);
        config.window.minHeight = std::max(0, config.window.minHeight);
        config.ui.scale = std::clamp(config.ui.scale, 0.5f, 3.0f);
        config.gameplay.wakeHour = std::clamp(config.gameplay.wakeHour, 0.0f, 23.0f);
        config.gameplay.fullSleepHours = std::clamp(config.gameplay.fullSleepHours, 1.0f, 24.0f);
        config.gameplay.hazardDamage = std::max(0, config.gameplay.hazardDamage);
        config.gameplay.hazardInterval = std::clamp(config.gameplay.hazardInterval, 0.1f, 10.0f);
        config.gameplay.deathGoldPenalty = std::clamp(config.gameplay.deathGoldPenalty, 0.0f, 1.0f);
        config.gameplay.autosaveDays = std::max(1, config.gameplay.autosaveDays);
        config.stamina.max = std::clamp(config.stamina.max, 1.0f, 1000.0f);
        config.stamina.regenPerSecond = std::clamp(config.stamina.regenPerSecond, 0.0f, 1000.0f);
        config.stamina.runDrainPerSecond = std::clamp(config.stamina.runDrainPerSecond, 0.0f, 1000.0f);
        config.stamina.dodgeCost = std::clamp(config.stamina.dodgeCost, 0.0f, config.stamina.max);
        config.stamina.attackCost = std::clamp(config.stamina.attackCost, 0.0f, config.stamina.max);
        config.stamina.toolCost = std::clamp(config.stamina.toolCost, 0.0f, config.stamina.max);
        return core::Result<GameConfig>::ok(std::move(config));
    }
};

}  // namespace fa::app
