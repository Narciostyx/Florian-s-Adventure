/// \file
/// \brief Composition root: the only translation unit that knows about every engine module.
///
/// Wires M02 (platform), M03 (events/jobs), M04 (frame loop), M06 (render) and
/// M07 (input) so window, event, timing, drawing and input behave end to end.

#include "GameConfig.hpp"
#include "LogToFile.hpp"
#include "StbImageDecoder.hpp"
#include "UiFontAtlas.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <fa/anim/AnimationClip.hpp>
#include <fa/anim/AnimationService.hpp>
#include <fa/anim/AnimationStateMachine.hpp>
#include <fa/anim/Animator.hpp>
#include <fa/assets/AssetCatalog.hpp>
#include <fa/assets/AssetError.hpp>
#include <fa/assets/AssetHandle.hpp>
#include <fa/assets/AssetId.hpp>
#include <fa/assets/AssetManager.hpp>
#include <fa/assets/AssetTypes.hpp>
#include <fa/assets/ITextureUploader.hpp>
#include <fa/assets/ImageData.hpp>
#include <fa/audio/AudioClip.hpp>
#include <fa/audio/AudioEngine.hpp>
#include <fa/audio/AudioTypes.hpp>
#include <fa/combat/CombatService.hpp>
#include <fa/combat/CombatTypes.hpp>
#if defined(FA_ENABLE_DEBUG_TOOLS)
#include <fa/debug/DebugCVar.hpp>
#include <fa/debug/DebugConsole.hpp>
#include <fa/debug/DebugDraw.hpp>
#include <fa/debug/DebugInspector.hpp>
#include <fa/debug/DebugLog.hpp>
#include <fa/debug/DebugStats.hpp>
#include <fa/debug/DebugTools.hpp>
#endif
#include <fa/ai/AiService.hpp>
#include <fa/ai/AiTypes.hpp>
#include <fa/content/ContentDatabase.hpp>
#include <fa/core/Math.hpp>
#include <fa/core/Result.hpp>
#include <fa/core/Rng.hpp>
#include <fa/core/Version.hpp>
#include <fa/economy/EconomyService.hpp>
#include <fa/economy/ShopDatabase.hpp>
#include <fa/flow/GameFlow.hpp>
#include <fa/flow/Sleep.hpp>
#if defined(FA_ENABLE_EDITOR)
#include <fa/editor/EditorOverlay.hpp>
#include <fa/editor/VisualEditor.hpp>
#endif
#include <fa/events/EventBus.hpp>
#include <fa/events/JobSystem.hpp>
#include <fa/farming/FarmingService.hpp>
#include <fa/farming/FarmingTools.hpp>
#include <fa/farming/FarmingTypes.hpp>
#include <fa/farming/FarmingWorld.hpp>
#include <fa/input/Input.hpp>
#include <fa/interaction/Interactable.hpp>
#include <fa/interaction/InteractionService.hpp>
#include <fa/interaction/PlacementGrid.hpp>
#include <fa/interaction/ToolHandler.hpp>
#include <fa/inventory/Equipment.hpp>
#include <fa/inventory/Hotbar.hpp>
#include <fa/inventory/Inventory.hpp>
#include <fa/inventory/InventoryError.hpp>
#include <fa/inventory/ItemStack.hpp>
#include <fa/inventory/ItemUseHandler.hpp>
#include <fa/inventory/ItemUseService.hpp>
#include <fa/inventory/PlayerInventory.hpp>
#include <fa/narrative/NarrativeDatabase.hpp>
#include <fa/narrative/NarrativeService.hpp>
#include <fa/physics/Collider.hpp>
#include <fa/physics/CollisionLayer.hpp>
#include <fa/physics/PhysicsSystem.hpp>
#include <fa/physics/PhysicsWorld.hpp>
#include <fa/physics/TileCollision.hpp>
#include <fa/platform/FileSystem.hpp>
#include <fa/platform/Platform.hpp>
#include <fa/platform/PlatformError.hpp>
#include <fa/player/PlayerController.hpp>
#include <fa/player/PlayerError.hpp>
#include <fa/player/PlayerSystem.hpp>
#include <fa/player/PlayerTypes.hpp>
#include <fa/render/IRenderer.hpp>
#include <fa/render/RenderError.hpp>
#include <fa/render/RenderQueue.hpp>
#include <fa/render/RenderTypes.hpp>
#include <fa/save/SaveArchive.hpp>
#include <fa/save/SaveError.hpp>
#include <fa/save/SaveService.hpp>
#include <fa/save/SaveTypes.hpp>
#include <fa/scene/CommandBuffer.hpp>
#include <fa/scene/Components.hpp>
#include <fa/scene/EntityId.hpp>
#include <fa/scene/Scene.hpp>
#include <fa/scene/SceneStack.hpp>
#include <fa/scene/SpriteExtractor.hpp>
#include <fa/scene/System.hpp>
#include <fa/scene/World.hpp>
#include <fa/tilemap/Tilemap.hpp>
#include <fa/tilemap/TilemapExtract.hpp>
#include <fa/tilemap/TilemapLoader.hpp>
#include <fa/time/FrameContext.hpp>
#include <fa/time/FrameLoop.hpp>
#include <fa/ui/Font.hpp>
#include <fa/ui/Text.hpp>
#include <fa/ui/Ui.hpp>
#include <fa/world/Calendar.hpp>
#include <fa/world/Lighting.hpp>

namespace {

struct Options {
    /// A visible window is opt-in for the automation modes so CI never blocks; the player
    /// modes (`--play`, `--debug`) turn it on by default.
    bool visibleWindow = false;
    /// 0 means "run until the user closes the window".
    int frames = 0;
    /// \brief Playable session rather than the headless scripted smoke.
    bool playMode = false;
    /// \brief `--debug`: the playable session plus debug overlays and a verbose log file.
    bool debugMode = false;
    /// \brief Runs the headless play-loop self check instead of waiting for input.
    bool playTest = false;
    /// \brief Optional window-size override applied after `config/game.json`.
    int width = 0;
    int height = 0;
    /// \brief Simulated soak duration in seconds (60 fixed steps per second).
    int soakSeconds = 0;
    /// \brief Sprite-only stress scene size; 0 disables `--stress`.
    int stressSprites = 0;
    /// \brief `--help` / `-h`: print the option reference and exit before any init.
    bool showHelp = false;

    /// \brief Modes a human plays: windowed by default and the only ones that write a log file.
    [[nodiscard]] bool isPlayerMode() const noexcept { return playMode && !playTest; }
};

/// \brief Prints the command-line reference (also used by `--help`).
void printUsage() {
    std::printf("usage: fa_app [options]\n"
                "  (no options)        headless scripted smoke: runs a few frames, prints the\n"
                "                      m02..m27 module report, exits\n"
                "  --help, -h          show this help and exit\n"
                "  --play              playable session, no debug info: window opens, WASD move,\n"
                "                      1-9 hotbar, wheel cycle, LMB/G use, RMB sword qi, E teleport\n"
                "  --debug             --play plus the debug toolset: console (`), F3 stats, F4 log,\n"
                "                      F5 collision boxes, F6 cursor marker; overlays on at startup,\n"
                "                      Debug-level logging\n"
                "  --play-test         headless play-loop self check: drives the real play mode\n"
                "                      through new game, farming, combat, portals, death, saves\n"
                "  --window            force a visible window even in automation modes\n"
                "  --frames N          stop after N frames (smoke/soak/stress run length)\n"
                "  --resolution WxH    override the window size from config/game.json (e.g. 1920x1080)\n"
                "  --soak SECONDS      soak test: run SECONDS*60 fixed steps with periodic\n"
                "                      save/load cycles and check for entity/texture leaks\n"
                "  --stress N          stress test: headless scene with N sprites for 600 frames\n"
                "                      (p50/p99 frame-time report)\n");
}

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            options.showHelp = true;
        } else if (arg == "--window") {
            options.visibleWindow = true;
        } else if (arg == "--play") {
            options.playMode = true;
        } else if (arg == "--debug") {
            options.playMode = true;
            options.debugMode = true;
        } else if (arg == "--play-test") {
            options.playMode = true;
            options.playTest = true;
        } else if (arg == "--frames" && i + 1 < argc) {
            options.frames = std::atoi(argv[++i]);
        } else if (arg == "--resolution" && i + 1 < argc) {
            const char* text = argv[++i];
            char* separator = nullptr;
            const long width = std::strtol(text, &separator, 10);
            const long height = separator != text && (*separator == 'x' || *separator == 'X')
                                    ? std::strtol(separator + 1, nullptr, 10)
                                    : 0;
            if (width > 0 && height > 0) {
                options.width = static_cast<int>(width);
                options.height = static_cast<int>(height);
            } else {
                std::printf("--resolution expects WIDTHxHEIGHT (e.g. 1920x1080)\n");
            }
        } else if (arg == "--soak" && i + 1 < argc) {
            options.soakSeconds = std::atoi(argv[++i]);
        } else if (arg == "--stress" && i + 1 < argc) {
            options.stressSprites = std::atoi(argv[++i]);
        }
    }
    if (options.soakSeconds > 0 && options.frames <= 0) {
        options.frames = options.soakSeconds * 60;
    }
    if (options.stressSprites > 0 && options.frames <= 0) {
        options.frames = 600;
    }
    // The player modes are the game, so they open a window; the automation modes
    // (`--play-test`, `--frames`, `--soak`, `--stress`) stay hidden so CI never blocks on
    // a display, and `--window` still forces a window for any of them.
    if (options.isPlayerMode()) {
        options.visibleWindow = true;
    }
    // A visible window without an explicit mode is the game, not the headless scripted
    // smoke. Keeping this implicit avoids the old "window + drifting marker + sealed showcase
    // map" trap; `--frames` without `--window` still runs the scripted smoke.
    if (options.visibleWindow && !options.playMode && !options.playTest) {
        options.playMode = true;
    }
    return options;
}

void printBanner() {
    const std::string_view version = fa::core::engineVersion();
    std::printf("Florian's Adventure - engine v%.*s\n", static_cast<int>(version.size()), version.data());
}

/// \brief The whole M05 catalog for the smoke run: coloured blocks, no image files.
///
/// Every texture in v1 is generated from `color`/`altColor`, so the run needs no art
/// on disk; the moment real PNGs arrive the same entries gain a `path` and the
/// placeholder fields stop being used.
/// \brief Built-in fallback catalog used when `data/catalog.json` is missing.
///
/// \note DUAL SOURCE BY DESIGN: this is the *minimal* catalog so a bare checkout
/// still boots (old entries render as color blocks, newer ones point at PNGs).
/// `data/catalog.json` is the authoritative list. When adding content that code
/// paths load by name, update BOTH copies or the fallback run silently loses it.
constexpr std::string_view kCatalogText = R"({
  "assets": [
    {"id": "tex.ground.grass", "kind": "texture", "size": [16, 16], "tile": 8,
     "color": "#4a8250", "altColor": "#5c8a42"},
    {"id": "tex.ground.soil", "kind": "texture", "size": [16, 16], "tile": 4,
     "color": "#6b4a2f", "altColor": "#7d5a3a"},
    {"id": "tex.ground.wall", "kind": "texture", "size": [16, 16], "tile": 4,
     "color": "#3d465e", "altColor": "#55627e"},
    {"id": "tex.actor.player", "kind": "texture", "path": "textures/characters/player.png",
     "size": [96, 24], "color": "#e8c39e", "altColor": "#c79a74"},
    {"id": "tex.actor.player_walk", "kind": "texture", "path": "textures/characters/player_walk.png",
     "size": [96, 96], "color": "#e8c39e", "altColor": "#c79a74"},
    {"id": "tex.actor.player_boat", "kind": "texture", "path": "textures/characters/player_boat.png",
     "size": [128, 96], "color": "#8a5a2b", "altColor": "#3d6fa5"},
    {"id": "tex.enemy.slime_walk", "kind": "texture", "path": "textures/characters/slime_walk.png",
     "size": [64, 16], "color": "#4fa84a", "altColor": "#2f7a36"},
    {"id": "tex.enemy.bat_walk", "kind": "texture", "path": "textures/characters/bat_walk.png",
     "size": [64, 16], "color": "#4e3e6e", "altColor": "#947cbe"},
    {"id": "tex.marker", "kind": "texture", "size": [8, 8], "color": "#ffffff"},
    {"id": "tex.ui.menu_bg", "kind": "texture", "path": "textures/ui/menu_bg.png", "size": [640, 360],
     "color": "#3d5c3a", "altColor": "#e8a33d"},
    {"id": "tex.ui.menu_button", "kind": "texture", "path": "textures/ui/menu_button.png", "size": [112, 24],
     "color": "#8a5a2b", "altColor": "#e6c47a"},
    {"id": "tex.ground.water", "kind": "texture", "path": "textures/tiles/water.png", "size": [16, 16],
     "tile": 4, "color": "#3d6fa5", "altColor": "#5c8ec4"},
    {"id": "tex.ground.flower", "kind": "texture", "path": "textures/tiles/flower.png", "size": [16, 16],
     "tile": 4, "color": "#69a86a", "altColor": "#e06fa0"},
    {"id": "tex.ground.grass_tuft", "kind": "texture", "path": "textures/tiles/grass_tuft.png", "size": [16, 16],
     "tile": 4, "color": "#5c8a42", "altColor": "#3f7d32"},
    {"id": "tex.ground.tree", "kind": "texture", "path": "textures/tiles/tree.png", "size": [16, 48],
     "tile": 4, "color": "#2f6a35", "altColor": "#4a8250"},
    {"id": "tex.ground.stone", "kind": "texture", "path": "textures/tiles/stone.png", "size": [16, 16],
     "tile": 4, "color": "#7d7f85", "altColor": "#5a5c63"},
    {"id": "tex.item.boat", "kind": "texture", "path": "textures/items/boat.png", "size": [16, 16],
     "color": "#8a5a2b", "altColor": "#c6a070"},
    {"id": "tex.ground.soil_wet", "kind": "texture", "path": "textures/tiles/soil_wet.png", "size": [16, 16],
     "tile": 4, "color": "#4a3322", "altColor": "#372418"},
    {"id": "tex.crop.turnip_seed", "kind": "texture", "path": "textures/crops/turnip_seed.png", "size": [16, 16],
     "color": "#6b4a2f", "altColor": "#a97c4f"},
    {"id": "tex.crop.turnip_sprout", "kind": "texture", "path": "textures/crops/turnip_sprout.png",
     "size": [16, 16], "color": "#3f7d32", "altColor": "#5fae45"},
    {"id": "tex.crop.turnip_young", "kind": "texture", "path": "textures/crops/turnip_young.png",
     "size": [16, 16], "color": "#3f7d32", "altColor": "#7ac95a"},
    {"id": "tex.crop.turnip_mature", "kind": "texture", "path": "textures/crops/turnip_mature.png",
     "size": [16, 16], "color": "#9a6fb0", "altColor": "#efe6f0"},
    {"id": "tex.object.chest", "kind": "texture", "path": "textures/objects/chest.png", "size": [16, 16],
     "color": "#8a5a2b", "altColor": "#5d3a1a"},
    {"id": "tex.enemy.slime", "kind": "texture", "path": "textures/characters/slime.png", "size": [16, 16],
     "color": "#4fa84a", "altColor": "#2f7a36"}
  ]
})";

/// \brief Small content pack used by the smoke run: one crop chain and one enemy.
///
/// It is inline on purpose: M15 takes text, not a path, so the smoke run proves
/// loading and reference validation without adding asset files. The ids are stable
/// and namespaced exactly as a real `data/` pack would be.
/// \brief Built-in fallback for `data/animations.json`: the four-way player walk clip.
constexpr std::string_view kAnimationsText = R"({
  "player_walk": {
    "texture": "tex.actor.player_walk",
    "frame_size": [24, 24],
    "sprite_size": [24, 24],
    "columns": 4,
    "rows": { "down": 0, "up": 1, "left": 2, "right": 3 },
    "walk_durations": [0.16, 0.10, 0.16, 0.10],
    "run_durations": [0.10, 0.06, 0.10, 0.06],
    "idle_frame": 0,
    "run_speed_threshold": 120.0,
    "enemies": [
      { "matches": "tex.enemy.slime", "texture": "tex.enemy.slime_walk", "frame_size": [16, 16],
        "columns": 4, "durations": [0.16, 0.10, 0.16, 0.10], "idle_frame": 0 },
      { "matches": "tex.enemy.bat", "texture": "tex.enemy.bat_walk", "frame_size": [16, 16],
        "columns": 4, "durations": [0.09, 0.09, 0.09, 0.09], "idle_frame": 1 }
    ]
  },
  "player_boat": {
    "texture": "tex.actor.player_boat",
    "frame_size": [32, 24],
    "sprite_size": [32, 24],
    "columns": 4,
    "rows": { "down": 0, "up": 1, "left": 2, "right": 3 },
    "walk_durations": [0.20, 0.14, 0.20, 0.14],
    "idle_frame": 0
  },
  "player_idle": {
    "texture": "tex.actor.player",
    "frame_size": [24, 24],
    "sprite_size": [24, 24],
    "columns": 4,
    "walk_durations": [0.35, 0.35, 0.35, 0.35],
    "idle_frame": 0
  }
})";

/// \brief One enemy type's walk clip: `matchTexture` is the texture id the spawn row
/// uses (`tex.enemy.slime`), the sheet is a single frame row applied in every state.
struct EnemyWalkAnimConfig {
    std::string matchTexture;
    std::string textureName;
    float frameWidth = 16.0f;
    float frameHeight = 16.0f;
    int columns = 4;
    int idleFrame = 0;
    std::vector<float> durations{0.14f, 0.10f, 0.14f, 0.10f};
};

/// \brief Data-driven player walk animation (M10 clips fed by M16 state), see
/// `data/animations.json`. Rows index the `player_walk` sprite sheet top-down.
struct PlayerWalkAnimConfig {
    bool valid = false;
    std::string textureName;
    float frameWidth = 16.0f;
    float frameHeight = 16.0f;
    float spriteWidth = 16.0f;
    float spriteHeight = 16.0f;
    int columns = 4;
    int rowDown = 0;
    int rowUp = 1;
    int rowLeft = 2;
    int rowRight = 3;
    int idleFrame = 0;
    float runSpeedThreshold = 120.0f;
    std::vector<float> walkDurations{0.16f, 0.10f, 0.16f, 0.10f};
    std::vector<float> runDurations{0.10f, 0.06f, 0.10f, 0.06f};
    std::vector<EnemyWalkAnimConfig> enemies;
};

[[nodiscard]] inline float jsonFloat(const nlohmann::json& value, float fallback) {
    return value.is_number() ? value.get<float>() : fallback;
}

[[nodiscard]] inline int jsonInt(const nlohmann::json& value, int fallback) {
    return value.is_number() ? value.get<int>() : fallback;
}

[[nodiscard]] inline std::vector<float> jsonFloats(const nlohmann::json& value, const std::vector<float>& fallback) {
    if (!value.is_array() || value.empty()) {
        return fallback;
    }
    std::vector<float> out;
    out.reserve(value.size());
    for (const nlohmann::json& item : value) {
        out.push_back(jsonFloat(item, fallback.front()));
    }
    return out;
}

/// \brief Lenient parse of one sprite-sheet section (`player_walk` / `player_boat`):
/// anything missing or malformed keeps its default; `valid` only flips when the
/// essentials (texture name, columns, durations) are present.
[[nodiscard]] inline PlayerWalkAnimConfig parseSheetAnimConfig(std::string_view text, const char* section) {
    PlayerWalkAnimConfig cfg;
    const nlohmann::json doc = nlohmann::json::parse(std::string(text), nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        return cfg;
    }
    const auto walk = doc.find(section);
    if (walk == doc.end() || !walk->is_object()) {
        return cfg;
    }
    const nlohmann::json& j = *walk;
    if (const auto it = j.find("texture"); it != j.end() && it->is_string()) {
        cfg.textureName = it->get<std::string>();
    }
    if (const auto it = j.find("frame_size"); it != j.end() && it->is_array() && it->size() == 2) {
        cfg.frameWidth = jsonFloat((*it)[0], cfg.frameWidth);
        cfg.frameHeight = jsonFloat((*it)[1], cfg.frameHeight);
    }
    if (const auto it = j.find("sprite_size"); it != j.end() && it->is_array() && it->size() == 2) {
        cfg.spriteWidth = jsonFloat((*it)[0], cfg.spriteWidth);
        cfg.spriteHeight = jsonFloat((*it)[1], cfg.spriteHeight);
    }
    if (const auto it = j.find("columns"); it != j.end()) {
        cfg.columns = jsonInt(*it, cfg.columns);
    }
    if (const auto it = j.find("rows"); it != j.end() && it->is_object()) {
        cfg.rowDown = jsonInt(it->value("down", nlohmann::json(cfg.rowDown)), cfg.rowDown);
        cfg.rowUp = jsonInt(it->value("up", nlohmann::json(cfg.rowUp)), cfg.rowUp);
        cfg.rowLeft = jsonInt(it->value("left", nlohmann::json(cfg.rowLeft)), cfg.rowLeft);
        cfg.rowRight = jsonInt(it->value("right", nlohmann::json(cfg.rowRight)), cfg.rowRight);
    }
    if (const auto it = j.find("walk_durations"); it != j.end()) {
        cfg.walkDurations = jsonFloats(*it, cfg.walkDurations);
    }
    if (const auto it = j.find("run_durations"); it != j.end()) {
        cfg.runDurations = jsonFloats(*it, cfg.runDurations);
    }
    if (const auto it = j.find("idle_frame"); it != j.end()) {
        cfg.idleFrame = jsonInt(*it, cfg.idleFrame);
    }
    if (const auto it = j.find("run_speed_threshold"); it != j.end()) {
        cfg.runSpeedThreshold = jsonFloat(*it, cfg.runSpeedThreshold);
    }
    if (const auto it = j.find("enemies"); it != j.end() && it->is_array()) {
        for (const nlohmann::json& e : *it) {
            if (!e.is_object()) {
                continue;
            }
            EnemyWalkAnimConfig ec;
            if (const auto f = e.find("matches"); f != e.end() && f->is_string()) {
                ec.matchTexture = f->get<std::string>();
            }
            if (const auto f = e.find("texture"); f != e.end() && f->is_string()) {
                ec.textureName = f->get<std::string>();
            }
            if (const auto f = e.find("frame_size"); f != e.end() && f->is_array() && f->size() == 2) {
                ec.frameWidth = jsonFloat((*f)[0], ec.frameWidth);
                ec.frameHeight = jsonFloat((*f)[1], ec.frameHeight);
            }
            if (const auto f = e.find("columns"); f != e.end()) {
                ec.columns = jsonInt(*f, ec.columns);
            }
            if (const auto f = e.find("idle_frame"); f != e.end()) {
                ec.idleFrame = jsonInt(*f, ec.idleFrame);
            }
            if (const auto f = e.find("durations"); f != e.end()) {
                ec.durations = jsonFloats(*f, ec.durations);
            }
            if (!ec.matchTexture.empty() && !ec.textureName.empty() && ec.columns > 0 && !ec.durations.empty() &&
                ec.frameWidth > 0.0f && ec.frameHeight > 0.0f) {
                cfg.enemies.push_back(std::move(ec));
            }
        }
    }
    cfg.valid = !cfg.textureName.empty() && cfg.columns > 0 && !cfg.walkDurations.empty() && cfg.frameWidth > 0.0f &&
                cfg.frameHeight > 0.0f;
    return cfg;
}

/// \brief Built-in fallback for `data/skills.json`: teleport + sword qi tuning.
constexpr std::string_view kSkillsText = R"({
  "teleport": { "range": 64.0, "stamina_cost": 12.0, "cooldown": 0.8 },
  "sword_qi": { "damage_multiplier": 1.5, "speed": 240.0, "lifetime": 0.5,
                "stamina_cost": 4.0, "cooldown": 0.6 }
})";

/// \brief Skill tuning from `data/skills.json` (E = character skill, Q = weapon skill).
struct SkillConfig {
    // Character skill: short-range teleport along the facing direction.
    float teleportRange = 64.0f;
    float teleportStaminaCost = 12.0f;
    float teleportCooldown = 0.8f;
    // Weapon skill: projectile sword qi, scaled from the equipped weapon's damage.
    float qiDamageMultiplier = 1.5f;
    float qiSpeed = 240.0f;
    float qiLifetime = 0.5f;
    float qiStaminaCost = 8.0f;
    float qiCooldown = 0.6f;
};

[[nodiscard]] inline SkillConfig parseSkillConfig(std::string_view text) {
    SkillConfig cfg;
    const nlohmann::json doc = nlohmann::json::parse(std::string(text), nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        return cfg;
    }
    if (const auto tp = doc.find("teleport"); tp != doc.end() && tp->is_object()) {
        cfg.teleportRange = jsonFloat(tp->value("range", nlohmann::json(cfg.teleportRange)), cfg.teleportRange);
        cfg.teleportStaminaCost =
            jsonFloat(tp->value("stamina_cost", nlohmann::json(cfg.teleportStaminaCost)), cfg.teleportStaminaCost);
        cfg.teleportCooldown =
            jsonFloat(tp->value("cooldown", nlohmann::json(cfg.teleportCooldown)), cfg.teleportCooldown);
    }
    if (const auto qi = doc.find("sword_qi"); qi != doc.end() && qi->is_object()) {
        cfg.qiDamageMultiplier =
            jsonFloat(qi->value("damage_multiplier", nlohmann::json(cfg.qiDamageMultiplier)), cfg.qiDamageMultiplier);
        cfg.qiSpeed = jsonFloat(qi->value("speed", nlohmann::json(cfg.qiSpeed)), cfg.qiSpeed);
        cfg.qiLifetime = jsonFloat(qi->value("lifetime", nlohmann::json(cfg.qiLifetime)), cfg.qiLifetime);
        cfg.qiStaminaCost = jsonFloat(qi->value("stamina_cost", nlohmann::json(cfg.qiStaminaCost)), cfg.qiStaminaCost);
        cfg.qiCooldown = jsonFloat(qi->value("cooldown", nlohmann::json(cfg.qiCooldown)), cfg.qiCooldown);
    }
    return cfg;
}

constexpr std::string_view kContentText = R"({
  "schema_version": 1,
  "items": [
    {"id": "item.turnip_seed", "display_name": "萝卜种子", "category": "seed",
     "tool_kind": "seed", "max_stack": 99, "sell_price": 5, "tags": ["seed", "spring"]},
    {"id": "item.turnip", "display_name": "萝卜", "category": "crop",
     "max_stack": 99, "sell_price": 35, "tags": ["crop", "spring"]},
    {"id": "item.turnip_soup", "display_name": "萝卜汤", "category": "food",
     "max_stack": 10, "sell_price": 120, "heal_amount": 20, "stamina_restore": 30},
    {"id": "item.hoe", "display_name": "锄头", "category": "tool",
     "tool_kind": "hoe", "max_stack": 1, "sell_price": 0, "tags": ["tool"]},
    {"id": "item.watering_can", "display_name": "水壶", "category": "tool",
     "tool_kind": "watering_can", "max_stack": 1, "sell_price": 0, "tags": ["tool"]},
    {"id": "item.sword", "display_name": "木剑", "category": "weapon",
     "tool_kind": "sword", "max_stack": 1, "sell_price": 0, "damage": 12,
     "tags": ["weapon"]},
    {"id": "item.boat", "display_name": "小船", "category": "tool",
     "tool_kind": "boat", "max_stack": 1, "sell_price": 30, "tags": ["tool", "boat"]}
  ],
  "crops": [
    {"id": "crop.turnip", "seed_item": "item.turnip_seed", "harvest_item": "item.turnip",
     "stage_days": [1, 2, 2],
     "stage_sprites": ["tex.crop.turnip_sprout", "tex.crop.turnip_young", "tex.crop.turnip_mature"],
     "seed_sprite": "tex.crop.turnip_seed",
     "seasons": ["spring", "fall"], "regrow": false, "regrow_days": 0}
  ],
  "recipes": [
    {"id": "recipe.turnip_soup",
     "inputs": [{"item": "item.turnip", "amount": 2}],
     "output": {"item": "item.turnip_soup", "amount": 1},
     "required_minutes": 30, "machine_kind": "kitchen", "seasons": "any"}
  ],
  "enemies": [
    {"id": "enemy.slime", "max_health": 30, "damage": 5, "move_speed": 40,
     "loot_table": "loot.slime", "tags": ["slime"]}
  ],
  "loot_tables": [
    {"id": "loot.slime", "entries": [
      {"item": "item.turnip_seed", "min_amount": 1, "max_amount": 2, "chance": 0.5}
    ]},
    {"id": "loot.test", "entries": [
      {"item": "item.turnip_seed", "min_amount": 2, "max_amount": 2, "chance": 1.0}
    ]}
  ]
})";

/// \brief One NPC, one branching dialogue and one collection quest for M23.
constexpr std::string_view kNarrativeText = R"({
  "schema_version": 1,
  "npcs": [
    {"id": "npc.elder", "name": "村长", "liked_items": ["item.turnip"]}
  ],
  "dialogues": [
    {"id": "dialogue.elder", "npc": "npc.elder", "start": "greet", "nodes": [
      {"id": "greet", "speaker": "村长",
       "text": "土地记得每一个季节。你愿意帮忙吗？",
       "choices": [
         {"text": "我去拔一个萝卜给你。", "next": "quest"},
         {"text": "现在不行。", "next": "bye"}
       ]},
      {"id": "quest", "speaker": "村长",
       "text": "给我一个萝卜，我就跟你换种子。",
       "actions": [{"kind": "start_quest", "id": "quest.turnip"}],
       "next": "bye"},
      {"id": "bye", "speaker": "村长",
       "text": "等太阳升高了再来吧。", "next": ""}
    ]}
  ],
  "quests": [
    {"id": "quest.turnip", "title": "给村长的萝卜",
     "description": "拔一个萝卜。",
     "objectives": [{"kind": "collect", "target": "item.turnip", "required": 1}],
     "rewards": [{"item": "item.turnip_seed", "amount": 2}], "gold": 10}
  ]
})";

/// \brief One general store for the M24 smoke sequence.
constexpr std::string_view kShopText = R"({
  "schema_version": 1,
  "shops": [
    {"id": "shop.general", "name": "杂货店",
     "buy_multiplier": 1.0, "sell_multiplier": 0.5,
     "stock": [
       {"item": "item.turnip_seed", "price": 10, "daily_limit": 5},
       {"item": "item.turnip", "price": 40},
       {"item": "item.turnip_soup", "price": 150}
     ]}
  ]
})";

/// \brief Feeds the asset cache's decoded pixels to the renderer.
///
/// M05 must not depend on M06, so the cache only knows `ITextureUploader`; this
/// adapter is the one place where the two meet, and the token it hands out is the
/// numeric value of the renderer's own handle.
class AssetUploaderAdapter final : public fa::assets::ITextureUploader {
public:
    explicit AssetUploaderAdapter(fa::render::IRenderer& renderer) noexcept : m_renderer(renderer) {}

    std::uint32_t upload(const fa::assets::ImageData& image, std::string_view debugName) override {
        fa::render::TextureDesc desc;
        desc.width = image.width;
        desc.height = image.height;
        desc.format = image.format == fa::assets::ImageFormat::R8 ? fa::render::TextureFormat::R8
                                                                  : fa::render::TextureFormat::Rgba8;
        desc.pixels = image.pixels.empty() ? nullptr : image.pixels.data();
        desc.rowPitchBytes = static_cast<int>(image.rowPitch());
        desc.debugName = debugName;

        fa::render::TextureHandle handle;
        if (m_renderer.createTexture(desc, handle).isError()) {
            return 0;
        }
        m_live.push_back(handle);
        return handle.value();
    }

    void release(std::uint32_t token) override {
        for (std::size_t i = 0; i < m_live.size(); ++i) {
            if (m_live[i].value() == token) {
                m_renderer.destroyTexture(m_live[i]);
                m_live.erase(m_live.begin() + static_cast<std::ptrdiff_t>(i));
                return;
            }
        }
    }

private:
    fa::render::IRenderer& m_renderer;
    std::vector<fa::render::TextureHandle> m_live;
};

/// \brief Published once per fixed simulation tick.
struct TickCompleted {
    std::uint64_t tick = 0;
};

/// \brief Published from a job completion, so it reaches handlers on the main thread.
struct UploadFinished {
    int value = 0;
};

/// \brief Minimal end-to-end use of M03 inside the smoke run.
///
/// Covers all three delivery paths the engine relies on: a synchronous publish from
/// the fixed update, work handed to the job pool, and a completion callback that
/// enqueues an event delivered by the next drain on the main thread.
class EventsAndJobsRun final {
public:
    EventsAndJobsRun() {
        m_tickSubscription =
            fa::events::Subscription(m_bus, m_bus.subscribe<TickCompleted>([this](const TickCompleted& event) {
                lastTick = event.tick;
                ++tickEvents;
            }));
        m_uploadSubscription =
            fa::events::Subscription(m_bus, m_bus.subscribe<UploadFinished>([this](const UploadFinished& event) {
                uploadedValue += event.value;
                ++uploadEvents;
            }));
    }

    void beginTick(std::uint64_t tick) {
        m_bus.publish(TickCompleted{tick});
        const fa::events::JobHandle handle =
            m_jobs.submit(fa::events::JobDesc{"smoke-upload", []() { return fa::core::okStatus(); }});
        m_inFlight.push_back(handle);
        m_jobs.onComplete(handle, [this, tick](fa::core::Status status) {
            if (status.isOk()) {
                m_bus.enqueue(UploadFinished{static_cast<int>(tick)});
            }
        });
    }

    void endFrame() {
        m_jobs.drainCompletedJobs();
        m_bus.dispatchQueued();
    }

    /// \brief Blocks for every submitted job so the run ends with every event delivered.
    /// \return False when any of those jobs reported an error.
    bool flush() {
        bool ok = true;
        for (const fa::events::JobHandle& handle : m_inFlight) {
            ok = m_jobs.wait(handle).isOk() && ok;
        }
        m_inFlight.clear();
        endFrame();
        return ok;
    }

    std::size_t tickEvents = 0;
    std::size_t uploadEvents = 0;
    std::uint64_t lastTick = 0;
    int uploadedValue = 0;

private:
    fa::events::EventBus m_bus;
    fa::events::JobSystem m_jobs{2};
    std::vector<fa::events::JobHandle> m_inFlight;
    fa::events::Subscription m_tickSubscription;
    fa::events::Subscription m_uploadSubscription;
};

/// \brief Counters for the one M17 sequence the smoke run performs.
struct InteractionRunStats {
    bool targetValid = false;
    bool targetReachable = false;
    bool targetIsEntity = false;
    float targetDistance = 0.0f;
    fa::tilemap::TileCoord targetCell{};
    bool toolUsed = false;
    std::size_t toolUses = 0;
    bool placementOk = false;
    std::size_t placements = 0;
    std::size_t events = 0;
    std::size_t occupiedCells = 0;
    bool previewBefore = false;
    bool previewAfter = false;
};

/// \brief Counters for the scripted M19 sequence.
struct FarmingRunStats {
    bool hoeOk = false;
    bool seedOk = false;
    bool waterOk = false;
    bool harvestOk = false;
    bool greenhouseOk = false;
    bool wiltedOk = false;
    int stage = 0;
    bool harvestable = false;
    int harvestCount = 0;
    int seedCount = 0;
    fa::farming::FarmingStats stats;
};

/// \brief Snapshot of the scripted M24 economy sequence.
struct EconomyRunStats {
    bool buyOk = false;
    bool sellOk = false;
    bool craftOk = false;
    bool collectOk = false;
    bool shipOk = false;
    int settledGold = 0;
    int gold = 0;
    int seeds = 0;
    int turnips = 0;
    int soups = 0;
    fa::economy::EconomyStats stats;
};

/// \brief Snapshot of the scripted M25 application-flow sequence.
struct FlowRunStats {
    bool newGameOk = false;
    bool pauseOk = false;
    bool saveOk = false;
    bool dayEndOk = false;
    bool gameOverOk = false;
    bool respawnOk = false;
    bool menuOk = false;
    bool loadOk = false;
    bool quitOk = false;
    fa::flow::FlowStats stats;
};

/// \brief Snapshot of the scripted M23 dialogue/quest sequence.
struct NarrativeRunStats {
    bool active = false;
    std::string node;
    std::size_t choices = 0;
    int affinity = 0;
    bool questCompleted = false;
    bool questActive = false;
    int seeds = 0;
    std::uint64_t dialoguesStarted = 0;
    std::uint64_t dialoguesEnded = 0;
    std::uint64_t nodes = 0;
    std::uint64_t choicesMade = 0;
    std::uint64_t gifts = 0;
    std::size_t events = 0;
};

/// \brief A dropped item waiting for the player to walk over it.
struct LootDrop {
    fa::content::ContentId item;
    int count = 1;
};

/// \brief Short tint pulse applied to an entity after it takes damage.
struct HitFlash {
    float remaining = 0.0f;
    fa::core::Color baseTint{1.0f, 1.0f, 1.0f, 1.0f};
};

/// \brief Rising damage text drawn in world space by the HUD pass.
struct FloatingDamage {
    fa::core::Vec2 position{};
    int amount = 0;
    float remaining = 0.0f;
    bool onPlayer = false;
};

inline constexpr float kHitFlashSeconds = 0.12f;
inline constexpr float kDamageTextSeconds = 0.7f;
inline constexpr std::size_t kMaxDamageTexts = 32;

/// \brief One procedural clip from `data/audio.json`.
struct AudioClipDef {
    std::string id;
    fa::audio::AudioBus bus = fa::audio::AudioBus::SFX;
    float frequency = 440.0f;
    float duration = 0.2f;
    float amplitude = 0.25f;
    float volume = 0.6f;
    bool loop = false;
};

/// \brief One play-mode entity from `data/entities.json`.
struct EntitySpawn {
    std::string id;
    std::string kind;
    /// \brief Map file this row belongs to (`"farm"`, `"forest"`, `"play/farm"`). Empty means
    /// "every map", which is how the pre-scoping spawn rows are stored.
    std::string map;
    fa::core::Vec2 position{};
    bool hasPosition = false;
    fa::tilemap::TileCoord cell{};
    bool hasCell = false;
    std::string texture;
    fa::core::Color tint{1.0f, 1.0f, 1.0f, 1.0f};
    int payload = 0;
    float radius = 28.0f;
    int health = 20;
    std::string loot;
    int damage = 5;
    /// \brief Dialogue graph an `npc` runs; unused by every other kind.
    std::string dialogue;
    /// \brief Optional AI tuning; omitted fields keep the slime defaults.
    float sightRadius = 150.0f;
    float attackRange = 22.0f;
    float moveSpeed = 42.0f;
    int attackDamage = 6;
    float attackCooldown = 1.2f;
};

/// \brief Parses and validates the entity spawn table; bad rows are skipped.
[[nodiscard]] std::vector<EntitySpawn> parseEntitySpawns(const fa::save::SaveArchive& root) {
    std::vector<EntitySpawn> spawns;
    const fa::save::SaveArchive list = root.child("entities");
    if (!list.isArray()) {
        return spawns;
    }
    const auto knownKind = [](std::string_view kind) {
        return kind == "bed" || kind == "shipping" || kind == "merchant" || kind == "enemy" || kind == "hazard" ||
               kind == "npc";
    };
    for (std::size_t index = 0; index < list.size(); ++index) {
        const fa::save::SaveArchive entry = list.at(index);
        if (!entry.isObject()) {
            continue;
        }
        EntitySpawn spawn;
        static_cast<void>(entry.getString("id", spawn.id));
        static_cast<void>(entry.getString("kind", spawn.kind));
        static_cast<void>(entry.getString("map", spawn.map));
        if (spawn.id.empty() || !knownKind(spawn.kind)) {
            continue;
        }
        spawn.hasPosition = entry.getVec2("position", spawn.position);
        fa::core::Vec2 cell{};
        spawn.hasCell = entry.getVec2("cell", cell);
        if (spawn.hasCell) {
            spawn.cell = fa::tilemap::TileCoord{static_cast<int>(cell.x), static_cast<int>(cell.y)};
        }
        if (!spawn.hasPosition && !spawn.hasCell) {
            continue;
        }
        static_cast<void>(entry.getString("texture", spawn.texture));
        std::string tint;
        if (entry.getString("tint", tint)) {
            const fa::core::Result<fa::core::Color> parsed = fa::assets::parseColorText(tint);
            if (parsed.isOk()) {
                spawn.tint = parsed.value();
            }
        }
        static_cast<void>(entry.getInt("payload", spawn.payload));
        static_cast<void>(entry.getFloat("radius", spawn.radius));
        static_cast<void>(entry.getInt("health", spawn.health));
        static_cast<void>(entry.getString("loot", spawn.loot));
        static_cast<void>(entry.getInt("damage", spawn.damage));
        static_cast<void>(entry.getString("dialogue", spawn.dialogue));
        static_cast<void>(entry.getFloat("sight_radius", spawn.sightRadius));
        static_cast<void>(entry.getFloat("attack_range", spawn.attackRange));
        static_cast<void>(entry.getFloat("move_speed", spawn.moveSpeed));
        static_cast<void>(entry.getInt("attack_damage", spawn.attackDamage));
        static_cast<void>(entry.getFloat("attack_cooldown", spawn.attackCooldown));

        const bool duplicate = std::any_of(
            spawns.begin(), spawns.end(), [&spawn](const EntitySpawn& existing) { return existing.id == spawn.id; });
        if (!duplicate) {
            spawns.push_back(std::move(spawn));
        }
    }
    return spawns;
}

/// \brief One AI agent's locked target, for the M27 overlay and debug lines.
struct AiTargetDebugSnapshot {
    fa::scene::EntityId agent{};
    fa::scene::EntityId target{};
    fa::ai::AiState state = fa::ai::AiState::Idle;
    bool hasTarget = false;
    bool targetVisible = false;
    fa::core::Vec2 agentCenter{};
    fa::core::Vec2 targetCenter{};
    bool hasAgentCenter = false;
    bool hasTargetCenter = false;
};

/// \brief Snapshot of the scripted M21 agent for the smoke summary.
struct AiRunStats {
    fa::ai::AiState state = fa::ai::AiState::Idle;
    bool targetValid = false;
    bool targetVisible = false;
    std::size_t pathNodes = 0;
    fa::ai::AiStats stats;
};

#if defined(FA_ENABLE_EDITOR)
/// \brief Snapshot of the M26 editor and its save/load round-trip.
struct EditorRunStats {
    bool active = false;
    fa::editor::EditorTool tool = fa::editor::EditorTool::Brush;
    int layer = 0;
    unsigned brush = 0;
    std::size_t undoDepth = 0;
    std::size_t redoDepth = 0;
    std::size_t previewCells = 0;
    std::uint64_t commands = 0;
    std::uint64_t undos = 0;
    std::uint64_t redos = 0;
    bool dirty = false;
    bool saveOk = false;
    bool loadOk = false;
    int mapWidth = 0;
    int mapHeight = 0;
};
#endif

/// \brief Records the one generic tool call the smoke run dispatches.
///
/// M19 owns Hoe/WateringCan/Seed/Harvest/Axe/Pickaxe now, so this recording
/// handler deliberately takes Sword to prove M17 dispatch without clashing.
class CountingToolHandler final : public fa::interaction::IToolHandler {
public:
    [[nodiscard]] bool handles(fa::interaction::ToolKind tool) const noexcept override {
        return tool == fa::interaction::ToolKind::Sword;
    }

    [[nodiscard]] fa::core::Status use(const fa::interaction::ToolUseContext& context) override {
        ++uses;
        lastPayload = context.request.payload;
        lastCell = context.request.target.cell;
        return fa::core::okStatus();
    }

    std::size_t uses = 0;
    std::uint32_t lastPayload = 0;
    fa::tilemap::TileCoord lastCell{};
};

/// \brief Counters for the one M18 sequence the smoke run performs.
struct InventoryRunStats {
    int backpackItems = 0;
    int backpackSlots = 0;
    int hotbarItems = 0;
    int selected = 0;
    bool toolEquipped = false;
    fa::content::ContentId toolId;
    int soupUses = 0;
    std::size_t inventoryEvents = 0;
    std::size_t useEvents = 0;
};

/// \brief Counters for the one M14 save/load round-trip the smoke run performs.
struct SaveRunStats {
    bool ok = false;
    bool recovered = false;
    std::string error;
    std::string slot;
    std::string scene;
    int version = 0;
    int day = 0;
    double playSeconds = 0.0;
    std::size_t entities = 0;
    fa::core::Vec2 marker{};
    float stamina = 0.0f;
    std::size_t slots = 0;
};

/// \brief Counters for the one M10 animation the smoke run plays.
struct AnimationRunStats {
    std::uint64_t updates = 0;
    std::uint64_t events = 0;
    std::size_t frameIndex = 0;
    std::size_t frameCount = 0;
    std::size_t lastEventFrame = 0;
    std::string lastEvent;
    bool finished = false;
};

/// \brief Counters for the scripted M20 combat sequence.
struct CombatRunStats {
    std::uint64_t updates = 0;
    std::uint64_t attacks = 0;
    std::uint64_t attackHits = 0;
    std::uint64_t damageDealt = 0;
    std::uint64_t kills = 0;
    std::uint64_t projectiles = 0;
    std::uint64_t projectileHits = 0;
    std::uint64_t projectileWallHits = 0;
    std::uint64_t statusApplications = 0;
    std::uint64_t statusTicks = 0;
    std::size_t damageEvents = 0;
    std::size_t deathEvents = 0;
    std::size_t projectileEvents = 0;
    std::size_t statusEvents = 0;
};

/// \brief Snapshot of the M22 calendar/weather/ambient state for the summary.
struct CalendarRunStats {
    int year = 1;
    fa::world::Season season = fa::world::Season::Spring;
    int day = 1;
    int hour = 8;
    int minute = 0;
    fa::world::Weather today = fa::world::Weather::Sunny;
    fa::world::Weather tomorrow = fa::world::Weather::Sunny;
    float ambientIntensity = 1.0f;
    bool autoWater = false;
    float growth = 1.0f;
    std::uint64_t days = 0;
    std::uint64_t hours = 0;
    std::uint64_t seasons = 0;
    std::uint64_t weatherChanges = 0;
    std::size_t events = 0;
};

/// \brief Consumes one food item and records the use.
///
/// M18 deliberately does not know what "food" does beyond the category string; a
/// real M19/M20 handler would restore health, play a sound, etc. The handler owns
/// consumption so the service can stay neutral.
class ConsumableUseHandler final : public fa::inventory::IItemUseHandler {
public:
    [[nodiscard]] bool handles(const fa::content::ItemDef& item) const noexcept override {
        return item.category == "food";
    }

    [[nodiscard]] fa::core::Status use(fa::inventory::ItemUseContext& context) override {
        ++uses;
        last = context.item.id;
        return context.inventory.removeAt(context.slot, 1).ignoreValue();
    }

    int uses = 0;
    fa::content::ContentId last;
};

/// \brief Which map a `GameScene` builds.
enum class GameMap { Farm, Forest };

/// \brief Long-lived state shared by every map scene.
///
/// `SceneStack::replace` destroys the old scene whenever the player walks through
/// a portal, so farmland, the showcase inventory, quest state and player vitals cannot
/// live on the scene. They live here, in the composition root, and each scene
/// borrows them.
struct GameState {
    fa::farming::FarmingService farming;
    fa::inventory::Inventory sceneInventory{4, 99, nullptr};
    fa::farming::DebrisLayer debris;
    fa::farming::FarmingToolHandler farmingTools{farming, sceneInventory};
    fa::narrative::NarrativeService narrative;
    /// \brief Player tuning copied from `config/game.json` at startup.
    fa::player::PlayerTuning playerTuning;
    float playerStaminaMax = 100.0f;
    int playerHealth = 100;
    int playerMaxHealth = 100;
    float playerStamina = 100.0f;
    fa::player::Facing playerFacing = fa::player::Facing::South;
};

/// \brief The showcase world: a tile map with a solid wall, a trigger, and one body.
///
/// This is the smallest scene that shows M11, M12 and M13 working together. The map
/// owns the ground, the wall and a tilled patch; M12 extracts one draw item per visible
/// tile and exposes the solid cells to M13 through `TilemapSolids`. The marker is the
/// only physics body besides a placed prop: `PhysicsSystem` integrates its `Velocity`,
/// and the wall column stops it part way across the plot, so the smoke run proves the
/// solver rather than just that a number changed. A trigger volume on the path reports
/// one enter and one leave.
///
/// M17 gets a crate to target and a free cell to place a prop into. The sequence runs
/// once at startup and records the exact target, tool dispatch and occupancy; the
/// per-frame pass only updates the cursor highlight.
///
/// Play mode builds one map per `GameMap`; the composition root swaps scenes with
/// `SceneStack::replace` when a portal is used.
class GameScene final : public fa::scene::BasicScene {
public:
    static constexpr int kColumns = 40;
    static constexpr int kRows = 23;
    static constexpr int kGateFirstRow = 10;
    static constexpr int kGateLastRow = 13;
    /// \brief Column of the one-tile wall the marker drives into.
    static constexpr int kWallColumn = 22;
    static constexpr float kMarkerSize = 8.0f;

    static constexpr std::uint16_t kGrassTile = 1;
    static constexpr std::uint16_t kWallTile = 2;
    static constexpr std::uint16_t kSoilTile = 3;
    static constexpr std::uint16_t kBrambleTile = 4;

    /// \brief Cell of the interactable crate, reachable from the marker's start.
    static constexpr int kCrateColumn = 19;
    static constexpr int kCrateRow = 12;
    /// \brief Free cell the smoke test places a prop into.
    static constexpr int kPlacedColumn = 19;
    static constexpr int kPlacedRow = 11;
    static constexpr std::uint32_t kCratePayload = 7;
    static constexpr std::uint32_t kToolPayload = 42;
    static constexpr std::uint32_t kPlacementPayload = 9;

    GameScene() = delete;
    GameScene(fa::input::IInput& input,
              const fa::content::ContentDatabase& content,
              const fa::narrative::NarrativeDatabase& narrative,
              GameState& state,
              GameMap map,
              bool scripted = true,
              const std::vector<EntitySpawn>* spawns = nullptr,
              std::optional<fa::core::Vec2> spawn = std::nullopt,
              std::string_view mapText = {},
              std::string mapId = {},
              bool showcaseContent = true)
        : BasicScene(map == GameMap::Forest ? "forest" : "farm"), m_input(input), m_state(state),
          m_farming(state.farming), m_farmInventory(state.sceneInventory), m_farmDebris(state.debris),
          m_farmingTools(state.farmingTools), m_narrative(state.narrative), m_interaction(m_map, &m_placement),
          m_scripted(scripted), m_spawns(spawns), m_mapKind(map), m_spawn(spawn), m_mapId(std::move(mapId)),
          m_showcaseContent(showcaseContent) {
        m_farming.setContentDatabase(&content);
        m_farmInventory.setContentDatabase(&content);
        m_farmingTools.setDebrisLayer(&m_farmDebris);
        m_narrative.setDatabase(&narrative);
        m_narrative.setInventory(&m_farmInventory);
        m_narrative.setSeason(fa::content::Season::Spring);
        // Maps are data. A missing or malformed file falls back to the built-in
        // layout so the smoke app still runs from a bare checkout.
        if (!mapText.empty()) {
            fa::core::Result<fa::tilemap::Tilemap> loaded = fa::tilemap::loadTilemapFromJson(mapText);
            if (loaded.isOk()) {
                m_map = std::move(loaded).take();
                m_mapLoaded = true;
            } else {
                std::printf("map: %s parse failed (%s), using built-in fallback\n",
                            map == GameMap::Forest ? "forest" : "farm",
                            loaded.error().message.c_str());
            }
        }
        if (m_mapLoaded) {
            static_cast<void>(m_placement.resize(m_map.width(), m_map.height()));
        } else {
            static_cast<void>(m_map.reset(kColumns, kRows, fa::render::kTileSize));
            static_cast<void>(m_placement.resize(kColumns, kRows));
        }
        m_interaction.addHandler(m_toolHandler);
        m_interaction.addHandler(m_farmingTools);
    }

    /// \brief Which map this scene owns.
    [[nodiscard]] GameMap mapKind() const noexcept { return m_mapKind; }
    [[nodiscard]] bool isForest() const noexcept { return m_mapKind == GameMap::Forest; }
    [[nodiscard]] bool isFarm() const noexcept { return m_mapKind == GameMap::Farm; }
    [[nodiscard]] std::string_view mapName() const noexcept { return m_mapKind == GameMap::Forest ? "forest" : "farm"; }

    /// \brief Hands the scene the textures the map and the marker draw with.
    ///
    /// Only the asset manager can produce these, and it needs a GL context, so the
    /// scene is configured right before it is pushed. The map stores plain handles;
    /// `setTileTexture` is the same binding path a JSON-loaded map will use.
    void setTextures(fa::render::TextureHandle grass,
                     fa::render::TextureHandle wall,
                     fa::render::TextureHandle soil,
                     fa::render::TextureHandle marker) {
        m_grass = grass;
        m_wall = wall;
        m_soil = soil;
        m_marker = marker;
    }

    /// \brief Extra gameplay textures; invalid handles keep the procedural fallbacks.
    void setDetailTextures(fa::render::TextureHandle soilWet,
                           fa::render::TextureHandle cropSeed,
                           fa::render::TextureHandle cropSprout,
                           fa::render::TextureHandle cropYoung,
                           fa::render::TextureHandle cropMature,
                           fa::render::TextureHandle chest,
                           fa::render::TextureHandle slime) {
        m_soilWet = soilWet;
        m_cropSeed = cropSeed;
        m_cropSprout = cropSprout;
        m_cropYoung = cropYoung;
        m_cropMature = cropMature;
        m_chest = chest;
        m_slime = slime;
    }

    /// \brief Turns the scripted/input axis into the controller's intent.
    ///
    /// The scene no longer writes `Velocity`: `PlayerSystem` owns that write, and
    /// `PhysicsSystem` still owns the position. The split is what lets M16 replace
    /// the old direct-drive path without touching the solver.
    void setScriptedMove(fa::core::Vec2 axis) {
        if (m_playerSystem != nullptr) {
            m_playerSystem->setScriptedMove(axis);
        }
    }

    /// \brief Lets `PlayerSystem` read the input axis directly.
    void clearScriptedMove() {
        if (m_playerSystem != nullptr) {
            m_playerSystem->clearScriptedMove();
        }
    }

    void onEnter() override {
        if (!m_built) {
            buildWorld();
            if (m_scripted) {
                runInteractionSmoke();
                runFarmingSmoke();
                runNarrativeSmoke();
            }
            m_built = true;
        }

        auto player = std::make_unique<fa::player::PlayerSystem>(m_input, m_state.playerTuning);
        m_playerSystem = player.get();
        systems().add(std::move(player));

        auto physics = std::make_unique<fa::physics::PhysicsSystem>(&m_solids, 64.0f);
        m_physics = physics.get();
        systems().add(std::move(physics));
        m_combat = std::make_unique<fa::combat::CombatService>(m_physics->physics());
        m_ai = std::make_unique<fa::ai::AiService>(m_map, m_physics->physics(), m_combat.get());
        if (m_ai != nullptr && !m_scripted) {
            // Wind-up + `hit` frame, matching the player swing. Enemies without a
            // dedicated effect (e.g. the farm's training agent) keep instant attacks.
            m_ai->setAttackHandler([this](fa::scene::EntityId agent, const fa::combat::AttackHitbox& hitbox) {
                if (startEnemyAttack(agent, hitbox)) {
                    return;
                }
                if (m_combat != nullptr) {
                    static_cast<void>(m_combat->spawnAttack(world(), agent, hitbox));
                }
            });
        }

        auto tiles = std::make_unique<fa::tilemap::TilemapExtractSystem>(m_map);
        m_tileSystem = tiles.get();
        systems().add(std::move(tiles));
        systems().add(std::make_unique<fa::scene::SpriteExtractSystem>());

        if (!m_scripted) {
            // Player vitals belong to the session, not to one map.
            setPlayerHealth(static_cast<float>(m_state.playerHealth));
            setPlayerStamina(m_state.playerStamina);
            setPlayerFacing(m_state.playerFacing);
            if (m_spawn.has_value()) {
                teleportMarker(*m_spawn);
            }
            syncPhysics();
        }
    }

    void onExit() override {
        if (!m_scripted && m_markerEntity.isValid()) {
            m_state.playerHealth = static_cast<int>(playerHealth());
            m_state.playerMaxHealth = static_cast<int>(playerMaxHealth());
            m_state.playerStamina = playerStamina();
            m_state.playerFacing = playerFacing();
        }
        // The AI service borrows the physics world and the combat service.
        m_ai.reset();
        // `CombatService` borrows the physics world owned by `PhysicsSystem`.
        m_combat.reset();
        // `PhysicsSystem` borrows `m_solids` and the tile system borrows `m_map`, so
        // both systems have to die before those members do.
        systems().clear();
        m_playerSystem = nullptr;
        m_physics = nullptr;
        m_tileSystem = nullptr;
    }

    /// \brief The marker, so the caller can prove the world really moved.
    [[nodiscard]] fa::core::Vec2 markerPosition() const {
        const fa::scene::Transform* transform = world().tryGet<fa::scene::Transform>(m_markerEntity);
        return transform != nullptr ? transform->position : fa::core::Vec2{};
    }

    /// \brief Centre of the player sprite, which is what the camera follows.
    [[nodiscard]] fa::core::Vec2 markerCenter() const {
        const fa::core::Vec2 position = markerPosition();
        return fa::core::Vec2{position.x + kMarkerSize * 0.5f, position.y + kMarkerSize * 0.5f};
    }

    /// \brief Debug hook for the `teleport` console command.
    ///
    /// The next physics step still owns position; this only moves the marker now so
    /// a developer can inspect a spot without walking there.
    void teleportMarker(fa::core::Vec2 position) {
        if (fa::scene::Transform* transform = world().tryGet<fa::scene::Transform>(m_markerEntity);
            transform != nullptr) {
            transform->position = position;
        }
    }

    [[nodiscard]] std::size_t groundTiles() const noexcept { return m_map.cellCount(); }

    /// \brief M26 edits the same map the runtime extracts.
    [[nodiscard]] fa::tilemap::Tilemap& editableMap() noexcept { return m_map; }
    [[nodiscard]] fa::core::Rect mapTileRect(fa::tilemap::TileCoord cell) const noexcept {
        return m_map.tileRect(cell);
    }

    [[nodiscard]] fa::tilemap::TilemapStats tilemapStats() const noexcept { return m_map.stats(); }

    [[nodiscard]] fa::tilemap::TileExtractStats tileExtractStats() const noexcept {
        return m_tileSystem != nullptr ? m_tileSystem->lastStats() : fa::tilemap::TileExtractStats{};
    }

    [[nodiscard]] fa::core::Rect mapBounds() const noexcept { return m_map.boundsRect(); }

    /// \brief Forwarded to the extraction system so only visible cells are submitted.
    void setViewBounds(fa::core::Rect bounds) noexcept {
        if (m_tileSystem != nullptr) {
            m_tileSystem->setViewBounds(bounds);
        }
    }

    /// \brief Counters for the one scripted M17 sequence, snapshotted before shutdown.
    [[nodiscard]] const InteractionRunStats& interactionRunStats() const noexcept { return m_interactionRunStats; }

    /// \brief Counters for the scripted M19 sequence.
    [[nodiscard]] const FarmingRunStats& farmingRunStats() const noexcept { return m_farmingRunStats; }

    /// \brief Farmland/crop overlay entries, drawn by the composition root.
    [[nodiscard]] std::span<const fa::farming::FarmTileEntry> farmingTiles() const noexcept {
        return m_farming.entries();
    }

    /// \brief Live M19 counters, used by the play-mode summary.
    [[nodiscard]] fa::farming::FarmingStats farmingStats() const noexcept { return m_farming.stats(); }

    /// \brief Mutable farming state, used by save/load.
    [[nodiscard]] fa::farming::FarmingService& farming() noexcept { return m_farming; }

    [[nodiscard]] fa::core::Vec2 farmingCellPosition(fa::tilemap::TileCoord cell) const noexcept {
        return m_map.gridToWorld(cell);
    }

    [[nodiscard]] fa::narrative::NarrativeService& narrative() noexcept { return m_narrative; }
    [[nodiscard]] const fa::narrative::NarrativeService& narrative() const noexcept { return m_narrative; }

    /// \brief Map file id this scene was built from; scopes `entities.json` rows.
    [[nodiscard]] std::string_view mapId() const noexcept { return m_mapId; }

    /// \brief Binds quest rewards and gift checks to the player's container.
    void setNarrativeInventory(fa::inventory::Inventory& inventory) noexcept { m_narrative.setInventory(&inventory); }

    /// \brief Starts the map's NPC dialogue; the caller should be near the NPC.
    [[nodiscard]] fa::core::Status startNpcDialogue() {
        if (!m_npcEntity.isValid()) {
            return fa::core::errorStatus(fa::narrative::narrative_error::kUnknownNpc, "这张地图没有可对话对象");
        }
        return m_narrative.startDialogue(fa::content::ContentId::fromName(m_npcDialogue), m_npcEntity);
    }

    [[nodiscard]] NarrativeRunStats narrativeRunStats() const {
        NarrativeRunStats result;
        const fa::narrative::NarrativeStats stats = m_narrative.stats();
        result.active = stats.dialogueActive;
        if (const fa::narrative::DialogueNode* node = m_narrative.currentNode(); node != nullptr) {
            result.node = node->id;
            result.choices = m_narrative.availableChoices().size();
        }
        const fa::content::ContentId elder = fa::content::ContentId::fromName("npc.elder");
        const fa::content::ContentId quest = fa::content::ContentId::fromName("quest.turnip");
        result.affinity = m_narrative.affinity(elder);
        result.questActive = m_narrative.isQuestActive(quest);
        result.questCompleted = m_narrative.isQuestCompleted(quest);
        result.seeds = m_farmInventory.countOf(fa::content::ContentId::fromName("item.turnip_seed"));
        result.dialoguesStarted = stats.dialoguesStarted;
        result.dialoguesEnded = stats.dialoguesEnded;
        result.nodes = stats.nodesEntered;
        result.choicesMade = stats.choices;
        result.gifts = stats.gifts;
        result.events = stats.events;
        return result;
    }

    /// \brief M10 playback counters and the last emitted event.
    [[nodiscard]] AnimationRunStats animationRunStats() const { return m_animStats; }

    /// \brief Public entry point for the composition root's fixed step.
    void advanceAnimation(float deltaSeconds) { updateAnimation(deltaSeconds); }

    /// \brief Composition-root entry for the E character skill (teleport).
    std::string requestTeleport() { return tryTeleport(); }
    /// \brief Composition-root entry for the Q weapon skill (sword qi).
    std::string requestWeaponSkill(int weaponDamage) { return tryWeaponSkill(weaponDamage); }
    /// \brief Public boat surface: shore boarding check + board/disembark commands.
    [[nodiscard]] bool facingWater() const { return frontIsWater(); }
    std::string boardBoat() { return requestBoardBoat(); }
    std::string disembarkBoatManually() { return requestDisembark(); }

    /// \brief M20 counters for the smoke summary.
    [[nodiscard]] CombatRunStats combatRunStats() const { return m_combatStats; }
    void advanceCombat(float deltaSeconds) { updateCombat(deltaSeconds); }

    /// \brief M21 snapshot for the smoke summary.
    [[nodiscard]] AiRunStats aiRunStats() const {
        AiRunStats result;
        if (m_ai != nullptr) {
            result.stats = m_ai->stats();
            result.pathNodes = m_ai->pathNodeCount(m_aiAgentEntity);
        }
        if (const fa::ai::AiAgent* agent = world().tryGet<fa::ai::AiAgent>(m_aiAgentEntity); agent != nullptr) {
            result.state = agent->state;
            result.targetValid = agent->target.isValid();
            result.targetVisible = agent->targetVisible;
        }
        return result;
    }

    void advanceAi(float deltaSeconds) {
        if (m_ai != nullptr) {
            m_ai->update(world(), deltaSeconds);
        }
    }

    /// \brief The entity the camera follows and the debug overlay calls "player".
    [[nodiscard]] fa::scene::EntityId playerEntity() const noexcept { return m_markerEntity; }

    /// \brief Routes seed costs and harvest output to the player's container.
    void setFarmingInventory(fa::inventory::Inventory& inventory) noexcept { m_farmingTools.setInventory(inventory); }

    /// \brief Seed consumed by the next `ToolKind::Seed` request.
    void setSelectedSeed(const fa::content::ContentId& seed) noexcept { m_farmingTools.setSelectedSeed(seed); }

    [[nodiscard]] fa::player::Facing playerFacing() const noexcept {
        const fa::player::FacingDir* dir = world().tryGet<fa::player::FacingDir>(m_markerEntity);
        return dir != nullptr ? dir->value : fa::player::Facing::South;
    }

    void setPlayerFacing(fa::player::Facing facing) {
        if (fa::player::FacingDir* dir = world().tryGet<fa::player::FacingDir>(m_markerEntity); dir != nullptr) {
            dir->value = facing;
        }
    }

    /// \brief Spawns a melee attack box in front of the player.
    [[nodiscard]] bool playerAttack(int damage, float reach) {
        if (m_combat == nullptr) {
            return false;
        }
        const fa::core::Vec2 direction = fa::player::directionVector(playerFacing());
        fa::combat::AttackHitbox hitbox;
        // Player transform is the 8x8 sprite's top-left; centre the 20x20 box
        // `reach` pixels ahead of the player's centre.
        hitbox.offset = fa::core::Vec2{4.0f + direction.x * reach - 10.0f, 4.0f + direction.y * reach - 10.0f};
        hitbox.size = fa::core::Vec2{20.0f, 20.0f};
        hitbox.activeFrom = 0.0f;
        hitbox.activeTo = 0.1f;
        hitbox.damage = damage;
        hitbox.targetMask = fa::physics::maskOf(fa::physics::CollisionLayer::Enemy);
        m_combat->spawnAttack(world(), m_markerEntity, hitbox);
        return true;
    }

    /// \brief Creates the 3-frame swing sprite. Play mode only; scripted smoke keeps
    /// its original entity budget.
    void buildAttackEffect() {
        const fa::render::TextureHandle slash = textureForName("tex.effect.slash");
        if (!slash.isValid()) {
            return;
        }
        m_slashEntity = world().create();
        fa::scene::Transform transform;
        transform.position = markerPosition();
        static_cast<void>(world().add<fa::scene::Transform>(m_slashEntity, transform));

        fa::scene::Sprite sprite;
        sprite.texture = slash;
        sprite.size = fa::core::Vec2{16.0f, 16.0f};
        sprite.layer = fa::render::RenderLayer::Actors;
        sprite.tint = fa::core::Color{1.0f, 1.0f, 1.0f, 0.0f};
        static_cast<void>(world().add<fa::scene::Sprite>(m_slashEntity, sprite));

        auto clip = std::make_shared<fa::anim::AnimationClip>();
        clip->texture = slash;
        clip->loop = false;
        clip->frames = {
            fa::anim::AnimationFrame{fa::core::makeRect(0.0f, 0.0f, 16.0f, 16.0f), 0.07f, {}},
            fa::anim::AnimationFrame{fa::core::makeRect(16.0f, 0.0f, 16.0f, 16.0f), 0.07f, "hit"},
            fa::anim::AnimationFrame{fa::core::makeRect(32.0f, 0.0f, 16.0f, 16.0f), 0.10f, {}},
        };
        m_slashClip = std::move(clip);
    }

    /// \brief Starts one swing; the hitbox is spawned by the clip's `hit` event.
    ///
    /// Returns false when a swing is already running. Without a swing texture the
    /// attack falls back to the old immediate hitbox so combat never breaks.
    [[nodiscard]] bool startAttack(int damage, float reach) {
        if (!m_slashClip || !m_slashEntity.isValid()) {
            return playerAttack(damage, reach);
        }
        if (m_slashActive) {
            return false;
        }
        m_slashDamage = damage;
        m_slashReach = reach;
        m_slashActive = true;
        m_animService.play(m_slashAnimator, m_slashClip, true);
        updateSlashTransform();
        if (fa::scene::Sprite* sprite = world().tryGet<fa::scene::Sprite>(m_slashEntity); sprite != nullptr) {
            sprite->tint.a = 1.0f;
            if (const fa::anim::AnimationFrame* frame = m_animService.currentFrame(m_slashAnimator); frame != nullptr) {
                sprite->source = frame->source;
            }
        }
        return true;
    }

    [[nodiscard]] bool attackAnimationActive() const noexcept { return m_slashActive; }

    /// \brief One enemy's in-flight wind-up and the hitbox it will spawn.
    struct EnemySwing {
        fa::scene::EntityId owner{};
        fa::scene::EntityId effect{};
        fa::anim::Animator animator;
        fa::combat::AttackHitbox hitbox;
        bool active = false;
    };

    /// \brief Creates one reusable swing sprite per forest enemy.
    void buildEnemyAttackEffects() {
        if (m_slashClip == nullptr) {
            return;
        }
        const fa::render::TextureHandle slash = textureForName("tex.effect.slash");
        if (!slash.isValid()) {
            return;
        }
        for (const fa::scene::EntityId owner : m_enemyIds) {
            EnemySwing swing;
            swing.owner = owner;
            swing.effect = world().create();
            fa::scene::Transform transform;
            transform.position = markerPosition();
            static_cast<void>(world().add<fa::scene::Transform>(swing.effect, transform));

            fa::scene::Sprite sprite;
            sprite.texture = slash;
            sprite.size = fa::core::Vec2{16.0f, 16.0f};
            sprite.layer = fa::render::RenderLayer::Actors;
            sprite.tint = fa::core::Color{1.0f, 0.35f, 0.30f, 0.0f};
            static_cast<void>(world().add<fa::scene::Sprite>(swing.effect, sprite));
            m_enemySwings.push_back(std::move(swing));
        }
    }

    /// \brief Starts an enemy wind-up; the hitbox spawns from its `hit` frame.
    [[nodiscard]] bool startEnemyAttack(fa::scene::EntityId owner, const fa::combat::AttackHitbox& hitbox) {
        for (EnemySwing& swing : m_enemySwings) {
            if (swing.owner != owner || swing.active) {
                continue;
            }
            swing.hitbox = hitbox;
            swing.active = true;
            m_animService.play(swing.animator, m_slashClip, true);
            if (fa::scene::Sprite* sprite = world().tryGet<fa::scene::Sprite>(swing.effect); sprite != nullptr) {
                sprite->tint.a = 1.0f;
                if (const fa::anim::AnimationFrame* frame = m_animService.currentFrame(swing.animator);
                    frame != nullptr) {
                    sprite->source = frame->source;
                }
            }
            updateEnemySwingTransform(swing);
            return true;
        }
        return false;
    }

    [[nodiscard]] bool enemyAttackAnimationActive(fa::scene::EntityId owner) const noexcept {
        for (const EnemySwing& swing : m_enemySwings) {
            if (swing.owner == owner) {
                return swing.active;
            }
        }
        return false;
    }

    void updateEnemySwingTransform(EnemySwing& swing) {
        fa::scene::Transform* effect = world().tryGet<fa::scene::Transform>(swing.effect);
        const fa::scene::Transform* owner = world().tryGet<fa::scene::Transform>(swing.owner);
        if (effect == nullptr || owner == nullptr) {
            return;
        }
        fa::core::Vec2 direction = swing.hitbox.offset;
        const float length = glm::length(direction);
        direction = length > 0.001f ? direction / length : fa::core::Vec2{1.0f, 0.0f};
        const fa::core::Vec2 center{owner->position.x + 8.0f, owner->position.y + 8.0f};
        effect->position = fa::core::Vec2{center.x + direction.x * 10.0f - 8.0f, center.y + direction.y * 10.0f - 8.0f};
        effect->rotation = std::atan2(direction.y, direction.x);
    }

    /// \brief Advances every enemy wind-up and spawns its hitbox on `hit`.
    void updateEnemyAttackEffects(float deltaSeconds) {
        for (EnemySwing& swing : m_enemySwings) {
            if (!swing.active) {
                continue;
            }
            m_animService.update(swing.animator, deltaSeconds);
            updateEnemySwingTransform(swing);
            if (fa::scene::Sprite* sprite = world().tryGet<fa::scene::Sprite>(swing.effect); sprite != nullptr) {
                if (const fa::anim::AnimationFrame* frame = m_animService.currentFrame(swing.animator);
                    frame != nullptr) {
                    sprite->texture = m_slashClip->texture;
                    sprite->source = frame->source;
                }
            }
            for (const fa::anim::AnimationEvent& event : swing.animator.events) {
                if (event.name == "hit" && m_combat != nullptr) {
                    static_cast<void>(m_combat->spawnAttack(world(), swing.owner, swing.hitbox));
                }
            }
            if (swing.animator.finished) {
                swing.active = false;
                if (fa::scene::Sprite* sprite = world().tryGet<fa::scene::Sprite>(swing.effect); sprite != nullptr) {
                    sprite->tint.a = 0.0f;
                }
            }
        }
    }

    void updateSlashTransform() {
        fa::scene::Transform* transform = world().tryGet<fa::scene::Transform>(m_slashEntity);
        if (transform == nullptr) {
            return;
        }
        const fa::core::Vec2 center = markerCenter();
        const fa::core::Vec2 direction = fa::player::directionVector(playerFacing());
        transform->position =
            fa::core::Vec2{center.x + direction.x * 10.0f - 8.0f, center.y + direction.y * 10.0f - 8.0f};
        // The arc art points east, so the facing vector's angle is exactly the rotation
        // that aims it. The renderer turns the quad about its centre, which the position
        // above already places, so the swing stays anchored to the player.
        transform->rotation = std::atan2(direction.y, direction.x);
    }

    [[nodiscard]] std::span<const fa::combat::DamageEvent> damageEvents() const noexcept {
        return m_combat != nullptr ? m_combat->damageEvents() : std::span<const fa::combat::DamageEvent>{};
    }

    [[nodiscard]] std::span<const fa::combat::DeathEvent> deathEvents() const noexcept {
        return m_combat != nullptr ? m_combat->deathEvents() : std::span<const fa::combat::DeathEvent>{};
    }

    void clearCombatEvents() noexcept {
        if (m_combat != nullptr) {
            m_combat->clearEvents();
        }
    }

    [[nodiscard]] fa::scene::EntityId combatDummy1() const noexcept { return m_combatDummy1; }
    [[nodiscard]] fa::scene::EntityId combatDummy2() const noexcept { return m_combatDummy2; }
    [[nodiscard]] fa::scene::EntityId forestDummy1() const noexcept { return m_forestDummy1; }
    [[nodiscard]] fa::scene::EntityId forestDummy2() const noexcept { return m_forestDummy2; }
    [[nodiscard]] std::size_t enemyCount() const noexcept { return m_enemyIds.size(); }
    [[nodiscard]] fa::scene::EntityId enemyAt(std::size_t index) const noexcept {
        return index < m_enemyIds.size() ? m_enemyIds[index] : fa::scene::EntityId{};
    }
    [[nodiscard]] std::string_view enemyRole(std::size_t index) const noexcept {
        return index < m_enemyRoles.size() ? std::string_view(m_enemyRoles[index]) : std::string_view{};
    }
    [[nodiscard]] fa::scene::EntityId aiAgentEntity() const noexcept { return m_aiAgentEntity; }
    [[nodiscard]] fa::scene::EntityId aiDecoyEntity() const noexcept { return m_aiTargetEntity; }
    [[nodiscard]] fa::scene::EntityId placedEntity() const noexcept { return m_placedEntity; }

    /// \brief Shared debris layer, persisted with the save's world objects.
    [[nodiscard]] fa::farming::DebrisLayer& debris() noexcept { return m_farmDebris; }
    [[nodiscard]] const fa::farming::DebrisLayer& debris() const noexcept { return m_farmDebris; }

    /// \brief Drops cached AI paths so a loaded agent recomputes from its saved state.
    void resetAiPaths() {
        if (m_ai != nullptr) {
            m_ai->clearPaths();
        }
    }

    /// \brief Portal containing the marker's centre, if any.
    [[nodiscard]] const fa::tilemap::MapPortal* portalAtMarker() const {
        return m_map.portalAt(m_map.worldToGrid(markerCenter()));
    }

    /// \brief World position of a tile's top-left corner.
    [[nodiscard]] fa::core::Vec2 cellPosition(fa::tilemap::TileCoord cell) const { return m_map.gridToWorld(cell); }

    [[nodiscard]] fa::core::Vec2 bedPosition() const {
        const fa::scene::Transform* bed = world().tryGet<fa::scene::Transform>(m_bedEntity);
        return bed != nullptr ? bed->position : fa::core::Vec2{};
    }

    /// \brief Spawn rows of \p kind that belong to this map; empty when the table is absent.
    [[nodiscard]] std::vector<const EntitySpawn*> findSpawns(std::string_view kind) const {
        std::vector<const EntitySpawn*> result;
        if (m_spawns == nullptr) {
            return result;
        }
        for (const EntitySpawn& spawn : *m_spawns) {
            if (spawn.kind != kind) {
                continue;
            }
            // A row with no `map` predates per-map scoping and stays on every map; anything
            // tagged belongs only to the map file that named it.
            if (!spawn.map.empty() && spawn.map != m_mapId) {
                continue;
            }
            result.push_back(&spawn);
        }
        return result;
    }

    [[nodiscard]] const EntitySpawn* findSpawn(std::string_view kind) const {
        const std::vector<const EntitySpawn*> matches = findSpawns(kind);
        return matches.empty() ? nullptr : matches.front();
    }

    [[nodiscard]] fa::core::Vec2 spawnPosition(const EntitySpawn& spawn) const {
        return spawn.hasPosition ? spawn.position : m_map.gridToWorld(spawn.cell);
    }

    /// \brief Catalog-driven texture table owned by the composition root.
    void setNamedTextures(const std::unordered_map<std::string, fa::render::TextureHandle>* textures) {
        m_namedTextures = textures;
    }

    /// \brief JSON-driven four-way walk config; null or invalid keeps the marker dot.
    void setWalkConfig(const PlayerWalkAnimConfig* config) { m_walkConfig = config; }

    /// \brief `data/skills.json` tuning for E (teleport) and Q (sword qi).
    void setSkillConfig(const SkillConfig* config) { m_skillConfig = config; }

    /// \brief `data/animations.json` player_boat section; rowing art while riding.
    void setBoatConfig(const PlayerWalkAnimConfig* config) { m_boatConfig = config; }

    /// \brief `data/animations.json` player_idle section; breathing clip while standing.
    void setIdleConfig(const PlayerWalkAnimConfig* config) { m_idleConfig = config; }

    /// \brief True while the player is riding the boat.
    [[nodiscard]] bool boatMode() const noexcept { return m_boatMode; }

    /// \brief Maps a catalog texture name to one of the handles the scene owns.
    [[nodiscard]] fa::render::TextureHandle textureForName(std::string_view name) const {
        if (m_namedTextures != nullptr) {
            const auto found = m_namedTextures->find(std::string(name));
            if (found != m_namedTextures->end() && found->second.isValid()) {
                return found->second;
            }
        }
        if (name == "tex.ground.grass") {
            return m_grass;
        }
        if (name == "tex.ground.wall") {
            return m_wall;
        }
        if (name == "tex.ground.soil_wet") {
            return m_soilWet;
        }
        if (name == "tex.crop.turnip_seed") {
            return m_cropSeed;
        }
        if (name == "tex.crop.turnip_sprout") {
            return m_cropSprout;
        }
        if (name == "tex.crop.turnip_young") {
            return m_cropYoung;
        }
        if (name == "tex.crop.turnip_mature") {
            return m_cropMature;
        }
        if (name == "tex.object.chest") {
            return m_chest;
        }
        if (name == "tex.enemy.slime") {
            return m_slime;
        }
        if (name == "tex.marker" || name == "tex.actor.player") {
            return m_marker;
        }
        return m_soil;
    }

    [[nodiscard]] fa::render::TextureHandle textureForAsset(const fa::assets::AssetId& id) const {
        return textureForName(id.name());
    }

    /// \brief Whether the player is within \p radius of an entity's 16x16 centre.
    [[nodiscard]] bool isNearEntity(fa::scene::EntityId id, float radius) const {
        const fa::scene::Transform* transform = world().tryGet<fa::scene::Transform>(id);
        if (transform == nullptr) {
            return false;
        }
        const fa::core::Vec2 center{transform->position.x + 8.0f, transform->position.y + 8.0f};
        const fa::core::Vec2 player = markerCenter();
        const float dx = center.x - player.x;
        const float dy = center.y - player.y;
        return dx * dx + dy * dy <= radius * radius;
    }

    /// \brief Whether the player is close enough to the bed to sleep.
    [[nodiscard]] bool isNearBed() const { return isNearEntity(m_bedEntity, 28.0f); }
    /// \brief Whether the player is close enough to the shipping box.
    [[nodiscard]] bool isNearShippingBox() const { return isNearEntity(m_shippingEntity, 28.0f); }
    /// \brief Whether the player is close enough to the merchant.
    [[nodiscard]] bool isNearMerchant() const { return isNearEntity(m_merchantEntity, 32.0f); }
    /// \brief Whether the player is close enough to the narrative NPC.
    [[nodiscard]] bool isNearNpc() const { return isNearEntity(m_npcEntity, 32.0f); }
    [[nodiscard]] fa::core::Vec2 npcPosition() const {
        const fa::scene::Transform* transform = world().tryGet<fa::scene::Transform>(m_npcEntity);
        return transform != nullptr ? transform->position : fa::core::Vec2{};
    }
    [[nodiscard]] fa::scene::EntityId shippingBoxEntity() const noexcept { return m_shippingEntity; }
    [[nodiscard]] fa::scene::EntityId merchantEntity() const noexcept { return m_merchantEntity; }

    /// \brief Rebuilds the physics broad phase; used by the headless self check.
    void syncPhysics() {
        if (m_physics != nullptr) {
            m_physics->physics().sync(world());
        }
    }

    [[nodiscard]] bool isAlive(fa::scene::EntityId id) const noexcept { return world().isAlive(id); }

    /// \brief The cell and entity directly in front of the player.
    [[nodiscard]] fa::interaction::GridTarget frontTarget() const {
        const fa::player::Facing cardinal = fa::player::cardinalFacing(playerFacing());
        return m_interaction.pickTargetInFront(world(), m_markerEntity, static_cast<fa::interaction::Facing>(cardinal));
    }

    [[nodiscard]] const fa::farming::CropState* cropAt(fa::tilemap::TileCoord cell) const noexcept {
        return m_farming.cropAt(cell);
    }

    /// \brief Lines for the F3 facing-tile hub: the cell one step along the player's
    /// cardinal facing, its tile definition flags, and the crop/entity attributes of
    /// anything standing on it. Composed here so the overlay stays content-free.
    /// Debug text is English everywhere, like the stats block above it.
    [[nodiscard]] std::vector<std::string> facingTileDebugLines() const {
        std::vector<std::string> lines;
        char buf[128]{};
        const fa::tilemap::TileCoord here = m_map.worldToGrid(markerCenter());
        const fa::core::Vec2 dir = fa::player::directionVector(fa::player::cardinalFacing(playerFacing()));
        const fa::tilemap::TileCoord cell{here.x + static_cast<int>(dir.x), here.y + static_cast<int>(dir.y)};
        std::snprintf(buf, sizeof(buf), "facing tile (%d, %d)", cell.x, cell.y);
        lines.push_back(buf);
        if (cell.x < 0 || cell.y < 0 || cell.x >= m_map.width() || cell.y >= m_map.height()) {
            lines.push_back("outside map (solid)");
            return lines;
        }
        // Topmost non-empty tile wins the headline; deeper non-empty layers (decor over
        // ground) get one extra line so overlays stay visible in the panel.
        const fa::tilemap::Tilemap& mapRef = m_map;
        std::vector<fa::tilemap::TileId> stacked;
        for (int layer = mapRef.layerCount() - 1; layer >= 0; --layer) {
            const fa::tilemap::TileId id = mapRef.tileAt(cell.x, cell.y, layer);
            if (id.isValid()) {
                stacked.push_back(id);
            }
        }
        const fa::tilemap::TileId id = stacked.empty() ? fa::tilemap::TileId{} : stacked.front();
        const fa::tilemap::TileDef* def = stacked.empty() ? nullptr : m_map.tileDef(id);
        if (def == nullptr) {
            std::snprintf(buf, sizeof(buf), "tile #%u undefined", static_cast<unsigned>(id.value));
            lines.push_back(buf);
        } else {
            std::snprintf(buf,
                          sizeof(buf),
                          "tile %s #%u tag=%s",
                          def->name.c_str(),
                          static_cast<unsigned>(def->id.value),
                          def->tag.empty() ? "-" : def->tag.c_str());
            lines.push_back(buf);
            std::string flags = def->walkable ? "walk" : "blocked";
            if (def->solid) {
                flags += " solid";
            }
            if (def->tillable) {
                flags += " tillable";
            }
            if (def->water) {
                flags += " water";
            }
            lines.push_back(std::move(flags));
            std::snprintf(buf, sizeof(buf), "tex %s", def->textureName.empty() ? "-" : def->textureName.c_str());
            lines.push_back(buf);
        }
        if (stacked.size() > 1) {
            const fa::tilemap::TileDef* under = m_map.tileDef(stacked[1]);
            std::snprintf(buf, sizeof(buf), "under %s", under != nullptr ? under->name.c_str() : "?");
            lines.push_back(buf);
        }

        if (const fa::farming::CropState* crop = m_farming.cropAt(cell); crop != nullptr) {
            std::snprintf(buf,
                          sizeof(buf),
                          "crop %s stage %d/%zu growth %.1fd watered %d harvest %d dead %d",
                          std::string(crop->crop.name()).c_str(),
                          crop->stage,
                          crop->stageDays.size(),
                          static_cast<double>(crop->growthDays),
                          crop->wateredToday ? 1 : 0,
                          crop->harvestable ? 1 : 0,
                          crop->dead ? 1 : 0);
            lines.push_back(buf);
        }

        // Entities whose 16x16 centre lands on the facing cell.
        const auto cellOf = [this](fa::scene::EntityId entity) {
            const fa::scene::Transform* transform = world().tryGet<fa::scene::Transform>(entity);
            if (transform == nullptr) {
                return fa::tilemap::TileCoord{-1, -1};
            }
            return m_map.worldToGrid(fa::core::Vec2{transform->position.x + 8.0f, transform->position.y + 8.0f});
        };
        const auto healthLine = [this](std::string& text, fa::scene::EntityId entity) {
            if (const fa::player::Health* hp = world().tryGet<fa::player::Health>(entity); hp != nullptr) {
                char tail[48]{};
                std::snprintf(tail, sizeof(tail), " hp %d/%d", hp->current, hp->max);
                text += tail;
            }
        };
        for (std::size_t index = 0; index < m_enemyIds.size(); ++index) {
            const fa::scene::EntityId enemy = m_enemyIds[index];
            if (cellOf(enemy) != cell || !world().isAlive(enemy)) {
                continue;
            }
            std::string text = "enemy " + std::string(index < m_enemyRoles.size() ? m_enemyRoles[index] : "?");
            healthLine(text, enemy);
            if (const fa::ai::AiAgent* agent = world().tryGet<fa::ai::AiAgent>(enemy); agent != nullptr) {
                char tail[80]{};
                std::snprintf(tail,
                              sizeof(tail),
                              " ai=%s speed %g sight %g",
                              std::string(fa::ai::toString(agent->state)).c_str(),
                              static_cast<double>(agent->profile.moveSpeed),
                              static_cast<double>(agent->profile.sightRadius));
                text += tail;
            }
            lines.push_back(std::move(text));
        }
        const std::pair<fa::scene::EntityId, const char*> props[] = {
            {m_bedEntity, "bed"},
            {m_shippingEntity, "shipping"},
            {m_merchantEntity, "merchant"},
            {m_npcEntity, "npc"},
            {m_crateEntity, "crate"},
            {m_placedEntity, "prop"},
        };
        for (const auto& [entity, label] : props) {
            if (!entity.isValid() || cellOf(entity) != cell || !world().isAlive(entity)) {
                continue;
            }
            std::string text = std::string("obj ") + label;
            if (const fa::interaction::Interactable* interactable =
                    world().tryGet<fa::interaction::Interactable>(entity);
                interactable != nullptr) {
                char tail[32]{};
                std::snprintf(tail, sizeof(tail), " payload=%u", interactable->payload);
                text += tail;
            }
            healthLine(text, entity);
            lines.push_back(std::move(text));
        }
        if (lines.size() == static_cast<std::size_t>(def != nullptr ? 4 : 2)) {
            lines.push_back("no object on tile");
        }
        return lines;
    }

    [[nodiscard]] fa::core::Status interact(const fa::interaction::InteractionRequest& request) {
        return m_interaction.interact(world(), request);
    }

    /// \brief Applies one day of farming growth/weather without advancing the clock.
    void advanceFarmingDay(const fa::farming::FarmingDayContext& context) {
        m_farming.setCurrentSeason(context.season);
        m_farming.onDayChanged(context);
    }

    /// \brief Copies and clears the controller's action requests for this step.
    [[nodiscard]] std::vector<fa::player::PlayerActionRequest> takePlayerActions() {
        std::vector<fa::player::PlayerActionRequest> actions;
        if (m_playerSystem != nullptr) {
            const std::span<const fa::player::PlayerActionRequest> events = m_playerSystem->controller().events();
            actions.assign(events.begin(), events.end());
            m_playerSystem->controller().clearEvents();
        }
        return actions;
    }

    /// \brief Every AI agent plus the target it currently has locked.
    [[nodiscard]] std::vector<AiTargetDebugSnapshot> aiTargetDebugSnapshot() const {
        std::vector<AiTargetDebugSnapshot> result;
        world().each<fa::ai::AiAgent, fa::scene::Transform>(
            [&result, this](fa::scene::EntityId id, const fa::ai::AiAgent& agent, const fa::scene::Transform&) {
                AiTargetDebugSnapshot snapshot;
                snapshot.agent = id;
                snapshot.target = agent.target;
                snapshot.state = agent.state;
                snapshot.hasTarget = agent.target.isValid();
                snapshot.targetVisible = agent.targetVisible;
                snapshot.hasAgentCenter = entityCenter(id, snapshot.agentCenter);
                if (snapshot.hasTarget) {
                    snapshot.hasTargetCenter = entityCenter(agent.target, snapshot.targetCenter);
                }
                result.push_back(snapshot);
            });
        return result;
    }

    /// \brief Per-frame cursor target, used only for the debug highlight.
    void setCursorWorld(fa::core::Vec2 worldPosition) {
        m_cursorTarget = m_interaction.pickTarget(world(), m_markerEntity, worldPosition);
    }

    [[nodiscard]] fa::core::Rect interactionTargetRect() const {
        return m_cursorTarget.valid ? m_map.tileRect(m_cursorTarget.cell) : fa::core::Rect{};
    }

    [[nodiscard]] fa::core::Color interactionTargetColor() const {
        return m_cursorTarget.reachable ? fa::core::Color{0.3f, 1.0f, 0.45f, 0.9f}
                                        : fa::core::Color{1.0f, 0.3f, 0.3f, 0.9f};
    }

    [[nodiscard]] fa::physics::PhysicsStats physicsStats() const {
        return m_physics != nullptr ? m_physics->physics().stats() : fa::physics::PhysicsStats{};
    }

    /// \brief M13 geometry for the M27 physics overlay.
    [[nodiscard]] std::vector<fa::physics::DebugShape> physicsDebugShapes() const {
        std::vector<fa::physics::DebugShape> shapes;
        if (m_physics != nullptr) {
            m_physics->physics().debugDraw(world(), shapes);
        }
        return shapes;
    }

    [[nodiscard]] fa::player::PlayerStats playerStats() const {
        return m_playerSystem != nullptr ? m_playerSystem->controller().stats() : fa::player::PlayerStats{};
    }

    [[nodiscard]] fa::player::PlayerAnimationParams playerAnimation() const {
        return m_playerSystem != nullptr ? m_playerSystem->controller().animationParams()
                                         : fa::player::PlayerAnimationParams{};
    }

    [[nodiscard]] float playerStamina() const {
        const fa::player::Stamina* stamina = world().tryGet<fa::player::Stamina>(m_markerEntity);
        return stamina != nullptr ? stamina->current : 0.0f;
    }

    /// \brief Sets current health, clamped to `[0, max]`.
    void setPlayerHealth(float value) {
        fa::player::Health* health = world().tryGet<fa::player::Health>(m_markerEntity);
        if (health != nullptr) {
            const float clamped = fa::core::clampf(value, 0.0f, static_cast<float>(health->max));
            health->current = static_cast<int>(clamped);
        }
    }

    /// \brief Subtracts health; returns the remaining value.
    int damagePlayer(int amount) {
        fa::player::Health* health = world().tryGet<fa::player::Health>(m_markerEntity);
        if (health == nullptr || amount <= 0) {
            return health != nullptr ? health->current : 0;
        }
        health->current = std::max(0, health->current - amount);
        return health->current;
    }

    /// \brief The marker's current tile, used by portals and hazards.
    [[nodiscard]] fa::tilemap::TileCoord markerCell() const { return m_map.worldToGrid(markerCenter()); }

    /// \brief Where a passed-out player wakes up: beside the bed.
    [[nodiscard]] fa::core::Vec2 respawnPoint() const {
        const fa::core::Vec2 bed = bedPosition();
        return fa::core::Vec2{bed.x - 16.0f, bed.y};
    }

    [[nodiscard]] float playerHealth() const {
        const fa::player::Health* health = world().tryGet<fa::player::Health>(m_markerEntity);
        return health != nullptr ? static_cast<float>(health->current) : 0.0f;
    }

    [[nodiscard]] float playerMaxHealth() const {
        const fa::player::Health* health = world().tryGet<fa::player::Health>(m_markerEntity);
        return health != nullptr ? static_cast<float>(health->max) : 0.0f;
    }

    /// \brief Sets current stamina, clamped to the component's maximum.
    void setPlayerStamina(float value) {
        fa::player::Stamina* stamina = world().tryGet<fa::player::Stamina>(m_markerEntity);
        if (stamina != nullptr) {
            stamina->current = fa::core::clampf(value, 0.0f, stamina->max);
        }
    }

    /// \brief Restores \p efficiency of the missing stamina; 1 refills completely.
    void recoverStamina(float efficiency) {
        fa::player::Stamina* stamina = world().tryGet<fa::player::Stamina>(m_markerEntity);
        if (stamina == nullptr) {
            return;
        }
        const float clamped = fa::core::clampf(efficiency, 0.0f, 1.0f);
        const float restored = (stamina->max - stamina->current) * clamped;
        stamina->current = std::min(stamina->max, stamina->current + restored);
    }

    /// \brief Full stamina, used by HUD and save summaries.
    [[nodiscard]] float playerMaxStamina() const {
        const fa::player::Stamina* stamina = world().tryGet<fa::player::Stamina>(m_markerEntity);
        return stamina != nullptr ? stamina->max : 0.0f;
    }

    /// \brief Distance from the marker to the wall straight ahead, or -1 on no hit.
    ///
    /// A real ray query rather than a coordinate subtraction: this is the path arrows
    /// and melee reach will take.
    [[nodiscard]] float probeDistance() const {
        if (m_physics == nullptr) {
            return -1.0f;
        }
        const fa::core::Vec2 position = markerPosition();
        const fa::core::Vec2 origin{position.x + kMarkerSize * 0.5f, position.y + kMarkerSize * 0.5f};
        const std::optional<fa::physics::RayHit> hit =
            m_physics->physics().raycast(world(),
                                         origin,
                                         fa::core::Vec2{1.0f, 0.0f},
                                         100.0f,
                                         fa::physics::maskOf(fa::physics::CollisionLayer::World));
        return hit.has_value() ? hit->distance : -1.0f;
    }

private:
    /// \brief Centre of an entity's collider, or its transform as a fallback.
    [[nodiscard]] bool entityCenter(fa::scene::EntityId id, fa::core::Vec2& out) const {
        const fa::scene::Transform* transform = world().tryGet<fa::scene::Transform>(id);
        if (transform == nullptr) {
            return false;
        }
        if (const fa::physics::Collider* collider = world().tryGet<fa::physics::Collider>(id); collider != nullptr) {
            out = fa::core::center(fa::physics::colliderRect(*transform, *collider));
        } else {
            out = transform->position;
        }
        return true;
    }

    /// \brief Binds catalog textures to the defs loaded from the map JSON.
    void bindMapTextures() {
        for (const fa::tilemap::TileDef& def : m_map.tileDefs()) {
            static_cast<void>(m_map.setTileTexture(def.id, textureForName(def.textureName), def.source));
        }
        // Collision zones resolve their texture by name too; an unnamed zone is
        // collision-only and simply never draws.
        for (fa::tilemap::MapBlocker& blocker : m_map.mutableBlockers()) {
            if (!blocker.textureName.empty()) {
                blocker.texture = textureForName(blocker.textureName);
            }
        }
    }

    void buildWorld() {
        if (!m_mapLoaded) {
            fa::tilemap::TileDef grass;
            grass.id = fa::tilemap::TileId{kGrassTile};
            grass.name = "grass";
            grass.textureName = "tex.ground.grass";
            grass.tag = "ground";
            static_cast<void>(m_map.addTileDef(grass));

            fa::tilemap::TileDef wall;
            wall.id = fa::tilemap::TileId{kWallTile};
            wall.name = "wall";
            wall.textureName = "tex.ground.wall";
            wall.walkable = false;
            wall.solid = true;
            wall.tag = "wall";
            static_cast<void>(m_map.addTileDef(wall));

            fa::tilemap::TileDef soil;
            soil.id = fa::tilemap::TileId{kSoilTile};
            soil.name = "soil";
            soil.textureName = "tex.ground.soil";
            soil.tillable = true;
            soil.tag = "farmland";
            static_cast<void>(m_map.addTileDef(soil));

            static_cast<void>(m_map.setTileTexture(grass.id, m_grass));
            static_cast<void>(m_map.setTileTexture(wall.id, m_wall));
            static_cast<void>(m_map.setTileTexture(soil.id, m_soil));

            fa::tilemap::TileLayer ground;
            ground.name = "ground";
            ground.renderLayer = fa::render::RenderLayer::Ground;
            ground.tiles.assign(m_map.cellCount(), fa::tilemap::TileId{kGrassTile});
            static_cast<void>(m_map.addLayer(std::move(ground)));

            static_cast<void>(m_map.setBorder(0, fa::tilemap::TileId{kWallTile}));

            if (isFarm()) {
                for (int row = 0; row < kRows; ++row) {
                    static_cast<void>(m_map.setTile(kWallColumn, row, 0, fa::tilemap::TileId{kWallTile}));
                }
                // A tilled patch proves the tillable flag travels with the layer data and
                // that more than one tile definition can be drawn from the same map.
                for (int row = 8; row <= 11; ++row) {
                    for (int column = 4; column <= 7; ++column) {
                        static_cast<void>(m_map.setTile(column, row, 0, fa::tilemap::TileId{kSoilTile}));
                    }
                }

                // M19 reads the greenhouse property from the map, not a hard-coded cell.
                fa::tilemap::MapArea greenhouse;
                greenhouse.areaId = 1;
                greenhouse.kind = fa::tilemap::AreaKind::Greenhouse;
                greenhouse.bounds = fa::tilemap::TileBounds{7, 11, 1, 1};
                m_map.addArea(greenhouse);

                if (!m_scripted) {
                    // The built-in fallback must match `data/maps/farm.json`: the
                    // internal wall has a gate, so play mode can reach the east side
                    // even when the JSON file is not found.
                    for (int row = kGateFirstRow; row <= kGateLastRow; ++row) {
                        static_cast<void>(m_map.setTile(kWallColumn, row, 0, fa::tilemap::TileId{kGrassTile}));
                    }
                    // East gate to the forest; the west border stays solid.
                    for (int row = kGateFirstRow; row <= kGateLastRow; ++row) {
                        static_cast<void>(m_map.setTile(kColumns - 1, row, 0, fa::tilemap::TileId{kGrassTile}));
                    }
                    fa::tilemap::MapPortal toForest;
                    toForest.id = "to_forest";
                    toForest.bounds =
                        fa::tilemap::TileBounds{kColumns - 1, kGateFirstRow, 1, kGateLastRow - kGateFirstRow + 1};
                    toForest.targetMap = "forest";
                    toForest.targetTile = fa::tilemap::TileCoord{1, 11};
                    m_map.addPortal(toForest);
                }
            } else {
                // The forest is a separate map; the west gate returns to the farm.
                for (int row = kGateFirstRow; row <= kGateLastRow; ++row) {
                    static_cast<void>(m_map.setTile(0, row, 0, fa::tilemap::TileId{kGrassTile}));
                }
                fa::tilemap::MapPortal toFarm;
                toFarm.id = "to_farm";
                toFarm.bounds = fa::tilemap::TileBounds{0, kGateFirstRow, 1, kGateLastRow - kGateFirstRow + 1};
                toFarm.targetMap = "farm";
                toFarm.targetTile = fa::tilemap::TileCoord{kColumns - 3, 11};
                m_map.addPortal(toFarm);

                fa::tilemap::TileDef bramble;
                bramble.id = fa::tilemap::TileId{kBrambleTile};
                bramble.name = "bramble";
                bramble.textureName = "tex.ground.soil";
                bramble.walkable = true;
                bramble.tag = "hazard";
                static_cast<void>(m_map.addTileDef(bramble));
                static_cast<void>(m_map.setTileTexture(bramble.id, m_soil));
                std::vector<fa::tilemap::TileCoord> hazardTiles;
                for (const EntitySpawn* spawn : findSpawns("hazard")) {
                    if (spawn->hasCell) {
                        hazardTiles.push_back(spawn->cell);
                    }
                }
                if (hazardTiles.empty()) {
                    for (int column = 8; column <= 10; ++column) {
                        hazardTiles.push_back(fa::tilemap::TileCoord{column, 14});
                    }
                }
                for (const fa::tilemap::TileCoord& cell : hazardTiles) {
                    static_cast<void>(m_map.setTile(cell.x, cell.y, 0, fa::tilemap::TileId{kBrambleTile}));
                }

                // Tree clusters; the gate corridor (rows 10-13) stays clear.
                const fa::tilemap::TileCoord trees[] = {
                    {5, 4},   {6, 4},   {5, 5},  {12, 3},  {13, 3},  {20, 5},  {21, 5},  {28, 3},  {35, 5},
                    {36, 5},  {9, 8},   {15, 8}, {22, 9},  {30, 8},  {37, 9},  {7, 15},  {14, 16}, {21, 15},
                    {28, 16}, {35, 15}, {4, 20}, {11, 20}, {18, 20}, {25, 20}, {32, 20},
                };
                for (const fa::tilemap::TileCoord& cell : trees) {
                    static_cast<void>(m_map.setTile(cell.x, cell.y, 0, fa::tilemap::TileId{kWallTile}));
                }
            }
        }

        bindMapTextures();

        m_markerEntity = world().create();
        fa::scene::Transform marker;
        marker.position = m_spawn.has_value() ? *m_spawn
                                              : (isFarm() ? fa::core::Vec2{320.0f, 200.0f}
                                                          : m_map.gridToWorld(fa::tilemap::TileCoord{1, 11}));
        static_cast<void>(world().add<fa::scene::Transform>(m_markerEntity, marker));

        fa::scene::Sprite sprite;
        sprite.texture = m_marker;
        sprite.size = fa::core::Vec2{kMarkerSize, kMarkerSize};
        sprite.tint = fa::core::Color{1.0f, 0.35f, 0.35f, 1.0f};
        sprite.layer = fa::render::RenderLayer::Actors;
        // Four-way walk art from `data/animations.json` + the sprite sheet; the tinted
        // marker dot stays as the fallback when either is unavailable.
        if (m_walkConfig != nullptr && m_walkConfig->valid) {
            const fa::render::TextureHandle walk = textureForName(m_walkConfig->textureName);
            if (walk.isValid()) {
                m_walkTexture = walk;
                sprite.texture = walk;
                sprite.size = fa::core::Vec2{m_walkConfig->spriteWidth, m_walkConfig->spriteHeight};
                // Keep the character's feet on the collider's bottom edge.
                sprite.offset = fa::core::Vec2{(kMarkerSize - m_walkConfig->spriteWidth) * 0.5f,
                                               kMarkerSize - m_walkConfig->spriteHeight};
                sprite.tint = fa::core::Color{1.0f, 1.0f, 1.0f, 1.0f};
                sprite.source = walkRect(walkRowFor(m_state.playerFacing), m_walkConfig->idleFrame);
            }
        }
        if (m_boatConfig != nullptr && m_boatConfig->valid) {
            m_boatTexture = textureForName(m_boatConfig->textureName);
        }
        if (m_idleConfig != nullptr && m_idleConfig->valid) {
            m_idleTexture = textureForName(m_idleConfig->textureName);
        }
        static_cast<void>(world().add<fa::scene::Sprite>(m_markerEntity, sprite));

        fa::physics::Collider body;
        body.size = fa::core::Vec2{kMarkerSize, kMarkerSize};
        body.layer = fa::physics::CollisionLayer::Player;
        body.mask = fa::physics::maskOf(fa::physics::CollisionLayer::World) | fa::physics::CollisionLayer::Trigger;
        static_cast<void>(world().add<fa::physics::Collider>(m_markerEntity, body));
        static_cast<void>(world().add<fa::physics::Velocity>(m_markerEntity, fa::physics::Velocity{}));
        fa::player::Stamina stamina;
        stamina.current = m_state.playerStamina;
        stamina.max = m_state.playerStaminaMax;
        fa::player::PlayerController::attach(world(), m_markerEntity, stamina, m_state.playerFacing);

        if (isFarm()) {
            // A bed beside the start position: E within 28 px advances the day and
            // triggers M25's end-of-day settlement + autosave.
            m_bedEntity = world().create();
            const EntitySpawn* bedSpawn = m_scripted ? nullptr : findSpawn("bed");
            fa::scene::Transform bedTransform;
            bedTransform.position = bedSpawn != nullptr ? spawnPosition(*bedSpawn) : fa::core::Vec2{336.0f, 200.0f};
            static_cast<void>(world().add<fa::scene::Transform>(m_bedEntity, bedTransform));

            fa::scene::Sprite bedSprite;
            bedSprite.texture = bedSpawn != nullptr ? textureForName(bedSpawn->texture) : m_soil;
            bedSprite.size = fa::core::Vec2{16.0f, 16.0f};
            bedSprite.layer = fa::render::RenderLayer::Objects;
            bedSprite.tint = bedSpawn != nullptr ? bedSpawn->tint : fa::core::Color{0.35f, 0.65f, 1.0f, 1.0f};
            static_cast<void>(world().add<fa::scene::Sprite>(m_bedEntity, bedSprite));

            fa::interaction::Interactable bedInteractable;
            bedInteractable.area = fa::core::Rect{0.0f, 0.0f, 16.0f, 16.0f};
            bedInteractable.priority = 10;
            bedInteractable.payload = bedSpawn != nullptr ? bedSpawn->payload : 99;
            static_cast<void>(world().add<fa::interaction::Interactable>(m_bedEntity, bedInteractable));

            if (!m_scripted) {
                // Play-only props: a shipping box for crops and a merchant for the shop.
                const auto addProp = [this](fa::core::Vec2 position,
                                            fa::render::TextureHandle texture,
                                            fa::core::Color tint,
                                            int payload,
                                            fa::scene::EntityId& out) {
                    out = world().create();
                    fa::scene::Transform transform;
                    transform.position = position;
                    static_cast<void>(world().add<fa::scene::Transform>(out, transform));

                    fa::scene::Sprite sprite;
                    sprite.texture = texture;
                    sprite.size = fa::core::Vec2{16.0f, 16.0f};
                    sprite.layer = fa::render::RenderLayer::Objects;
                    sprite.tint = tint;
                    static_cast<void>(world().add<fa::scene::Sprite>(out, sprite));

                    fa::interaction::Interactable interactable;
                    interactable.area = fa::core::Rect{0.0f, 0.0f, 16.0f, 16.0f};
                    interactable.priority = 10;
                    interactable.payload = payload;
                    static_cast<void>(world().add<fa::interaction::Interactable>(out, interactable));
                };
                const auto addSpawnProp = [&](const char* kind,
                                              fa::core::Vec2 fallbackPosition,
                                              fa::core::Color fallbackTint,
                                              int fallbackPayload,
                                              fa::scene::EntityId& out) {
                    const EntitySpawn* spawn = findSpawn(kind);
                    addProp(spawn != nullptr ? spawnPosition(*spawn) : fallbackPosition,
                            spawn != nullptr ? textureForName(spawn->texture) : m_soil,
                            spawn != nullptr ? spawn->tint : fallbackTint,
                            spawn != nullptr ? spawn->payload : fallbackPayload,
                            out);
                };
                addSpawnProp("shipping",
                             fa::core::Vec2{352.0f, 216.0f},
                             fa::core::Color{0.90f, 0.55f, 0.20f, 1.0f},
                             100,
                             m_shippingEntity);
                addSpawnProp("merchant",
                             fa::core::Vec2{288.0f, 232.0f},
                             fa::core::Color{0.80f, 0.40f, 0.90f, 1.0f},
                             101,
                             m_merchantEntity);
            }

            // A trigger volume on the marker's path, tall enough that the marker cannot slip
            // around it, so both entering and leaving are observed in the same run.
            const fa::scene::EntityId trigger = world().create();
            fa::scene::Transform triggerTransform;
            triggerTransform.position = fa::core::Vec2{330.0f, 196.0f};
            static_cast<void>(world().add<fa::scene::Transform>(trigger, triggerTransform));

            fa::physics::Collider volume;
            volume.size = fa::core::Vec2{8.0f, 40.0f};
            volume.layer = fa::physics::CollisionLayer::Trigger;
            volume.mask = fa::physics::maskOf(fa::physics::CollisionLayer::Player);
            volume.isTrigger = true;
            static_cast<void>(world().add<fa::physics::Collider>(trigger, volume));

            // M17: one crate to target and one prop entity whose cell the smoke test claims.
            m_crateEntity = world().create();
            fa::scene::Transform crateTransform;
            crateTransform.position = m_map.gridToWorld(fa::tilemap::TileCoord{kCrateColumn, kCrateRow});
            static_cast<void>(world().add<fa::scene::Transform>(m_crateEntity, crateTransform));

            fa::scene::Sprite crateSprite;
            crateSprite.texture = m_chest.isValid() ? m_chest : m_soil;
            crateSprite.size =
                fa::core::Vec2{static_cast<float>(fa::render::kTileSize), static_cast<float>(fa::render::kTileSize)};
            crateSprite.layer = fa::render::RenderLayer::Objects;
            static_cast<void>(world().add<fa::scene::Sprite>(m_crateEntity, crateSprite));

            fa::interaction::Interactable crate;
            crate.area = fa::core::makeRect(
                0.0f, 0.0f, static_cast<float>(fa::render::kTileSize), static_cast<float>(fa::render::kTileSize));
            crate.priority = 1;
            crate.payload = kCratePayload;
            static_cast<void>(world().add<fa::interaction::Interactable>(m_crateEntity, crate));

            m_placedEntity = world().create();
            fa::scene::Transform placedTransform;
            placedTransform.position = m_map.gridToWorld(fa::tilemap::TileCoord{kPlacedColumn, kPlacedRow});
            static_cast<void>(world().add<fa::scene::Transform>(m_placedEntity, placedTransform));

            fa::scene::Sprite placedSprite;
            placedSprite.texture = m_wall;
            placedSprite.size = crateSprite.size;
            placedSprite.layer = fa::render::RenderLayer::Objects;
            static_cast<void>(world().add<fa::scene::Sprite>(m_placedEntity, placedSprite));

            fa::physics::Collider placedCollider;
            placedCollider.size = placedSprite.size;
            placedCollider.layer = fa::physics::CollisionLayer::World;
            placedCollider.mask = fa::physics::maskOf(fa::physics::CollisionLayer::Player);
            static_cast<void>(world().add<fa::physics::Collider>(m_placedEntity, placedCollider));
        }

        if (isFarm()) {
            // The animated grass quad and the AI agent/decoy pair are smoke fixtures:
            // they exist for the scripted and `--play-test` reports, never for `--play`.
            if (m_showcaseContent) {
                buildAnimation();
                buildAi();
            }
            buildNarrative();
        }
        buildCombat();
    }

    /// \brief Creates one animated grass quad for M10.
    ///
    /// Frames are the four quadrants of the placeholder grass texture. The entity has
    /// no collider, so it exercises animation and extraction without touching physics.
    void buildAnimation() {
        m_animEntity = world().create();
        fa::scene::Transform transform;
        transform.position = fa::core::Vec2{120.0f, 160.0f};
        static_cast<void>(world().add<fa::scene::Transform>(m_animEntity, transform));

        fa::scene::Sprite sprite;
        sprite.texture = m_grass;
        sprite.size = fa::core::Vec2{16.0f, 16.0f};
        sprite.source = fa::core::makeRect(0.0f, 0.0f, 8.0f, 8.0f);
        sprite.layer = fa::render::RenderLayer::Objects;
        static_cast<void>(world().add<fa::scene::Sprite>(m_animEntity, sprite));

        auto clip = std::make_shared<fa::anim::AnimationClip>();
        clip->texture = m_grass;
        clip->loop = true;
        clip->frames = {
            fa::anim::AnimationFrame{fa::core::makeRect(0.0f, 0.0f, 8.0f, 8.0f), 0.12f, "step"},
            fa::anim::AnimationFrame{fa::core::makeRect(8.0f, 0.0f, 8.0f, 8.0f), 0.12f, {}},
            fa::anim::AnimationFrame{fa::core::makeRect(0.0f, 8.0f, 8.0f, 8.0f), 0.12f, "step"},
            fa::anim::AnimationFrame{fa::core::makeRect(8.0f, 8.0f, 8.0f, 8.0f), 0.12f, {}},
        };
        m_animStats = AnimationRunStats{};
        m_animStats.frameCount = clip->frames.size();
        m_animService.play(m_animator, std::move(clip), true);
    }

    /// \brief Creates two enemy dummies and gives the marker a Health component.
    void buildCombat() {
        if (!world().has<fa::player::Health>(m_markerEntity)) {
            fa::player::Health health;
            health.current = 100;
            health.max = 100;
            static_cast<void>(world().add<fa::player::Health>(m_markerEntity, health));
        }
        // Only the forest is hostile: the farm's training dummies must not chase.
        if (!m_scripted && !isFarm() && !world().has<fa::ai::AiTargetTag>(m_markerEntity)) {
            static_cast<void>(world().add<fa::ai::AiTargetTag>(m_markerEntity, fa::ai::AiTargetTag{}));
        }
        if (!m_scripted) {
            buildAttackEffect();
        }

        const auto makeDummy = [this](fa::core::Vec2 position,
                                      int health,
                                      std::string_view loot,
                                      std::string_view textureName,
                                      fa::core::Color tint,
                                      const EntitySpawn* tuning) -> fa::scene::EntityId {
            const fa::scene::EntityId id = world().create();
            fa::scene::Transform transform;
            transform.position = position;
            static_cast<void>(world().add<fa::scene::Transform>(id, transform));

            fa::scene::Sprite sprite;
            if (!textureName.empty()) {
                sprite.texture = textureForName(textureName);
                sprite.tint = tint;
            } else if (m_slime.isValid()) {
                sprite.texture = m_slime;
                sprite.tint = fa::core::Color{1.0f, 1.0f, 1.0f, 1.0f};
            } else {
                sprite.texture = m_wall;
                sprite.tint = fa::core::Color{1.0f, 0.55f, 0.55f, 1.0f};
            }
            sprite.size = fa::core::Vec2{16.0f, 16.0f};
            sprite.layer = fa::render::RenderLayer::Objects;
            // Enemy walk sheets from `data/animations.json`: swap the static texture
            // for the clip and register per-entity playback state.
            const EnemyWalkAnimConfig* enemyCfg = nullptr;
            if (m_walkConfig != nullptr) {
                const std::string_view match = textureName.empty() ? std::string_view("tex.enemy.slime") : textureName;
                for (const EnemyWalkAnimConfig& e : m_walkConfig->enemies) {
                    if (e.matchTexture == match) {
                        enemyCfg = &e;
                        break;
                    }
                }
            }
            if (enemyCfg != nullptr) {
                const fa::render::TextureHandle sheet = textureForName(enemyCfg->textureName);
                if (sheet.isValid()) {
                    sprite.texture = sheet;
                    sprite.size = fa::core::Vec2{enemyCfg->frameWidth, enemyCfg->frameHeight};
                    sprite.tint = fa::core::Color{1.0f, 1.0f, 1.0f, 1.0f};
                    sprite.source = fa::core::makeRect(static_cast<float>(enemyCfg->idleFrame) * enemyCfg->frameWidth,
                                                       0.0f,
                                                       enemyCfg->frameWidth,
                                                       enemyCfg->frameHeight);
                } else {
                    enemyCfg = nullptr;
                }
            }
            static_cast<void>(world().add<fa::scene::Sprite>(id, sprite));
            if (enemyCfg != nullptr) {
                auto clip = std::make_shared<fa::anim::AnimationClip>();
                clip->texture = sprite.texture;
                clip->loop = true;
                for (int column = 0; column < enemyCfg->columns; ++column) {
                    const float duration = column < static_cast<int>(enemyCfg->durations.size())
                                               ? enemyCfg->durations[static_cast<std::size_t>(column)]
                                               : enemyCfg->durations.back();
                    clip->frames.push_back(
                        fa::anim::AnimationFrame{fa::core::makeRect(static_cast<float>(column) * enemyCfg->frameWidth,
                                                                    0.0f,
                                                                    enemyCfg->frameWidth,
                                                                    enemyCfg->frameHeight),
                                                 duration,
                                                 {}});
                }
                EnemyWalkState state;
                state.id = id;
                state.clip = std::move(clip);
                state.lastPos = position;
                state.cfg = enemyCfg;
                m_animService.play(state.animator, state.clip, true);
                m_enemyWalks.push_back(std::move(state));
            }

            fa::physics::Collider collider;
            collider.size = fa::core::Vec2{16.0f, 16.0f};
            collider.layer = fa::physics::CollisionLayer::Enemy;
            collider.mask =
                fa::physics::maskOf(fa::physics::CollisionLayer::World) | fa::physics::CollisionLayer::PlayerAttack;
            static_cast<void>(world().add<fa::physics::Collider>(id, collider));

            fa::player::Health hp;
            hp.current = health;
            hp.max = health;
            static_cast<void>(world().add<fa::player::Health>(id, hp));

            fa::combat::LootSource lootSource;
            lootSource.lootTable = std::string(loot);
            static_cast<void>(world().add<fa::combat::LootSource>(id, lootSource));

            // Forest enemies chase and hit the player; farm dummies stay as training
            // targets for the scripted smoke and the manual test route. Tuning comes
            // from `data/entities.json`, so a new enemy is data-only.
            if (!m_scripted && !isFarm()) {
                fa::ai::AiAgent agent;
                agent.home = fa::core::Vec2{position.x + 8.0f, position.y + 8.0f};
                agent.profile.sightRadius = tuning != nullptr ? tuning->sightRadius : 150.0f;
                agent.profile.attackRange = tuning != nullptr ? tuning->attackRange : 22.0f;
                agent.profile.moveSpeed = tuning != nullptr ? tuning->moveSpeed : 42.0f;
                agent.profile.attackDamage = tuning != nullptr ? tuning->attackDamage : 6;
                agent.profile.attackCooldown = tuning != nullptr ? tuning->attackCooldown : 1.2f;
                agent.profile.fleeHealthRatio = 0.0f;
                agent.profile.loseTargetSeconds = 4.0f;
                agent.profile.repathSeconds = 0.6f;
                agent.profile.perceptionIntervalSteps = 6;
                agent.profile.attackMask = fa::physics::maskOf(fa::physics::CollisionLayer::Player);
                static_cast<void>(world().add<fa::ai::AiAgent>(id, agent));
            }
            return id;
        };

        if (isFarm()) {
            // Training dummies belong to the automated modes; `--play` starts with an
            // empty farm.
            if (m_showcaseContent) {
                m_combatDummy1 = makeDummy(fa::core::Vec2{300.0f, 220.0f},
                                           20,
                                           "loot.test",
                                           {},
                                           fa::core::Color{1.0f, 1.0f, 1.0f, 1.0f},
                                           nullptr);
                m_combatDummy2 = makeDummy(fa::core::Vec2{300.0f, 240.0f},
                                           30,
                                           "loot.slime",
                                           {},
                                           fa::core::Color{1.0f, 1.0f, 1.0f, 1.0f},
                                           nullptr);
            }
            return;
        }

        const std::vector<const EntitySpawn*> enemies = findSpawns("enemy");
        const auto spawnEnemy =
            [&](std::size_t index, fa::core::Vec2 fallbackPosition, int fallbackHealth, std::string_view fallbackLoot) {
                const EntitySpawn* spawn = index < enemies.size() ? enemies[index] : nullptr;
                const fa::core::Vec2 position = spawn != nullptr ? spawnPosition(*spawn) : fallbackPosition;
                const int health = spawn != nullptr ? spawn->health : fallbackHealth;
                const std::string loot = spawn != nullptr ? spawn->loot : std::string(fallbackLoot);
                const fa::scene::EntityId id =
                    makeDummy(position,
                              health,
                              loot,
                              spawn != nullptr ? spawn->texture : std::string_view{},
                              spawn != nullptr ? spawn->tint : fa::core::Color{1.0f, 1.0f, 1.0f, 1.0f},
                              spawn);
                m_enemyIds.push_back(id);
                m_enemyRoles.push_back(
                    index == 0
                        ? "dummy1"
                        : (index == 1 ? "dummy2" : (spawn != nullptr ? spawn->id : "enemy" + std::to_string(index))));
                return id;
            };
        if (enemies.empty()) {
            // Fallback dummies only for the automated modes; `--play` forest stays empty
            // until real spawn rows are authored.
            if (m_showcaseContent) {
                m_forestDummy1 = spawnEnemy(0, fa::core::Vec2{320.0f, 128.0f}, 25, "loot.slime");
                m_forestDummy2 = spawnEnemy(1, fa::core::Vec2{400.0f, 224.0f}, 20, "loot.test");
            }
            return;
        }
        for (std::size_t index = 0; index < enemies.size(); ++index) {
            const fa::scene::EntityId id = spawnEnemy(index, fa::core::Vec2{}, 20, "loot.test");
            if (index == 0) {
                m_forestDummy1 = id;
            } else if (index == 1) {
                m_forestDummy2 = id;
            }
        }
        buildEnemyAttackEffects();
    }

    /// \brief Creates one M21 agent and a stationary decoy target.
    ///
    /// The decoy is deliberately separate from the M20 dummies: the AI chases and
    /// attacks it without moving the scripted combat targets.
    void buildAi() {
        const fa::core::Vec2 agentPosition{255.0f, 180.0f};
        m_aiAgentEntity = world().create();
        fa::scene::Transform agentTransform;
        agentTransform.position = agentPosition;
        static_cast<void>(world().add<fa::scene::Transform>(m_aiAgentEntity, agentTransform));

        fa::scene::Sprite agentSprite;
        agentSprite.texture = m_wall;
        agentSprite.size = fa::core::Vec2{16.0f, 16.0f};
        agentSprite.layer = fa::render::RenderLayer::Actors;
        agentSprite.tint = fa::core::Color{0.72f, 0.45f, 1.0f, 1.0f};
        static_cast<void>(world().add<fa::scene::Sprite>(m_aiAgentEntity, agentSprite));

        fa::physics::Collider agentCollider;
        agentCollider.size = fa::core::Vec2{16.0f, 16.0f};
        agentCollider.layer = fa::physics::CollisionLayer::Enemy;
        agentCollider.mask = fa::physics::maskOf(fa::physics::CollisionLayer::World);
        static_cast<void>(world().add<fa::physics::Collider>(m_aiAgentEntity, agentCollider));
        static_cast<void>(world().add<fa::player::Health>(m_aiAgentEntity, fa::player::Health{60, 60}));

        fa::ai::AiAgent agent;
        agent.home = fa::core::Vec2{agentPosition.x + 8.0f, agentPosition.y + 8.0f};
        agent.profile.sightRadius = 160.0f;
        agent.profile.attackRange = 24.0f;
        agent.profile.moveSpeed = 60.0f;
        agent.profile.attackDamage = 5;
        agent.profile.attackCooldown = 1.0f;
        agent.profile.fleeHealthRatio = 0.0f;
        agent.profile.loseTargetSeconds = 2.0f;
        agent.profile.repathSeconds = 0.5f;
        agent.profile.perceptionIntervalSteps = 6;
        agent.profile.attackMask = fa::physics::maskOf(fa::physics::CollisionLayer::Enemy);
        static_cast<void>(world().add<fa::ai::AiAgent>(m_aiAgentEntity, agent));

        m_aiTargetEntity = world().create();
        fa::scene::Transform targetTransform;
        targetTransform.position = fa::core::Vec2{300.0f, 180.0f};
        static_cast<void>(world().add<fa::scene::Transform>(m_aiTargetEntity, targetTransform));

        fa::scene::Sprite targetSprite;
        targetSprite.texture = m_soil;
        targetSprite.size = fa::core::Vec2{16.0f, 16.0f};
        targetSprite.layer = fa::render::RenderLayer::Objects;
        targetSprite.tint = fa::core::Color{0.45f, 0.70f, 1.0f, 1.0f};
        static_cast<void>(world().add<fa::scene::Sprite>(m_aiTargetEntity, targetSprite));

        fa::physics::Collider targetCollider;
        targetCollider.size = fa::core::Vec2{16.0f, 16.0f};
        targetCollider.layer = fa::physics::CollisionLayer::Enemy;
        targetCollider.mask = 0;
        static_cast<void>(world().add<fa::physics::Collider>(m_aiTargetEntity, targetCollider));
        static_cast<void>(world().add<fa::player::Health>(m_aiTargetEntity, fa::player::Health{50, 50}));
        static_cast<void>(world().add<fa::ai::AiTargetTag>(m_aiTargetEntity, fa::ai::AiTargetTag{}));
    }

    /// \brief Scripted M20 sequence: two melee hits, one projectile and poison.
    void updateCombat(float deltaSeconds) {
        if (m_combat == nullptr) {
            return;
        }

        const fa::scene::Transform* marker = world().tryGet<fa::scene::Transform>(m_markerEntity);
        if (m_scripted && (m_combatStats.updates == 30 || m_combatStats.updates == 50)) {
            if (marker != nullptr) {
                fa::combat::AttackHitbox hitbox;
                hitbox.offset = fa::core::Vec2{-40.0f, -16.0f};
                hitbox.size = fa::core::Vec2{40.0f, 48.0f};
                hitbox.activeFrom = 0.0f;
                hitbox.activeTo = 0.05f;
                hitbox.damage = 10;
                hitbox.targetMask = fa::physics::maskOf(fa::physics::CollisionLayer::Enemy);
                hitbox.attackId = static_cast<std::uint32_t>(m_combatStats.updates);
                m_combat->spawnAttack(world(), m_markerEntity, hitbox);
            }
        }

        if (m_scripted && m_combatStats.updates == 30 && marker != nullptr) {
            fa::combat::ProjectileDesc projectile;
            projectile.position = marker->position + fa::core::Vec2{0.0f, 20.0f};
            projectile.velocity = fa::core::Vec2{-160.0f, 0.0f};
            projectile.lifetime = 0.5f;
            projectile.damage = 5;
            projectile.targetMask = fa::physics::maskOf(fa::physics::CollisionLayer::Enemy);
            projectile.owner = m_markerEntity;
            const fa::scene::EntityId entity = m_combat->spawnProjectile(world(), projectile);
            if (entity.isValid()) {
                fa::scene::Sprite sprite;
                sprite.texture = m_marker;
                sprite.size = fa::core::Vec2{4.0f, 4.0f};
                sprite.layer = fa::render::RenderLayer::Objects;
                sprite.tint = fa::core::Color{1.0f, 1.0f, 0.5f, 1.0f};
                static_cast<void>(world().add<fa::scene::Sprite>(entity, sprite));
            }
        }

        if (m_scripted && m_combatStats.updates == 35) {
            fa::combat::StatusEffect poison;
            poison.kind = fa::combat::StatusKind::Poison;
            poison.duration = 1.0f;
            poison.magnitude = 4.0f;
            poison.source = m_markerEntity;
            static_cast<void>(m_combat->applyStatus(world(), m_combatDummy2, poison));
        }

        m_combat->update(world(), deltaSeconds);
        ++m_combatStats.updates;

        const fa::combat::CombatStats& stats = m_combat->stats();
        m_combatStats.attacks = stats.attacks;
        m_combatStats.attackHits = stats.attackHits;
        m_combatStats.damageDealt = stats.damageDealt;
        m_combatStats.kills = stats.kills;
        m_combatStats.projectiles = stats.projectilesSpawned;
        m_combatStats.projectileHits = stats.projectileHits;
        m_combatStats.projectileWallHits = stats.projectileWallHits;
        m_combatStats.statusApplications = stats.statusApplications;
        m_combatStats.statusTicks = stats.statusTicks;
        m_combatStats.damageEvents += m_combat->damageEvents().size();
        m_combatStats.deathEvents += m_combat->deathEvents().size();
        m_combatStats.projectileEvents += m_combat->projectileEvents().size();
        m_combatStats.statusEvents += m_combat->statusEvents().size();
        if (m_scripted) {
            m_combat->clearEvents();
        }
    }

    /// \brief Advances the player swing independently of the decorative grass clip.
    ///
    /// The forest map has no grass animator, so this must not sit behind the
    /// `m_animator.clip == nullptr` early return or forest attacks never hit.
    void updateAttackEffect(float deltaSeconds) {
        if (!m_slashActive || !m_slashClip) {
            return;
        }
        m_animService.update(m_slashAnimator, deltaSeconds);
        updateSlashTransform();
        if (fa::scene::Sprite* sprite = world().tryGet<fa::scene::Sprite>(m_slashEntity); sprite != nullptr) {
            if (const fa::anim::AnimationFrame* frame = m_animService.currentFrame(m_slashAnimator); frame != nullptr) {
                sprite->texture = m_slashClip->texture;
                sprite->source = frame->source;
            }
        }
        for (const fa::anim::AnimationEvent& event : m_slashAnimator.events) {
            if (event.name == "hit") {
                static_cast<void>(playerAttack(m_slashDamage, m_slashReach));
            }
        }
        if (m_slashAnimator.finished) {
            m_slashActive = false;
            if (fa::scene::Sprite* sprite = world().tryGet<fa::scene::Sprite>(m_slashEntity); sprite != nullptr) {
                sprite->tint.a = 0.0f;
            }
        }
    }

    /// \brief Sprite-sheet row index for the player's cardinal facing.
    [[nodiscard]] int walkRowFor(fa::player::Facing facing) const noexcept {
        if (m_walkConfig == nullptr) {
            return 0;
        }
        switch (fa::player::cardinalFacing(facing)) {
            case fa::player::Facing::North:
                return m_walkConfig->rowUp;
            case fa::player::Facing::West:
                return m_walkConfig->rowLeft;
            case fa::player::Facing::East:
                return m_walkConfig->rowRight;
            default:
                return m_walkConfig->rowDown;
        }
    }

    /// \brief Source rect of one walk-sheet cell.
    [[nodiscard]] fa::core::Rect walkRect(int row, int column) const noexcept {
        if (m_walkConfig == nullptr) {
            return fa::core::makeRect(0.0f, 0.0f, 0.0f, 0.0f);
        }
        return fa::core::makeRect(static_cast<float>(column) * m_walkConfig->frameWidth,
                                  static_cast<float>(row) * m_walkConfig->frameHeight,
                                  m_walkConfig->frameWidth,
                                  m_walkConfig->frameHeight);
    }

    /// \brief Lazily built walk/run clip for one sheet row; index = row * 2 + run.
    [[nodiscard]] const std::shared_ptr<const fa::anim::AnimationClip>& walkClip(int row, bool running) {
        const int index = row * 2 + (running ? 1 : 0);
        if (index < 0 || static_cast<std::size_t>(index) >= m_walkClips.size()) {
            static const std::shared_ptr<const fa::anim::AnimationClip> kNone;
            return kNone;
        }
        if (m_walkClips[static_cast<std::size_t>(index)] == nullptr && m_walkConfig != nullptr &&
            m_walkTexture.isValid()) {
            const std::vector<float>& durations = running ? m_walkConfig->runDurations : m_walkConfig->walkDurations;
            auto clip = std::make_shared<fa::anim::AnimationClip>();
            clip->texture = m_walkTexture;
            clip->loop = true;
            for (int column = 0; column < m_walkConfig->columns; ++column) {
                const float duration = column < static_cast<int>(durations.size())
                                           ? durations[static_cast<std::size_t>(column)]
                                           : (durations.empty() ? 0.12f : durations.back());
                clip->frames.push_back(fa::anim::AnimationFrame{walkRect(row, column), duration, {}});
            }
            m_walkClips[static_cast<std::size_t>(index)] = std::move(clip);
        }
        return m_walkClips[static_cast<std::size_t>(index)];
    }

    /// \brief Whether the cell the player faces is water (boarding check from the shore).
    [[nodiscard]] bool frontIsWater() const {
        const fa::interaction::GridTarget target = frontTarget();
        return target.valid && m_map.isWater(target.cell.x, target.cell.y);
    }

    /// \brief Board from the shore: the boat floats onto the water cell in front and
    /// the player moves with it; afterwards only water is passable.
    std::string requestBoardBoat() {
        if (m_boatMode) {
            return "已经在船上了";
        }
        const fa::interaction::GridTarget target = frontTarget();
        if (!target.valid || !m_map.isWater(target.cell.x, target.cell.y)) {
            return "面向水域按 G 才能上船";
        }
        if (fa::scene::Transform* transform = world().tryGet<fa::scene::Transform>(m_markerEntity);
            transform != nullptr) {
            transform->position = cellCenterOnMap(target.cell);
        }
        m_boatMode = true;
        m_map.setWaterPassable(true);
        return "上船了！靠近岸边按 G 跳岸下船";
    }

    /// \brief Disembark by hopping onto a shore cell adjacent to the boat. Refused
    /// when no walkable land touches the boat, so the boat never stays on land.
    std::string requestDisembark() {
        if (!m_boatMode) {
            return {};
        }
        const fa::tilemap::TileCoord here = m_map.worldToGrid(markerCenter());
        // Restore normal rules first: shore cells are only walkable when not riding.
        m_map.setWaterPassable(false);
        static constexpr int kDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& dir : kDirs) {
            const fa::tilemap::TileCoord cell{here.x + dir[0], here.y + dir[1]};
            if (m_map.isWater(cell.x, cell.y) || m_map.isSolidCell(cell.x, cell.y)) {
                continue;
            }
            if (fa::scene::Transform* transform = world().tryGet<fa::scene::Transform>(m_markerEntity);
                transform != nullptr) {
                transform->position = cellCenterOnMap(cell);
            }
            m_boatMode = false;
            m_boatSelection = -1;
            return "下船了";
        }
        m_map.setWaterPassable(true);
        return "船旁边没有岸边";
    }

    /// \brief Top-left of the 8x8 collider centered inside a map cell.
    [[nodiscard]] fa::core::Vec2 cellCenterOnMap(fa::tilemap::TileCoord cell) const {
        const float pad = static_cast<float>(m_map.tileSize() - static_cast<int>(kMarkerSize)) * 0.5f;
        return m_map.gridToWorld(cell) + fa::core::Vec2{pad, pad};
    }

    /// \brief Source rect of one boat-sheet cell (same row layout as the walk sheet).
    [[nodiscard]] fa::core::Rect boatRect(int row, int column) const noexcept {
        if (m_boatConfig == nullptr) {
            return fa::core::makeRect(0.0f, 0.0f, 0.0f, 0.0f);
        }
        return fa::core::makeRect(static_cast<float>(column) * m_boatConfig->frameWidth,
                                  static_cast<float>(row) * m_boatConfig->frameHeight,
                                  m_boatConfig->frameWidth,
                                  m_boatConfig->frameHeight);
    }

    /// \brief Lazily built rowing clip for one boat-sheet row.
    [[nodiscard]] const std::shared_ptr<const fa::anim::AnimationClip>& boatClip(int row) {
        if (row < 0 || row >= 4) {
            static const std::shared_ptr<const fa::anim::AnimationClip> kNone;
            return kNone;
        }
        if (m_boatClips[static_cast<std::size_t>(row)] == nullptr && m_boatConfig != nullptr &&
            m_boatTexture.isValid()) {
            auto clip = std::make_shared<fa::anim::AnimationClip>();
            clip->texture = m_boatTexture;
            clip->loop = true;
            for (int column = 0; column < m_boatConfig->columns; ++column) {
                const float duration =
                    column < static_cast<int>(m_boatConfig->walkDurations.size())
                        ? m_boatConfig->walkDurations[static_cast<std::size_t>(column)]
                        : (m_boatConfig->walkDurations.empty() ? 0.15f : m_boatConfig->walkDurations.back());
                clip->frames.push_back(fa::anim::AnimationFrame{boatRect(row, column), duration, {}});
            }
            m_boatClips[static_cast<std::size_t>(row)] = std::move(clip);
        }
        return m_boatClips[static_cast<std::size_t>(row)];
    }

    /// \brief Rowing playback while riding; true when this frame belongs to the boat.
    ///
    /// Riding persists after landing: disembarking is a deliberate G press, refused
    /// while the boat floats on water.
    bool updateBoatAnimation(const fa::player::PlayerAnimationParams& params,
                             fa::scene::Sprite& sprite,
                             float deltaSeconds) {
        if (!m_boatMode || m_boatConfig == nullptr || !m_boatConfig->valid || !m_boatTexture.isValid()) {
            return false;
        }
        sprite.texture = m_boatTexture;
        sprite.size = fa::core::Vec2{m_boatConfig->spriteWidth, m_boatConfig->spriteHeight};
        sprite.offset =
            fa::core::Vec2{(kMarkerSize - m_boatConfig->spriteWidth) * 0.5f, kMarkerSize - m_boatConfig->spriteHeight};
        const int row = walkRowFor(params.facing);
        if (!params.moving) {
            m_boatSelection = -1;
            m_animService.stop(m_boatAnimator);
            sprite.source = boatRect(row, m_boatConfig->idleFrame);
            return true;
        }
        const auto& clip = boatClip(row);
        if (clip == nullptr) {
            return true;
        }
        if (row != m_boatSelection) {
            m_animService.play(m_boatAnimator, clip, true);
            m_boatSelection = row;
        }
        static_cast<void>(m_animService.update(m_boatAnimator, deltaSeconds));
        if (const fa::anim::AnimationFrame* frame = m_animService.currentFrame(m_boatAnimator); frame != nullptr) {
            sprite.source = frame->source;
        }
        return true;
    }

    /// \brief Plays the breathing idle clip (player.png sheet) while standing.
    /// \return false when idle art is unavailable and the caller should fall back.
    bool updateIdleAnimation(fa::scene::Sprite& sprite, float deltaSeconds) {
        if (m_idleConfig == nullptr || !m_idleConfig->valid || !m_idleTexture.isValid()) {
            return false;
        }
        if (m_idleClip == nullptr) {
            auto clip = std::make_shared<fa::anim::AnimationClip>();
            clip->texture = m_idleTexture;
            clip->loop = true;
            for (int column = 0; column < m_idleConfig->columns; ++column) {
                const float duration = column < static_cast<int>(m_idleConfig->walkDurations.size())
                                           ? m_idleConfig->walkDurations[static_cast<std::size_t>(column)]
                                           : 0.35f;
                clip->frames.push_back(
                    fa::anim::AnimationFrame{fa::core::makeRect(static_cast<float>(column) * m_idleConfig->frameWidth,
                                                                0.0f,
                                                                m_idleConfig->frameWidth,
                                                                m_idleConfig->frameHeight),
                                             duration,
                                             {}});
            }
            m_idleClip = std::move(clip);
        }
        sprite.texture = m_idleTexture;
        sprite.size = fa::core::Vec2{m_idleConfig->spriteWidth, m_idleConfig->spriteHeight};
        sprite.offset =
            fa::core::Vec2{(kMarkerSize - m_idleConfig->spriteWidth) * 0.5f, kMarkerSize - m_idleConfig->spriteHeight};
        if (!m_idleActive) {
            m_animService.play(m_idleAnimator, m_idleClip, true);
            m_idleActive = true;
        }
        static_cast<void>(m_animService.update(m_idleAnimator, deltaSeconds));
        if (const fa::anim::AnimationFrame* frame = m_animService.currentFrame(m_idleAnimator); frame != nullptr) {
            sprite.source = frame->source;
        }
        return true;
    }

    /// \brief Plays the four-way player walk clip and copies frames onto the sprite.
    ///
    /// Idle stands on the configured idle frame of the facing row; running switches to
    /// the fast durations once the M16 speed crosses the JSON threshold.
    void updatePlayerAnimation(float deltaSeconds) {
        if (m_walkConfig == nullptr || !m_walkConfig->valid || !m_walkTexture.isValid()) {
            return;
        }
        fa::scene::Sprite* sprite = world().tryGet<fa::scene::Sprite>(m_markerEntity);
        if (sprite == nullptr) {
            return;
        }
        const fa::player::PlayerAnimationParams params = playerAnimation();
        if (updateBoatAnimation(params, *sprite, deltaSeconds)) {
            return;
        }
        // Standing: the dedicated idle clip owns the sprite; the walk branch below
        // restores the walk sheet as soon as the player moves again.
        if (!params.moving && updateIdleAnimation(*sprite, deltaSeconds)) {
            m_walkActive = false;
            m_walkSelection = -1;
            m_walkRunLatch = false;
            m_walkConfirmedRow = walkRowFor(params.facing);
            m_walkPendingRow = m_walkConfirmedRow;
            m_walkPendingFrames = 0;
            m_animService.stop(m_playerAnimator);
            return;
        }
        m_idleActive = false;
        m_animService.stop(m_idleAnimator);
        // Moving (or no idle art): restore the walk sheet (idle owns the texture while standing).
        sprite->texture = m_walkTexture;
        sprite->size = fa::core::Vec2{m_walkConfig->spriteWidth, m_walkConfig->spriteHeight};
        sprite->offset =
            fa::core::Vec2{(kMarkerSize - m_walkConfig->spriteWidth) * 0.5f, kMarkerSize - m_walkConfig->spriteHeight};
        if (!params.moving) {
            m_walkActive = false;
            m_walkSelection = -1;
            m_walkRunLatch = false;
            m_walkConfirmedRow = walkRowFor(params.facing);
            m_walkPendingRow = m_walkConfirmedRow;
            m_walkPendingFrames = 0;
            m_animService.stop(m_playerAnimator);
            sprite->source = walkRect(m_walkConfirmedRow, m_walkConfig->idleFrame);
            return;
        }
        // Facing can wobble for a frame or two (input sampling, collision nudges);
        // require a few consecutive frames before switching rows so the sprite never
        // flashes the wrong direction mid-step.
        const int rawRow = walkRowFor(params.facing);
        if (rawRow != m_walkConfirmedRow) {
            if (rawRow == m_walkPendingRow) {
                ++m_walkPendingFrames;
                if (m_walkPendingFrames >= 3) {
                    m_walkConfirmedRow = rawRow;
                    m_walkPendingFrames = 0;
                }
            } else {
                m_walkPendingRow = rawRow;
                m_walkPendingFrames = 1;
            }
        } else {
            m_walkPendingFrames = 0;
        }
        if (m_walkConfirmedRow < 0) {
            m_walkConfirmedRow = rawRow;
        }
        const int row = m_walkConfirmedRow;
        // Hysteresis: leave "running" only below 85% of the threshold so speed noise
        // around the boundary cannot restart the clip every other frame.
        const float threshold = m_walkConfig->runSpeedThreshold;
        const bool running = m_walkRunLatch ? params.speed > threshold * 0.85f : params.speed > threshold;
        m_walkRunLatch = running;
        const int selection = row * 2 + (running ? 1 : 0);
        const auto& clip = walkClip(row, running);
        if (clip == nullptr) {
            return;
        }
        if (!m_walkActive || selection != m_walkSelection) {
            m_animService.play(m_playerAnimator, clip, true);
            m_walkActive = true;
            m_walkSelection = selection;
        }
        static_cast<void>(m_animService.update(m_playerAnimator, deltaSeconds));
        if (const fa::anim::AnimationFrame* frame = m_animService.currentFrame(m_playerAnimator); frame != nullptr) {
            sprite->source = frame->source;
        }
    }

    /// \brief Advances enemy walk clips; actual movement (position delta) gates play,
    /// so farm dummies stay on their idle frame and forest enemies bounce while chasing.
    void updateEnemyAnimations(float deltaSeconds) {
        for (EnemyWalkState& state : m_enemyWalks) {
            const fa::scene::Transform* transform = world().tryGet<fa::scene::Transform>(state.id);
            fa::scene::Sprite* sprite = world().tryGet<fa::scene::Sprite>(state.id);
            if (transform == nullptr || sprite == nullptr || state.clip == nullptr || state.cfg == nullptr) {
                continue;
            }
            const float dx = transform->position.x - state.lastPos.x;
            const float dy = transform->position.y - state.lastPos.y;
            const bool moving = dx * dx + dy * dy > 0.02f;
            state.lastPos = transform->position;
            if (moving) {
                m_animService.update(state.animator, deltaSeconds);
                if (const fa::anim::AnimationFrame* frame = m_animService.currentFrame(state.animator);
                    frame != nullptr) {
                    sprite->source = frame->source;
                }
            } else {
                sprite->source = fa::core::makeRect(static_cast<float>(state.cfg->idleFrame) * state.cfg->frameWidth,
                                                    0.0f,
                                                    state.cfg->frameWidth,
                                                    state.cfg->frameHeight);
            }
        }
    }

    /// \brief Whether an 8x8 body placed at `topLeft` overlaps any solid cell.
    [[nodiscard]] bool bodyBlockedAt(fa::core::Vec2 topLeft) const {
        const fa::core::Vec2 probes[4] = {
            {topLeft.x + 0.5f, topLeft.y + 0.5f},
            {topLeft.x + kMarkerSize - 0.5f, topLeft.y + 0.5f},
            {topLeft.x + 0.5f, topLeft.y + kMarkerSize - 0.5f},
            {topLeft.x + kMarkerSize - 0.5f, topLeft.y + kMarkerSize - 0.5f},
        };
        for (const fa::core::Vec2& probe : probes) {
            const fa::tilemap::TileCoord cell = m_map.worldToGrid(probe);
            if (m_map.isSolidCell(cell.x, cell.y)) {
                return true;
            }
        }
        return false;
    }

    /// \brief E skill: teleport along the facing direction onto the farthest clear spot.
    /// \return Empty on success, otherwise the localized failure reason.
    std::string tryTeleport() {
        if (m_skillConfig == nullptr) {
            return {};
        }
        if (m_boatMode) {
            return "划船时不能瞬移";
        }
        if (m_teleportCooldown > 0.0f) {
            return "瞬移冷却中";
        }
        fa::player::Stamina* stamina = world().tryGet<fa::player::Stamina>(m_markerEntity);
        if (stamina != nullptr && stamina->current < m_skillConfig->teleportStaminaCost) {
            return "体力不足";
        }
        const fa::core::Vec2 dir = fa::player::directionVector(playerFacing());
        const fa::core::Vec2 from = markerPosition();
        for (float dist = m_skillConfig->teleportRange; dist >= 4.0f; dist -= 4.0f) {
            const fa::core::Vec2 to{from.x + dir.x * dist, from.y + dir.y * dist};
            if (bodyBlockedAt(to)) {
                continue;
            }
            spawnTeleportFx(from, to);
            fa::scene::Transform* transform = world().tryGet<fa::scene::Transform>(m_markerEntity);
            if (transform != nullptr) {
                transform->position = to;
            }
            if (stamina != nullptr) {
                stamina->current -= m_skillConfig->teleportStaminaCost;
            }
            m_teleportCooldown = m_skillConfig->teleportCooldown;
            return {};
        }
        return "前方无处瞬移";
    }

    /// \brief Q skill: fire a sword-qi projectile along the facing direction.
    /// \return Empty on success, otherwise the localized failure reason.
    std::string tryWeaponSkill(int weaponDamage) {
        if (m_skillConfig == nullptr) {
            return {};
        }
        if (m_qiCooldown > 0.0f) {
            return "剑气冷却中";
        }
        if (m_combat == nullptr) {
            return {};
        }
        fa::player::Stamina* stamina = world().tryGet<fa::player::Stamina>(m_markerEntity);
        if (stamina != nullptr && stamina->current < m_skillConfig->qiStaminaCost) {
            return "体力不足";
        }
        const fa::core::Vec2 dir = fa::player::directionVector(fa::player::cardinalFacing(playerFacing()));
        fa::combat::ProjectileDesc desc;
        desc.position = markerCenter() + dir * 10.0f;
        desc.velocity = dir * m_skillConfig->qiSpeed;
        desc.lifetime = m_skillConfig->qiLifetime;
        desc.damage =
            std::max(1, static_cast<int>(static_cast<float>(weaponDamage) * m_skillConfig->qiDamageMultiplier));
        desc.targetMask = fa::physics::maskOf(fa::physics::CollisionLayer::Enemy);
        desc.owner = m_markerEntity;
        const fa::scene::EntityId entity = m_combat->spawnProjectile(world(), desc);
        if (!entity.isValid()) {
            return {};
        }
        // Visual: the mid-swing slash frame, rotated along the flight direction.
        const fa::render::TextureHandle slash = textureForName("tex.effect.slash");
        if (slash.isValid()) {
            if (fa::scene::Transform* transform = world().tryGet<fa::scene::Transform>(entity); transform != nullptr) {
                transform->position = desc.position - fa::core::Vec2{8.0f, 8.0f};
                transform->rotation = std::atan2(dir.y, dir.x);
            }
            fa::scene::Sprite sprite;
            sprite.texture = slash;
            sprite.size = fa::core::Vec2{16.0f, 16.0f};
            sprite.source = fa::core::makeRect(16.0f, 0.0f, 16.0f, 16.0f);
            sprite.layer = fa::render::RenderLayer::Actors;
            sprite.tint = fa::core::Color{0.70f, 0.90f, 1.0f, 1.0f};
            static_cast<void>(world().add<fa::scene::Sprite>(entity, sprite));
        }
        if (stamina != nullptr) {
            stamina->current -= m_skillConfig->qiStaminaCost;
        }
        m_qiCooldown = m_skillConfig->qiCooldown;
        return {};
    }

    /// \brief Reusable cyan ring pair marking the teleport origin and destination.
    void spawnTeleportFx(fa::core::Vec2 from, fa::core::Vec2 to) {
        for (int index = 0; index < 2; ++index) {
            const fa::core::Vec2 pos = (index == 0 ? from : to) - fa::core::Vec2{2.0f, 2.0f};
            if (!m_tpFx[index].isValid()) {
                m_tpFx[index] = world().create();
                fa::scene::Transform transform;
                static_cast<void>(world().add<fa::scene::Transform>(m_tpFx[index], transform));
                fa::scene::Sprite sprite;
                sprite.texture = m_marker;
                sprite.size = fa::core::Vec2{12.0f, 12.0f};
                sprite.layer = fa::render::RenderLayer::Actors;
                static_cast<void>(world().add<fa::scene::Sprite>(m_tpFx[index], sprite));
            }
            if (fa::scene::Transform* transform = world().tryGet<fa::scene::Transform>(m_tpFx[index]);
                transform != nullptr) {
                transform->position = pos;
            }
            if (fa::scene::Sprite* sprite = world().tryGet<fa::scene::Sprite>(m_tpFx[index]); sprite != nullptr) {
                sprite->tint = fa::core::Color{0.55f, 0.85f, 1.0f, 1.0f};
            }
        }
        m_tpFxTimer = 0.25f;
    }

    /// \brief Fades the teleport rings and ticks the skill cooldowns on the fixed step.
    void updateSkillFx(float deltaSeconds) {
        m_teleportCooldown = std::max(0.0f, m_teleportCooldown - deltaSeconds);
        m_qiCooldown = std::max(0.0f, m_qiCooldown - deltaSeconds);
        if (m_tpFxTimer <= 0.0f) {
            return;
        }
        m_tpFxTimer = std::max(0.0f, m_tpFxTimer - deltaSeconds);
        const float alpha = m_tpFxTimer / 0.25f;
        for (const fa::scene::EntityId fx : m_tpFx) {
            if (fa::scene::Sprite* sprite = world().tryGet<fa::scene::Sprite>(fx); sprite != nullptr) {
                sprite->tint.a = alpha;
            }
        }
    }

    /// \brief Advances M10 on the fixed step and copies the current source rect.
    void updateAnimation(float deltaSeconds) {
        updateSkillFx(deltaSeconds);
        updatePlayerAnimation(deltaSeconds);
        updateEnemyAnimations(deltaSeconds);
        updateAttackEffect(deltaSeconds);
        updateEnemyAttackEffects(deltaSeconds);
        if (m_animEntity.index == 0 || m_animator.clip == nullptr) {
            return;
        }

        m_animService.update(m_animator, deltaSeconds);
        ++m_animStats.updates;
        m_animStats.frameIndex = m_animator.frameIndex;
        m_animStats.finished = m_animator.finished;

        if (const fa::anim::AnimationFrame* frame = m_animService.currentFrame(m_animator); frame != nullptr) {
            if (fa::scene::Sprite* sprite = world().tryGet<fa::scene::Sprite>(m_animEntity); sprite != nullptr) {
                sprite->texture = m_animator.clip->texture;
                sprite->source = frame->source;
            }
        }
        for (const fa::anim::AnimationEvent& event : m_animator.events) {
            ++m_animStats.events;
            m_animStats.lastEvent = event.name;
            m_animStats.lastEventFrame = event.frame;
        }
    }

    /// \brief Runs the one interaction/placement sequence the smoke summary reports.
    ///
    /// The cell and tool are fixed so the output is comparable across runs; the
    /// per-frame highlight is separate and does not add events.
    void runInteractionSmoke() {
        const fa::tilemap::TileCoord crateCell{kCrateColumn, kCrateRow};
        const fa::interaction::GridTarget target =
            m_interaction.pickTarget(world(), m_markerEntity, fa::core::center(m_map.tileRect(crateCell)));

        m_interactionRunStats.targetValid = target.valid;
        m_interactionRunStats.targetReachable = target.reachable;
        m_interactionRunStats.targetIsEntity = target.entity.isValid();
        m_interactionRunStats.targetDistance = target.distance;
        m_interactionRunStats.targetCell = target.cell;

        fa::interaction::InteractionRequest use;
        use.actor = m_markerEntity;
        use.target = target;
        use.tool = fa::interaction::ToolKind::Sword;
        use.payload = kToolPayload;
        m_interactionRunStats.toolUsed = m_interaction.interact(world(), use).isOk();
        m_interactionRunStats.toolUses = m_toolHandler.uses;

        fa::interaction::PlacementRequest place;
        place.actor = m_markerEntity;
        place.owner = m_placedEntity;
        place.cell = fa::tilemap::TileCoord{kPlacedColumn, kPlacedRow};
        place.size =
            fa::core::Vec2{static_cast<float>(fa::render::kTileSize), static_cast<float>(fa::render::kTileSize)};
        place.payload = kPlacementPayload;

        m_interactionRunStats.previewBefore = m_interaction.previewPlacement(world(), place).valid;
        m_interactionRunStats.placementOk = m_interaction.requestPlacement(world(), place).isOk();
        if (m_interactionRunStats.placementOk) {
            ++m_interactionRunStats.placements;
        }
        m_interactionRunStats.previewAfter = m_interaction.previewPlacement(world(), place).valid;
        m_interactionRunStats.events = m_interaction.events().size();
        m_interactionRunStats.occupiedCells = m_placement.occupiedCells();
    }

    /// \brief Dispatches one farming tool without going through target selection.
    fa::core::Status useFarmingTool(fa::tilemap::TileCoord cell, fa::interaction::ToolKind tool) {
        fa::interaction::InteractionRequest request;
        request.actor = m_markerEntity;
        request.target.cell = cell;
        request.target.tile = m_map.tileAt(cell.x, cell.y, 0);
        request.target.valid = true;
        request.target.reachable = true;
        request.tool = tool;
        const fa::interaction::ToolUseContext context{world(), m_map, &m_placement, request};
        return m_farmingTools.use(context);
    }

    /// \brief Runs the scripted M19 sequence the smoke summary reports.
    ///
    /// The plot cycles through hoe/seed/water/harvest, then proves both sides of
    /// the season rule: a normal cell wilts in winter, a greenhouse cell survives.
    void runFarmingSmoke() {
        using fa::content::ContentId;
        using fa::content::Season;
        using fa::interaction::ToolKind;

        m_farmingRunStats = FarmingRunStats{};
        const ContentId seed = ContentId::fromName("item.turnip_seed");
        const ContentId crop = ContentId::fromName("item.turnip");
        m_farming.setCurrentSeason(Season::Spring);
        m_farmingTools.setSelectedSeed(seed);
        static_cast<void>(m_farmInventory.add(fa::inventory::ItemStack{seed, 5, -1}));

        const fa::tilemap::TileCoord plot{4, 8};
        m_farmingRunStats.hoeOk = useFarmingTool(plot, ToolKind::Hoe).isOk();
        m_farmingRunStats.seedOk = useFarmingTool(plot, ToolKind::Seed).isOk();
        m_farmingRunStats.waterOk = useFarmingTool(plot, ToolKind::WateringCan).isOk();
        for (int day = 0; day < 5; ++day) {
            if (day > 0) {
                static_cast<void>(useFarmingTool(plot, ToolKind::WateringCan));
            }
            m_farming.onDayChanged(fa::farming::FarmingDayContext{Season::Spring, false, 1.0f});
        }
        if (const fa::farming::CropState* grown = m_farming.cropAt(plot); grown != nullptr) {
            m_farmingRunStats.stage = grown->stage;
            m_farmingRunStats.harvestable = grown->harvestable;
        }
        m_farmingRunStats.harvestOk = useFarmingTool(plot, ToolKind::Harvest).isOk();
        m_farmingRunStats.harvestCount = m_farmInventory.countOf(crop);

        m_farming.setCurrentSeason(Season::Spring);
        const fa::tilemap::TileCoord greenhouse{7, 11};
        static_cast<void>(useFarmingTool(greenhouse, ToolKind::Hoe));
        static_cast<void>(useFarmingTool(greenhouse, ToolKind::Seed));
        static_cast<void>(useFarmingTool(greenhouse, ToolKind::WateringCan));
        m_farming.onDayChanged(fa::farming::FarmingDayContext{Season::Winter, false, 1.0f});
        if (const fa::farming::CropState* surviving = m_farming.cropAt(greenhouse); surviving != nullptr) {
            m_farmingRunStats.greenhouseOk = !surviving->dead;
        }

        m_farming.setCurrentSeason(Season::Spring);
        const fa::tilemap::TileCoord outdoor{6, 9};
        static_cast<void>(useFarmingTool(outdoor, ToolKind::Hoe));
        static_cast<void>(useFarmingTool(outdoor, ToolKind::Seed));
        static_cast<void>(useFarmingTool(outdoor, ToolKind::WateringCan));
        m_farming.onDayChanged(fa::farming::FarmingDayContext{Season::Winter, false, 1.0f});
        if (const fa::farming::CropState* wilted = m_farming.cropAt(outdoor); wilted != nullptr) {
            m_farmingRunStats.wiltedOk = wilted->dead;
        }

        m_farmingRunStats.seedCount = m_farmInventory.countOf(seed);
        m_farmingRunStats.stats = m_farming.stats();
    }

    /// \brief Creates the NPC the player talks to, from an `npc` spawn row.
    ///
    /// A map with no `npc` row simply has nobody to talk to: that is how the placeholder world
    /// stays free of scripted content while the showcase farm keeps its elder. The row also picks
    /// the dialogue graph, so a new town can hand out a different quest without code changes.
    void buildNarrative() {
        const EntitySpawn* row = findSpawn("npc");
        if (row == nullptr) {
            return;
        }
        if (!row->dialogue.empty()) {
            m_npcDialogue = row->dialogue;
        }

        m_npcEntity = world().create();
        fa::scene::Transform transform;
        transform.position = spawnPosition(*row);
        static_cast<void>(world().add<fa::scene::Transform>(m_npcEntity, transform));

        fa::scene::Sprite sprite;
        sprite.texture = m_marker;
        sprite.size = fa::core::Vec2{16.0f, 16.0f};
        sprite.tint = row->tint;
        sprite.layer = fa::render::RenderLayer::Actors;
        static_cast<void>(world().add<fa::scene::Sprite>(m_npcEntity, sprite));

        fa::interaction::Interactable interactable;
        interactable.area = fa::core::makeRect(0.0f, 0.0f, 16.0f, 16.0f);
        interactable.priority = 1;
        interactable.payload = 23;
        static_cast<void>(world().add<fa::interaction::Interactable>(m_npcEntity, interactable));
    }

    /// \brief Runs one full dialogue/quest/gift sequence, then leaves a dialogue open.
    void runNarrativeSmoke() {
        using fa::content::ContentId;
        const ContentId dialogue = ContentId::fromName("dialogue.elder");
        const ContentId elder = ContentId::fromName("npc.elder");
        const ContentId turnip = ContentId::fromName("item.turnip");
        const ContentId quest = ContentId::fromName("quest.turnip");

        if (!m_narrative.startDialogue(dialogue, m_npcEntity).isOk()) {
            return;
        }
        static_cast<void>(m_narrative.choose(0));
        static_cast<void>(m_narrative.advance());
        static_cast<void>(m_narrative.advance());

        fa::narrative::QuestEvent collected;
        collected.kind = fa::narrative::QuestObjectiveKind::Collect;
        collected.target = turnip;
        collected.amount = 1;
        m_narrative.reportEvent(collected);
        static_cast<void>(m_narrative.giveGift(elder, turnip, m_farmInventory));

        // Leave the second greeting open so the dialogue panel and choice routing
        // are exercised by the visible app, not only by the unit tests.
        static_cast<void>(m_narrative.startDialogue(dialogue, m_npcEntity));
    }

    fa::render::TextureHandle m_grass;
    fa::render::TextureHandle m_wall;
    fa::render::TextureHandle m_soil;
    fa::render::TextureHandle m_marker;
    fa::render::TextureHandle m_soilWet;
    fa::render::TextureHandle m_cropSeed;
    fa::render::TextureHandle m_cropSprout;
    fa::render::TextureHandle m_cropYoung;
    fa::render::TextureHandle m_cropMature;
    fa::render::TextureHandle m_chest;
    fa::render::TextureHandle m_slime;
    /// \brief Borrowed; the scene's PlayerSystem reads the same input as the app.
    fa::input::IInput& m_input;
    /// Declared before the systems are created so it outlives them: both systems
    /// borrow it, and `onExit` clears the pipeline before this member dies.
    fa::tilemap::Tilemap m_map;
    /// Non-owning bridge from the map to `PhysicsSystem`; declared after `m_map`.
    fa::tilemap::TilemapSolids m_solids{m_map};
    fa::interaction::PlacementGrid m_placement;
    CountingToolHandler m_toolHandler;
    /// \brief Borrowed session state that survives `SceneStack::replace`.
    GameState& m_state;
    /// \brief M19 state. The inventory is the smoke test's seed/harvest sink.
    fa::farming::FarmingService& m_farming;
    fa::inventory::Inventory& m_farmInventory;
    fa::farming::DebrisLayer& m_farmDebris;
    fa::farming::FarmingToolHandler& m_farmingTools;
    FarmingRunStats m_farmingRunStats;
    /// \brief M23 dialogue/quest state and the NPC entity it talks to.
    fa::narrative::NarrativeService& m_narrative;
    fa::scene::EntityId m_npcEntity;
    /// \brief Dialogue graph the map's NPC runs; an `npc` spawn row can override it.
    std::string m_npcDialogue = "dialogue.elder";
    /// Declared after its map, grid and handlers so those outlive it.
    fa::interaction::InteractionService m_interaction;
    /// \brief False in `--play` so startup does not run the scripted smoke tests.
    bool m_scripted = true;
    /// \brief Optional spawn table loaded from `data/entities.json`.
    const std::vector<EntitySpawn>* m_spawns = nullptr;
    /// \brief Which map this scene builds.
    GameMap m_mapKind = GameMap::Farm;
    /// \brief Map file id (`"farm"`, `"forest"`, `"play/farm"`); scopes the spawn table.
    std::string m_mapId;
    /// \brief True for the scripted smoke and `--play-test`: spawns the grass-clip,
    /// AI agent pair and combat dummies. `--play` runs with this false.
    bool m_showcaseContent = true;
    /// \brief Optional player spawn, set when entering through a portal or loading.
    std::optional<fa::core::Vec2> m_spawn;
    /// \brief True when the map came from JSON; false uses the built-in fallback.
    bool m_mapLoaded = false;
    fa::scene::EntityId m_markerEntity;
    fa::scene::EntityId m_crateEntity;
    fa::scene::EntityId m_bedEntity;
    fa::scene::EntityId m_shippingEntity;
    fa::scene::EntityId m_merchantEntity;
    fa::scene::EntityId m_placedEntity;
    fa::scene::EntityId m_animEntity;
    fa::anim::AnimationService m_animService;
    fa::anim::Animator m_animator;
    AnimationRunStats m_animStats;
    /// \brief JSON-driven four-way player walk (M10 clips + M16 state).
    const PlayerWalkAnimConfig* m_walkConfig = nullptr;
    fa::render::TextureHandle m_walkTexture;
    fa::anim::Animator m_playerAnimator;
    std::array<std::shared_ptr<const fa::anim::AnimationClip>, 8> m_walkClips{};
    int m_walkSelection = -1;
    bool m_walkActive = false;
    bool m_walkRunLatch = false;
    int m_walkConfirmedRow = -1;
    int m_walkPendingRow = -1;
    int m_walkPendingFrames = 0;
    /// \brief E/Q skill state (`data/skills.json` tuning).
    const SkillConfig* m_skillConfig = nullptr;
    float m_teleportCooldown = 0.0f;
    float m_qiCooldown = 0.0f;
    float m_tpFxTimer = 0.0f;
    std::array<fa::scene::EntityId, 2> m_tpFx{};
    /// \brief Boat riding: water passable + rowing animation while active.
    const PlayerWalkAnimConfig* m_boatConfig = nullptr;
    fa::render::TextureHandle m_boatTexture;
    bool m_boatMode = false;
    int m_boatSelection = -1;
    fa::anim::Animator m_boatAnimator;
    std::array<std::shared_ptr<const fa::anim::AnimationClip>, 4> m_boatClips{};
    /// \brief Idle breathing clip from `player.png` (tex.actor.player) while standing.
    const PlayerWalkAnimConfig* m_idleConfig = nullptr;
    fa::render::TextureHandle m_idleTexture;
    fa::anim::Animator m_idleAnimator;
    std::shared_ptr<const fa::anim::AnimationClip> m_idleClip;
    bool m_idleActive = false;
    /// \brief One animated enemy (walk sheet + playback state, driven by movement).
    struct EnemyWalkState {
        fa::scene::EntityId id;
        fa::anim::Animator animator;
        std::shared_ptr<const fa::anim::AnimationClip> clip;
        fa::core::Vec2 lastPos{};
        const EnemyWalkAnimConfig* cfg = nullptr;
    };
    std::vector<EnemyWalkState> m_enemyWalks;
    /// \brief Player swing effect; its `hit` frame starts the M20 hitbox.
    fa::scene::EntityId m_slashEntity;
    fa::anim::Animator m_slashAnimator;
    std::shared_ptr<const fa::anim::AnimationClip> m_slashClip;
    bool m_slashActive = false;
    int m_slashDamage = 0;
    float m_slashReach = 14.0f;
    /// \brief One reusable wind-up effect per forest enemy.
    std::vector<EnemySwing> m_enemySwings;
    std::unique_ptr<fa::combat::CombatService> m_combat;
    fa::scene::EntityId m_combatDummy1;
    fa::scene::EntityId m_combatDummy2;
    fa::scene::EntityId m_forestDummy1;
    fa::scene::EntityId m_forestDummy2;
    /// \brief Every enemy spawned from `data/entities.json`, with its save role.
    std::vector<fa::scene::EntityId> m_enemyIds;
    std::vector<std::string> m_enemyRoles;
    const std::unordered_map<std::string, fa::render::TextureHandle>* m_namedTextures = nullptr;
    CombatRunStats m_combatStats;
    /// \brief M21 service and the scripted agent/decoy pair.
    std::unique_ptr<fa::ai::AiService> m_ai;
    fa::scene::EntityId m_aiAgentEntity;
    fa::scene::EntityId m_aiTargetEntity;
    AiRunStats m_aiRunStats;
    fa::interaction::GridTarget m_cursorTarget;
    InteractionRunStats m_interactionRunStats;
    fa::player::PlayerSystem* m_playerSystem = nullptr;
    fa::physics::PhysicsSystem* m_physics = nullptr;
    fa::tilemap::TilemapExtractSystem* m_tileSystem = nullptr;
    bool m_built = false;
};

/// \brief Temporary sink: counts events and reports the frame timing it observes.
///
/// Stands in for the layers that will own this work later (M03 events, M06 render)
/// so the loop itself can be verified from the app. It also owns the M07 input system
/// and drives it in the order the module contract requires:
/// `poll(events)` in onEvents, read actions during the updates, `endFrame()` in onRender.
///
/// M06 is driven in the same hook: the map and the marker are extracted by the scene
/// stack into one render queue, which is a small but real exercise of batching,
/// sorting, the viewport mapping and the debug helpers. The draw items themselves come
/// from M11 and M12: the scene stack extracts the world and the tile map into one
/// render queue and the renderer takes it in one submitAll() call. M17 adds the
/// per-frame cursor target and the debug highlight; its scripted sequence runs once
/// in `GameScene::onEnter()`.
/// \brief Chinese label for a season name coming from M15/M22's `toString`.
///
/// The engine and the data packs keep season ids language-neutral (`"spring"`), so the
/// translation lives at the display edge instead of leaking into content or world code.
[[nodiscard]] std::string_view seasonLabel(std::string_view season) {
    if (season == "spring") {
        return "春";
    }
    if (season == "summer") {
        return "夏";
    }
    if (season == "fall") {
        return "秋";
    }
    if (season == "winter") {
        return "冬";
    }
    return season;
}

/// \brief Chinese label for a tool kind; `toString` stays language-neutral for logs/JSON.
[[nodiscard]] std::string_view toolLabel(fa::interaction::ToolKind tool) noexcept {
    switch (tool) {
        case fa::interaction::ToolKind::Hoe:
            return "锄头";
        case fa::interaction::ToolKind::WateringCan:
            return "水壶";
        case fa::interaction::ToolKind::Axe:
            return "斧头";
        case fa::interaction::ToolKind::Pickaxe:
            return "镐";
        case fa::interaction::ToolKind::Sword:
            return "剑";
        case fa::interaction::ToolKind::Seed:
            return "种子";
        case fa::interaction::ToolKind::Harvest:
            return "收获";
        case fa::interaction::ToolKind::Placeable:
            return "放置";
        default:
            return "工具";
    }
}

class SmokeSink final : public fa::time::IFrameSink {
public:
    SmokeSink() {
        input.addDevice(std::make_unique<fa::input::KeyboardDevice>());
        input.addDevice(std::make_unique<fa::input::MouseDevice>());
        input.applyDefaultBindings();

        // Gameplay actions live in M16; M07 only knows names and bindings, so the
        // composition root is where the two meet.
        const auto bindKey = [this](std::string_view name, fa::platform::Scancode key) {
            const fa::input::ActionId action = input.registerAction(name);
            static_cast<void>(input.bind(fa::input::InputContext::Gameplay, action, fa::input::Binding::fromKey(key)));
        };
        const auto bindMouse = [this](std::string_view name, fa::platform::MouseButton button) {
            const fa::input::ActionId action = input.registerAction(name);
            static_cast<void>(
                input.bind(fa::input::InputContext::Gameplay, action, fa::input::Binding::fromMouseButton(button)));
        };
        bindKey(fa::player::kActionNameRun, fa::platform::Scancode::LeftShift);
        bindKey(fa::player::kActionNameDodge, fa::platform::Scancode::Space);
        bindKey(fa::player::kActionNameInteract, fa::platform::Scancode::G);
        // The backpack panel is opened by the raw I key only (see the play loop);
        // the old Tab binding was removed to keep a single inventory hotkey.
        bindMouse(fa::player::kActionNameUseTool, fa::platform::MouseButton::Left);
        bindMouse(fa::player::kActionNameAttack, fa::platform::MouseButton::Right);

        static constexpr std::array<fa::platform::Scancode, 9> kDigits{fa::platform::Scancode::Digit1,
                                                                       fa::platform::Scancode::Digit2,
                                                                       fa::platform::Scancode::Digit3,
                                                                       fa::platform::Scancode::Digit4,
                                                                       fa::platform::Scancode::Digit5,
                                                                       fa::platform::Scancode::Digit6,
                                                                       fa::platform::Scancode::Digit7,
                                                                       fa::platform::Scancode::Digit8,
                                                                       fa::platform::Scancode::Digit9};
        for (std::size_t index = 0; index < m_hotbarActions.size(); ++index) {
            const fa::input::ActionId action = input.registerAction("Hotbar" + std::to_string(index + 1));
            static_cast<void>(
                input.bind(fa::input::InputContext::Gameplay, action, fa::input::Binding::fromKey(kDigits[index])));
            m_hotbarActions[index] = action;
        }
    }

    /// \brief Resolved app configuration; defaults until `initializeConfig` runs.
    fa::app::GameConfig config;
    /// \brief Validated play-mode spawn table from `data/entities.json`.
    std::vector<EntitySpawn> entitySpawns;
    /// \brief Hazard cells derived from the spawn table; empty means built-in.
    std::vector<fa::tilemap::TileCoord> hazardCells;
    fa::app::StbImageDecoder imageDecoder;

    /// \brief Loads config/game.json and data/entities.json, warning on fallback.
    void initializeConfig(fa::platform::IPlatform& platform) {
        fa::platform::IFileSystem& files = platform.files();
        fileSystem = &files;
        const fa::core::Result<std::string> configText = files.readText(config.paths.config);
        if (configText.isOk()) {
            fa::core::Result<fa::app::GameConfig> loaded = fa::app::GameConfig::loadFromText(configText.value());
            if (loaded.isOk()) {
                config = std::move(loaded).take();
            } else {
                std::printf("config: %s, using defaults\n", loaded.error().message.c_str());
            }
        } else {
            std::printf("config: %s not found, using defaults\n", config.paths.config.c_str());
        }

        const std::string entitiesPath = config.paths.data + "/entities.json";
        const fa::core::Result<std::string> entitiesText = files.readText(entitiesPath);
        if (entitiesText.isOk()) {
            const fa::core::Result<fa::save::SaveArchive> parsed = fa::save::SaveArchive::parse(entitiesText.value());
            if (parsed.isOk()) {
                entitySpawns = parseEntitySpawns(parsed.value());
            } else {
                std::printf("entities: %s, using built-in spawns\n", parsed.error().message.c_str());
            }
        } else {
            std::printf("entities: %s not found, using built-in spawns\n", entitiesPath.c_str());
        }
        gameState.playerStaminaMax = config.stamina.max;
        gameState.playerStamina = config.stamina.max;
        gameState.playerTuning.dodgeStaminaCost = config.stamina.dodgeCost;
        gameState.playerTuning.attackStaminaCost = config.stamina.attackCost;
        gameState.playerTuning.useToolStaminaCost = config.stamina.toolCost;
        gameState.playerTuning.runStaminaPerSecond = config.stamina.runDrainPerSecond;
        gameState.playerTuning.staminaRegenPerSecond = config.stamina.regenPerSecond;

        hazardCells.clear();
        for (const EntitySpawn& spawn : entitySpawns) {
            if (spawn.kind == "hazard" && spawn.hasCell) {
                hazardCells.push_back(spawn.cell);
            }
        }
        std::printf("config: window %dx%d (min %dx%d, %s), ui scale %.2f, %zu entity spawn(s)\n",
                    config.window.width,
                    config.window.height,
                    config.window.minWidth,
                    config.window.minHeight,
                    config.window.fill ? "fill" : "integer scale",
                    static_cast<double>(config.ui.scale),
                    entitySpawns.size());
        std::printf("config: stamina max %.0f, regen %.1f/s, run %.1f/s, dodge %.0f, "
                    "attack %.0f, tool %.0f\n",
                    static_cast<double>(config.stamina.max),
                    static_cast<double>(config.stamina.regenPerSecond),
                    static_cast<double>(config.stamina.runDrainPerSecond),
                    static_cast<double>(config.stamina.dodgeCost),
                    static_cast<double>(config.stamina.attackCost),
                    static_cast<double>(config.stamina.toolCost));
    }

    /// \brief True for `--play` / `--debug` / `--play-test`: input drives the session.
    bool playMode = false;
    /// \brief True for `--debug`: debug overlays on, verbose log level, log file.
    bool debugMode = false;
    /// \brief Run log owned by main; null unless this is a player mode.
    fa::app::RunLogFileSink* runLogFile = nullptr;
    int logFlushTick = 0;
    /// \brief True for `--play-test`: initializeFlow uses the `playtest` slot.
    bool playTest = false;
    /// \brief Set by main once the frame loop exists; the menu's Quit calls it.
    std::function<void()> requestAppQuit;
    /// \brief True while the save-slot panel is open.
    bool slotsOpen = false;
    /// \brief Save-slot metadata cached when the panel opens.
    std::vector<fa::save::SaveMetadata> slotList;
    /// \brief Count of slot-menu buttons pressed, mirrored into the smoke summary.
    int slotActions = 0;
    /// \brief True while the merchant panel owns input.
    bool shopOpen = false;
    /// \brief Last shop action result, shown inside the panel.
    std::string shopMessage;
    float shopMessageTimer = 0.0f;
    /// \brief Debounces a portal until the player has stepped off it.
    float portalCooldown = 0.0f;
    /// \brief Debounces thorn damage while standing on brambles.
    float hazardCooldown = 0.0f;
    /// \brief True while the backpack panel owns input.
    bool inventoryOpen = false;
    /// \brief `--soak` state: periodic save/load plus a leak baseline.
    bool soakMode = false;
    bool soakOk = true;
    int soakCycles = 0;
    std::size_t soakBaselineEntities = 0;
    std::size_t soakBaselineTextures = 0;
    /// \brief `--stress` state: frame times for the p50/p99 baseline.
    bool stressMode = false;
    int stressSpriteCount = 0;
    std::vector<float> stressFrameTimes;
    /// \brief Last inventory action result.
    std::string inventoryMessage;
    float inventoryMessageTimer = 0.0f;
    /// \brief True while the quest log owns input.
    bool questLogOpen = false;
    /// \brief Backpack category filter (0 = all, then `ItemDef::category` names).
    int inventoryCategory = 0;
    /// \brief Backpack page for the filtered stack list.
    int inventoryPage = 0;

    /// \brief Creates the GL context the smoke run draws with.
    ///
    /// \return The failure from initialize(); the caller decides whether a headless
    /// run may continue without rendering.
    fa::core::Status initializeRenderer(fa::platform::IPlatform& platform, bool vsync) {
        renderer = fa::render::createRenderer();

        fa::render::RendererDesc desc;
        desc.vsync = vsync;
        desc.fillWindow = config.window.fill;
        const fa::core::Status status = renderer->initialize(platform, desc);
        if (status.isError()) {
            renderer.reset();
        }
        return status;
    }

    /// \brief Reads `data/<name>`, falling back to the built-in text.
    [[nodiscard]] std::string readDataText(const char* name, std::string_view fallback) const {
        if (fileSystem != nullptr) {
            const std::string path = config.paths.data + "/" + name;
            const fa::core::Result<std::string> text = fileSystem->readText(path);
            if (text.isOk()) {
                return text.value();
            }
            std::printf("data: %s missing, using built-in fallback\n", path.c_str());
        }
        return std::string(fallback);
    }

    /// \brief Loads the content pack from `data/` and prints every collected error.
    fa::core::Status initializeContent() {
        const std::string contentText = readDataText("content.json", kContentText);
        const fa::core::Status status = content.loadDocument(contentText);
        if (status.isError()) {
            for (const fa::content::ContentError& error : content.errors()) {
                std::printf("content error %d [%s]: %s\n", error.code, error.context.c_str(), error.message.c_str());
            }
            return status;
        }
        const std::string narrativeText = readDataText("narrative.json", kNarrativeText);
        const fa::core::Status narrativeStatus = narrative.loadDocument(narrativeText);
        if (narrativeStatus.isError()) {
            std::printf("narrative init failed (%d): %s\n",
                        narrativeStatus.error().code,
                        narrativeStatus.error().message.c_str());
            return narrativeStatus;
        }
        const std::string shopText = readDataText("shops.json", kShopText);
        const fa::core::Status shopStatus = shops.loadDocument(shopText);
        if (shopStatus.isError()) {
            std::printf("shop init failed (%d): %s\n", shopStatus.error().code, shopStatus.error().message.c_str());
        }
        // Player walk animation is optional polish: a broken file just keeps the marker dot.
        playerWalkConfig = parseSheetAnimConfig(readDataText("animations.json", kAnimationsText), "player_walk");
        if (!playerWalkConfig.valid) {
            std::printf("data: animations.json has no usable player_walk section\n");
        }
        boatWalkConfig = parseSheetAnimConfig(readDataText("animations.json", kAnimationsText), "player_boat");
        idleAnimConfig = parseSheetAnimConfig(readDataText("animations.json", kAnimationsText), "player_idle");
        skillConfig = parseSkillConfig(readDataText("skills.json", kSkillsText));
        return shopStatus;
    }

    /// \brief GPU handle behind a cached texture, or an invalid one for fallbacks.
    static fa::render::TextureHandle gpuTextureOf(const fa::assets::AssetHandle<fa::assets::TextureAsset>& handle) {
        const fa::assets::TextureAsset* asset = handle.get();
        return fa::render::TextureHandle{asset != nullptr ? asset->gpuTexture : 0};
    }

    /// \brief Map file and spawn scope for the current mode.
    ///
    /// The three showcase worlds (`maps/farm.json`, `maps/farm-scripted.json`,
    /// `maps/forest.json`) carry the scripted content the automation modes assert on:
    /// Elder + quest chain, merchant, shipping bin, bed, thorns and the forest enemies.
    /// They therefore stay owned by `--play-test` / the smoke run. `--play` and `--debug`
    /// start from the placeholder world under `maps/play/` with **no spawns at all** -
    /// the real starting content is the next design step. A missing play map falls back to
    /// M12's built-in layout, so the game still boots.
    [[nodiscard]] bool usesShowcaseMaps() const noexcept { return !playMode || playTest; }

    std::unique_ptr<GameScene> makeGameScene(GameMap map, std::optional<fa::core::Vec2> spawn) {
        const bool showcaseContent = usesShowcaseMaps();
        const char* mapFile = showcaseContent
                                  ? (map == GameMap::Forest ? "maps/forest.json"
                                                            : (playMode ? "maps/farm.json" : "maps/farm-scripted.json"))
                                  : (map == GameMap::Forest ? "maps/play/forest.json" : "maps/play/farm.json");
        const std::string mapText = readDataText(mapFile, std::string_view{});
        currentMapFile = mapFile;
        // One shared spawn table, scoped per map: the player world's id matches nothing that
        // is tagged for the showcase worlds, so `--play` stays free of scripted content while the
        // editor can still author rows for it.
        const std::string mapId = showcaseContent ? (map == GameMap::Forest ? "forest" : "farm")
                                                  : (map == GameMap::Forest ? "play/forest" : "play/farm");
        auto scene = std::make_unique<GameScene>(input,
                                                 content,
                                                 narrative,
                                                 gameState,
                                                 map,
                                                 !playMode,
                                                 &entitySpawns,
                                                 spawn,
                                                 mapText,
                                                 mapId,
                                                 showcaseContent);
        scene->setTextures(gpuTextureOf(grassTexture),
                           gpuTextureOf(wallTexture),
                           gpuTextureOf(soilTexture),
                           gpuTextureOf(markerTexture));
        scene->setDetailTextures(gpuTextureOf(soilWetTexture),
                                 gpuTextureOf(cropSeedTexture),
                                 gpuTextureOf(cropSproutTexture),
                                 gpuTextureOf(cropYoungTexture),
                                 gpuTextureOf(cropMatureTexture),
                                 gpuTextureOf(chestTexture),
                                 gpuTextureOf(slimeTexture));
        scene->setNamedTextures(&namedTextureHandles);
        scene->setWalkConfig(&playerWalkConfig);
        scene->setBoatConfig(&boatWalkConfig);
        scene->setIdleConfig(&idleAnimConfig);
        scene->setSkillConfig(&skillConfig);
        return scene;
    }

    /// \brief Swaps the live map through `SceneStack::replace`.
    ///
    /// The replacement destroys the old scene, so every long-lived value that must
    /// survive lives in `gameState`; the new scene re-links the player's hotbar and,
    /// in editor builds, points the overlay at the new map.
    [[nodiscard]] bool switchMap(GameMap target, std::optional<fa::core::Vec2> spawn) {
        if (!playMode || gameScene == nullptr) {
            return false;
        }
        auto scene = makeGameScene(target, spawn);
        GameScene* raw = scene.get();
        if (const fa::core::Status replaced = stack.replace(std::move(scene)); replaced.isError()) {
            setPlayMessage("地图加载失败");
            return false;
        }
        gameScene = raw;
        clearCombatFeedback();
        if (playerInventory != nullptr) {
            gameScene->setFarmingInventory(playerInventory->hotbar().inventory());
            gameScene->setNarrativeInventory(playerInventory->hotbar().inventory());
            gameScene->setSelectedSeed(fa::content::ContentId::fromName("item.turnip_seed"));
        }
#if defined(FA_ENABLE_EDITOR)
        editor = std::make_unique<fa::editor::EditorOverlay>(gameScene->editableMap());
        // The visual editor borrowed the destroyed scene's map; force a rebind
        // on the next open instead of editing freed memory.
        visualEditor.reset();
#endif
        return true;
    }

    /// \brief Loads the catalog and uploads the four textures the frame draws.
    ///
    /// M05 only caches and uploads; it never talks to the renderer directly, so this
    /// is where `AssetUploaderAdapter` ties the two modules together.
    fa::core::Status initializeAssets(fa::platform::IPlatform& platform) {
        if (renderer == nullptr) {
            return fa::core::errorStatus(fa::assets::asset_error::kNotInitialized, "no renderer to upload through");
        }
        const std::string catalogText = readDataText("catalog.json", kCatalogText);
        const fa::core::Status catalogStatus = catalog.loadFromText(catalogText);
        if (catalogStatus.isError()) {
            return catalogStatus;
        }
        fileSystem = &platform.files();
        uploader = std::make_unique<AssetUploaderAdapter>(*renderer);

        fa::assets::AssetManagerDesc desc;
        desc.fileSystem = fileSystem;
        desc.catalog = &catalog;
        desc.uploader = uploader.get();
        desc.rootPath = config.paths.assets;
        fa::assets::IImageDecoder* decoders[] = {&imageDecoder};
        desc.decoders = decoders;
        assets = fa::assets::createAssetManager(desc);
        if (assets == nullptr) {
            return fa::core::errorStatus(fa::assets::asset_error::kNotInitialized, "asset manager creation failed");
        }

        const auto load = [this](const char* name,
                                 fa::assets::AssetHandle<fa::assets::TextureAsset>& out) -> fa::core::Status {
            fa::core::Result<fa::assets::AssetHandle<fa::assets::TextureAsset>> loaded =
                assets->loadTexture(fa::assets::AssetId::fromName(name));
            if (loaded.isError()) {
                return fa::core::errorStatus(loaded.error().code, loaded.error().message);
            }
            out = std::move(loaded).take();
            if (!out) {
                return fa::core::errorStatus(fa::assets::asset_error::kUploadFailed, "a smoke texture stayed unusable");
            }
            return fa::core::okStatus();
        };

        if (const fa::core::Status status = load("tex.ground.grass", grassTexture); status.isError()) {
            return status;
        }
        if (const fa::core::Status status = load("tex.ground.wall", wallTexture); status.isError()) {
            return status;
        }
        if (const fa::core::Status status = load("tex.ground.soil", soilTexture); status.isError()) {
            return status;
        }
        if (const fa::core::Status status = load("tex.marker", markerTexture); status.isError()) {
            return status;
        }

        const auto loadOptional = [this](const char* name, fa::assets::AssetHandle<fa::assets::TextureAsset>& out) {
            fa::core::Result<fa::assets::AssetHandle<fa::assets::TextureAsset>> loaded =
                assets->loadTexture(fa::assets::AssetId::fromName(name));
            if (loaded.isError()) {
                std::printf("asset: %s unavailable (%s), using fallback\n", name, loaded.error().message.c_str());
                return;
            }
            out = std::move(loaded).take();
            if (!out) {
                std::printf("asset: %s stayed unusable, using fallback\n", name);
            }
        };
        loadOptional("tex.ground.soil_wet", soilWetTexture);
        loadOptional("tex.crop.turnip_seed", cropSeedTexture);
        loadOptional("tex.crop.turnip_sprout", cropSproutTexture);
        loadOptional("tex.crop.turnip_young", cropYoungTexture);
        loadOptional("tex.crop.turnip_mature", cropMatureTexture);
        loadOptional("tex.object.chest", chestTexture);
        loadOptional("tex.enemy.slime", slimeTexture);

        // Load every other catalog texture generically so new crops/enemies only need
        // a catalog entry plus a PNG; no code change is required for the handle table.
        namedTextureAssets.clear();
        namedTextureHandles.clear();
        for (const fa::assets::AssetEntry& entry : catalog.entries()) {
            if (entry.kind != fa::assets::AssetKind::Texture) {
                continue;
            }
            fa::core::Result<fa::assets::AssetHandle<fa::assets::TextureAsset>> loaded = assets->loadTexture(entry.id);
            if (loaded.isError() || !loaded.value()) {
                std::printf("asset: %s unavailable, using fallback\n", entry.id.name().c_str());
                continue;
            }
            fa::assets::AssetHandle<fa::assets::TextureAsset> handle = std::move(loaded).take();
            const fa::assets::TextureAsset* texture = handle.get();
            if (texture == nullptr) {
                continue;
            }
            namedTextureHandles[entry.id.name()] = fa::render::TextureHandle{texture->gpuTexture};
            namedTextureAssets[entry.id.name()] = std::move(handle);
        }

        const fa::assets::TextureAsset* grassAsset = grassTexture.get();
        const fa::assets::TextureAsset* wallAsset = wallTexture.get();
        const fa::assets::TextureAsset* soilAsset = soilTexture.get();
        const fa::assets::TextureAsset* markerAsset = markerTexture.get();
        if (grassAsset == nullptr || wallAsset == nullptr || soilAsset == nullptr || markerAsset == nullptr) {
            return fa::core::errorStatus(fa::assets::asset_error::kUploadFailed, "a smoke texture stayed unusable");
        }

        // M11 takes over from here: the textures go into a scene, and the scene becomes
        // the only source of draw items.
        auto scene = makeGameScene(GameMap::Farm, std::nullopt);
        gameScene = scene.get();
        const fa::core::Status pushed = stack.push(std::move(scene));
        if (pushed.isError()) {
            gameScene = nullptr;
            return pushed;
        }
        if (playMode && playerInventory != nullptr) {
            // Farming charges seeds to and credits harvests into the hotbar, which
            // is the container the player is actually selecting from. Narrative uses
            // the same container so quest rewards and gift checks see the real bag.
            gameScene->setFarmingInventory(playerInventory->hotbar().inventory());
            gameScene->setNarrativeInventory(playerInventory->hotbar().inventory());
            gameScene->setSelectedSeed(fa::content::ContentId::fromName("item.turnip_seed"));
        }

#if defined(FA_ENABLE_EDITOR)
        editor = std::make_unique<fa::editor::EditorOverlay>(gameScene->editableMap());
        runEditorSmoke();
#endif
        return fa::core::okStatus();
    }

#if defined(FA_ENABLE_EDITOR)
    /// \brief Saves the live map through M26 and proves the runtime loader reads it.
    void runEditorSmoke() {
        editorStats = EditorRunStats{};
        if (editor == nullptr || fileSystem == nullptr) {
            return;
        }
        static_cast<void>(fileSystem->createDirectories("saves"));
        const std::string text = editor->mapText();
        const fa::core::Status saved = fileSystem->writeTextAtomic("saves/editor-map.json", text);
        editorStats.saveOk = saved.isOk();
        const fa::core::Result<fa::tilemap::Tilemap> loaded = fa::tilemap::loadTilemapFromJson(text);
        editorStats.loadOk = loaded.isOk() && loaded.value().width() == gameScene->editableMap().width() &&
                             loaded.value().height() == gameScene->editableMap().height();
    }

    /// \brief Saves the current editor map to the editor slot.
    void saveEditorMap() {
        if (editor == nullptr || fileSystem == nullptr) {
            return;
        }
        static_cast<void>(fileSystem->createDirectories("saves"));
        editorStats.saveOk = fileSystem->writeTextAtomic("saves/editor-map.json", editor->mapText()).isOk();
    }

    /// \brief Reads the editor slot and validates it with the runtime loader.
    void loadEditorMap() {
        if (editor == nullptr || fileSystem == nullptr) {
            return;
        }
        const fa::core::Result<std::string> text = fileSystem->readText("saves/editor-map.json");
        if (text.isError()) {
            editorStats.loadOk = false;
            return;
        }
        const fa::core::Result<fa::tilemap::Tilemap> loaded = fa::tilemap::loadTilemapFromJson(text.value());
        editorStats.loadOk = loaded.isOk();
    }

    /// \brief Cell containing a world position, for rows that only store a position.
    [[nodiscard]] static fa::tilemap::TileCoord tilemapCell(const fa::core::Vec2& position, int tileSize) {
        const int size = tileSize > 0 ? tileSize : static_cast<int>(fa::render::kTileSize);
        return fa::tilemap::TileCoord{static_cast<int>(std::floor(position.x / static_cast<float>(size))),
                                      static_cast<int>(std::floor(position.y / static_cast<float>(size)))};
    }

    /// \brief Loads the rows the editor authors from the spawn table, for this map only.
    ///
    /// The editor works in cells; a shipped row may carry a sub-tile world position, which is
    /// converted for display and written back as a cell. That is deliberate: map authoring is
    /// cell-granular, and only rows the editor touched lose the fractional offset.
    void buildEditorPlacements() {
        editorPlacements.clear();
        editorMapId.clear();
        if (gameScene == nullptr) {
            return;
        }
        editorMapId = std::string{gameScene->mapId()};
        const int tileSize = gameScene->editableMap().tileSize();
        for (const EntitySpawn& row : entitySpawns) {
            if (!row.map.empty() && row.map != editorMapId) {
                continue;
            }
            fa::editor::MapPlacement placement;
            placement.id = row.id;
            placement.kind = row.kind;
            placement.cell = row.hasCell ? row.cell : tilemapCell(row.position, tileSize);
            placement.target = row.dialogue;
            editorPlacements.push_back(std::move(placement));
        }
    }

    /// \brief Read-only view model for the editor's entity list scope: one row per
    /// spawn belonging to the map currently open, with the JSON field names as keys.
    [[nodiscard]] std::vector<fa::editor::EntityInfoRow> buildEditorEntityInfos() const {
        std::vector<fa::editor::EntityInfoRow> rows;
        if (gameScene == nullptr) {
            return rows;
        }
        const std::string mapId{gameScene->mapId()};
        const int tileSize = gameScene->editableMap().tileSize();
        const auto num = [](double value) {
            char buffer[32]{};
            std::snprintf(buffer, sizeof(buffer), "%g", value);
            return std::string(buffer);
        };
        const auto hex = [](fa::core::Color c) {
            char buffer[16]{};
            const auto channel = [](float v) { return static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
            std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x", channel(c.r), channel(c.g), channel(c.b));
            return std::string(buffer);
        };
        for (const EntitySpawn& row : entitySpawns) {
            if (!row.map.empty() && row.map != mapId) {
                continue;
            }
            fa::editor::EntityInfoRow info;
            info.id = row.id;
            info.kind = row.kind;
            info.cell = row.hasCell ? row.cell : tilemapCell(row.position, tileSize);
            info.hasCell = true;
            auto& p = info.properties;
            p.emplace_back("kind", row.kind);
            p.emplace_back("map", row.map.empty() ? "*" : row.map);
            if (row.hasPosition) {
                char pos[48]{};
                std::snprintf(pos,
                              sizeof(pos),
                              "(%.0f, %.0f)",
                              static_cast<double>(row.position.x),
                              static_cast<double>(row.position.y));
                p.emplace_back("position", pos);
            } else {
                char cell[32]{};
                std::snprintf(cell, sizeof(cell), "(%d, %d)", row.cell.x, row.cell.y);
                p.emplace_back("cell", cell);
            }
            p.emplace_back("texture", row.texture.empty() ? "-" : row.texture);
            p.emplace_back("tint", hex(row.tint));
            p.emplace_back("payload", std::to_string(row.payload));
            if (row.kind == "npc") {
                p.emplace_back("radius", num(row.radius));
                p.emplace_back("dialogue", row.dialogue.empty() ? "-" : row.dialogue);
            } else if (row.kind == "enemy") {
                p.emplace_back("health", std::to_string(row.health));
                p.emplace_back("loot", row.loot.empty() ? "-" : row.loot);
                p.emplace_back("sight_radius", num(row.sightRadius));
                p.emplace_back("attack_range", num(row.attackRange));
                p.emplace_back("move_speed", num(row.moveSpeed));
                p.emplace_back("attack_damage", std::to_string(row.attackDamage));
                p.emplace_back("attack_cooldown", num(row.attackCooldown));
            } else if (row.kind == "hazard") {
                p.emplace_back("damage", std::to_string(row.damage));
            } else {
                p.emplace_back("radius", num(row.radius));
            }
            rows.push_back(std::move(info));
        }
        return rows;
    }

    /// \brief Palette options. The editor stays content-free: kinds, their Chinese labels and
    /// the dialogue ids an NPC may run are all supplied from here.
    [[nodiscard]] std::vector<fa::editor::PlacementKind> editorPlacementKinds() const {
        std::vector<std::string> dialogues;
        for (const fa::narrative::DialogueDef& def : narrative.dialogues()) {
            dialogues.emplace_back(def.id.name());
        }
        return {
            fa::editor::PlacementKind{"npc", "NPC 可对话", dialogues},
            fa::editor::PlacementKind{"bed", "床", {}},
            fa::editor::PlacementKind{"shipping", "出货箱", {}},
            fa::editor::PlacementKind{"merchant", "商人", {}},
            fa::editor::PlacementKind{"enemy", "敌人", {}},
            fa::editor::PlacementKind{"hazard", "荆棘", {}},
        };
    }

    /// \brief Maps a portal may lead to. `play/*` ids exist so a portal authored in the
    /// player world can point at it once the map files are wired up.
    [[nodiscard]] static std::vector<std::string> editorPortalTargets() {
        return {"farm", "forest", "play/farm", "play/forest"};
    }

    /// \brief Merges the editor's rows back into the spawn table and writes entities.json.
    ///
    /// Rows belonging to other maps are preserved untouched, so editing the farm cannot
    /// destroy the forest's enemies. A row that already existed keeps its tuning fields
    /// (health, loot, AI ranges, tint...); only id/kind/map/cell/dialogue come from the edit.
    [[nodiscard]] fa::core::Status saveEntitySpawns(const std::vector<fa::editor::MapPlacement>& rows) {
        if (fileSystem == nullptr) {
            return fa::core::Status::err(
                fa::core::makeError(fa::core::error_code::kInvalidArgument, std::string{"no file system"}));
        }

        std::vector<EntitySpawn> merged;
        merged.reserve(entitySpawns.size() + rows.size());
        for (const EntitySpawn& row : entitySpawns) {
            if (row.map != editorMapId) {
                merged.push_back(row);
            }
        }
        for (const fa::editor::MapPlacement& placement : rows) {
            EntitySpawn row;
            for (const EntitySpawn& existing : entitySpawns) {
                if (existing.id == placement.id) {
                    row = existing;
                    break;
                }
            }
            row.id = placement.id;
            row.kind = placement.kind;
            row.map = editorMapId;
            row.cell = placement.cell;
            row.hasCell = true;
            row.hasPosition = false;
            row.dialogue = placement.target;
            merged.push_back(std::move(row));
        }
        entitySpawns = std::move(merged);

        fa::save::SaveArchive document = fa::save::SaveArchive::object();
        document.setInt("schema_version", 1);
        fa::save::SaveArchive list = fa::save::SaveArchive::array();
        for (const EntitySpawn& row : entitySpawns) {
            fa::save::SaveArchive entry = fa::save::SaveArchive::object();
            entry.setString("id", row.id);
            entry.setString("kind", row.kind);
            entry.setString("map", row.map);
            if (row.hasPosition) {
                entry.setVec2("position", row.position);
            }
            if (row.hasCell) {
                entry.setVec2("cell", fa::core::Vec2{static_cast<float>(row.cell.x), static_cast<float>(row.cell.y)});
            }
            if (!row.texture.empty()) {
                entry.setString("texture", row.texture);
            }
            char tint[16]{};
            const auto byte = [](float channel) {
                return static_cast<int>(std::clamp(channel, 0.0f, 1.0f) * 255.0f + 0.5f);
            };
            std::snprintf(tint, sizeof(tint), "#%02x%02x%02x", byte(row.tint.r), byte(row.tint.g), byte(row.tint.b));
            entry.setString("tint", tint);
            entry.setInt("payload", row.payload);
            if (!row.dialogue.empty()) {
                entry.setString("dialogue", row.dialogue);
            }
            // Only write the fields a kind actually reads, so an authored file stays readable
            // instead of carrying slime AI values on a bed.
            if (row.kind == "bed" || row.kind == "shipping" || row.kind == "merchant" || row.kind == "npc") {
                entry.setFloat("radius", row.radius);
            } else if (row.kind == "enemy") {
                entry.setInt("health", row.health);
                if (!row.loot.empty()) {
                    entry.setString("loot", row.loot);
                }
                entry.setFloat("sight_radius", row.sightRadius);
                entry.setFloat("attack_range", row.attackRange);
                entry.setFloat("move_speed", row.moveSpeed);
                entry.setInt("attack_damage", row.attackDamage);
                entry.setFloat("attack_cooldown", row.attackCooldown);
            } else if (row.kind == "hazard") {
                entry.setInt("damage", row.damage);
            }
            list.push(std::move(entry));
        }
        document.setChild("entities", std::move(list));
        return fileSystem->writeTextAtomic(config.paths.data + "/entities.json", document.dump());
    }

    /// \brief Opens the visual editor on the map the running game scene owns.
    void openVisualEditor() {
        if (gameScene == nullptr) {
            return;
        }
        // The editor borrows the scene's tilemap. A new game or a map switch
        // destroys the old scene, so a leftover editor would dangle on freed
        // memory and the next session died with exit code 3 (abort). Rebind
        // whenever the live map object changed.
        if (visualEditor == nullptr || &visualEditor->map() != &gameScene->editableMap()) {
            visualEditor = std::make_unique<fa::editor::VisualEditor>(gameScene->editableMap());
        }
        buildEditorPlacements();
        visualEditor->setPlacements(&editorPlacements, editorPlacementKinds());
        visualEditor->setPortalTargets(editorPortalTargets());
        visualEditor->setEntityInfos(buildEditorEntityInfos());
        visualEditor->setPlacementSave(
            [this](const std::vector<fa::editor::MapPlacement>& rows) { return saveEntitySpawns(rows); });
        visualEditor->setActive(true);
        visualEditor->clearCloseRequest();
        editorWindowOpen = true;
        slotsOpen = false;
        std::printf("editor: visual open (%dx%d, map %s)\n",
                    visualEditor->map().width(),
                    visualEditor->map().height(),
                    editorMapId.c_str());
        std::fflush(stdout);
    }

    /// \brief Hides the visual editor without discarding its undo history.
    void closeVisualEditor() {
        if (visualEditor != nullptr) {
            visualEditor->setActive(false);
        }
        editorWindowOpen = false;
        std::printf("editor: visual closed\n");
        std::fflush(stdout);
    }

    /// \brief Writes the edited map back to the file the runtime loads.
    [[nodiscard]] fa::core::Status saveVisualEditorMap() {
        if (fileSystem == nullptr || visualEditor == nullptr) {
            return fa::core::Status::err(
                fa::core::makeError(fa::core::error_code::kInvalidArgument, std::string{"editor unavailable"}));
        }
        // Saving must land on the map that is actually open: the editor can be launched on
        // the forest or the player world, and hard-coding the farm path silently destroyed it.
        const std::string path = config.paths.data + "/" + currentMapFile;
        const fa::core::Status saved = fileSystem->writeTextAtomic(path, visualEditor->mapText());
        if (saved.isOk()) {
            // Repaint with the freshly chosen texture names so the live map keeps
            // drawing correctly after the editor closes.
            fa::tilemap::Tilemap& map = visualEditor->map();
            for (const fa::tilemap::TileDef& def : map.tileDefs()) {
                static_cast<void>(map.setTileTexture(def.id, gameScene->textureForName(def.textureName), def.source));
            }
            for (fa::tilemap::MapBlocker& blocker : map.mutableBlockers()) {
                blocker.texture = blocker.textureName.empty() ? fa::render::TextureHandle{}
                                                              : gameScene->textureForName(blocker.textureName);
            }
        }
        return saved;
    }

    /// \brief Submits the edited map into the render queue with live textures.
    void drawEditorMap() {
        if (visualEditor == nullptr || gameScene == nullptr) {
            return;
        }
        const fa::core::Rect view = camera.visibleRect();
        const fa::core::Rect screen = fa::core::makeRect(0.0f, 0.0f, view.w, view.h);
        const fa::core::Rect viewport = fa::editor::VisualEditor::mapRect(screen);
        const fa::tilemap::Tilemap& map = visualEditor->map();
        const float step = 16.0f * visualEditor->tileScale();
        for (int index = 0; index < map.layerCount(); ++index) {
            const fa::tilemap::TileLayer* layer = map.layerAt(index);
            if (layer == nullptr || !layer->visible) {
                continue;
            }
            for (int row = 0; row < map.height(); ++row) {
                for (int column = 0; column < map.width(); ++column) {
                    const fa::tilemap::TileDef* def = map.tileDef(map.tileAt(column, row, index));
                    if (def == nullptr) {
                        continue;
                    }
                    fa::render::DrawItem item;
                    item.texture = gameScene->textureForName(def->textureName);
                    if (!item.texture.isValid()) {
                        continue;
                    }
                    // Tall defs (trees) draw their whole height upward from the
                    // anchor cell, the same way the runtime extractor does.
                    const int height = def->renderHeight > 1 ? def->renderHeight : 1;
                    item.position = fa::core::Vec2{
                        view.x + viewport.x + static_cast<float>(column) * step,
                        view.y + viewport.y + (static_cast<float>(row) - static_cast<float>(height - 1)) * step};
                    item.size = fa::core::Vec2{step, step * static_cast<float>(height)};
                    item.source = def->source;
                    item.layer = layer->renderLayer;
                    renderQueue.submit(item);
                }
            }
        }
        // Collision zones: the texture may span more rows than the box blocks.
        for (const fa::tilemap::MapBlocker& blocker : map.blockers()) {
            if (blocker.textureName.empty()) {
                continue;
            }
            fa::render::DrawItem item;
            item.texture = gameScene->textureForName(blocker.textureName);
            if (!item.texture.isValid()) {
                continue;
            }
            const int height = blocker.renderHeight > 1 ? blocker.renderHeight : 1;
            item.position = fa::core::Vec2{
                view.x + viewport.x + static_cast<float>(blocker.bounds.x) * step,
                view.y + viewport.y + static_cast<float>(blocker.bounds.y + blocker.bounds.height - height) * step};
            item.size =
                fa::core::Vec2{static_cast<float>(blocker.bounds.width) * step, step * static_cast<float>(height)};
            item.layer = fa::render::RenderLayer::Objects;
            renderQueue.submit(item);
        }
    }

    /// \brief Paints one cell through the editor input path and reloads the text.
    bool runVisualEditorCheck() {
        if (fileSystem == nullptr || gameScene == nullptr) {
            return false;
        }
        openVisualEditor();
        if (visualEditor == nullptr) {
            return false;
        }
        const fa::core::Rect visible = camera.visibleRect();
        const fa::core::Rect mapArea =
            fa::editor::VisualEditor::mapRect(fa::core::makeRect(0.0f, 0.0f, visible.w, visible.h));
        // The first pass only establishes the tile scale, so park the cursor outside.
        fa::editor::VisualEditorInput idle;
        idle.cursorScreen = fa::core::Vec2{-100.0f, -100.0f};
        visualEditor->update(idle, mapArea);
        const float step = 16.0f * visualEditor->tileScale();
        fa::tilemap::Tilemap& map = visualEditor->map();
        const fa::tilemap::TileId original = map.tileAt(5, 5, 0);
        fa::tilemap::TileId brush = original;
        for (const fa::tilemap::TileDef& def : map.tileDefs()) {
            if (!(def.id == original)) {
                brush = def.id;
                break;
            }
        }
        visualEditor->editor().setBrush(brush);

        fa::editor::VisualEditorInput click;
        click.cursorScreen = fa::core::Vec2{mapArea.x + 5.5f * step, mapArea.y + 5.5f * step};
        click.primaryPressed = true;
        // Brush tool: first click inspects, second click on the same cell replaces.
        visualEditor->update(click, mapArea);
        visualEditor->update(click, mapArea);
        const bool painted = map.tileAt(5, 5, 0) == brush;

        const std::string text = visualEditor->mapText();
        static_cast<void>(fileSystem->createDirectories("saves"));
        const bool saved = fileSystem->writeTextAtomic("saves/editor-visual.json", text).isOk();
        const fa::core::Result<fa::tilemap::Tilemap> loaded = fa::tilemap::loadTilemapFromJson(text);
        const bool reloaded = saved && loaded.isOk() && loaded.value().tileAt(5, 5, 0) == brush;

        static_cast<void>(map.setTile(5, 5, 0, original));
        closeVisualEditor();
        return painted && reloaded;
    }
#endif

    /// \brief Builds the player containers and runs the one M18 smoke sequence.
    ///
    /// The sequence exercises stacking (120 seeds become 99 + 21), quick transfer to
    /// the hotbar, equipping a tool, selection changes and one food use. It is
    /// deliberately data-only: no world entity is resolved, so the synthetic
    /// `EntityId` is just recorded in the use event.
    fa::core::Status initializeInventory(bool forPlay) {
        playerInventory = std::make_unique<fa::inventory::PlayerInventory>(20, 10, &content);
        itemUse = std::make_unique<fa::inventory::ItemUseService>(content);
        consumable = std::make_unique<ConsumableUseHandler>();
        itemUse->registerHandler(*consumable);

        const auto add = [](fa::inventory::Inventory& target, std::string_view name, int count) -> fa::core::Status {
            const auto result = target.add(fa::inventory::ItemStack{fa::content::ContentId::fromName(name), count, -1});
            if (result.isError()) {
                return fa::core::Status::err(result.error());
            }
            if (result.value() != 0) {
                return fa::core::errorStatus(fa::inventory::inventory_error::kInventoryFull,
                                             "the inventory is too small");
            }
            return fa::core::okStatus();
        };

        if (forPlay) {
            fa::inventory::Inventory& hotbar = playerInventory->hotbar().inventory();
            if (const fa::core::Status status = add(hotbar, "item.hoe", 1); status.isError()) {
                return status;
            }
            if (const fa::core::Status status = add(hotbar, "item.watering_can", 1); status.isError()) {
                return status;
            }
            if (const fa::core::Status status = add(hotbar, "item.turnip_seed", 20); status.isError()) {
                return status;
            }
            if (const fa::core::Status status = add(hotbar, "item.turnip_soup", 3); status.isError()) {
                return status;
            }
            // The boat lets the farmer cross the pond while it is the selected slot.
            if (const fa::core::Status status = add(hotbar, "item.boat", 1); status.isError()) {
                return status;
            }
            // The wooden sword lives in the hotbar too: right-click only swings while
            // a weapon is the selected slot.
            if (const fa::core::Status status = add(hotbar, "item.sword", 1); status.isError()) {
                return status;
            }
            if (const fa::core::Status status = add(playerInventory->backpack(), "item.turnip", 3); status.isError()) {
                return status;
            }
            // The sword stays equipped in the weapon slot (save/equipment semantics);
            // swings and the qi read the damage of the weapon held in the SELECTED
            // hotbar slot, so a copy also sits in the hotbar for `--play`.
            static_cast<void>(playerInventory->equipment().equip(
                fa::inventory::EquipmentSlot::Weapon,
                fa::inventory::ItemStack{fa::content::ContentId::fromName("item.sword"), 1, -1}));
            static_cast<void>(playerInventory->hotbar().select(0));
            inventoryStats = InventoryRunStats{};
            inventoryStats.backpackItems = playerInventory->backpack().totalItems();
            inventoryStats.backpackSlots = playerInventory->backpack().usedSlots();
            inventoryStats.hotbarItems = hotbar.totalItems();
            inventoryStats.selected = playerInventory->hotbar().selected();
            const fa::inventory::ItemStack& selected = playerInventory->hotbar().selectedStack();
            inventoryStats.toolEquipped = !selected.empty();
            inventoryStats.toolId = selected.id;
            return fa::core::okStatus();
        }

        if (const fa::core::Status status = add(playerInventory->backpack(), "item.turnip_seed", 120);
            status.isError()) {
            return status;
        }
        if (const fa::core::Status status = add(playerInventory->backpack(), "item.turnip", 5); status.isError()) {
            return status;
        }
        if (const fa::core::Status status = add(playerInventory->backpack(), "item.turnip_soup", 3); status.isError()) {
            return status;
        }
        if (const fa::core::Status status = add(playerInventory->backpack(), "item.hoe", 1); status.isError()) {
            return status;
        }

        // Slot 1 holds the 21 seeds that overflowed the first stack; slot 3 holds
        // soup and slot 4 the hoe after the earlier adds.
        if (const auto moved = playerInventory->moveToHotbar(1, -1); moved.isError()) {
            return fa::core::Status::err(moved.error());
        }
        if (const auto moved = playerInventory->moveToHotbar(3, 1); moved.isError()) {
            return fa::core::Status::err(moved.error());
        }

        auto hoe = playerInventory->backpack().takeAt(4);
        if (hoe.isError()) {
            return fa::core::Status::err(hoe.error());
        }
        if (const auto equipped = playerInventory->equipment().equip(fa::inventory::EquipmentSlot::Tool, hoe.value());
            equipped.isError()) {
            return fa::core::Status::err(equipped.error());
        }

        static_cast<void>(playerInventory->hotbar().select(0));
        static_cast<void>(playerInventory->hotbar().cycle(1));
        static_cast<void>(playerInventory->hotbar().cycle(-1));

        const fa::scene::EntityId player{1u, 1u};
        if (const fa::core::Status used = itemUse->use(playerInventory->hotbar().inventory(), 1, player);
            used.isError()) {
            return used;
        }

        const fa::inventory::Inventory& backpack = playerInventory->backpack();
        const fa::inventory::Inventory& hotbar = playerInventory->hotbar().inventory();
        const fa::inventory::ItemStack& tool = playerInventory->equipment().at(fa::inventory::EquipmentSlot::Tool);

        inventoryStats.backpackItems = backpack.totalItems();
        inventoryStats.backpackSlots = backpack.usedSlots();
        inventoryStats.hotbarItems = hotbar.totalItems();
        inventoryStats.selected = playerInventory->hotbar().selected();
        inventoryStats.toolEquipped = !tool.empty();
        inventoryStats.toolId = tool.id;
        inventoryStats.soupUses = consumable->uses;
        inventoryStats.inventoryEvents = backpack.events().size() + hotbar.events().size();
        inventoryStats.useEvents = itemUse->events().size();
        return fa::core::okStatus();
    }

    /// \brief Creates the M24 service and, in smoke mode, runs the scripted sequence.
    void initializeEconomy(bool scripted) {
        if (playerInventory == nullptr) {
            return;
        }
        economy = std::make_unique<fa::economy::EconomyService>(&content, &shops);
        economy->setGold(100);
        economy->setSeason(fa::content::Season::Spring);
        economy->setDay(1);
        if (!scripted) {
            economyStats = EconomyRunStats{};
            economyStats.stats = economy->stats();
            economyStats.gold = economy->gold();
            return;
        }

        fa::inventory::Inventory& bag = playerInventory->backpack();
        const fa::content::ContentId shop = fa::content::ContentId::fromName("shop.general");
        const fa::content::ContentId seed = fa::content::ContentId::fromName("item.turnip_seed");
        const fa::content::ContentId turnip = fa::content::ContentId::fromName("item.turnip");
        const fa::content::ContentId soup = fa::content::ContentId::fromName("item.turnip_soup");
        const fa::content::ContentId recipe = fa::content::ContentId::fromName("recipe.turnip_soup");

        economyStats = EconomyRunStats{};
        economyStats.buyOk = economy->buy(shop, seed, 3, bag).isOk();
        economyStats.sellOk = economy->sell(shop, turnip, 2, bag).isOk();
        economyStats.craftOk = economy->startCraft(recipe, "kitchen", bag).isOk();
        economy->update(30);
        economyStats.collectOk = economy->collectCraft(0, bag).isOk();
        economyStats.shipOk = economy->ship(soup, 1, bag).isOk();
        economyStats.settledGold = economy->onDayChanged();

        economyStats.stats = economy->stats();
        economyStats.gold = economyStats.stats.gold;
        economyStats.seeds = bag.countOf(seed);
        economyStats.turnips = bag.countOf(turnip);
        economyStats.soups = bag.countOf(soup) + playerInventory->hotbar().inventory().countOf(soup);
    }

    /// \brief Creates the M14 save service rooted at the ignored `saves/` directory.
    void initializeSave(fa::platform::IPlatform& platform) {
        saveService = std::make_unique<fa::save::SaveService>(platform.files(), config.paths.saves);
        fa::save::registerDefaultMigrations(*saveService);
    }

    /// \brief Creates the M22 calendar and two placeholder point lights.
    ///
    /// The scripted smoke runs the clock fast (0.01 real seconds per game minute) so
    /// the 60-frame run shows an hour change; play mode keeps the product default.
    void initializeCalendar(bool forPlay) {
        calendar = std::make_unique<fa::world::WorldCalendar>(20261018ULL);
        if (!forPlay) {
            calendar->setRealSecondsPerGameMinute(0.01f);
        }
        calendarEvents = 0;
        lastEconomyTimeOfDay = calendar->timeOfDay();
        economyMinuteCarry = 0.0f;

        fa::world::PointLight markerLight;
        markerLight.position = fa::core::Vec2{344.0f, 260.0f};
        markerLight.radius = 64.0f;
        markerLight.color = fa::core::Color{1.0f, 0.80f, 0.40f, 1.0f};
        markerLight.intensity = 0.8f;
        static_cast<void>(lights.add(markerLight));

        fa::world::PointLight animLight;
        animLight.position = fa::core::Vec2{120.0f, 160.0f};
        animLight.radius = 48.0f;
        animLight.color = fa::core::Color{0.50f, 0.80f, 1.00f, 1.0f};
        animLight.intensity = 0.7f;
        static_cast<void>(lights.add(animLight));
    }

    [[nodiscard]] CalendarRunStats calendarStats() const {
        CalendarRunStats stats;
        if (calendar == nullptr) {
            return stats;
        }
        stats.year = calendar->date().year;
        stats.season = calendar->season();
        stats.day = calendar->date().day;
        stats.hour = calendar->hour();
        stats.minute = calendar->minute();
        stats.today = calendar->today();
        stats.tomorrow = calendar->tomorrow();
        stats.ambientIntensity = calendar->ambientIntensity();
        stats.autoWater = calendar->autoWatersCrops();
        stats.growth = calendar->cropGrowthMultiplier();
        const fa::world::CalendarStats raw = calendar->stats();
        stats.days = raw.daysElapsed;
        stats.hours = raw.hoursElapsed;
        stats.seasons = raw.seasonChanges;
        stats.weatherChanges = raw.weatherChanges;
        stats.events = static_cast<std::size_t>(calendarEvents);
        return stats;
    }

    void shutdownCalendar() {
        calendar.reset();
        lights.clear();
    }

    /// \brief Serializes one item stack.
    static void writeStack(fa::save::SaveArchive& parent, std::string_view key, const fa::inventory::ItemStack& stack) {
        fa::save::SaveArchive entry = fa::save::SaveArchive::object();
        if (!stack.empty()) {
            entry.setString("id", stack.id.name());
            entry.setInt("count", stack.count);
            entry.setInt("durability", stack.durability);
        }
        parent.setChild(key, std::move(entry));
    }

    [[nodiscard]] static fa::inventory::ItemStack readStack(const fa::save::SaveArchive& parent, std::string_view key) {
        const fa::save::SaveArchive entry = parent.child(key);
        fa::inventory::ItemStack stack;
        std::string id;
        int count = 0;
        if (entry.isObject() && entry.getString("id", id) && entry.getInt("count", count) && count > 0) {
            stack.id = fa::content::ContentId::fromName(id);
            stack.count = count;
            static_cast<void>(entry.getInt("durability", stack.durability));
        }
        return stack;
    }

    /// \brief Serializes every slot, including empty ones, to keep slot indices.
    static void
    writeInventory(fa::save::SaveArchive& parent, std::string_view key, const fa::inventory::Inventory& inventory) {
        fa::save::SaveArchive slots = fa::save::SaveArchive::array();
        for (int slot = 0; slot < inventory.slotCount(); ++slot) {
            fa::save::SaveArchive entry = fa::save::SaveArchive::object();
            const fa::inventory::ItemStack& stack = inventory.at(slot);
            if (!stack.empty()) {
                entry.setString("id", stack.id.name());
                entry.setInt("count", stack.count);
                entry.setInt("durability", stack.durability);
            }
            slots.push(std::move(entry));
        }
        parent.setChild(key, std::move(slots));
    }

    static void
    readInventory(const fa::save::SaveArchive& parent, std::string_view key, fa::inventory::Inventory& inventory) {
        const fa::save::SaveArchive slots = parent.child(key);
        if (!slots.isArray()) {
            return;
        }
        std::vector<fa::inventory::ItemStack> stacks(static_cast<std::size_t>(inventory.slotCount()));
        const std::size_t count = slots.size() < stacks.size() ? slots.size() : stacks.size();
        for (std::size_t index = 0; index < count; ++index) {
            const fa::save::SaveArchive entry = slots.at(index);
            std::string id;
            int amount = 0;
            if (entry.isObject() && entry.getString("id", id) && entry.getInt("count", amount) && amount > 0) {
                fa::inventory::ItemStack stack;
                stack.id = fa::content::ContentId::fromName(id);
                stack.count = amount;
                static_cast<void>(entry.getInt("durability", stack.durability));
                stacks[index] = stack;
            }
        }
        static_cast<void>(inventory.restore(stacks));
    }

    [[nodiscard]] static std::vector<int> parseIntList(std::string_view text) {
        std::vector<int> values;
        std::size_t start = 0;
        while (start < text.size()) {
            const std::size_t comma = text.find(',', start);
            const std::string_view part =
                text.substr(start, comma == std::string_view::npos ? text.size() - start : comma - start);
            if (!part.empty()) {
                values.push_back(std::atoi(std::string(part).c_str()));
            }
            if (comma == std::string_view::npos) {
                break;
            }
            start = comma + 1;
        }
        return values;
    }

    static void writeFarming(fa::save::SaveArchive& parent, std::span<const fa::farming::FarmTileEntry> entries) {
        fa::save::SaveArchive array = fa::save::SaveArchive::array();
        for (const fa::farming::FarmTileEntry& entry : entries) {
            fa::save::SaveArchive object = fa::save::SaveArchive::object();
            object.setInt("x", entry.cell.x);
            object.setInt("y", entry.cell.y);
            object.setBool("tilled", entry.tile.tilled);
            object.setBool("watered", entry.tile.watered);
            object.setBool("greenhouse", entry.tile.greenhouse);
            object.setInt("daysSinceWatered", entry.tile.daysSinceWatered);
            object.setBool("hasCrop", entry.hasCrop);
            if (entry.hasCrop) {
                object.setString("crop", entry.crop.crop.name());
                object.setString("seed", entry.crop.seed.name());
                object.setString("harvestItem", entry.crop.harvestItem.name());
                std::string stageDays;
                for (std::size_t index = 0; index < entry.crop.stageDays.size(); ++index) {
                    if (index != 0) {
                        stageDays += ',';
                    }
                    stageDays += std::to_string(entry.crop.stageDays[index]);
                }
                object.setString("stageDays", stageDays);
                object.setInt("seasons", static_cast<int>(entry.crop.seasons));
                object.setBool("regrow", entry.crop.regrow);
                object.setInt("regrowDays", entry.crop.regrowDays);
                object.setFloat("growthDays", entry.crop.growthDays);
                object.setInt("stage", entry.crop.stage);
                object.setBool("harvestable", entry.crop.harvestable);
                object.setBool("dead", entry.crop.dead);
                object.setBool("wateredToday", entry.crop.wateredToday);
            }
            array.push(std::move(object));
        }
        parent.setChild("farming", std::move(array));
    }

    static void readFarming(const fa::save::SaveArchive& parent,
                            const fa::tilemap::Tilemap& map,
                            fa::farming::FarmingService& farming) {
        const fa::save::SaveArchive array = parent.child("farming");
        if (!array.isArray()) {
            return;
        }
        std::vector<fa::farming::FarmTileEntry> entries;
        for (std::size_t index = 0; index < array.size(); ++index) {
            const fa::save::SaveArchive object = array.at(index);
            int x = 0;
            int y = 0;
            if (!object.isObject() || !object.getInt("x", x) || !object.getInt("y", y) || !map.contains(x, y)) {
                continue;
            }
            fa::farming::FarmTileEntry entry;
            entry.cell = fa::tilemap::TileCoord{x, y};
            static_cast<void>(object.getBool("tilled", entry.tile.tilled));
            static_cast<void>(object.getBool("watered", entry.tile.watered));
            static_cast<void>(object.getBool("greenhouse", entry.tile.greenhouse));
            static_cast<void>(object.getInt("daysSinceWatered", entry.tile.daysSinceWatered));
            static_cast<void>(object.getBool("hasCrop", entry.hasCrop));
            if (entry.hasCrop) {
                std::string id;
                if (object.getString("crop", id)) {
                    entry.crop.crop = fa::content::ContentId::fromName(id);
                }
                if (object.getString("seed", id)) {
                    entry.crop.seed = fa::content::ContentId::fromName(id);
                }
                if (object.getString("harvestItem", id)) {
                    entry.crop.harvestItem = fa::content::ContentId::fromName(id);
                }
                std::string stageDays;
                if (object.getString("stageDays", stageDays)) {
                    entry.crop.stageDays = parseIntList(stageDays);
                }
                int seasons = 0;
                static_cast<void>(object.getInt("seasons", seasons));
                entry.crop.seasons = static_cast<std::uint8_t>(seasons);
                static_cast<void>(object.getBool("regrow", entry.crop.regrow));
                static_cast<void>(object.getInt("regrowDays", entry.crop.regrowDays));
                static_cast<void>(object.getFloat("growthDays", entry.crop.growthDays));
                static_cast<void>(object.getInt("stage", entry.crop.stage));
                static_cast<void>(object.getBool("harvestable", entry.crop.harvestable));
                static_cast<void>(object.getBool("dead", entry.crop.dead));
                static_cast<void>(object.getBool("wateredToday", entry.crop.wateredToday));
            }
            entries.push_back(std::move(entry));
        }
        farming.restore(entries);
    }

    [[nodiscard]] static std::string joinIntList(std::span<const int> values) {
        std::string text;
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (index != 0) {
                text += ',';
            }
            text += std::to_string(values[index]);
        }
        return text;
    }

    /// \brief Stable role of a mutable scene object in the current map.
    ///
    /// Entity ids are scene-local, so saves store this role instead of a handle and
    /// the load path resolves it against the freshly built scene.
    [[nodiscard]] fa::scene::EntityId worldObjectForRole(std::string_view role) const {
        if (gameScene == nullptr) {
            return {};
        }
        if (role == "placed") {
            return gameScene->placedEntity();
        }
        if (gameScene->isFarm()) {
            if (role == "dummy1") {
                return gameScene->combatDummy1();
            }
            if (role == "dummy2") {
                return gameScene->combatDummy2();
            }
            if (role == "ai.agent") {
                return gameScene->aiAgentEntity();
            }
            if (role == "ai.decoy") {
                return gameScene->aiDecoyEntity();
            }
            return {};
        }
        if (role == "dummy1") {
            return gameScene->forestDummy1();
        }
        if (role == "dummy2") {
            return gameScene->forestDummy2();
        }
        for (std::size_t index = 2; index < gameScene->enemyCount(); ++index) {
            if (role == gameScene->enemyRole(index)) {
                return gameScene->enemyAt(index);
            }
        }
        return {};
    }

    /// \brief Inverse of \ref worldObjectForRole, used when writing agent state.
    [[nodiscard]] std::string_view worldObjectRoleFor(fa::scene::EntityId id) const {
        if (gameScene == nullptr || !id.isValid()) {
            return {};
        }
        if (id == gameScene->placedEntity()) {
            return "placed";
        }
        if (gameScene->isFarm()) {
            if (id == gameScene->combatDummy1()) {
                return "dummy1";
            }
            if (id == gameScene->combatDummy2()) {
                return "dummy2";
            }
            if (id == gameScene->aiAgentEntity()) {
                return "ai.agent";
            }
            if (id == gameScene->aiDecoyEntity()) {
                return "ai.decoy";
            }
            return {};
        }
        if (id == gameScene->forestDummy1()) {
            return "dummy1";
        }
        if (id == gameScene->forestDummy2()) {
            return "dummy2";
        }
        for (std::size_t index = 2; index < gameScene->enemyCount(); ++index) {
            if (id == gameScene->enemyAt(index)) {
                return gameScene->enemyRole(index);
            }
        }
        return {};
    }

    /// \brief Fills the shared M14/M25 save context from the live game scene.
    fa::core::Status fillSaveContext(fa::save::SaveContext& context) {
        context.sceneId = "showcase";
        context.world.setBool("play", playMode);
        if (gameScene != nullptr) {
            context.world.setString("map", gameScene->mapName());
        }
        context.inGameDay = calendar != nullptr ? calendar->date().day : 1;
        context.playSeconds = flow != nullptr && !flow->session().slot.empty() ? flow->session().playSeconds
                                                                               : static_cast<double>(fixedSteps) / 60.0;

        const fa::scene::WorldStats world = sceneWorldStats();
        context.world.setInt("entities", static_cast<int>(world.liveEntities));
        context.world.setVec2("marker", sceneMarkerPosition());
        context.player.setFloat("stamina", scenePlayerStamina());
        if (gameScene != nullptr) {
            context.player.setFloat("health", gameScene->playerHealth());
        }
        if (playerInventory != nullptr) {
            context.inventories.setInt("backpackItems", playerInventory->backpack().totalItems());
            context.inventories.setInt("backpackSlots", playerInventory->backpack().usedSlots());
            context.inventories.setInt("hotbarItems", playerInventory->hotbar().inventory().totalItems());
            context.inventories.setInt("selected", playerInventory->hotbar().selected());
            writeInventory(context.inventories, "backpack", playerInventory->backpack());
            writeInventory(context.inventories, "hotbar", playerInventory->hotbar().inventory());
            writeStack(
                context.inventories, "tool", playerInventory->equipment().at(fa::inventory::EquipmentSlot::Tool));
            writeStack(
                context.inventories, "weapon", playerInventory->equipment().at(fa::inventory::EquipmentSlot::Weapon));
        }
        if (economy != nullptr) {
            context.economy.setInt("gold", economy->gold());

            fa::save::SaveArchive shipping = fa::save::SaveArchive::array();
            for (const fa::inventory::ItemStack& itemStack : economy->shipping()) {
                fa::save::SaveArchive entry = fa::save::SaveArchive::object();
                entry.setString("id", itemStack.id.name());
                entry.setInt("count", itemStack.count);
                shipping.push(std::move(entry));
            }
            context.economy.setChild("shipping", std::move(shipping));

            fa::save::SaveArchive jobs = fa::save::SaveArchive::array();
            for (const fa::economy::ProcessingJob& job : economy->jobs()) {
                fa::save::SaveArchive entry = fa::save::SaveArchive::object();
                entry.setString("recipe", job.recipe.name());
                entry.setString("output", job.output.name());
                entry.setInt("outputCount", job.outputCount);
                entry.setString("machine", job.machineKind);
                entry.setInt("remainingMinutes", job.remainingMinutes);
                entry.setBool("ready", job.ready);
                jobs.push(std::move(entry));
            }
            context.economy.setChild("jobs", std::move(jobs));

            fa::save::SaveArchive stockList = fa::save::SaveArchive::array();
            for (const fa::economy::ShopDef& shop : shops.shops()) {
                for (const fa::economy::ShopEntry& listed : shop.stock) {
                    if (listed.dailyLimit < 0) {
                        continue;
                    }
                    fa::save::SaveArchive entry = fa::save::SaveArchive::object();
                    entry.setString("shop", shop.id.name());
                    entry.setString("item", listed.item.name());
                    entry.setInt("remaining", economy->remaining(shop.id, listed.item));
                    stockList.push(std::move(entry));
                }
            }
            context.economy.setChild("stock", std::move(stockList));
        }
        if (gameScene != nullptr) {
            writeFarming(context.world, gameScene->farmingTiles());
            context.world.setInt("facing", static_cast<int>(gameScene->playerFacing()));

            fa::save::SaveArchive loot = fa::save::SaveArchive::array();
            gameScene->world().each<LootDrop, fa::scene::Transform>(
                [&loot](fa::scene::EntityId, const LootDrop& drop, const fa::scene::Transform& transform) {
                    fa::save::SaveArchive entry = fa::save::SaveArchive::object();
                    entry.setString("id", drop.item.name());
                    entry.setInt("count", drop.count);
                    entry.setVec2("position", transform.position);
                    loot.push(std::move(entry));
                });
            context.world.setChild("loot", std::move(loot));

            fa::save::SaveArchive objects = fa::save::SaveArchive::array();
            const auto writeWorldObject = [this, &objects](std::string_view role, fa::scene::EntityId id) {
                if (!id.isValid()) {
                    return;
                }
                fa::save::SaveArchive entry = fa::save::SaveArchive::object();
                entry.setString("role", role);
                entry.setBool("alive", gameScene->world().isAlive(id));
                if (const fa::scene::Transform* transform = gameScene->world().tryGet<fa::scene::Transform>(id);
                    transform != nullptr) {
                    entry.setVec2("position", transform->position);
                }
                if (const fa::player::Health* health = gameScene->world().tryGet<fa::player::Health>(id);
                    health != nullptr) {
                    entry.setInt("health", health->current);
                }
                objects.push(std::move(entry));
            };
            writeWorldObject("placed", gameScene->placedEntity());
            if (gameScene->isFarm()) {
                writeWorldObject("dummy1", gameScene->combatDummy1());
                writeWorldObject("dummy2", gameScene->combatDummy2());
                writeWorldObject("ai.agent", gameScene->aiAgentEntity());
                writeWorldObject("ai.decoy", gameScene->aiDecoyEntity());
            } else {
                for (std::size_t index = 0; index < gameScene->enemyCount(); ++index) {
                    writeWorldObject(gameScene->enemyRole(index), gameScene->enemyAt(index));
                }
            }
            context.world.setChild("objects", std::move(objects));

            fa::save::SaveArchive debris = fa::save::SaveArchive::array();
            for (const fa::farming::Debris& piece : gameScene->debris().debris()) {
                fa::save::SaveArchive entry = fa::save::SaveArchive::object();
                entry.setInt("x", piece.cell.x);
                entry.setInt("y", piece.cell.y);
                entry.setInt("kind", static_cast<int>(piece.kind));
                entry.setString("yield", piece.yield.name());
                entry.setInt("amount", piece.amount);
                debris.push(std::move(entry));
            }
            context.world.setChild("debris", std::move(debris));

            fa::save::SaveArchive aiList = fa::save::SaveArchive::array();
            gameScene->world().each<fa::ai::AiAgent>(
                [this, &aiList](fa::scene::EntityId id, const fa::ai::AiAgent& agent) {
                    const std::string_view role = worldObjectRoleFor(id);
                    if (role.empty()) {
                        return;
                    }
                    const fa::ai::AiAgentSnapshot snapshot = fa::ai::captureAiAgent(agent);
                    fa::save::SaveArchive entry = fa::save::SaveArchive::object();
                    entry.setString("role", role);
                    entry.setInt("state", static_cast<int>(snapshot.state));
                    entry.setVec2("home", snapshot.home);
                    entry.setFloat("stateTime", snapshot.stateTime);
                    entry.setFloat("attackCooldown", snapshot.attackCooldown);
                    entry.setFloat("loseTimer", snapshot.loseTimer);
                    entry.setFloat("repathTimer", snapshot.repathTimer);
                    entry.setBool("targetVisible", snapshot.targetVisible);
                    std::string targetRole;
                    if (agent.target.isValid()) {
                        if (agent.target == gameScene->aiDecoyEntity()) {
                            targetRole = "decoy";
                        } else if (agent.target == gameScene->playerEntity()) {
                            targetRole = "player";
                        }
                    }
                    entry.setString("target", targetRole);
                    aiList.push(std::move(entry));
                });
            if (aiList.size() > 0) {
                context.world.setChild("ai", std::move(aiList));
            }

            const fa::narrative::NarrativeSnapshot narrativeState = gameScene->narrative().snapshot();
            fa::save::SaveArchive affinity = fa::save::SaveArchive::object();
            for (const auto& [npc, value] : narrativeState.affinity) {
                affinity.setInt(npc.name(), value);
            }
            context.quests.setChild("affinity", std::move(affinity));

            const auto writeIdSet = [](const auto& entries) {
                fa::save::SaveArchive set = fa::save::SaveArchive::object();
                for (const auto& [id, enabled] : entries) {
                    if (enabled) {
                        set.setBool(id.name(), true);
                    }
                }
                return set;
            };
            context.quests.setChild("flags", writeIdSet(narrativeState.flags));
            context.quests.setChild("talkedToday", writeIdSet(narrativeState.talkedToday));
            context.quests.setChild("giftedToday", writeIdSet(narrativeState.giftedToday));

            fa::save::SaveArchive questList = fa::save::SaveArchive::array();
            for (const auto& [id, quest] : narrativeState.quests) {
                fa::save::SaveArchive entry = fa::save::SaveArchive::object();
                entry.setString("id", id.name());
                entry.setBool("active", quest.active);
                entry.setBool("completed", quest.completed);
                entry.setString("progress", joinIntList(quest.progress));
                questList.push(std::move(entry));
            }
            context.quests.setChild("quests", std::move(questList));
        }
        if (calendar != nullptr) {
            context.world.setInt("dateYear", calendar->date().year);
            context.world.setInt("dateSeason", static_cast<int>(calendar->season()));
            context.world.setInt("dateDay", calendar->date().day);
            context.world.setFloat("timeOfDay", calendar->timeOfDay());
            context.world.setInt("weatherToday", static_cast<int>(calendar->today()));
            context.world.setInt("weatherTomorrow", static_cast<int>(calendar->tomorrow()));
        }
        return fa::core::okStatus();
    }

    /// \brief Writes one slot using the shared builder.
    fa::core::Status saveToSlot(std::string_view slot) {
        if (saveService == nullptr) {
            return fa::core::errorStatus(fa::save::save_error::kIoFailure, "save service is not initialized");
        }
        fa::save::SaveContext context;
        if (const fa::core::Status built = fillSaveContext(context); built.isError()) {
            return built;
        }
        context.slotName = std::string(slot);
        return saveService->save(slot, context);
    }

    /// \brief Restores the calendar, scene, farming and inventory sections.
    fa::core::Status applySaveFile(const fa::save::SaveFile& file) {
        if (calendar != nullptr) {
            int year = calendar->date().year;
            int season = static_cast<int>(calendar->season());
            int day = calendar->date().day;
            float timeOfDay = calendar->timeOfDay();
            int today = static_cast<int>(calendar->today());
            int tomorrow = static_cast<int>(calendar->tomorrow());
            static_cast<void>(file.world.getInt("dateYear", year));
            static_cast<void>(file.world.getInt("dateSeason", season));
            static_cast<void>(file.world.getInt("dateDay", day));
            static_cast<void>(file.world.getFloat("timeOfDay", timeOfDay));
            static_cast<void>(file.world.getInt("weatherToday", today));
            static_cast<void>(file.world.getInt("weatherTomorrow", tomorrow));

            fa::world::GameDate date;
            date.year = year > 0 ? year : 1;
            const int seasonCount = static_cast<int>(fa::world::Season::Count);
            date.season = static_cast<fa::world::Season>(season >= 0 && season < seasonCount ? season : 0);
            date.day = day > 0 ? day : 1;
            const int weatherCount = static_cast<int>(fa::world::Weather::Count);
            const fa::world::Weather weatherToday =
                static_cast<fa::world::Weather>(today >= 0 && today < weatherCount ? today : 0);
            const fa::world::Weather weatherTomorrow =
                static_cast<fa::world::Weather>(tomorrow >= 0 && tomorrow < weatherCount ? tomorrow : 0);
            calendar->restore(date, timeOfDay, weatherToday, weatherTomorrow);
            lastEconomyTimeOfDay = calendar->timeOfDay();
            economyMinuteCarry = 0.0f;
        }

        if (gameScene != nullptr) {
            if (playMode) {
                std::string savedMap;
                static_cast<void>(file.world.getString("map", savedMap));
                const GameMap target = savedMap == "forest"
                                           ? GameMap::Forest
                                           : (savedMap == "farm" ? GameMap::Farm : gameScene->mapKind());
                // Rebuild even when the map matches: entities, enemies and AI state
                // are scene-local, so every saved section must land on a fresh scene.
                if (!switchMap(target, std::nullopt)) {
                    return fa::core::errorStatus(fa::save::save_error::kCorruptSave, "could not restore the saved map");
                }
            }
            fa::core::Vec2 marker;
            if (file.world.getVec2("marker", marker)) {
                gameScene->teleportMarker(marker);
            }
            int facing = 0;
            if (file.world.getInt("facing", facing) && facing >= 0 &&
                facing < static_cast<int>(fa::player::Facing::Count)) {
                gameScene->setPlayerFacing(static_cast<fa::player::Facing>(facing));
            }
            readFarming(file.world, gameScene->editableMap(), gameScene->farming());
            float stamina = gameScene->playerStamina();
            if (file.player.getFloat("stamina", stamina)) {
                gameScene->setPlayerStamina(stamina);
                gameState.playerStamina = gameScene->playerStamina();
            }
            float health = gameScene->playerHealth();
            if (file.player.getFloat("health", health)) {
                gameScene->setPlayerHealth(health);
                gameState.playerHealth = static_cast<int>(gameScene->playerHealth());
            }

            clearLootDrops();
            const fa::save::SaveArchive lootArray = file.world.child("loot");
            if (lootArray.isArray()) {
                for (std::size_t index = 0; index < lootArray.size(); ++index) {
                    const fa::save::SaveArchive entry = lootArray.at(index);
                    std::string id;
                    int count = 0;
                    fa::core::Vec2 position{};
                    if (entry.isObject() && entry.getString("id", id) && entry.getInt("count", count) && count > 0 &&
                        entry.getVec2("position", position)) {
                        spawnLoot(position, fa::content::ContentId::fromName(id), count);
                    }
                }
            }

            const fa::save::SaveArchive objects = file.world.child("objects");
            if (objects.isArray()) {
                for (std::size_t index = 0; index < objects.size(); ++index) {
                    const fa::save::SaveArchive entry = objects.at(index);
                    std::string role;
                    if (!entry.isObject() || !entry.getString("role", role)) {
                        continue;
                    }
                    const fa::scene::EntityId id = worldObjectForRole(role);
                    if (!id.isValid()) {
                        continue;
                    }
                    bool alive = true;
                    static_cast<void>(entry.getBool("alive", alive));
                    if (!alive) {
                        gameScene->world().destroy(id);
                        continue;
                    }
                    if (fa::scene::Transform* transform = gameScene->world().tryGet<fa::scene::Transform>(id);
                        transform != nullptr) {
                        fa::core::Vec2 position;
                        if (entry.getVec2("position", position)) {
                            transform->position = position;
                        }
                    }
                    if (fa::player::Health* hp = gameScene->world().tryGet<fa::player::Health>(id); hp != nullptr) {
                        int storedHealth = hp->current;
                        if (entry.getInt("health", storedHealth)) {
                            hp->current = std::max(0, std::min(storedHealth, hp->max));
                        }
                    }
                }
                gameScene->syncPhysics();
            }

            gameScene->debris().clear();
            const fa::save::SaveArchive debrisArray = file.world.child("debris");
            if (debrisArray.isArray()) {
                const int debrisKinds = static_cast<int>(fa::farming::DebrisKind::Count);
                for (std::size_t index = 0; index < debrisArray.size(); ++index) {
                    const fa::save::SaveArchive entry = debrisArray.at(index);
                    int x = 0;
                    int y = 0;
                    int kind = 0;
                    int amount = 1;
                    std::string yield;
                    if (!entry.isObject() || !entry.getInt("x", x) || !entry.getInt("y", y)) {
                        continue;
                    }
                    fa::farming::Debris piece;
                    piece.cell = fa::tilemap::TileCoord{x, y};
                    static_cast<void>(entry.getInt("kind", kind));
                    piece.kind = static_cast<fa::farming::DebrisKind>(kind >= 0 && kind < debrisKinds ? kind : 0);
                    if (entry.getString("yield", yield)) {
                        piece.yield = fa::content::ContentId::fromName(yield);
                    }
                    static_cast<void>(entry.getInt("amount", amount));
                    piece.amount = std::max(1, amount);
                    static_cast<void>(gameScene->debris().add(piece));
                }
            }

            const auto restoreAi = [this](const fa::save::SaveArchive& entry, fa::scene::EntityId id) {
                fa::ai::AiAgent* agent = gameScene->world().tryGet<fa::ai::AiAgent>(id);
                if (agent == nullptr) {
                    return;
                }
                fa::ai::AiAgentSnapshot snapshot;
                int state = static_cast<int>(snapshot.state);
                if (entry.getInt("state", state)) {
                    snapshot.state = static_cast<fa::ai::AiState>(state);
                }
                static_cast<void>(entry.getVec2("home", snapshot.home));
                static_cast<void>(entry.getFloat("stateTime", snapshot.stateTime));
                static_cast<void>(entry.getFloat("attackCooldown", snapshot.attackCooldown));
                static_cast<void>(entry.getFloat("loseTimer", snapshot.loseTimer));
                static_cast<void>(entry.getFloat("repathTimer", snapshot.repathTimer));
                static_cast<void>(entry.getBool("targetVisible", snapshot.targetVisible));
                fa::ai::applyAiAgentSnapshot(*agent, snapshot);

                std::string targetRole;
                static_cast<void>(entry.getString("target", targetRole));
                if (targetRole == "decoy") {
                    agent->target = gameScene->aiDecoyEntity();
                } else if (targetRole == "player") {
                    agent->target = gameScene->playerEntity();
                } else {
                    agent->target = fa::scene::EntityId{};
                }
                if (!agent->target.isValid() || !gameScene->isAlive(agent->target)) {
                    agent->target = fa::scene::EntityId{};
                    agent->targetVisible = false;
                }
            };

            const fa::save::SaveArchive ai = file.world.child("ai");
            if (ai.isArray()) {
                for (std::size_t index = 0; index < ai.size(); ++index) {
                    const fa::save::SaveArchive entry = ai.at(index);
                    std::string role;
                    if (!entry.isObject() || !entry.getString("role", role)) {
                        continue;
                    }
                    const fa::scene::EntityId id = worldObjectForRole(role);
                    if (id.isValid()) {
                        restoreAi(entry, id);
                    }
                }
            } else if (ai.isObject()) {
                // P0-3 wrote a single object for the farm agent.
                restoreAi(ai, gameScene->aiAgentEntity());
            }
            gameScene->resetAiPaths();

            fa::narrative::NarrativeSnapshot narrativeState;
            const fa::save::SaveArchive affinity = file.quests.child("affinity");
            if (affinity.isObject()) {
                for (const std::string& key : affinity.keys()) {
                    int value = 0;
                    if (affinity.getInt(key, value)) {
                        narrativeState.affinity[fa::content::ContentId::fromName(key)] = value;
                    }
                }
            }
            const auto readIdSet = [&file](std::string_view key, auto& out) {
                const fa::save::SaveArchive set = file.quests.child(key);
                if (!set.isObject()) {
                    return;
                }
                for (const std::string& id : set.keys()) {
                    bool enabled = false;
                    if (set.getBool(id, enabled) && enabled) {
                        out[fa::content::ContentId::fromName(id)] = true;
                    }
                }
            };
            readIdSet("flags", narrativeState.flags);
            readIdSet("talkedToday", narrativeState.talkedToday);
            readIdSet("giftedToday", narrativeState.giftedToday);

            const fa::save::SaveArchive questList = file.quests.child("quests");
            if (questList.isArray()) {
                for (std::size_t index = 0; index < questList.size(); ++index) {
                    const fa::save::SaveArchive entry = questList.at(index);
                    std::string id;
                    if (!entry.isObject() || !entry.getString("id", id) || id.empty()) {
                        continue;
                    }
                    fa::narrative::QuestState state;
                    state.id = fa::content::ContentId::fromName(id);
                    static_cast<void>(entry.getBool("active", state.active));
                    static_cast<void>(entry.getBool("completed", state.completed));
                    std::string progress;
                    if (entry.getString("progress", progress)) {
                        state.progress = parseIntList(progress);
                    }
                    narrativeState.quests[state.id] = std::move(state);
                }
            }
            gameScene->narrative().restore(narrativeState);
        }

        if (playerInventory != nullptr) {
            readInventory(file.inventories, "backpack", playerInventory->backpack());
            readInventory(file.inventories, "hotbar", playerInventory->hotbar().inventory());
            const fa::inventory::ItemStack tool = readStack(file.inventories, "tool");
            if (!tool.empty()) {
                static_cast<void>(playerInventory->equipment().equip(fa::inventory::EquipmentSlot::Tool, tool));
            } else {
                static_cast<void>(playerInventory->equipment().unequip(fa::inventory::EquipmentSlot::Tool));
            }
            const fa::inventory::ItemStack weapon = readStack(file.inventories, "weapon");
            if (!weapon.empty()) {
                static_cast<void>(playerInventory->equipment().equip(fa::inventory::EquipmentSlot::Weapon, weapon));
            } else {
                static_cast<void>(playerInventory->equipment().unequip(fa::inventory::EquipmentSlot::Weapon));
            }
            int selected = playerInventory->hotbar().selected();
            if (file.inventories.getInt("selected", selected)) {
                static_cast<void>(playerInventory->hotbar().select(selected));
            }
        }

        if (economy != nullptr) {
            int gold = economy->gold();
            if (file.economy.getInt("gold", gold)) {
                economy->setGold(gold);
            }

            std::vector<fa::inventory::ItemStack> shipping;
            const fa::save::SaveArchive shippingArray = file.economy.child("shipping");
            if (shippingArray.isArray()) {
                for (std::size_t index = 0; index < shippingArray.size(); ++index) {
                    const fa::save::SaveArchive entry = shippingArray.at(index);
                    std::string id;
                    int count = 0;
                    if (entry.isObject() && entry.getString("id", id) && entry.getInt("count", count) && count > 0) {
                        shipping.push_back(fa::inventory::ItemStack{fa::content::ContentId::fromName(id), count, -1});
                    }
                }
            }
            economy->restoreShipping(shipping);

            std::vector<fa::economy::ProcessingJob> jobs;
            const fa::save::SaveArchive jobsArray = file.economy.child("jobs");
            if (jobsArray.isArray()) {
                for (std::size_t index = 0; index < jobsArray.size(); ++index) {
                    const fa::save::SaveArchive entry = jobsArray.at(index);
                    if (!entry.isObject()) {
                        continue;
                    }
                    fa::economy::ProcessingJob job;
                    std::string id;
                    if (entry.getString("recipe", id)) {
                        job.recipe = fa::content::ContentId::fromName(id);
                    }
                    if (entry.getString("output", id)) {
                        job.output = fa::content::ContentId::fromName(id);
                    }
                    if (entry.getString("machine", id)) {
                        job.machineKind = id;
                    }
                    static_cast<void>(entry.getInt("outputCount", job.outputCount));
                    static_cast<void>(entry.getInt("remainingMinutes", job.remainingMinutes));
                    static_cast<void>(entry.getBool("ready", job.ready));
                    jobs.push_back(std::move(job));
                }
            }
            economy->restoreJobs(jobs);

            const fa::save::SaveArchive stockArray = file.economy.child("stock");
            if (stockArray.isArray()) {
                for (std::size_t index = 0; index < stockArray.size(); ++index) {
                    const fa::save::SaveArchive entry = stockArray.at(index);
                    std::string shop;
                    std::string item;
                    int remaining = 0;
                    if (entry.isObject() && entry.getString("shop", shop) && entry.getString("item", item) &&
                        entry.getInt("remaining", remaining)) {
                        economy->restoreRemaining(
                            fa::content::ContentId::fromName(shop), fa::content::ContentId::fromName(item), remaining);
                    }
                }
            }
        }
        syncEconomyCalendar();
        return fa::core::okStatus();
    }

    /// \brief Loads one slot; the caller decides whether to apply it to the scene.
    fa::core::Result<fa::save::SaveFile> loadFromSlot(std::string_view slot) {
        if (saveService == nullptr) {
            return fa::core::Result<fa::save::SaveFile>::err(fa::save::save_error::kIoFailure,
                                                             "save service is not initialized");
        }
        return saveService->load(slot);
    }

    /// \brief Writes and immediately reads `autosave`, then snapshots the header.
    void runSaveSmoke() {
        saveStats = SaveRunStats{};
        if (saveService == nullptr || gameScene == nullptr) {
            saveStats.error = "no scene or save service";
            return;
        }

        const fa::core::Status saved = saveToSlot("autosave");
        if (saved.isError()) {
            saveStats.error = saved.error().message;
            return;
        }

        const fa::core::Result<fa::save::SaveFile> loaded = loadFromSlot("autosave");
        if (loaded.isError()) {
            saveStats.error = loaded.error().message;
            return;
        }

        const fa::save::SaveFile& file = loaded.value();
        saveStats.ok = true;
        saveStats.recovered = file.recoveredFromBackup;
        saveStats.slot = file.meta.slotName;
        saveStats.scene = file.meta.sceneId;
        saveStats.version = file.meta.saveVersion;
        saveStats.day = file.meta.inGameDay;
        saveStats.playSeconds = file.meta.playSeconds;
        int entities = 0;
        static_cast<void>(file.world.getInt("entities", entities));
        saveStats.entities = static_cast<std::size_t>(entities > 0 ? entities : 0);
        static_cast<void>(file.world.getVec2("marker", saveStats.marker));
        static_cast<void>(file.player.getFloat("stamina", saveStats.stamina));
        saveStats.slots = saveService->listSlots().size();
    }

    void shutdownSave() { saveService.reset(); }

    [[nodiscard]] SaveRunStats saveRunStats() const { return saveStats; }

    /// \brief Creates the M25 state machine over the live game services.
    void initializeFlow() {
        fa::narrative::NarrativeService* narrativeService = gameScene != nullptr ? &gameScene->narrative() : nullptr;
        flow = std::make_unique<fa::flow::GameFlow>(saveService.get(), calendar.get(), economy.get(), narrativeService);
        flow->setSaveBuilder([this](fa::save::SaveContext& context) { return fillSaveContext(context); });
        flow->setLoadHandler([this](const fa::save::SaveFile& file) { return applySaveFile(file); });
        // "开始新游戏" must wipe the previous session: the scene, the shared GameState
        // services (farming/debris/narrative are referenced by every scene), inventory,
        // economy, calendar and transient UI. Without this handler the world simply
        // kept running where the last session left it.
        flow->setNewGameHandler([this]() -> fa::core::Status {
            gameState.farming = fa::farming::FarmingService{&content};
            gameState.debris = fa::farming::DebrisLayer{};
            gameState.narrative = fa::narrative::NarrativeService{&narrative};
            gameState.narrative.setSeason(fa::content::Season::Spring);
            gameState.playerHealth = gameState.playerMaxHealth;
            gameState.playerStamina = gameState.playerStaminaMax;
            gameState.playerFacing = fa::player::Facing::South;

            if (const fa::core::Status status = initializeInventory(true); status.isError()) {
                return status;
            }
            if (economy != nullptr) {
                economy->setGold(100);
                economy->setSeason(fa::content::Season::Spring);
                economy->setDay(1);
                economy->restoreShipping({});
                economy->restoreJobs({});
                economy->clear();
            }
            if (calendar != nullptr) {
                fa::world::GameDate fresh;
                fresh.year = 1;
                fresh.season = fa::world::Season::Spring;
                fresh.day = 1;
                calendar->restore(
                    fresh, config.gameplay.wakeHour, fa::world::Weather::Sunny, fa::world::Weather::Sunny);
                lastEconomyTimeOfDay = calendar->timeOfDay();
                economyMinuteCarry = 0.0f;
            }
            if (!switchMap(GameMap::Farm, std::nullopt)) {
                return fa::core::errorStatus(fa::core::error_code::kUnsupported, "could not rebuild the farm scene");
            }
            damageTexts.clear();
            playMessage.clear();
            playMessageTimer = 0.0f;
            setPlayMessage("新的冒险开始！");
            return fa::core::okStatus();
        });
        flow->setEndOfDayHandler([this] {
            if (gameScene == nullptr || calendar == nullptr) {
                return;
            }
            fa::farming::FarmingDayContext context;
            context.season = contentSeason(calendar->season());
            context.rain = calendar->isRaining();
            context.growthMultiplier = calendar->cropGrowthMultiplier();
            gameScene->advanceFarmingDay(context);
        });
        flow->setAutosaveIntervalDays(config.gameplay.autosaveDays);
        flow->setAutosaveOnQuit(true);

        if (playMode) {
            // Sleep resumes at the configured wake hour.
            flow->setDayStartHour(config.gameplay.wakeHour);
            if (playTest) {
                static_cast<void>(flow->startNewGame("playtest"));
            } else {
                // The pause/main-menu overlays drive New Game and Continue;
                // starting in MainMenu keeps every manual run behind the UI.
                static_cast<void>(flow->enterMainMenu());
            }
        }
    }

    /// \brief True when gameplay simulation and player input should run.
    [[nodiscard]] bool gameplayActive() const {
        if (!playMode || flow == nullptr) {
            return true;
        }
        return flow->state() == fa::flow::AppState::Playing;
    }

    /// \brief Refreshes the cached save-slot metadata for the menu.
    void refreshSlots() {
        slotList = saveService != nullptr ? saveService->listSlots() : std::vector<fa::save::SaveMetadata>{};
    }

    /// \brief Continues the autosave when it belongs to a play session.
    void continueGame() {
        if (flow == nullptr) {
            return;
        }
        if (saveService != nullptr) {
            const fa::core::Result<fa::save::SaveFile> existing = saveService->load(fa::flow::kAutosaveSlot);
            bool playSave = false;
            if (existing.isOk() && existing.value().world.getBool("play", playSave) && playSave &&
                existing.value().inventories.child("backpack").isArray() &&
                flow->loadGame(fa::flow::kAutosaveSlot).isOk()) {
                return;
            }
        }
        static_cast<void>(flow->startNewGame("play"));
    }

    /// \brief Loads a slot from the menu, leaving the current session first.
    void loadSlot(std::string_view slot) {
        if (flow == nullptr) {
            return;
        }
        if (flow->state() != fa::flow::AppState::MainMenu) {
            if (flow->enterMainMenu().isError()) {
                return;
            }
        }
        static_cast<void>(flow->loadGame(slot));
        slotsOpen = false;
    }

    /// \brief Saves the active session into a manual slot.
    void saveSlot(std::string_view slot) {
        if (flow == nullptr) {
            return;
        }
        if (flow->saveToSlot(slot).isOk()) {
            ++slotActions;
        }
        refreshSlots();
    }

    /// \brief Walks new game, pause, save, day end, death, menu, load and quit.
    void runFlowSmoke() {
        flowStats = FlowRunStats{};
        if (flow == nullptr) {
            return;
        }

        flowStats.newGameOk = flow->startNewGame("slot1").isOk();
        flow->update(1.5);
        flow->requestPause(true);
        const bool paused = flow->state() == fa::flow::AppState::Paused;
        flow->requestPause(false);
        flowStats.pauseOk = paused && flow->state() == fa::flow::AppState::Playing;
        flowStats.saveOk = flow->saveGame().isOk();
        flowStats.dayEndOk = flow->endDay().isOk();
        flowStats.gameOverOk = flow->requestGameOver("smoke").isOk();
        flowStats.respawnOk = flow->respawn().isOk();
        flowStats.menuOk = flow->enterMainMenu().isOk();
        flowStats.loadOk = flow->loadGame("slot1").isOk();
        flow->markDirty();
        flow->requestQuit();
        flowStats.quitOk = flow->state() == fa::flow::AppState::Quit;
        flowStats.stats = flow->stats();
    }

    void shutdownFlow() { flow.reset(); }

    [[nodiscard]] FlowRunStats flowRunStats() const {
        if (playMode && flow != nullptr) {
            FlowRunStats live = flowStats;
            live.stats = flow->stats();
            live.newGameOk = flow->stats().newGames > 0 || flow->stats().loads > 0;
            live.saveOk = flow->stats().saves > 0 || flow->stats().autosaves > 0;
            live.dayEndOk = flow->stats().dayEnds > 0;
            live.menuOk = flow->stats().menuReturns > 0;
            live.loadOk = flow->stats().loads > 0;
            live.pauseOk = flow->stats().pauses > 0;
            live.gameOverOk = flow->stats().deaths > 0;
            live.respawnOk = flow->stats().respawns > 0;
            live.quitOk = flow->stats().quitRequests > 0;
            return live;
        }
        return flowStats;
    }

    /// \brief Loads `data/audio.json`; missing content keeps the built-in fallback.
    bool loadAudioCatalog() {
        const std::string text = readDataText("audio.json", std::string_view{});
        if (text.empty()) {
            return false;
        }
        const fa::core::Result<fa::save::SaveArchive> parsed = fa::save::SaveArchive::parse(text);
        if (parsed.isError()) {
            std::printf("audio: %s, using fallback tones\n", parsed.error().message.c_str());
            return false;
        }
        const fa::save::SaveArchive clips = parsed.value().child("clips");
        if (!clips.isArray()) {
            std::printf("audio: \"clips\" must be an array, using fallback tones\n");
            return false;
        }
        audioClipDefs.clear();
        audioClips.clear();
        for (std::size_t index = 0; index < clips.size(); ++index) {
            const fa::save::SaveArchive entry = clips.at(index);
            AudioClipDef def;
            if (!entry.isObject() || !entry.getString("id", def.id) || def.id.empty()) {
                continue;
            }
            std::string bus;
            if (entry.getString("bus", bus)) {
                if (bus == "music") {
                    def.bus = fa::audio::AudioBus::Music;
                } else if (bus == "ambience") {
                    def.bus = fa::audio::AudioBus::Ambience;
                } else if (bus == "master") {
                    def.bus = fa::audio::AudioBus::Master;
                }
            }
            static_cast<void>(entry.getFloat("frequency_hz", def.frequency));
            static_cast<void>(entry.getFloat("duration", def.duration));
            static_cast<void>(entry.getFloat("amplitude", def.amplitude));
            static_cast<void>(entry.getFloat("volume", def.volume));
            static_cast<void>(entry.getBool("loop", def.loop));
            if (def.frequency <= 0.0f || def.duration <= 0.0f) {
                std::printf("audio: clip %s has a non-positive frequency/duration\n", def.id.c_str());
                continue;
            }
            def.amplitude = fa::core::clampf(def.amplitude, 0.0f, 1.0f);
            def.volume = fa::core::clampf(def.volume, 0.0f, 1.0f);
            audioClipDefs[def.id] = def;
            audioClips[def.id] = std::make_shared<fa::audio::AudioClip>(
                fa::audio::makeTone(def.frequency, def.duration, 48000, 2, def.amplitude));
        }
        return !audioClips.empty();
    }

    /// \brief Plays one catalog clip; unknown ids and a missing device are no-ops.
    void playSfx(std::string_view id, float volumeScale = 1.0f) {
        if (audioEngine == nullptr) {
            return;
        }
        const auto clip = audioClips.find(std::string(id));
        if (clip == audioClips.end() || !clip->second) {
            return;
        }
        const auto def = audioClipDefs.find(std::string(id));
        fa::audio::SoundDesc desc;
        if (def != audioClipDefs.end()) {
            desc.bus = def->second.bus;
            desc.volume = def->second.volume * volumeScale;
            desc.loop = def->second.loop;
        } else {
            desc.volume = 0.6f * volumeScale;
        }
        static_cast<void>(audioEngine->play(clip->second, desc));
    }

    /// \brief Opens the platform audio device and starts the scripted M08 voices.
    ///
    /// v1 has no decoder, so clips come from `data/audio.json` as tone specs. The
    /// built-in fallback is a 0.25 s 440 Hz tone followed by 0.1 s of silence and a
    /// looping 220 Hz tone. The one-shot is stopped after two rendered frames.
    fa::core::Status initializeAudio(fa::platform::IPlatform& platform) {
        fa::platform::IAudioDevice* device = platform.audio();
        if (device == nullptr) {
            return fa::core::errorStatus(fa::platform::platform_error::kAudioInitFailed,
                                         "the platform has no audio device");
        }

        audioEngine = std::make_unique<fa::audio::AudioEngine>();
        const fa::core::Status status = audioEngine->initialize(*device);
        if (status.isError()) {
            audioEngine.reset();
            return status;
        }

        audioEngine->setBusVolume(fa::audio::AudioBus::Master, 0.8f);
        audioEngine->setBusVolume(fa::audio::AudioBus::SFX, 0.7f);

        if (!loadAudioCatalog()) {
            fa::audio::AudioClip tone = fa::audio::makeTone(440.0f, 0.25f);
            const fa::audio::AudioClip silence = fa::audio::makeSilence(0.1f);
            tone.samples.insert(tone.samples.end(), silence.samples.begin(), silence.samples.end());
            toneClip = std::make_shared<fa::audio::AudioClip>(std::move(tone));
            loopClip = std::make_shared<fa::audio::AudioClip>(fa::audio::makeTone(220.0f, 0.5f));

            fa::audio::SoundDesc oneShot;
            oneShot.bus = fa::audio::AudioBus::SFX;
            oneShot.volume = 0.6f;
            toneHandle = audioEngine->play(toneClip, oneShot);

            fa::audio::SoundDesc loop;
            loop.bus = fa::audio::AudioBus::Music;
            loop.volume = 0.25f;
            loop.loop = true;
            loopHandle = audioEngine->play(loopClip, loop);
            return fa::core::okStatus();
        }

        // Music: the first looping Music clip in the catalog.
        for (const auto& [id, def] : audioClipDefs) {
            if (!def.loop || def.bus != fa::audio::AudioBus::Music) {
                continue;
            }
            fa::audio::SoundDesc music;
            music.bus = def.bus;
            music.volume = def.volume;
            music.loop = true;
            loopHandle = audioEngine->play(audioClips[id], music);
            break;
        }
        // Keep one shot so the M08 smoke still proves the one-shot path.
        if (const auto one = audioClips.find("sfx.pickup"); one != audioClips.end()) {
            fa::audio::SoundDesc oneShot;
            oneShot.bus = fa::audio::AudioBus::SFX;
            oneShot.volume = audioClipDefs["sfx.pickup"].volume;
            toneHandle = audioEngine->play(one->second, oneShot);
        }
        return fa::core::okStatus();
    }

#if defined(FA_ENABLE_DEBUG_TOOLS)
    /// \brief Creates the M27 console/overlay and registers app-level commands.
    ///
    /// Only constructed for `--debug` and the automation runs; a plain `--play`
    /// session never sees any of it. Stats/physics overlays start visible only
    /// when `--debug` was requested; the cursor marker (F6) starts hidden
    /// everywhere.
    ///
    /// The commands intentionally go through public APIs only: `teleport` writes a
    /// Transform, `give` uses the M18 backpack, `entities` uses the M27 inspector.
    void initializeDebug() {
        debugTools = std::make_unique<fa::debug::DebugTools>();
        debugTools->attachLogSink();
        if (debugMode) {
            // `--debug` opens with the overlays the play session needs: the stats panel
            // (which also draws each AI's target line) and the physics shapes. The
            // cursor marker (F6) and the log panel (F4) still start hidden.
            debugTools->setStatsVisible(true);
            debugTools->setPhysicsVisible(true);
        }

        static_cast<void>(debugTools->registerCommand(
            fa::debug::ConsoleCommand{"entities",
                                      "list up to 16 entities in the game scene",
                                      [this](fa::debug::DebugConsole& console, std::span<const std::string>) {
                                          if (gameScene == nullptr) {
                                              console.print("no scene");
                                              return;
                                          }
                                          const std::vector<fa::debug::EntityInspection> entities =
                                              fa::debug::inspectWorld(gameScene->world(), 16);
                                          console.print("entities: " + std::to_string(entities.size()));
                                          for (const fa::debug::EntityInspection& entity : entities) {
                                              console.print(fa::debug::formatInspection(entity));
                                          }
                                      }}));

        static_cast<void>(debugTools->registerCommand(
            fa::debug::ConsoleCommand{"teleport",
                                      "teleport the marker: teleport <x> <y>",
                                      [this](fa::debug::DebugConsole& console, std::span<const std::string> args) {
                                          if (args.size() != 2 || gameScene == nullptr) {
                                              console.print("usage: teleport <x> <y>");
                                              return;
                                          }
                                          const float x = static_cast<float>(std::atof(args[0].c_str()));
                                          const float y = static_cast<float>(std::atof(args[1].c_str()));
                                          gameScene->teleportMarker(fa::core::Vec2{x, y});
                                          console.print("marker moved");
                                      }}));

        static_cast<void>(debugTools->registerCommand(fa::debug::ConsoleCommand{
            "give",
            "give an item: give <item.id> <count>",
            [this](fa::debug::DebugConsole& console, std::span<const std::string> args) {
                if (args.size() != 2 || playerInventory == nullptr) {
                    console.print("usage: give <item.id> <count>");
                    return;
                }
                const int count = std::atoi(args[1].c_str());
                const fa::core::Result<int> added = playerInventory->backpack().add(
                    fa::inventory::ItemStack{fa::content::ContentId::fromName(args[0]), count, -1});
                if (added.isError()) {
                    console.print("give failed: " + added.error().message);
                    return;
                }
                console.print("remaining " + std::to_string(added.value()));
            }}));

        static_cast<void>(debugTools->registerCommand(
            fa::debug::ConsoleCommand{"save",
                                      "save the game: save [slot]",
                                      [this](fa::debug::DebugConsole& console, std::span<const std::string> args) {
                                          const std::string slot = args.empty() ? std::string("quicksave") : args[0];
                                          const fa::core::Status saved = saveToSlot(slot);
                                          if (saved.isError()) {
                                              console.print("save failed: " + saved.error().message);
                                              return;
                                          }
                                          console.print("saved " + slot);
                                      }}));

        static_cast<void>(debugTools->registerCommand(fa::debug::ConsoleCommand{
            "load",
            "load a slot: load [slot]",
            [this](fa::debug::DebugConsole& console, std::span<const std::string> args) {
                const std::string slot = args.empty() ? std::string("autosave") : args[0];
                const fa::core::Result<fa::save::SaveFile> loaded = loadFromSlot(slot);
                if (loaded.isError()) {
                    console.print("load failed: " + loaded.error().message);
                    return;
                }
                const fa::save::SaveFile& file = loaded.value();
                console.print("loaded " + file.meta.slotName + " v" + std::to_string(file.meta.saveVersion) + " day " +
                              std::to_string(file.meta.inGameDay));
                fa::core::Vec2 marker;
                if (gameScene != nullptr && file.world.getVec2("marker", marker)) {
                    gameScene->teleportMarker(marker);
                    console.print("marker restored");
                }
            }}));

        static_cast<void>(debugTools->registerCommand(fa::debug::ConsoleCommand{
            "slots", "list save slots", [this](fa::debug::DebugConsole& console, std::span<const std::string>) {
                if (saveService == nullptr) {
                    console.print("save service unavailable");
                    return;
                }
                for (const fa::save::SaveMetadata& metadata : saveService->listSlots()) {
                    console.print(metadata.slotName + (metadata.readable ? " readable" : " corrupt") + " day " +
                                  std::to_string(metadata.inGameDay));
                }
            }}));

        static_cast<void>(debugTools->registerCommand(fa::debug::ConsoleCommand{
            "delete",
            "delete a slot: delete <slot>",
            [this](fa::debug::DebugConsole& console, std::span<const std::string> args) {
                if (args.size() != 1 || saveService == nullptr) {
                    console.print("usage: delete <slot>");
                    return;
                }
                const fa::core::Status status = saveService->deleteSlot(args[0]);
                console.print(status.isOk() ? "deleted " + args[0] : "delete failed: " + status.error().message);
            }}));

        static_cast<void>(debugTools->registerCommand(
            fa::debug::ConsoleCommand{"time.addDays",
                                      "advance N game days: time.addDays <1..365>",
                                      [this](fa::debug::DebugConsole& console, std::span<const std::string> args) {
                                          if (args.size() != 1 || calendar == nullptr) {
                                              console.print("usage: time.addDays <1..365>");
                                              return;
                                          }
                                          const int days = std::atoi(args[0].c_str());
                                          if (days < 1 || days > 365) {
                                              console.print("days must be 1..365");
                                              return;
                                          }
                                          if (playMode && flow != nullptr) {
                                              for (int index = 0; index < days; ++index) {
                                                  const fa::core::Status status = flow->endDay();
                                                  if (status.isError()) {
                                                      console.print("time.addDays failed: " + status.error().message);
                                                      return;
                                                  }
                                              }
                                          } else {
                                              advanceDays(days);
                                          }
                                          console.print("advanced " + std::to_string(days) + " day(s)");
                                      }}));

        static_cast<void>(debugTools->registerCommand(fa::debug::ConsoleCommand{
            "time.set",
            "set the clock: time.set HH:MM",
            [this](fa::debug::DebugConsole& console, std::span<const std::string> args) {
                const std::size_t colon = args.size() == 1 ? args[0].find(':') : std::string::npos;
                if (colon == std::string::npos || calendar == nullptr) {
                    console.print("usage: time.set HH:MM");
                    return;
                }
                const int hour = std::atoi(args[0].substr(0, colon).c_str());
                const int minute = std::atoi(args[0].substr(colon + 1).c_str());
                if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
                    console.print("usage: time.set HH:MM");
                    return;
                }
                setClock(hour, minute);
                console.print("time set");
            }}));
    }

#endif

    /// \brief Maps the content string to the M17 tool enum.
    [[nodiscard]] static fa::interaction::ToolKind toolKindFor(const fa::content::ItemDef* def) noexcept {
        if (def == nullptr) {
            return fa::interaction::ToolKind::None;
        }
        const std::string& kind = def->toolKind;
        if (kind == "hoe") {
            return fa::interaction::ToolKind::Hoe;
        }
        if (kind == "watering_can") {
            return fa::interaction::ToolKind::WateringCan;
        }
        if (kind == "axe") {
            return fa::interaction::ToolKind::Axe;
        }
        if (kind == "pickaxe") {
            return fa::interaction::ToolKind::Pickaxe;
        }
        if (kind == "sword") {
            return fa::interaction::ToolKind::Sword;
        }
        if (kind == "seed") {
            return fa::interaction::ToolKind::Seed;
        }
        if (kind == "harvest") {
            return fa::interaction::ToolKind::Harvest;
        }
        if (kind == "placeable") {
            return fa::interaction::ToolKind::Placeable;
        }
        return fa::interaction::ToolKind::None;
    }

    void setPlayMessage(std::string message) {
        playMessage = std::move(message);
        playMessageTimer = 2.5f;
    }

    void setShopMessage(std::string message) {
        shopMessage = std::move(message);
        shopMessageTimer = 2.5f;
    }

    [[nodiscard]] static fa::content::ContentId shopGeneralId() {
        return fa::content::ContentId::fromName("shop.general");
    }

    /// \brief Unit buy price for an item listed in the general store; 0 when absent.
    [[nodiscard]] int shopBuyPrice(const fa::content::ContentId& item) const {
        if (economy == nullptr) {
            return 0;
        }
        const fa::economy::ShopDef* shop = shops.shop(shopGeneralId());
        if (shop == nullptr) {
            return 0;
        }
        for (const fa::economy::ShopEntry& entry : shop->stock) {
            if (entry.item == item) {
                return economy->buyPrice(*shop, entry, 1);
            }
        }
        return 0;
    }

    /// \brief Unit sell price for an item in the general store.
    [[nodiscard]] int shopSellPrice(const fa::content::ContentId& item) const {
        if (economy == nullptr) {
            return 0;
        }
        const fa::economy::ShopDef* shop = shops.shop(shopGeneralId());
        return shop != nullptr ? economy->sellPrice(*shop, item) : 0;
    }

    [[nodiscard]] fa::core::Status buyFromShop(const fa::content::ContentId& item, int count) {
        if (economy == nullptr || playerInventory == nullptr) {
            return fa::core::errorStatus(fa::economy::economy_error::kNotLoaded, "the shop is not available");
        }
        const fa::core::Status status = economy->buy(shopGeneralId(), item, count, playerInventory->backpack());
        if (status.isOk()) {
            playSfx("sfx.buy");
        }
        return status;
    }

    /// \brief Sells from the hotbar first, then the backpack.
    [[nodiscard]] fa::core::Status sellToShop(const fa::content::ContentId& item, int count) {
        if (economy == nullptr || playerInventory == nullptr) {
            return fa::core::errorStatus(fa::economy::economy_error::kNotLoaded, "the shop is not available");
        }
        const fa::core::Status hotbar =
            economy->sell(shopGeneralId(), item, count, playerInventory->hotbar().inventory());
        if (hotbar.isOk()) {
            return hotbar;
        }
        return economy->sell(shopGeneralId(), item, count, playerInventory->backpack());
    }

    /// \brief Queues one turnip soup; consumes 2 turnips from the backpack.
    [[nodiscard]] fa::core::Status craftTurnipSoup() {
        if (economy == nullptr || playerInventory == nullptr) {
            return fa::core::errorStatus(fa::economy::economy_error::kNotLoaded, "the kitchen is not available");
        }
        // Harvests land in the hotbar; cooking reads the backpack. Pull up to two
        // turnips across so the normal harvest -> cook flow works without micro-
        // managing containers.
        const fa::content::ContentId turnip = fa::content::ContentId::fromName("item.turnip");
        fa::inventory::Inventory& bag = playerInventory->backpack();
        fa::inventory::Inventory& hotbar = playerInventory->hotbar().inventory();
        constexpr int kRequired = 2;
        for (int slot = 0; slot < hotbar.slotCount() && bag.countOf(turnip) < kRequired; ++slot) {
            if (hotbar.at(slot).id == turnip) {
                static_cast<void>(hotbar.transferTo(bag, slot, kRequired - bag.countOf(turnip)));
            }
        }
        return economy->startCraft(fa::content::ContentId::fromName("recipe.turnip_soup"), "kitchen", bag);
    }

    /// \brief Collects the first ready processing job into the backpack.
    [[nodiscard]] fa::core::Status collectReadyCraft() {
        if (economy == nullptr || playerInventory == nullptr) {
            return fa::core::errorStatus(fa::economy::economy_error::kNotLoaded, "the kitchen is not available");
        }
        const std::span<const fa::economy::ProcessingJob> jobs = economy->jobs();
        for (std::size_t index = 0; index < jobs.size(); ++index) {
            if (jobs[index].ready) {
                return economy->collectCraft(index, playerInventory->backpack());
            }
        }
        return fa::core::errorStatus(fa::economy::economy_error::kJobNotReady, "no processing job is ready");
    }

    /// \brief Ships every crop stack in the hotbar and backpack.
    [[nodiscard]] int shipAllCrops() {
        if (economy == nullptr || playerInventory == nullptr) {
            return 0;
        }
        int shipped = 0;
        const auto shipFrom = [this, &shipped](fa::inventory::Inventory& inventory) {
            // Backwards with a value copy: ship() removes slots, so a live reference
            // or a forward index would skip or dangle.
            for (int slot = inventory.slotCount() - 1; slot >= 0; --slot) {
                const fa::inventory::ItemStack itemStack = inventory.at(slot);
                if (itemStack.empty()) {
                    continue;
                }
                const fa::content::ItemDef* def = content.item(itemStack.id);
                if (def == nullptr || def->category != "crop") {
                    continue;
                }
                if (economy->ship(itemStack.id, itemStack.count, inventory).isOk()) {
                    shipped += itemStack.count;
                }
            }
        };
        shipFrom(playerInventory->hotbar().inventory());
        shipFrom(playerInventory->backpack());
        return shipped;
    }

    /// \brief Uses the selected hotbar item on the cell in front of the player.
    [[nodiscard]] fa::core::Status handleUseTool() {
        if (!playMode || gameScene == nullptr || playerInventory == nullptr) {
            return fa::core::errorStatus(fa::core::error_code::kUnsupported, "play mode is not active");
        }
        const fa::inventory::ItemStack& selected = playerInventory->hotbar().selectedStack();
        if (selected.empty()) {
            setPlayMessage("当前栏位没有工具");
            return fa::core::errorStatus(fa::inventory::inventory_error::kSlotEmpty, "the selected slot is empty");
        }
        const fa::content::ItemDef* def = content.item(selected.id);
        // The boat is not an LMB tool any more: point at the water and press G.
        if (def != nullptr && def->toolKind == "boat") {
            setPlayMessage(gameScene->boatMode() ? "靠近岸边按 G/左键跳岸下船" : "面向水域按 G/左键上船");
            return fa::core::okStatus();
        }
        fa::interaction::ToolKind tool = toolKindFor(def);
        if (tool == fa::interaction::ToolKind::None) {
            setPlayMessage("不是工具：" + std::string(selected.id.name()));
            return fa::core::errorStatus(fa::core::error_code::kUnsupported, "the selected item is not a tool");
        }

        const fa::interaction::GridTarget target = gameScene->frontTarget();
        if (tool == fa::interaction::ToolKind::Seed) {
            gameScene->setSelectedSeed(selected.id);
        }
        fa::content::ContentId harvestItem;
        if (target.valid) {
            const fa::farming::CropState* crop = gameScene->cropAt(target.cell);
            if (crop != nullptr) {
                harvestItem = crop->harvestItem;
                if (crop->harvestable) {
                    tool = fa::interaction::ToolKind::Harvest;
                }
            }
        }
        const auto countHarvest = [this, &harvestItem]() {
            return playerInventory->hotbar().inventory().countOf(harvestItem) +
                   playerInventory->backpack().countOf(harvestItem);
        };
        const int harvestBefore = harvestItem.isValid() ? countHarvest() : 0;

        fa::interaction::InteractionRequest request;
        request.actor = gameScene->playerEntity();
        request.target = target;
        request.tool = tool;
        const fa::core::Status status = gameScene->interact(request);
        if (status.isOk() && harvestItem.isValid()) {
            const int harvestAfter = countHarvest();
            if (harvestAfter > harvestBefore) {
                fa::narrative::QuestEvent event;
                event.kind = fa::narrative::QuestObjectiveKind::Collect;
                event.target = harvestItem;
                event.amount = harvestAfter - harvestBefore;
                gameScene->narrative().reportEvent(event);
            }
        }
        const std::string_view toolName = toolLabel(tool);
        if (status.isOk()) {
            switch (tool) {
                case fa::interaction::ToolKind::Hoe:
                    playSfx("sfx.till");
                    break;
                case fa::interaction::ToolKind::WateringCan:
                    playSfx("sfx.water");
                    break;
                case fa::interaction::ToolKind::Seed:
                    playSfx("sfx.plant");
                    break;
                case fa::interaction::ToolKind::Harvest:
                    playSfx("sfx.harvest");
                    break;
                default:
                    break;
            }
            setPlayMessage(std::string(toolName) + " 完成");
        } else {
            setPlayMessage(std::string(toolName) + "失败：" + status.error().message);
        }
        return status;
    }

    /// \brief Damage of the equipped weapon, or an unarmed fallback.
    [[nodiscard]] int weaponDamage() const {
        constexpr int kUnarmedDamage = 5;
        if (playerInventory == nullptr) {
            return kUnarmedDamage;
        }
        const fa::inventory::ItemStack& weapon = playerInventory->equipment().at(fa::inventory::EquipmentSlot::Weapon);
        if (weapon.empty()) {
            return kUnarmedDamage;
        }
        const fa::content::ItemDef* def = content.item(weapon.id);
        return def != nullptr && def->damage > 0 ? def->damage : kUnarmedDamage;
    }

    /// \brief Q: the selected hotbar slot must hold a weapon; its `damage` scales the qi.
    void handleWeaponSkill() {
        if (gameScene == nullptr || playerInventory == nullptr) {
            return;
        }
        const fa::inventory::ItemStack& selected = playerInventory->hotbar().selectedStack();
        const fa::content::ItemDef* def = selected.empty() ? nullptr : content.item(selected.id);
        if (def == nullptr || (def->category != "weapon" && def->damage <= 0)) {
            setPlayMessage("需要手持武器才能释放剑气");
            return;
        }
        const std::string error = gameScene->requestWeaponSkill(std::max(1, def->damage));
        if (!error.empty()) {
            setPlayMessage(error);
        }
    }

    /// \brief Spawns one melee attack in front of the player.
    ///
    /// Right-click only swings while the selected hotbar slot holds a weapon; the
    /// swing uses that weapon's `damage`, so the sword must literally be in hand.
    void handleAttack() {
        if (!playMode || gameScene == nullptr) {
            return;
        }
        const fa::inventory::ItemStack& selected =
            playerInventory != nullptr ? playerInventory->hotbar().selectedStack() : fa::inventory::ItemStack{};
        const fa::content::ItemDef* def = selected.empty() ? nullptr : content.item(selected.id);
        if (def == nullptr || (def->category != "weapon" && def->damage <= 0)) {
            setPlayMessage("需要手持武器才能攻击");
            return;
        }
        const int damage = std::max(1, def->damage);
        const bool started = gameScene->startAttack(damage, 14.0f);
        setPlayMessage(started ? "攻击 " + std::to_string(damage)
                               : (gameScene->attackAnimationActive() ? "攻击收势中" : "无法攻击"));
    }

    /// \brief Consumes the selected hotbar item; only food has an effect.
    [[nodiscard]] fa::core::Status useSelectedFood() {
        if (!playMode || gameScene == nullptr || playerInventory == nullptr || itemUse == nullptr) {
            return fa::core::errorStatus(fa::core::error_code::kUnsupported, "play mode is not active");
        }
        const int slot = playerInventory->hotbar().selected();
        fa::inventory::Inventory& hotbar = playerInventory->hotbar().inventory();
        const fa::inventory::ItemStack& selected = hotbar.at(slot);
        if (selected.empty()) {
            setPlayMessage("未选中物品");
            return fa::core::errorStatus(fa::inventory::inventory_error::kSlotEmpty, "the selected slot is empty");
        }
        const fa::content::ItemDef* def = content.item(selected.id);
        if (def == nullptr || def->category != "food") {
            setPlayMessage("不是食物：" + std::string(selected.id.name()));
            return fa::core::errorStatus(fa::core::error_code::kUnsupported, "the selected item is not food");
        }
        const int heal = def->healAmount;
        const int stamina = def->staminaRestore;
        const fa::core::Status status = itemUse->use(hotbar, slot, gameScene->playerEntity());
        if (status.isError()) {
            setPlayMessage("使用失败：" + status.error().message);
            return status;
        }
        if (heal > 0) {
            gameScene->setPlayerHealth(gameScene->playerHealth() + static_cast<float>(heal));
        }
        if (stamina > 0) {
            gameScene->setPlayerStamina(gameScene->playerStamina() + static_cast<float>(stamina));
        }
        std::string message = "吃下 " + def->displayName;
        if (heal > 0) {
            message += " +" + std::to_string(heal) + "生命";
        }
        if (stamina > 0) {
            message += " +" + std::to_string(stamina) + "体力";
        }
        setPlayMessage(std::move(message));
        return status;
    }

    /// \brief Creates a visible drop entity at a world position.
    void spawnLoot(fa::core::Vec2 position, const fa::content::ContentId& item, int count) {
        if (gameScene == nullptr) {
            return;
        }
        fa::scene::World& world = gameScene->world();
        const fa::scene::EntityId id = world.create();
        fa::scene::Transform transform;
        transform.position = position;
        static_cast<void>(world.add<fa::scene::Transform>(id, transform));

        fa::scene::Sprite sprite;
        sprite.texture = uiWhiteTexture;
        sprite.size = fa::core::Vec2{6.0f, 6.0f};
        sprite.layer = fa::render::RenderLayer::Objects;
        sprite.tint = fa::core::Color{1.0f, 0.85f, 0.25f, 1.0f};
        static_cast<void>(world.add<fa::scene::Sprite>(id, sprite));
        static_cast<void>(world.add<LootDrop>(id, LootDrop{item, count}));
    }

    void spawnDamageText(fa::scene::EntityId target, int amount, bool onPlayer) {
        if (gameScene == nullptr || amount <= 0) {
            return;
        }
        FloatingDamage text;
        if (const fa::scene::Transform* transform = gameScene->world().tryGet<fa::scene::Transform>(target);
            transform != nullptr) {
            text.position = fa::core::Vec2{transform->position.x, transform->position.y - 4.0f};
        } else {
            text.position = gameScene->markerPosition();
        }
        text.amount = amount;
        text.remaining = kDamageTextSeconds;
        text.onPlayer = onPlayer;
        if (damageTexts.size() >= kMaxDamageTexts) {
            damageTexts.erase(damageTexts.begin());
        }
        damageTexts.push_back(text);
    }

    void applyHitFlash(fa::scene::EntityId target) {
        if (gameScene == nullptr || !gameScene->isAlive(target)) {
            return;
        }
        fa::scene::World& world = gameScene->world();
        fa::scene::Sprite* sprite = world.tryGet<fa::scene::Sprite>(target);
        if (sprite == nullptr) {
            return;
        }
        if (HitFlash* flash = world.tryGet<HitFlash>(target); flash != nullptr) {
            flash->remaining = kHitFlashSeconds;
        } else {
            HitFlash created;
            created.baseTint = sprite->tint;
            created.remaining = kHitFlashSeconds;
            static_cast<void>(world.add<HitFlash>(target, created));
        }
        sprite->tint = fa::core::Color{1.0f, 0.45f, 0.35f, 1.0f};
    }

    void updateCombatFeedback(float deltaSeconds) {
        if (gameScene != nullptr) {
            gameScene->world().each<HitFlash, fa::scene::Sprite>(
                [deltaSeconds](fa::scene::EntityId, HitFlash& flash, fa::scene::Sprite& sprite) {
                    if (flash.remaining <= 0.0f) {
                        return;
                    }
                    flash.remaining -= deltaSeconds;
                    if (flash.remaining <= 0.0f) {
                        sprite.tint = flash.baseTint;
                    }
                });
        }
        for (FloatingDamage& text : damageTexts) {
            text.remaining -= deltaSeconds;
            text.position.y -= 22.0f * deltaSeconds;
        }
        damageTexts.erase(std::remove_if(damageTexts.begin(),
                                         damageTexts.end(),
                                         [](const FloatingDamage& text) { return text.remaining <= 0.0f; }),
                          damageTexts.end());
    }

    void clearCombatFeedback() { damageTexts.clear(); }

    /// \brief Rolls death loot, destroys dead enemies and reports hits.
    void processCombatEvents() {
        if (!playMode || gameScene == nullptr) {
            return;
        }
        for (const fa::combat::DamageEvent& damage : gameScene->damageEvents()) {
            if (damage.amount <= 0) {
                continue;
            }
            const bool onPlayer = damage.target == gameScene->playerEntity();
            playSfx(onPlayer ? "sfx.hurt" : "sfx.hit");
            setPlayMessage((onPlayer ? "受伤 " : "命中 ") + std::to_string(damage.amount));
            spawnDamageText(damage.target, damage.amount, onPlayer);
            applyHitFlash(damage.target);
        }

        for (const fa::combat::DeathEvent& death : gameScene->deathEvents()) {
            // M20 reports a death for every health-zero entity. The player must enter
            // the GameOver flow instead of being destroyed like an enemy.
            if (death.entity == gameScene->playerEntity()) {
                handlePassOut("combat");
                continue;
            }
            fa::core::Vec2 position{};
            if (const fa::scene::Transform* transform = gameScene->world().tryGet<fa::scene::Transform>(death.entity);
                transform != nullptr) {
                position = transform->position;
            }
            if (const fa::content::LootTableDef* table = content.lootTable(death.lootTable); table != nullptr) {
                for (const fa::content::LootEntry& entry : table->entries) {
                    if (!m_lootRng.chance(entry.chance)) {
                        continue;
                    }
                    const int count = m_lootRng.range(entry.minAmount, entry.maxAmount + 1);
                    if (count > 0) {
                        spawnLoot(position, entry.item, count);
                    }
                }
            }
            if (gameScene->isAlive(death.entity)) {
                gameScene->world().destroy(death.entity);
            }
            setPlayMessage("击倒敌人");
        }
        gameScene->clearCombatEvents();
    }

    /// \brief Picks up drops within 14 px of the player's centre.
    void collectLoot() {
        if (!playMode || gameScene == nullptr || playerInventory == nullptr) {
            return;
        }
        fa::inventory::Inventory& bag = playerInventory->backpack();
        const fa::core::Vec2 player = gameScene->markerCenter();
        std::vector<fa::scene::EntityId> collected;
        gameScene->world().each<LootDrop, fa::scene::Transform>(
            [&](fa::scene::EntityId id, LootDrop& drop, const fa::scene::Transform& transform) {
                const fa::core::Vec2 center{transform.position.x + 3.0f, transform.position.y + 3.0f};
                const float dx = center.x - player.x;
                const float dy = center.y - player.y;
                if (dx * dx + dy * dy > 14.0f * 14.0f) {
                    return;
                }
                const fa::inventory::ItemStack stack{drop.item, drop.count, -1};
                if (bag.spaceFor(stack) < drop.count) {
                    setPlayMessage("背包已满");
                    return;
                }
                const fa::core::Result<int> added = bag.add(stack);
                if (added.isError() || added.value() != 0) {
                    setPlayMessage("背包已满");
                    return;
                }
                collected.push_back(id);
                fa::narrative::QuestEvent questEvent;
                questEvent.kind = fa::narrative::QuestObjectiveKind::Collect;
                questEvent.target = drop.item;
                questEvent.amount = drop.count;
                gameScene->narrative().reportEvent(questEvent);
                const fa::content::ItemDef* def = content.item(drop.item);
                setPlayMessage("拾取 " + (def != nullptr ? def->displayName : std::string(drop.item.name())) + " x" +
                               std::to_string(drop.count));
            });
        for (const fa::scene::EntityId id : collected) {
            gameScene->world().destroy(id);
        }
        if (!collected.empty()) {
            playSfx("sfx.pickup");
        }
    }

    void setInventoryMessage(std::string message) {
        inventoryMessage = std::move(message);
        inventoryMessageTimer = 2.5f;
    }

    /// \brief Opens the backpack panel and pauses the clock through Dialogue.
    void openInventoryPanel() {
        if (!playMode || flow == nullptr || flow->state() != fa::flow::AppState::Playing || inventoryOpen ||
            questLogOpen) {
            return;
        }
        if (flow->enterDialogue().isOk()) {
            inventoryOpen = true;
            inventoryMessage.clear();
            inventoryMessageTimer = 0.0f;
            inventoryPage = 0;
        }
    }

    void closeInventoryPanel() {
        inventoryOpen = false;
        if (flow != nullptr && flow->state() == fa::flow::AppState::Dialogue) {
            static_cast<void>(flow->exitDialogue());
        }
    }

    /// \brief Opens the quest log and pauses the clock through Dialogue.
    void openQuestLog() {
        if (!playMode || flow == nullptr || shopOpen || inventoryOpen || questLogOpen ||
            flow->state() != fa::flow::AppState::Playing) {
            return;
        }
        if (flow->enterDialogue().isOk()) {
            questLogOpen = true;
        }
    }

    void closeQuestLog() {
        questLogOpen = false;
        if (flow != nullptr && flow->state() == fa::flow::AppState::Dialogue) {
            static_cast<void>(flow->exitDialogue());
        }
    }

    /// \brief Leaves `GameFlow`'s Dialogue state when the narrative graph ends.
    void syncDialogueState() {
        if (flow != nullptr && gameScene != nullptr && flow->state() == fa::flow::AppState::Dialogue &&
            !gameScene->narrative().dialogueActive()) {
            static_cast<void>(flow->exitDialogue());
        }
    }

    /// \brief Starts the elder's dialogue and pauses gameplay through M25.
    [[nodiscard]] bool openNpcDialogue() {
        if (!playMode || gameScene == nullptr || flow == nullptr || flow->state() != fa::flow::AppState::Playing) {
            return false;
        }
        if (flow->enterDialogue().isError()) {
            return false;
        }
        const fa::core::Status started = gameScene->startNpcDialogue();
        if (started.isError()) {
            static_cast<void>(flow->exitDialogue());
            setPlayMessage("交谈：" + started.error().message);
            return false;
        }
        setPlayMessage("村长：按 1/2 选择  回车继续");
        return true;
    }

    void chooseDialogue(std::size_t index) {
        if (gameScene == nullptr) {
            return;
        }
        const fa::core::Status status = gameScene->narrative().choose(index);
        if (status.isError()) {
            setPlayMessage("对话：" + status.error().message);
        }
        syncDialogueState();
        processNarrativeEvents();
    }

    void advanceDialogue() {
        if (gameScene == nullptr) {
            return;
        }
        const fa::core::Status status = gameScene->narrative().advance();
        if (status.isError()) {
            setPlayMessage("对话：" + status.error().message);
        }
        syncDialogueState();
        processNarrativeEvents();
    }

    void cancelDialogue() {
        if (gameScene == nullptr) {
            return;
        }
        static_cast<void>(gameScene->narrative().endDialogue());
        syncDialogueState();
    }

    /// \brief Applies quest events produced by dialogue, harvest and loot.
    void processNarrativeEvents() {
        if (!playMode || gameScene == nullptr) {
            return;
        }
        for (const fa::narrative::NarrativeEvent& event : gameScene->narrative().events()) {
            switch (event.kind) {
                case fa::narrative::NarrativeEventKind::QuestStarted:
                    setPlayMessage("任务开始：" + event.text);
                    break;
                case fa::narrative::NarrativeEventKind::QuestCompleted: {
                    if (event.amount > 0 && economy != nullptr) {
                        economy->addGold(event.amount);
                    }
                    std::string message = "任务完成：" + event.text;
                    if (event.amount > 0) {
                        message += " +" + std::to_string(event.amount) + "G";
                    }
                    setPlayMessage(std::move(message));
                    break;
                }
                default:
                    break;
            }
        }
        gameScene->narrative().clearEvents();
    }

    [[nodiscard]] bool isHazardCell(fa::tilemap::TileCoord cell) const {
        if (gameScene == nullptr || !gameScene->isForest()) {
            return false;
        }
        if (!hazardCells.empty()) {
            return std::any_of(hazardCells.begin(), hazardCells.end(), [cell](fa::tilemap::TileCoord value) {
                return value.x == cell.x && value.y == cell.y;
            });
        }
        return cell.y == 14 && cell.x >= 8 && cell.x <= 10;
    }

    /// \brief Applies one thorn tick when the marker stands on brambles.
    [[nodiscard]] bool tickHazard() {
        if (!playMode || gameScene == nullptr || flow == nullptr || flow->state() != fa::flow::AppState::Playing ||
            hazardCooldown > 0.0f) {
            return false;
        }
        if (!isHazardCell(gameScene->markerCell())) {
            return false;
        }
        const float healthBefore = gameScene->playerHealth();
        gameScene->damagePlayer(config.gameplay.hazardDamage);
        const int applied = static_cast<int>(healthBefore - gameScene->playerHealth());
        if (applied > 0) {
            spawnDamageText(gameScene->playerEntity(), applied, true);
            applyHitFlash(gameScene->playerEntity());
        }
        hazardCooldown = config.gameplay.hazardInterval;
        if (gameScene->playerHealth() <= 0.0f) {
            handlePassOut();
            return true;
        }
        setPlayMessage("荆棘 -" + std::to_string(config.gameplay.hazardDamage));
        return true;
    }

    /// \brief Applies the death penalty once and opens the GameOver panel.
    ///
    /// The player is not moved here: the panel offers respawn, loading the autosave
    /// or returning to the main menu. Keeping the penalty in this one function means
    /// pressing Respawn cannot charge it twice.
    void handlePassOut(std::string_view reason = "thorns") {
        if (flow == nullptr || gameScene == nullptr || flow->state() != fa::flow::AppState::Playing) {
            return;
        }
        int penalty = 0;
        if (economy != nullptr) {
            penalty = static_cast<int>(static_cast<float>(economy->gold()) * config.gameplay.deathGoldPenalty);
            if (penalty > 0) {
                economy->addGold(-penalty);
            }
        }
        lastDeathPenalty = penalty;
        playSfx("sfx.hurt");
        lastDeathReason = reason == "thorns" ? "荆棘" : "战斗";
        const fa::core::Status over = flow->requestGameOver(reason);
        if (over.isError()) {
            // The state already changed or the session is gone; do not leave a
            // penalty applied without a visible panel.
            if (penalty > 0 && economy != nullptr) {
                economy->addGold(penalty);
            }
            lastDeathPenalty = 0;
            return;
        }
        clearCombatFeedback();
    }

    /// \brief Enters the death flow if combat left the player at zero health.
    void checkPlayerDeath() {
        if (!playMode || gameScene == nullptr || flow == nullptr || flow->state() != fa::flow::AppState::Playing) {
            return;
        }
        if (gameScene->playerHealth() <= 0.0f) {
            handlePassOut("combat");
        }
    }

    /// \brief Respawns beside the bed from the GameOver panel.
    [[nodiscard]] bool respawnFromDeath() {
        if (flow == nullptr || gameScene == nullptr || flow->state() != fa::flow::AppState::GameOver) {
            return false;
        }
        if (gameScene->isForest()) {
            // The bed lives on the farm map, so respawning also changes maps.
            if (!switchMap(GameMap::Farm, std::nullopt)) {
                return false;
            }
        }
        gameScene->teleportMarker(gameScene->respawnPoint());
        gameScene->setPlayerHealth(gameScene->playerMaxHealth());
        gameScene->setPlayerStamina(gameScene->playerMaxStamina());
        gameScene->syncPhysics();
        if (flow->respawn().isError()) {
            return false;
        }
        setPlayMessage("已复活");
        return true;
    }

    /// \brief Loads the autosave from the GameOver panel.
    [[nodiscard]] bool loadFromDeath() {
        if (flow == nullptr || flow->state() != fa::flow::AppState::GameOver) {
            return false;
        }
        if (flow->loadGame(fa::flow::kAutosaveSlot).isError()) {
            // GameFlow moves to the main menu on a failed load.
            return false;
        }
        setPlayMessage("已读取自动存档");
        return true;
    }

    /// \brief Returns to the main menu from the GameOver panel.
    [[nodiscard]] bool mainMenuFromDeath() {
        if (flow == nullptr || flow->state() != fa::flow::AppState::GameOver) {
            return false;
        }
        if (flow->enterMainMenu().isError()) {
            return false;
        }
        shopOpen = false;
        inventoryOpen = false;
        questLogOpen = false;
        slotsOpen = false;
        return true;
    }

    void clearLootDrops() {
        if (gameScene == nullptr) {
            return;
        }
        std::vector<fa::scene::EntityId> drops;
        gameScene->world().each<LootDrop, fa::scene::Transform>(
            [&drops](fa::scene::EntityId id, LootDrop&, const fa::scene::Transform&) { drops.push_back(id); });
        for (const fa::scene::EntityId id : drops) {
            gameScene->world().destroy(id);
        }
    }

    [[nodiscard]] int countLoot(const fa::content::ContentId& item, int count) const {
        int total = 0;
        if (gameScene == nullptr) {
            return total;
        }
        gameScene->world().each<LootDrop, fa::scene::Transform>(
            [&total, &item, count](fa::scene::EntityId, const LootDrop& drop, const fa::scene::Transform&) {
                if (drop.item == item && drop.count == count) {
                    total += drop.count;
                }
            });
        return total;
    }

    /// \brief Teleports through a portal under the marker, with a short cooldown.
    [[nodiscard]] bool checkPortal() {
        if (!playMode || gameScene == nullptr || portalCooldown > 0.0f) {
            return false;
        }
        const fa::tilemap::MapPortal* portal = gameScene->portalAtMarker();
        if (portal == nullptr) {
            return false;
        }
        const GameMap target = portal->targetMap == "forest" ? GameMap::Forest : GameMap::Farm;
        const fa::core::Vec2 spawn = gameScene->cellPosition(portal->targetTile);
        if (target != gameScene->mapKind()) {
            // A separate map means a real scene replacement; the shared session
            // state keeps farmland, quests and inventory alive across it.
            if (!switchMap(target, spawn)) {
                return false;
            }
        } else {
            gameScene->teleportMarker(spawn);
            gameScene->syncPhysics();
        }
        portalCooldown = 0.6f;
        setPlayMessage(target == GameMap::Forest ? "进入林地" : "进入农场");
        return true;
    }

    /// \brief Sleeps until the next wake hour and applies the recovery rule.
    ///
    /// A sleep always ends at `flow::kWakeHour`; going to bed later than
    /// `kWakeHour + 24 - kFullSleepHours` (22:00) restores less than full
    /// stamina, down to the 1% floor for a very short nap.
    [[nodiscard]] fa::core::Status sleepAtBed() {
        if (flow == nullptr || gameScene == nullptr) {
            return fa::core::errorStatus(fa::flow::flow_error::kNotInitialized, "sleep needs an active session");
        }
        const float hours = calendar != nullptr
                                ? fa::flow::sleepHoursUntil(calendar->timeOfDay(), config.gameplay.wakeHour)
                                : config.gameplay.fullSleepHours;
        const float recovery = fa::flow::sleepRecovery(hours, config.gameplay.fullSleepHours);
        const fa::core::Status slept = flow->endDay();
        if (slept.isError()) {
            return slept;
        }
        gameScene->recoverStamina(recovery);
        syncEconomyCalendar();
        char message[64]{};
        std::snprintf(message,
                      sizeof(message),
                      "睡了 %.1f 小时  体力 +%d%%",
                      static_cast<double>(hours),
                      static_cast<int>(recovery * 100.0f + 0.5f));
        setPlayMessage(message);
        return slept;
    }

    /// \brief LMB and G share this entry (they are equivalent): a weapon in hand
    /// swings melee, otherwise the interact chain runs.
    void handleUniversalUse() {
        if (!playMode || gameScene == nullptr || playerInventory == nullptr) {
            return;
        }
        const fa::inventory::ItemStack& selected = gameScene != nullptr && playerInventory != nullptr
                                                       ? playerInventory->hotbar().selectedStack()
                                                       : fa::inventory::ItemStack{};
        const fa::content::ItemDef* def = selected.empty() ? nullptr : content.item(selected.id);
        if (def != nullptr && (def->category == "weapon" || def->damage > 0)) {
            handleAttack();
            return;
        }
        handleUseOrInteract();
    }

    /// \brief The shared interact chain: proximity targets, then boat/food/tool use
    /// driven by the selected hotbar slot.
    void handleUseOrInteract() {
        if (gameScene == nullptr) {
            return;
        }
        if (gameScene->isNearNpc()) {
            static_cast<void>(openNpcDialogue());
        } else if (gameScene->isNearBed()) {
            const fa::core::Status slept = sleepAtBed();
            if (slept.isError()) {
                setPlayMessage("睡觉失败：" + slept.error().message);
            }
        } else if (gameScene->isNearShippingBox()) {
            const int shipped = shipAllCrops();
            setPlayMessage(shipped > 0 ? "已出货 " + std::to_string(shipped) + " 作物" : "没有可出货的东西");
        } else if (gameScene->isNearMerchant() && flow != nullptr && flow->state() == fa::flow::AppState::Playing) {
            if (flow->enterDialogue().isOk()) {
                shopOpen = true;
                shopMessage.clear();
                shopMessageTimer = 0.0f;
            }
        } else if (playerInventory != nullptr && playerInventory->hotbar().selectedStack().id.name() == "item.boat") {
            // Boat selected: disembark on land, or board when facing water.
            if (gameScene->boatMode()) {
                setPlayMessage(gameScene->disembarkBoatManually());
            } else if (gameScene->facingWater()) {
                setPlayMessage(gameScene->boardBoat());
            } else {
                setPlayMessage("面向水域按 G/左键才能上船");
            }
        } else if (playerInventory != nullptr &&
                   content.item(playerInventory->hotbar().selectedStack().id) != nullptr &&
                   content.item(playerInventory->hotbar().selectedStack().id)->category == "food") {
            // Food selected: eat it.
            static_cast<void>(useSelectedFood());
        } else if (playerInventory != nullptr &&
                   toolKindFor(content.item(playerInventory->hotbar().selectedStack().id)) !=
                       fa::interaction::ToolKind::None) {
            // Farming tool selected: till/water/plant/harvest in front of the player.
            static_cast<void>(handleUseTool());
        } else {
            setPlayMessage("交互：村长 / 床 / 出货箱 / 商人");
        }
    }

    /// \brief Consumes the controller's per-step action records.
    void processPlayerActions() {
        if (!playMode || gameScene == nullptr) {
            return;
        }
        for (const fa::player::PlayerActionRequest& action : gameScene->takePlayerActions()) {
            switch (action.kind) {
                case fa::player::PlayerActionKind::UseTool:
                    // LMB: weapon swings melee, otherwise the interact chain runs.
                    handleUniversalUse();
                    break;
                case fa::player::PlayerActionKind::Attack:
                    // RMB: the weapon skill (sword qi); requires a weapon in hand.
                    handleWeaponSkill();
                    break;
                case fa::player::PlayerActionKind::Interact:
                    // G: equivalent to LMB.
                    handleUniversalUse();
                    break;
                case fa::player::PlayerActionKind::OpenInventory:
                    openInventoryPanel();
                    break;
                case fa::player::PlayerActionKind::ActionRejected:
                    if (action.errorCode == fa::player::player_error::kInsufficientStamina) {
                        setPlayMessage("体力不足");
                    } else if (action.errorCode == fa::player::player_error::kCooldown) {
                        setPlayMessage("闪避冷却中");
                    } else {
                        setPlayMessage("动作锁定中");
                    }
                    break;
                default:
                    break;
            }
        }
    }

    [[nodiscard]] static fa::content::Season contentSeason(fa::world::Season season) noexcept {
        switch (season) {
            case fa::world::Season::Spring:
                return fa::content::Season::Spring;
            case fa::world::Season::Summer:
                return fa::content::Season::Summer;
            case fa::world::Season::Fall:
                return fa::content::Season::Fall;
            case fa::world::Season::Winter:
                return fa::content::Season::Winter;
            case fa::world::Season::Count:
                break;
        }
        return fa::content::Season::Spring;
    }

    /// \brief Keeps shop seasons and daily prices in step with the calendar.
    void syncEconomyCalendar() {
        if (economy == nullptr || calendar == nullptr) {
            return;
        }
        economy->setSeason(contentSeason(calendar->season()));
        economy->setDay(calendar->date().day);
    }

    /// \brief Advances N game days and runs the M19/M24/M23 daily steps.
    void advanceDays(int days) {
        if (calendar == nullptr || days <= 0) {
            return;
        }
        for (int index = 0; index < days; ++index) {
            calendar->skipToNextDay();
            fa::farming::FarmingDayContext context;
            context.season = contentSeason(calendar->season());
            context.rain = calendar->isRaining();
            context.growthMultiplier = calendar->cropGrowthMultiplier();
            if (gameScene != nullptr) {
                gameScene->advanceFarmingDay(context);
                gameScene->narrative().onDayChanged();
            }
            if (economy != nullptr) {
                static_cast<void>(economy->onDayChanged());
            }
        }
        calendar->clearEvents();
        syncEconomyCalendar();
        setPlayMessage("前进 " + std::to_string(days) + " 天");
    }

    /// \brief Starts `--soak`: counts live entities and textures before the loop.
    void beginSoak() {
        soakMode = true;
        soakOk = true;
        soakCycles = 0;
        soakBaselineEntities = sceneWorldStats().liveEntities;
        soakBaselineTextures = assetStats().textureCount;
        std::printf("soak: start entities %zu, textures %zu\n", soakBaselineEntities, soakBaselineTextures);
    }

    /// \brief Ends `--soak`, compares counters with the baseline and cleans the slot.
    [[nodiscard]] bool finishSoak() {
        if (!soakMode) {
            return true;
        }
        const std::size_t entities = sceneWorldStats().liveEntities;
        const std::size_t textures = assetStats().textureCount;
        const bool ok = soakOk && entities == soakBaselineEntities && textures == soakBaselineTextures;
        std::printf("soak: %d cycle(s), entities %zu -> %zu, textures %zu -> %zu -> %s\n",
                    soakCycles,
                    soakBaselineEntities,
                    entities,
                    soakBaselineTextures,
                    textures,
                    ok ? "ok" : "bad");
        if (saveService != nullptr) {
            static_cast<void>(saveService->deleteSlot("soak"));
        }
        return ok;
    }

    /// \brief Spawns a sprite-only stress scene: N draws plus 20 idle AI agents.
    void beginStress(int sprites) {
        if (gameScene == nullptr || sprites <= 0) {
            return;
        }
        stressMode = true;
        stressSpriteCount = sprites;
        stressFrameTimes.clear();
        stressFrameTimes.reserve(1024);
        fa::scene::World& world = gameScene->world();
        const fa::render::TextureHandle texture = gpuTextureOf(grassTexture);
        for (int index = 0; index < sprites; ++index) {
            const fa::scene::EntityId id = world.create();
            fa::scene::Transform transform;
            transform.position = fa::core::Vec2{8.0f + static_cast<float>(index % 50) * 12.0f,
                                                8.0f + static_cast<float>(index / 50) * 16.0f};
            static_cast<void>(world.add<fa::scene::Transform>(id, transform));

            fa::scene::Sprite sprite;
            sprite.texture = texture;
            sprite.size = fa::core::Vec2{8.0f, 8.0f};
            sprite.layer = fa::render::RenderLayer::Objects;
            sprite.tint = fa::core::Color{0.6f + 0.4f * static_cast<float>(index % 3) / 2.0f, 0.7f, 0.9f, 1.0f};
            static_cast<void>(world.add<fa::scene::Sprite>(id, sprite));

            if (index < 20) {
                // Idle agents keep the AI sweep in the measurement without combat.
                fa::ai::AiAgent agent;
                agent.home = transform.position;
                agent.profile.sightRadius = 0.0f;
                agent.profile.attackDamage = 0;
                static_cast<void>(world.add<fa::ai::AiAgent>(id, agent));
            }
        }
        gameScene->syncPhysics();
        std::printf("stress: spawned %d sprite(s), 20 idle agent(s)\n", sprites);
    }

    /// \brief Reports p50/p99 frame time for `--stress`.
    [[nodiscard]] bool finishStress() {
        if (!stressMode || stressFrameTimes.empty()) {
            return true;
        }
        std::vector<float> sorted = stressFrameTimes;
        std::sort(sorted.begin(), sorted.end());
        const float p50 = sorted[sorted.size() / 2];
        const float p99 = sorted[std::min(sorted.size() - 1, (sorted.size() * 99) / 100)];
        double total = 0.0;
        for (const float value : sorted) {
            total += static_cast<double>(value);
        }
        const double averageFps = total > 0.0 ? static_cast<double>(sorted.size()) / total : 0.0;
        std::printf("stress: %d sprite(s), %zu frame(s), p50 %.3f ms, p99 %.3f ms, "
                    "avg %.1f fps\n",
                    stressSpriteCount,
                    sorted.size(),
                    static_cast<double>(p50) * 1000.0,
                    static_cast<double>(p99) * 1000.0,
                    averageFps);
        return true;
    }

    /// \brief Saves, reloads and reapplies the soak slot to shake out leaks.
    void runSoakCycle() {
        if (saveService == nullptr || gameScene == nullptr) {
            soakOk = false;
            return;
        }
        if (saveToSlot("soak").isError()) {
            soakOk = false;
            return;
        }
        const fa::core::Result<fa::save::SaveFile> file = loadFromSlot("soak");
        if (file.isError() || applySaveFile(file.value()).isError()) {
            soakOk = false;
            return;
        }
        ++soakCycles;
    }

    /// \brief Headless check of the play loop: select -> use -> grow -> harvest.
    [[nodiscard]] bool runPlaySelfTest() {
        if (!playMode || gameScene == nullptr || playerInventory == nullptr) {
            std::printf("play-test: scene or inventory unavailable\n");
            return false;
        }

        const fa::core::Rect start = gameScene->mapTileRect(fa::tilemap::TileCoord{4, 8});
        gameScene->teleportMarker(fa::core::Vec2{start.x, start.y});
        fa::inventory::Hotbar& hotbar = playerInventory->hotbar();
        const fa::content::ContentId turnip = fa::content::ContentId::fromName("item.turnip");
        const auto useSlot = [&](int slot) {
            static_cast<void>(hotbar.select(slot));
            return handleUseTool();
        };

        bool ok = true;
        const bool staminaConfigOk = std::abs(gameScene->playerMaxStamina() - config.stamina.max) < 0.01f;
        std::printf("play-test: stamina max %.1f -> %s\n",
                    static_cast<double>(gameScene->playerMaxStamina()),
                    staminaConfigOk ? "ok" : "bad");
        ok = ok && staminaConfigOk;

        const fa::content::ContentId sword = fa::content::ContentId::fromName("item.sword");
        const fa::content::ItemDef* swordDef = content.item(sword);
        const bool weaponOk = swordDef != nullptr && swordDef->damage > 0 && weaponDamage() == swordDef->damage;
        std::printf("play-test: weapon damage %d (content %d) -> %s\n",
                    weaponDamage(),
                    swordDef != nullptr ? swordDef->damage : 0,
                    weaponOk ? "ok" : "bad");
        ok = ok && weaponOk;

        const fa::content::ContentStats contentStats = content.stats();
        const fa::content::CropDef* potatoDef = content.crop(fa::content::ContentId::fromName("crop.potato"));
        const fa::content::CropDef* strawberryDef = content.crop(fa::content::ContentId::fromName("crop.strawberry"));
        const fa::content::EnemyDef* batDef = content.enemy(fa::content::ContentId::fromName("enemy.bat"));
        int potatoDays = 0;
        if (potatoDef != nullptr) {
            for (const int days : potatoDef->stageDays) {
                potatoDays += days;
            }
        }
        const bool contentOk = contentStats.items == 13 && contentStats.crops == 3 && contentStats.enemies == 2 &&
                               contentStats.lootTables == 3 && potatoDef != nullptr && potatoDays == 6 &&
                               strawberryDef != nullptr && strawberryDef->regrow && strawberryDef->regrowDays == 3 &&
                               strawberryDef->stageSprites.size() == strawberryDef->stageDays.size() &&
                               batDef != nullptr && batDef->maxHealth == 18 &&
                               batDef->lootTable == fa::content::ContentId::fromName("loot.bat");
        std::printf("play-test: content %zu item(s), %zu crop(s), %zu enemy(ies), "
                    "%zu loot table(s) -> %s\n",
                    contentStats.items,
                    contentStats.crops,
                    contentStats.enemies,
                    contentStats.lootTables,
                    contentOk ? "ok" : "bad");
        ok = ok && contentOk;

        int sfxCount = 0;
        bool hasMusic = false;
        for (const auto& [id, def] : audioClipDefs) {
            static_cast<void>(id);
            if (def.bus == fa::audio::AudioBus::Music && def.loop) {
                hasMusic = true;
            } else if (def.bus == fa::audio::AudioBus::SFX) {
                ++sfxCount;
            }
        }
        // A CI runner may have no audio device; the catalog itself is still loaded.
        const bool audioOk = audioEngine == nullptr || (sfxCount >= 8 && hasMusic);
        std::printf("play-test: audio %d sfx, looping music %s -> %s\n",
                    sfxCount,
                    hasMusic ? "ok" : "bad",
                    audioOk ? "ok" : "bad");
        ok = ok && audioOk;

        const fa::content::ContentId soup = fa::content::ContentId::fromName("item.turnip_soup");
        const int soupBefore = hotbar.inventory().countOf(soup);
        static_cast<void>(hotbar.select(3));
        gameScene->setPlayerHealth(50.0f);
        gameScene->setPlayerStamina(50.0f);
        const fa::core::Status foodStatus = useSelectedFood();
        const int soupAfter = hotbar.inventory().countOf(soup);
        const bool foodOk = foodStatus.isOk() && soupAfter == soupBefore - 1 && gameScene->playerHealth() > 50.0f &&
                            gameScene->playerStamina() > 50.0f;
        std::printf("play-test: food hp 50 -> %.0f, stamina 50 -> %.1f, soup %d -> %d -> %s\n",
                    static_cast<double>(gameScene->playerHealth()),
                    static_cast<double>(gameScene->playerStamina()),
                    soupBefore,
                    soupAfter,
                    foodOk ? "ok" : "bad");
        ok = ok && foodOk;
        gameScene->setPlayerHealth(gameScene->playerMaxHealth());
        gameScene->setPlayerStamina(gameScene->playerMaxStamina());
        static_cast<void>(hotbar.select(0));

        const auto report = [&ok](const char* step, const fa::core::Status& status) {
            if (status.isOk()) {
                std::printf("play-test: %s ok\n", step);
            } else {
                std::printf(
                    "play-test: %s failed (%d): %s\n", step, status.error().code, status.error().message.c_str());
                ok = false;
            }
        };

        // Narrative: talk to the elder, accept the turnip quest, then leave.
        const fa::content::ContentId quest = fa::content::ContentId::fromName("quest.turnip");
        const fa::content::ContentId seedItem = fa::content::ContentId::fromName("item.turnip_seed");
        gameScene->teleportMarker(gameScene->npcPosition());
        const bool talkOk = openNpcDialogue() && gameScene->narrative().dialogueActive() && flow != nullptr &&
                            flow->state() == fa::flow::AppState::Dialogue;
        chooseDialogue(0);
        advanceDialogue();
        advanceDialogue();
        const bool questAccepted = gameScene->narrative().isQuestActive(quest) && flow != nullptr &&
                                   flow->state() == fa::flow::AppState::Playing;
        std::printf("play-test: dialogue %s, quest accepted %s\n", talkOk ? "ok" : "bad", questAccepted ? "ok" : "bad");
        ok = ok && talkOk && questAccepted;
        gameScene->teleportMarker(fa::core::Vec2{start.x, start.y});

        report("till", useSlot(0));
        report("plant", useSlot(2));
        report("water", useSlot(1));

        for (int day = 0; day < 5; ++day) {
            report("water", useSlot(1));
            if (flow != nullptr) {
                report("day", flow->endDay());
            }
        }

        const int before = hotbar.inventory().countOf(turnip);
        const int goldBeforeQuest = economy != nullptr ? economy->gold() : 0;
        const int seedsBeforeQuest =
            hotbar.inventory().countOf(seedItem) + playerInventory->backpack().countOf(seedItem);
        report("harvest", useSlot(0));
        processNarrativeEvents();
        const int after = hotbar.inventory().countOf(turnip);
        const bool questCompleted = gameScene->narrative().isQuestCompleted(quest);
        const int goldAfterQuest = economy != nullptr ? economy->gold() : 0;
        const int seedsAfterQuest =
            hotbar.inventory().countOf(seedItem) + playerInventory->backpack().countOf(seedItem);
        const bool questRewardOk =
            questCompleted && goldAfterQuest >= goldBeforeQuest + 10 && seedsAfterQuest >= seedsBeforeQuest + 2;
        std::printf("play-test: turnips %d -> %d, quest %s, reward gold %d -> %d, seeds %d -> %d "
                    "-> %s\n",
                    before,
                    after,
                    questCompleted ? "complete" : "open",
                    goldBeforeQuest,
                    goldAfterQuest,
                    seedsBeforeQuest,
                    seedsAfterQuest,
                    questRewardOk ? "ok" : "bad");
        if (after <= before || !questRewardOk) {
            ok = false;
        }

        bool questLogOk = false;
        for (const fa::narrative::QuestState& state : gameScene->narrative().questStates()) {
            if (state.id == quest && state.completed && !state.progress.empty()) {
                questLogOk = true;
            }
        }
        std::printf("play-test: quest log %s\n", questLogOk ? "ok" : "bad");
        ok = ok && questLogOk;

        // Leave a live crop in the field so the save/load round-trip exercises the
        // full crop state, not just the tilled flag.
        report("replant", useSlot(2));

        const fa::content::CropDef* turnipDef = content.crop(fa::content::ContentId::fromName("crop.turnip"));
        const bool cropDataOk = turnipDef != nullptr && turnipDef->seedSprite.name() == "tex.crop.turnip_seed" &&
                                turnipDef->stageSprites.size() == 3;
        const bool textureOk = renderer == nullptr || (gameScene->textureForName("tex.ground.soil_wet").isValid() &&
                                                       gameScene->textureForName("tex.crop.turnip_seed").isValid() &&
                                                       gameScene->textureForName("tex.crop.turnip_sprout").isValid() &&
                                                       gameScene->textureForName("tex.crop.turnip_young").isValid() &&
                                                       gameScene->textureForName("tex.crop.turnip_mature").isValid() &&
                                                       gameScene->textureForName("tex.object.chest").isValid() &&
                                                       gameScene->textureForName("tex.enemy.slime").isValid());
        std::printf(
            "play-test: crop sprites %s, detail textures %s\n", cropDataOk ? "ok" : "bad", textureOk ? "ok" : "bad");
        ok = ok && cropDataOk && textureOk;

        // Backpack panel: transfer one turnip each way and pause via Dialogue.
        const int bagTurnips = playerInventory->backpack().countOf(turnip);
        const int hotTurnips = hotbar.inventory().countOf(turnip);
        int bagSlot = -1;
        for (int slot = 0; slot < playerInventory->backpack().slotCount(); ++slot) {
            const fa::inventory::ItemStack& itemStack = playerInventory->backpack().at(slot);
            if (!itemStack.empty() && itemStack.id == turnip) {
                bagSlot = slot;
                break;
            }
        }
        bool invMoveOk = false;
        if (bagSlot >= 0) {
            const fa::core::Result<int> toHot = playerInventory->moveToHotbar(bagSlot, 1);
            if (toHot.isOk() && toHot.value() == 1) {
                int hotSlot = -1;
                for (int slot = 0; slot < hotbar.inventory().slotCount(); ++slot) {
                    const fa::inventory::ItemStack& itemStack = hotbar.inventory().at(slot);
                    if (!itemStack.empty() && itemStack.id == turnip) {
                        hotSlot = slot;
                        break;
                    }
                }
                if (hotSlot >= 0) {
                    const fa::core::Result<int> toBag = playerInventory->moveToBackpack(hotSlot, 1);
                    invMoveOk = toBag.isOk() && toBag.value() == 1 &&
                                playerInventory->backpack().countOf(turnip) == bagTurnips &&
                                hotbar.inventory().countOf(turnip) == hotTurnips;
                }
            }
        }
        openInventoryPanel();
        const bool invOpenOk = inventoryOpen && flow->state() == fa::flow::AppState::Dialogue;
        closeInventoryPanel();
        const bool invCloseOk = !inventoryOpen && flow->state() == fa::flow::AppState::Playing;
        const bool healthOk = gameScene->playerHealth() >= 99.0f;
        std::printf("play-test: inventory move %s, panel %s/%s, health %.0f -> %s\n",
                    invMoveOk ? "ok" : "bad",
                    invOpenOk ? "ok" : "bad",
                    invCloseOk ? "ok" : "bad",
                    static_cast<double>(gameScene->playerHealth()),
                    (invMoveOk && invOpenOk && invCloseOk && healthOk) ? "ok" : "bad");
        ok = ok && invMoveOk && invOpenOk && invCloseOk && healthOk;

        // Backpack category filter: seeds are seed-only, swords are weapon-only.
        const fa::content::ItemDef* seedDef = content.item(seedItem);
        const fa::content::ItemDef* swordItemDef = content.item(fa::content::ContentId::fromName("item.sword"));
        const bool filterOk = itemMatchesInventoryFilter(seedDef, 0) && itemMatchesInventoryFilter(seedDef, 1) &&
                              !itemMatchesInventoryFilter(seedDef, 2) && itemMatchesInventoryFilter(swordItemDef, 5) &&
                              !itemMatchesInventoryFilter(swordItemDef, 4);
        std::printf("play-test: inventory filter %s\n", filterOk ? "ok" : "bad");
        ok = ok && filterOk;

        // Combat: face the first dummy, swing until it dies, then pick up loot.
        // handleAttack requires a weapon in the selected slot: equip the sword for
        // the whole attack sequence and restore the previous slot afterwards.
        int combatSwordSlot = -1;
        if (playerInventory != nullptr) {
            fa::inventory::Inventory& swordHotbar = playerInventory->hotbar().inventory();
            for (int slot = 0; slot < swordHotbar.slotCount(); ++slot) {
                if (swordHotbar.at(slot).id.name() == "item.sword") {
                    combatSwordSlot = slot;
                    break;
                }
            }
        }
        const int combatSlotBefore = playerInventory != nullptr ? playerInventory->hotbar().selected() : 0;
        if (combatSwordSlot >= 0 && playerInventory != nullptr) {
            static_cast<void>(playerInventory->hotbar().select(combatSwordSlot));
        }
        gameScene->teleportMarker(fa::core::Vec2{288.0f, 212.0f});
        gameScene->setPlayerFacing(fa::player::Facing::East);
        gameScene->syncPhysics();
        const fa::content::ContentId seed = fa::content::ContentId::fromName("item.turnip_seed");
        const int seedsBefore = playerInventory->backpack().countOf(seed);
        const fa::scene::EntityId dummy = gameScene->combatDummy1();

        // Attack animation: the hitbox comes from the clip's `hit` frame, so the
        // first frames must not damage the target.
        int timingBefore = 0;
        if (const fa::player::Health* hp = gameScene->world().tryGet<fa::player::Health>(dummy); hp != nullptr) {
            timingBefore = hp->current;
        }
        handleAttack();
        const bool attackAnimStarted = gameScene->attackAnimationActive();
        int timingEarly = timingBefore;
        for (int step = 0; step < 2; ++step) {
            gameScene->advanceAnimation(1.0f / 60.0f);
            gameScene->advanceCombat(1.0f / 60.0f);
            if (const fa::player::Health* hp = gameScene->world().tryGet<fa::player::Health>(dummy); hp != nullptr) {
                timingEarly = hp->current;
            }
        }
        for (int step = 0; step < 30 && gameScene->attackAnimationActive(); ++step) {
            gameScene->advanceAnimation(1.0f / 60.0f);
            gameScene->advanceCombat(1.0f / 60.0f);
        }
        int timingAfter = timingBefore;
        if (const fa::player::Health* hp = gameScene->world().tryGet<fa::player::Health>(dummy); hp != nullptr) {
            timingAfter = hp->current;
        }
        const bool attackAnimOk = attackAnimStarted && timingEarly == timingBefore && timingAfter < timingBefore;
        std::printf("play-test: attack animation start %s, wind-up %s, hit %s -> %s\n",
                    attackAnimStarted ? "ok" : "bad",
                    timingEarly == timingBefore ? "ok" : "bad",
                    timingAfter < timingBefore ? "ok" : "bad",
                    attackAnimOk ? "ok" : "bad");
        ok = ok && attackAnimOk;

        for (int attempt = 0; attempt < 6 && gameScene->isAlive(dummy); ++attempt) {
            handleAttack();
            for (int step = 0; step < 18; ++step) {
                gameScene->advanceAnimation(1.0f / 60.0f);
                gameScene->advanceCombat(1.0f / 60.0f);
                processCombatEvents();
                collectLoot();
            }
        }
        const bool killed = !gameScene->isAlive(dummy);
        const int seedsAfter = playerInventory->backpack().countOf(seed);
        std::printf("play-test: combat %s, seeds %d -> %d\n", killed ? "killed" : "failed", seedsBefore, seedsAfter);
        if (!killed || seedsAfter <= seedsBefore) {
            ok = false;
        }

        // P0-3: narrative, AI, enemy/debris/placed-object state and player health
        // survive a save/load. Everything is mutated after saving so a no-op load
        // cannot pass by accident.
        if (flow == nullptr) {
            std::printf("play-test: flow unavailable for save-state checks\n");
            return false;
        }
        const fa::content::ContentId elder = fa::content::ContentId::fromName("npc.elder");
        const int affinityBefore = gameScene->narrative().affinity(elder);
        const bool questCompletedBefore = gameScene->narrative().isQuestCompleted(quest);
        const fa::core::Vec2 placedSaved{12.0f, 13.0f};
        if (fa::scene::Transform* transform =
                gameScene->world().tryGet<fa::scene::Transform>(gameScene->placedEntity());
            transform != nullptr) {
            transform->position = placedSaved;
        }
        if (fa::player::Health* hp = gameScene->world().tryGet<fa::player::Health>(gameScene->combatDummy2());
            hp != nullptr) {
            hp->current = 7;
        }
        const fa::tilemap::TileCoord debrisCell{31, 20};
        static_cast<void>(gameScene->debris().add(fa::farming::Debris{
            debrisCell, fa::farming::DebrisKind::Stone, fa::content::ContentId::fromName("item.turnip_seed"), 1}));
        gameScene->setPlayerHealth(61.0f);
        fa::ai::AiAgent* aiState = gameScene->world().tryGet<fa::ai::AiAgent>(gameScene->aiAgentEntity());
        if (aiState != nullptr) {
            aiState->state = fa::ai::AiState::Chase;
            aiState->stateTime = 0.125f;
            aiState->loseTimer = 0.75f;
            aiState->repathTimer = 0.25f;
            aiState->targetVisible = true;
            aiState->target = gameScene->aiDecoyEntity();
        }
        report("save-state", flow->saveToSlot("playtest"));

        if (aiState != nullptr) {
            aiState->state = fa::ai::AiState::Idle;
            aiState->target = fa::scene::EntityId{};
            aiState->targetVisible = false;
        }
        if (fa::player::Health* hp = gameScene->world().tryGet<fa::player::Health>(gameScene->combatDummy2());
            hp != nullptr) {
            hp->current = hp->max;
        }
        gameScene->debris().clear();
        gameScene->narrative().clear();
        gameScene->setPlayerHealth(gameScene->playerMaxHealth());
        if (fa::scene::Transform* transform =
                gameScene->world().tryGet<fa::scene::Transform>(gameScene->placedEntity());
            transform != nullptr) {
            transform->position = fa::core::Vec2{0.0f, 0.0f};
        }
        report("save-state-menu", flow->enterMainMenu());
        report("save-state-load", flow->loadGame("playtest"));

        const fa::ai::AiAgent* restoredAi = gameScene->world().tryGet<fa::ai::AiAgent>(gameScene->aiAgentEntity());
        const bool aiLoaded = restoredAi != nullptr && restoredAi->state == fa::ai::AiState::Chase &&
                              std::abs(restoredAi->stateTime - 0.125f) < 0.001f &&
                              std::abs(restoredAi->loseTimer - 0.75f) < 0.001f &&
                              std::abs(restoredAi->repathTimer - 0.25f) < 0.001f && restoredAi->targetVisible &&
                              restoredAi->target == gameScene->aiDecoyEntity();
        const fa::player::Health* restoredDummy =
            gameScene->world().tryGet<fa::player::Health>(gameScene->combatDummy2());
        const bool enemyLoaded = restoredDummy != nullptr && restoredDummy->current == 7;
        const bool debrisLoaded = gameScene->debris().at(debrisCell) != nullptr;
        const fa::scene::Transform* restoredPlaced =
            gameScene->world().tryGet<fa::scene::Transform>(gameScene->placedEntity());
        const bool placedLoaded = restoredPlaced != nullptr && restoredPlaced->position.x == placedSaved.x &&
                                  restoredPlaced->position.y == placedSaved.y;
        const bool narrativeLoaded =
            gameScene->narrative().isQuestCompleted(quest) && gameScene->narrative().affinity(elder) == affinityBefore;
        const bool healthLoaded = std::abs(gameScene->playerHealth() - 61.0f) < 0.01f;
        const bool saveStateOk = aiLoaded && enemyLoaded && debrisLoaded && placedLoaded && narrativeLoaded &&
                                 healthLoaded && questCompletedBefore;
        std::printf("play-test: save-state ai %s, enemy %s, debris %s, placed %s, narrative %s, "
                    "health %s -> %s\n",
                    aiLoaded ? "ok" : "bad",
                    enemyLoaded ? "ok" : "bad",
                    debrisLoaded ? "ok" : "bad",
                    placedLoaded ? "ok" : "bad",
                    narrativeLoaded ? "ok" : "bad",
                    healthLoaded ? "ok" : "bad",
                    saveStateOk ? "ok" : "bad");
        ok = ok && saveStateOk;
        gameScene->setPlayerHealth(gameScene->playerMaxHealth());

        // Save migration: a v1-shaped file without the P0-3 sections must load via
        // the shipped v1 -> v2 migration and gain empty containers.
        bool migrationOk = false;
        if (fileSystem != nullptr && saveService != nullptr) {
            fa::save::SaveService legacy(*fileSystem, config.paths.saves, 1);
            fa::save::SaveContext old;
            old.slotName = "migration_v1";
            old.inGameDay = 1;
            old.world.setString("map", "farm");
            old.world.setVec2("marker", fa::core::Vec2{1.0f, 2.0f});
            if (legacy.save("migration_v1", old).isOk()) {
                const fa::core::Result<fa::save::SaveFile> migrated = saveService->load("migration_v1");
                migrationOk = migrated.isOk() && migrated.value().meta.saveVersion == fa::save::kSaveVersion &&
                              migrated.value().world.child("objects").isArray() &&
                              migrated.value().quests.child("quests").isArray();
                static_cast<void>(saveService->deleteSlot("migration_v1"));
            }
        }
        std::printf("play-test: save migration v1->v2 %s\n", migrationOk ? "ok" : "bad");
        ok = ok && migrationOk;

#if defined(FA_ENABLE_EDITOR)
        // M26: the visual editor must paint through its pointer path and round-trip
        // the edited cell through the runtime map loader.
        const bool visualEditorOk = runVisualEditorCheck();
        std::printf("play-test: visual editor %s\n", visualEditorOk ? "ok" : "bad");
        ok = ok && visualEditorOk;
#endif

        // Bed proximity + save/load round-trip.
        const fa::core::Vec2 bed = gameScene->bedPosition();
        gameScene->teleportMarker(fa::core::Vec2{bed.x - 16.0f, bed.y});
        const bool nearBed = gameScene->isNearBed();
        std::printf("play-test: bed %s\n", nearBed ? "near" : "far");
        ok = ok && nearBed;

        if (flow == nullptr || calendar == nullptr) {
            std::printf("play-test: flow/calendar unavailable\n");
            return false;
        }

        // Sleep rule: an 8 h night refills stamina, 7 h restores about 77%,
        // and a 1 h nap is floored at 1%.
        const auto sleepFrom = [&](float hour) {
            calendar->restore(calendar->date(), hour, calendar->today(), calendar->tomorrow());
            gameScene->setPlayerStamina(0.0f);
            return sleepAtBed();
        };
        const auto sleepWithHours = [&](float hours) {
            float hour = config.gameplay.wakeHour - hours;
            while (hour < 0.0f) {
                hour += 24.0f;
            }
            return sleepFrom(hour);
        };
        const float fullHours = config.gameplay.fullSleepHours;
        const float shortHours = std::max(1.0f, fullHours - 1.0f);
        report("sleep-full", sleepWithHours(fullHours));
        const float fullStamina = gameScene->playerStamina();
        report("sleep-short", sleepWithHours(shortHours));
        const float shortStamina = gameScene->playerStamina();
        report("sleep-nap", sleepWithHours(1.0f));
        const float napStamina = gameScene->playerStamina();
        const bool wakeOk = calendar->timeOfDay() > config.gameplay.wakeHour - 0.01f &&
                            calendar->timeOfDay() < config.gameplay.wakeHour + 0.01f;
        const float shortFraction = shortHours / fullHours;
        const float expectedShort = shortFraction * shortFraction * 100.0f;
        const bool sleepOk = fullStamina >= 99.0f && std::abs(shortStamina - expectedShort) < 2.0f &&
                             napStamina >= 0.5f && napStamina <= 2.0f;
        std::printf("play-test: sleep wake %02d:%02d, full %.0fh %.1f, short %.0fh %.1f, "
                    "1h %.1f -> %s\n",
                    calendar->hour(),
                    calendar->minute(),
                    static_cast<double>(fullHours),
                    static_cast<double>(fullStamina),
                    static_cast<double>(shortHours),
                    static_cast<double>(shortStamina),
                    static_cast<double>(napStamina),
                    (sleepOk && wakeOk) ? "ok" : "bad");
        ok = ok && sleepOk && wakeOk;

        // Economy: buy a seed, sell a turnip, ship the rest, then settle next day.
        if (economy == nullptr) {
            std::printf("play-test: economy unavailable\n");
            return false;
        }
        const int goldBeforeShop = economy->gold();
        const int seedCost = shopBuyPrice(seed);
        const int turnipSale = shopSellPrice(turnip);
        report("shop-buy", buyFromShop(seed, 1));
        report("shop-sell", sellToShop(turnip, 1));
        const int shippedCrops = shipAllCrops();
        const bool shopOk = seedCost > 0 && turnipSale > 0 && economy->gold() == goldBeforeShop - seedCost + turnipSale;

        // Seasonal stock and deterministic daily prices are data-driven.
        bool seasonOk = false;
        bool swingOk = false;
        if (const fa::economy::ShopDef* general = shops.shop(shopGeneralId()); general != nullptr) {
            const fa::economy::ShopEntry* soupEntry = nullptr;
            const fa::economy::ShopEntry* seedEntry = nullptr;
            for (const fa::economy::ShopEntry& entry : general->stock) {
                if (entry.item == soup) {
                    soupEntry = &entry;
                } else if (entry.item == seed) {
                    seedEntry = &entry;
                }
            }
            if (soupEntry != nullptr) {
                economy->setSeason(fa::content::Season::Spring);
                const bool springSoup = economy->seasonAllows(*soupEntry);
                economy->setSeason(fa::content::Season::Summer);
                const bool summerSoup = economy->seasonAllows(*soupEntry);
                seasonOk = springSoup && !summerSoup;
            }
            if (seedEntry != nullptr) {
                economy->setDay(3);
                const int first = economy->buyPrice(*general, *seedEntry, 1);
                economy->setDay(4);
                const int next = economy->buyPrice(*general, *seedEntry, 1);
                economy->setDay(3);
                swingOk = first > 0 && next > 0 && first == economy->buyPrice(*general, *seedEntry, 1);
            }
        }
        syncEconomyCalendar();
        std::printf("play-test: shop season %s, price swing %s -> %s\n",
                    seasonOk ? "ok" : "bad",
                    swingOk ? "ok" : "bad",
                    (seasonOk && swingOk) ? "ok" : "bad");
        ok = ok && seasonOk && swingOk;
        const int goldBeforeSettle = economy->gold();
        report("ship-settle", sleepAtBed());
        const int settlement = economy->gold() - goldBeforeSettle;
        const bool shipOk = shippedCrops >= 2 && settlement > 0 && economy->shipping().empty();
        std::printf("play-test: shop gold %d, shipped %d, settled +%d -> %s\n",
                    economy->gold(),
                    shippedCrops,
                    settlement,
                    (shopOk && shipOk) ? "ok" : "bad");
        ok = ok && shopOk && shipOk;

        // The shipping box and merchant must exist and be reachable in play mode.
        gameScene->teleportMarker(fa::core::Vec2{352.0f, 216.0f});
        const bool shipPropOk = gameScene->isNearShippingBox();
        gameScene->teleportMarker(fa::core::Vec2{288.0f, 232.0f});
        const bool merchantPropOk = gameScene->isNearMerchant();
        std::printf(
            "play-test: props shipping %s, merchant %s\n", shipPropOk ? "ok" : "bad", merchantPropOk ? "ok" : "bad");
        ok = ok && shipPropOk && merchantPropOk;

        bool spawnOk = true;
        bool batSpawnOk = false;
        for (const EntitySpawn& spawn : entitySpawns) {
            if (spawn.kind == "bed" && spawn.hasPosition) {
                const fa::core::Vec2 spawnBed = gameScene->bedPosition();
                spawnOk = spawnBed.x == spawn.position.x && spawnBed.y == spawn.position.y;
            }
            if (spawn.id == "forest_bat" && spawn.kind == "enemy" && spawn.texture == "tex.enemy.bat" &&
                spawn.moveSpeed > 60.0f) {
                batSpawnOk = true;
            }
        }
        std::printf("play-test: %zu entity spawn(s), bed from table %s, bat spawn %s\n",
                    entitySpawns.size(),
                    spawnOk ? "ok" : "bad",
                    batSpawnOk ? "ok" : "bad");
        ok = ok && spawnOk && batSpawnOk;

        // Separate maps: the farm-side gate replaces the farm scene with the forest.
        const bool farmGateOk =
            gameScene->editableMap().isWalkable(22, 11) && gameScene->editableMap().isWalkable(23, 11);
        gameScene->teleportMarker(gameScene->cellPosition(fa::tilemap::TileCoord{GameScene::kColumns - 1, 11}));
        const bool toForest = checkPortal() && gameScene->isForest() && gameScene->editableMap().isWalkable(0, 11);
        const bool forestDummyOk = gameScene->isAlive(gameScene->forestDummy1());

        // The forest map has no decorative grass animator, so this proves the swing
        // still advances and its `hit` frame spawns the hitbox on both maps.
        const fa::scene::EntityId forestTarget = gameScene->forestDummy1();
        fa::core::Vec2 forestTargetPosition{};
        if (const fa::scene::Transform* transform = gameScene->world().tryGet<fa::scene::Transform>(forestTarget);
            transform != nullptr) {
            forestTargetPosition = transform->position;
        }
        gameScene->teleportMarker(fa::core::Vec2{forestTargetPosition.x - 18.0f, forestTargetPosition.y});
        gameScene->setPlayerFacing(fa::player::Facing::East);
        gameScene->syncPhysics();
        int forestHealthBefore = 0;
        if (const fa::player::Health* hp = gameScene->world().tryGet<fa::player::Health>(forestTarget); hp != nullptr) {
            forestHealthBefore = hp->current;
        }
        handleAttack();
        for (int step = 0; step < 30 && gameScene->attackAnimationActive(); ++step) {
            gameScene->advanceAnimation(1.0f / 60.0f);
            gameScene->advanceCombat(1.0f / 60.0f);
        }
        int forestHealthAfter = forestHealthBefore;
        if (const fa::player::Health* hp = gameScene->world().tryGet<fa::player::Health>(forestTarget); hp != nullptr) {
            forestHealthAfter = hp->current;
        }
        const bool forestAttackOk = forestHealthBefore > 0 && forestHealthAfter < forestHealthBefore;
        std::printf("play-test: forest attack animation %s\n", forestAttackOk ? "ok" : "bad");
        ok = ok && forestAttackOk;
        if (playerInventory != nullptr) {
            static_cast<void>(playerInventory->hotbar().select(combatSlotBefore));
        }

        // P1-1: forest dummies are AI agents that chase and hit the player.
        const fa::scene::EntityId attacker = gameScene->forestDummy1();
        fa::core::Vec2 attackerPosition{};
        if (const fa::scene::Transform* transform = gameScene->world().tryGet<fa::scene::Transform>(attacker);
            transform != nullptr) {
            attackerPosition = transform->position;
        }
        gameScene->teleportMarker(fa::core::Vec2{attackerPosition.x - 18.0f, attackerPosition.y});
        gameScene->syncPhysics();
        gameScene->setPlayerHealth(gameScene->playerMaxHealth());
        const float healthBeforeCounter = gameScene->playerHealth();
        bool sawChase = false;
        bool sawAttack = false;
        bool sawSwing = false;
        bool swingWindupOk = false;
        bool hurtByEnemy = false;
        float previousHealth = healthBeforeCounter;
        for (int step = 0; step < 240 && flow != nullptr && flow->state() == fa::flow::AppState::Playing; ++step) {
            gameScene->advanceAi(1.0f / 60.0f);
            if (const fa::ai::AiAgent* agent = gameScene->world().tryGet<fa::ai::AiAgent>(attacker); agent != nullptr) {
                sawChase = sawChase || agent->state == fa::ai::AiState::Chase;
                sawAttack = sawAttack || agent->state == fa::ai::AiState::Attack;
            }
            if (!sawSwing && gameScene->enemyAttackAnimationActive(attacker)) {
                sawSwing = true;
                // A wind-up must not damage on the frame it starts.
                swingWindupOk = gameScene->playerHealth() == previousHealth;
            }
            gameScene->advanceAnimation(1.0f / 60.0f);
            gameScene->advanceCombat(1.0f / 60.0f);
            processCombatEvents();
            previousHealth = gameScene->playerHealth();
            if (gameScene->playerHealth() < healthBeforeCounter) {
                hurtByEnemy = true;
                break;
            }
        }
        const bool counterOk = toForest && forestDummyOk && sawChase && sawAttack && sawSwing && swingWindupOk &&
                               hurtByEnemy && flow != nullptr && flow->state() == fa::flow::AppState::Playing;
        std::printf("play-test: enemy counterattack chase %s, attack %s, swing %s, "
                    "wind-up %s, hurt %s -> %s\n",
                    sawChase ? "ok" : "bad",
                    sawAttack ? "ok" : "bad",
                    sawSwing ? "ok" : "bad",
                    swingWindupOk ? "ok" : "bad",
                    hurtByEnemy ? "ok" : "bad",
                    counterOk ? "ok" : "bad");
        ok = ok && counterOk;
        gameScene->setPlayerHealth(gameScene->playerMaxHealth());

        portalCooldown = 0.0f;
        gameScene->teleportMarker(gameScene->cellPosition(fa::tilemap::TileCoord{0, 11}));
        const bool toFarm = checkPortal() && gameScene->isFarm() && gameScene->editableMap().isWalkable(22, 11);
        std::printf("play-test: farm gate %s, portal farm->forest %s, forest->farm %s, "
                    "forest dummy %s\n",
                    farmGateOk ? "ok" : "bad",
                    toForest ? "ok" : "bad",
                    toFarm ? "ok" : "bad",
                    forestDummyOk ? "ok" : "bad");
        ok = ok && farmGateOk && toForest && toFarm && forestDummyOk;

        // Enemy damage can kill the player: this must enter GameOver without
        // destroying the player entity, then respawn at the farm bed.
        portalCooldown = 0.0f;
        gameScene->teleportMarker(gameScene->cellPosition(fa::tilemap::TileCoord{GameScene::kColumns - 1, 11}));
        const bool combatDeathForest = checkPortal() && gameScene->isForest();
        const fa::scene::EntityId killer = gameScene->forestDummy1();
        fa::core::Vec2 killerPosition{};
        if (const fa::scene::Transform* transform = gameScene->world().tryGet<fa::scene::Transform>(killer);
            transform != nullptr) {
            killerPosition = transform->position;
        }
        gameScene->teleportMarker(fa::core::Vec2{killerPosition.x - 18.0f, killerPosition.y});
        gameScene->setPlayerHealth(1.0f);
        gameScene->syncPhysics();
        for (int step = 0; step < 240 && flow != nullptr && flow->state() == fa::flow::AppState::Playing; ++step) {
            gameScene->advanceAi(1.0f / 60.0f);
            gameScene->advanceAnimation(1.0f / 60.0f);
            gameScene->advanceCombat(1.0f / 60.0f);
            processCombatEvents();
        }
        const bool playerIntact = gameScene->isAlive(gameScene->playerEntity());
        const bool combatDeathOk = combatDeathForest && flow != nullptr &&
                                   flow->state() == fa::flow::AppState::GameOver && gameScene->playerHealth() <= 0.0f &&
                                   playerIntact;
        const bool combatRespawnOk =
            combatDeathOk && respawnFromDeath() && flow->state() == fa::flow::AppState::Playing && gameScene->isFarm();
        std::printf("play-test: combat death %s, player intact %s, respawn %s -> %s\n",
                    combatDeathOk ? "ok" : "bad",
                    playerIntact ? "ok" : "bad",
                    combatRespawnOk ? "ok" : "bad",
                    (combatDeathOk && combatRespawnOk) ? "ok" : "bad");
        ok = ok && combatDeathOk && combatRespawnOk;

        // Brambles knock the player out: the forest scene is replaced by the farm
        // and the player wakes beside the bed with a 10% gold loss.
        const int goldBeforeDeath = economy->gold();
        portalCooldown = 0.0f;
        gameScene->teleportMarker(gameScene->cellPosition(fa::tilemap::TileCoord{GameScene::kColumns - 1, 11}));
        const bool deathForestOk = checkPortal() && gameScene->isForest();
        gameScene->teleportMarker(gameScene->cellPosition(fa::tilemap::TileCoord{8, 14}));
        gameScene->setPlayerHealth(10.0f);
        hazardCooldown = 0.0f;
        static_cast<void>(tickHazard());
        hazardCooldown = 0.0f;
        static_cast<void>(tickHazard());
        const int expectedGold = goldBeforeDeath - goldBeforeDeath / 10;
        const bool deathMenuOk = deathForestOk && gameScene->isForest() &&
                                 flow->state() == fa::flow::AppState::GameOver && gameScene->playerHealth() <= 0.0f &&
                                 economy->gold() == expectedGold;
        const bool respawnOk = respawnFromDeath() && gameScene->isFarm() &&
                               flow->state() == fa::flow::AppState::Playing && gameScene->playerHealth() >= 99.0f &&
                               economy->gold() == expectedGold;
        const bool deathOk = deathMenuOk && respawnOk;
        std::printf("play-test: thorns death menu %s, respawn %s, gold %d -> %d -> %s\n",
                    deathMenuOk ? "ok" : "bad",
                    respawnOk ? "ok" : "bad",
                    goldBeforeDeath,
                    economy->gold(),
                    deathOk ? "ok" : "bad");
        ok = ok && deathOk;

        // Loot persistence on the forest map: save/load must restore the map too.
        portalCooldown = 0.0f;
        gameScene->teleportMarker(gameScene->cellPosition(fa::tilemap::TileCoord{GameScene::kColumns - 1, 11}));
        const bool reenteredForest = checkPortal() && gameScene->isForest();
        spawnLoot(gameScene->cellPosition(fa::tilemap::TileCoord{25, 15}), turnip, 2);
        const int lootBefore = countLoot(turnip, 2);

        const fa::core::Vec2 savedMarker = gameScene->markerPosition();
        const int savedTurnips = playerInventory->backpack().countOf(turnip) + hotbar.inventory().countOf(turnip);
        const int savedSeeds = playerInventory->backpack().countOf(seed) + hotbar.inventory().countOf(seed);
        const int savedDay = calendar->date().day;
        const int savedGold = economy->gold();
        if (fa::ai::AiAgent* forestAi = gameScene->world().tryGet<fa::ai::AiAgent>(gameScene->forestDummy1());
            forestAi != nullptr) {
            forestAi->state = fa::ai::AiState::Chase;
            forestAi->target = gameScene->playerEntity();
            forestAi->targetVisible = true;
            forestAi->loseTimer = 0.5f;
        }
        report("save", flow->saveGame());
        gameScene->teleportMarker(fa::core::Vec2{0.0f, 0.0f});
        static_cast<void>(playerInventory->backpack().add(fa::inventory::ItemStack{turnip, 5, -1}));
        economy->addGold(1000);
        report("menu", flow->enterMainMenu());
        report("load", flow->loadGame("playtest"));

        const fa::core::Vec2 loadedMarker = gameScene->markerPosition();
        const bool markerOk = loadedMarker.x == savedMarker.x && loadedMarker.y == savedMarker.y;
        const int loadedTurnips = playerInventory->backpack().countOf(turnip) + hotbar.inventory().countOf(turnip);
        const int loadedSeeds = playerInventory->backpack().countOf(seed) + hotbar.inventory().countOf(seed);
        const bool bagOk = loadedTurnips == savedTurnips && loadedSeeds == savedSeeds;
        const bool dayOk = calendar->date().day == savedDay;
        const bool goldOk = economy->gold() == savedGold;
        const int loadedLoot = countLoot(turnip, 2);
        const bool lootOk = lootBefore == 2 && loadedLoot == 2;
        const bool mapOk = reenteredForest && gameScene->isForest();
        const fa::ai::AiAgent* loadedForestAi = gameScene->world().tryGet<fa::ai::AiAgent>(gameScene->forestDummy1());
        const bool forestAiOk = loadedForestAi != nullptr && loadedForestAi->state == fa::ai::AiState::Chase &&
                                loadedForestAi->targetVisible && loadedForestAi->target == gameScene->playerEntity() &&
                                std::abs(loadedForestAi->loseTimer - 0.5f) < 0.001f;
        bool farmOk = false;
        for (const fa::farming::FarmTileEntry& entry : gameScene->farmingTiles()) {
            if (entry.cell.x == 4 && entry.cell.y == 9 && entry.tile.tilled && entry.hasCrop &&
                entry.crop.seed == seed) {
                farmOk = true;
            }
        }
        std::printf("play-test: load marker %s, bag %s, day %s, farm %s, map %s, gold %s, "
                    "loot %s, forest ai %s\n",
                    markerOk ? "ok" : "bad",
                    bagOk ? "ok" : "bad",
                    dayOk ? "ok" : "bad",
                    farmOk ? "ok" : "bad",
                    mapOk ? "ok" : "bad",
                    goldOk ? "ok" : "bad",
                    lootOk ? "ok" : "bad",
                    forestAiOk ? "ok" : "bad");
        ok = ok && markerOk && bagOk && dayOk && farmOk && mapOk && goldOk && lootOk && forestAiOk;

        // Menu wiring: pause, explicit slot save, main menu and slot load.
        flow->requestPause(true);
        const bool pausedOk = flow->state() == fa::flow::AppState::Paused;
        flow->requestPause(false);
        report("pause",
               pausedOk ? fa::core::okStatus()
                        : fa::core::errorStatus(fa::flow::flow_error::kNotPlaying, "pause did not take"));
        report("slot-save", flow->saveToSlot("slot1"));
        refreshSlots();
        const bool slotListed = !slotList.empty();
        report("main-menu", flow->enterMainMenu());
        const bool menuOk = flow->state() == fa::flow::AppState::MainMenu;
        report("slot-load", flow->loadGame("slot1"));
        const bool resumedOk = flow->state() == fa::flow::AppState::Playing;
        std::printf("play-test: menu paused %s, slot listed %s, menu %s, resumed %s\n",
                    pausedOk ? "ok" : "bad",
                    slotListed ? "ok" : "bad",
                    menuOk ? "ok" : "bad",
                    resumedOk ? "ok" : "bad");
        ok = ok && pausedOk && slotListed && menuOk && resumedOk;

        // Crafting: add two turnips to the backpack, cook, advance 30 game minutes
        // and collect. This is a play-only check, so it runs after the save round-trip.
        const int soupForCraft = playerInventory->backpack().countOf(soup) + hotbar.inventory().countOf(soup);
        static_cast<void>(playerInventory->backpack().add(fa::inventory::ItemStack{turnip, 2, -1}));
        const fa::core::Status craftStatus = craftTurnipSoup();
        economy->update(30);
        const bool craftReady = economy->readyJobCount() > 0;
        const fa::core::Status collectStatus = collectReadyCraft();
        const int soupAfterCraft = playerInventory->backpack().countOf(soup) + hotbar.inventory().countOf(soup);
        const bool craftOk =
            craftStatus.isOk() && craftReady && collectStatus.isOk() && soupAfterCraft == soupForCraft + 1;
        std::printf("play-test: craft soup %s (ready %s, collect %s), soup %d -> %d -> %s\n",
                    craftStatus.isOk() ? "ok" : "bad",
                    craftReady ? "ok" : "bad",
                    collectStatus.isOk() ? "ok" : "bad",
                    soupForCraft,
                    soupAfterCraft,
                    craftOk ? "ok" : "bad");
        ok = ok && craftOk;

        // Death panel load path: die again in the forest and load the autosave.
        if (gameScene->isFarm()) {
            portalCooldown = 0.0f;
            gameScene->teleportMarker(gameScene->cellPosition(fa::tilemap::TileCoord{GameScene::kColumns - 1, 11}));
            static_cast<void>(checkPortal());
        }
        const bool deathLoadForest = gameScene->isForest();
        gameScene->teleportMarker(gameScene->cellPosition(fa::tilemap::TileCoord{8, 14}));
        gameScene->setPlayerHealth(10.0f);
        hazardCooldown = 0.0f;
        static_cast<void>(tickHazard());
        hazardCooldown = 0.0f;
        static_cast<void>(tickHazard());
        const bool deathLoadMenu = deathLoadForest && flow->state() == fa::flow::AppState::GameOver;
        const bool deathLoadOk = deathLoadMenu && loadFromDeath() && flow->state() == fa::flow::AppState::Playing;
        std::printf("play-test: death load menu %s, loaded %s -> %s\n",
                    deathLoadMenu ? "ok" : "bad",
                    flow->state() == fa::flow::AppState::Playing ? "ok" : "bad",
                    deathLoadOk ? "ok" : "bad");
        ok = ok && deathLoadOk;

        // Death panel main-menu path: die once more and return to the menu.
        if (gameScene->isFarm()) {
            portalCooldown = 0.0f;
            gameScene->teleportMarker(gameScene->cellPosition(fa::tilemap::TileCoord{GameScene::kColumns - 1, 11}));
            static_cast<void>(checkPortal());
        }
        gameScene->teleportMarker(gameScene->cellPosition(fa::tilemap::TileCoord{8, 14}));
        gameScene->setPlayerHealth(10.0f);
        hazardCooldown = 0.0f;
        static_cast<void>(tickHazard());
        hazardCooldown = 0.0f;
        static_cast<void>(tickHazard());
        const bool deathMainMenuOpen = flow->state() == fa::flow::AppState::GameOver;
        const bool deathMainMenuOk =
            deathMainMenuOpen && mainMenuFromDeath() && flow->state() == fa::flow::AppState::MainMenu;
        std::printf(
            "play-test: death main-menu %s -> %s\n", deathMainMenuOpen ? "ok" : "bad", deathMainMenuOk ? "ok" : "bad");
        ok = ok && deathMainMenuOk;

        std::printf("play-test: %s\n", ok ? "OK" : "FAILED");
        return ok;
    }

    /// \brief Sets the time of day without changing date or weather.
    void setClock(int hour, int minute) {
        if (calendar == nullptr) {
            return;
        }
        const float timeOfDay = static_cast<float>(hour) + static_cast<float>(minute) / 60.0f;
        calendar->restore(calendar->date(), timeOfDay, calendar->today(), calendar->tomorrow());
        calendar->clearEvents();
        setPlayMessage("时间已设置");
    }

#if defined(FA_ENABLE_DEBUG_TOOLS)
    /// \brief Fills the M27 snapshot from the modules the app already owns.
    void updateDebugStats() {
        if (debugTools == nullptr) {
            return;
        }

        fa::debug::DebugStats stats;
        stats.fps = lastRealDelta > 0.0 ? static_cast<float>(1.0 / lastRealDelta) : 0.0f;
        stats.frameMs = static_cast<float>(lastRealDelta * 1000.0);
        stats.frameCount = static_cast<std::uint64_t>(renderedFrames);
        stats.fixedSteps = fixedSteps;
        const fa::scene::WorldStats world = sceneWorldStats();
        stats.entities = world.liveEntities;
        stats.componentTypes = world.componentTypes;
        stats.components = world.components;
        stats.worldDrawItems = extractedItems;
        stats.uiDrawItems = uiItems;
        stats.drawCalls = lastStats.drawCalls;
        stats.quads = lastStats.quads;
        stats.textureSwitches = lastStats.textureSwitches;
        stats.liveTextures = renderer != nullptr ? static_cast<std::uint32_t>(renderer->liveTextureCount()) : 0u;
        const fa::assets::AssetStats assetsSnapshot = assetStats();
        stats.assetTextures = assetsSnapshot.textureCount;
        stats.placeholderTextures = assetsSnapshot.placeholderCount;
        stats.failedAssets = assetsSnapshot.failedCount;
        const fa::physics::PhysicsStats physicsSnapshot = scenePhysicsStats();
        stats.physicsBodies = physicsSnapshot.bodies;
        stats.physicsCells = physicsSnapshot.cells;
        stats.physicsMoves = physicsSnapshot.moves;
        stats.physicsBlocked = physicsSnapshot.blockedMoves;
        const fa::audio::AudioStats audioSnapshot = audioStats();
        stats.audioVoices = audioSnapshot.activeVoices;
        stats.audioSampleRate = audioSnapshot.sampleRate;
        stats.audioDropped = audioSnapshot.dropped;
        const fa::tilemap::TilemapStats mapSnapshot = sceneTilemapStats();
        stats.mapTiles = mapSnapshot.nonEmptyTiles;
        stats.mapSolidTiles = mapSnapshot.solidTiles;

        // The camera belongs to the controlled character: report where it is and
        // which entity it follows.
        stats.camera.valid = true;
        stats.camera.position = camera.position();
        stats.camera.viewportSize = camera.viewportSize();
        stats.camera.visible = camera.visibleRect();
        const fa::render::CameraFollowConfig follow = camera.followConfig();
        stats.camera.deadzoneX = follow.deadzone.x;
        stats.camera.deadzoneY = follow.deadzone.y;
        stats.camera.smoothing = follow.smoothing;
        if (gameScene != nullptr) {
            stats.hasPlayerPosition = true;
            stats.playerPosition = gameScene->markerCenter();
            const fa::tilemap::TileCoord cell = gameScene->markerCell();
            stats.hasPlayerCell = true;
            stats.playerCell = fa::core::Vec2{static_cast<float>(cell.x), static_cast<float>(cell.y)};
            stats.camera.followTarget = gameScene->playerEntity();
            stats.camera.following = stats.camera.followTarget.isValid();
            for (const AiTargetDebugSnapshot& snapshot : gameScene->aiTargetDebugSnapshot()) {
                fa::debug::DebugAiTargetInfo info;
                info.agent = snapshot.agent;
                info.target = snapshot.target;
                info.hasTarget = snapshot.hasTarget;
                info.targetVisible = snapshot.targetVisible;
                info.state.assign(fa::ai::toString(snapshot.state));
                info.agentPosition = snapshot.agentCenter;
                info.targetPosition = snapshot.targetCenter;
                info.hasAgentPosition = snapshot.hasAgentCenter;
                info.hasTargetPosition = snapshot.hasTargetCenter;
                stats.aiTargets.push_back(std::move(info));
            }
        }

        // Facing-tile hub lines (F3 second panel).
        if (gameScene != nullptr) {
            stats.facingTileInfo = gameScene->facingTileDebugLines();
        }

        debugTools->setStats(stats);
    }

    void shutdownDebug() {
        if (debugTools != nullptr) {
            debugTools->detachLogSink();
            debugTools.reset();
        }
    }

    [[nodiscard]] std::size_t debugCommandCount() const {
        return debugTools != nullptr ? debugTools->console().commands().size() : 0;
    }
    [[nodiscard]] std::size_t debugCVarCount() const { return debugTools != nullptr ? debugTools->cvars().size() : 0; }
    [[nodiscard]] fa::debug::DebugStats debugSnapshot() const {
        return debugTools != nullptr ? debugTools->stats() : fa::debug::DebugStats{};
    }
#endif

    /// \brief Uploads the UI font atlas and the 1x1 white quad texture.
    ///
    /// M09 owns no GL objects: the composition root creates both textures and hands
    /// M09 plain handles, exactly like the asset pipeline hands handles to sprites.
    ///
    /// The baked atlas (tools/build_font_atlas.py) carries Chinese; when it is absent the
    /// built-in 3x5 ASCII font takes over, which keeps a fresh clone, CI and the smoke run
    /// working with no art on disk - the same degradation rule as textures.
    fa::core::Status initializeUi(fa::platform::IPlatform& platform) {
        if (renderer == nullptr) {
            return fa::core::errorStatus(fa::render::render_error::kNotInitialized, "no renderer to upload the font");
        }
        fontSource = "builtin";
        fa::core::Result<fa::ui::FontAtlas> baked = fa::app::loadBakedUiFont(platform.files(), config.paths.assets);
        if (baked.isOk()) {
            uiFont = std::move(baked).take();
            fontSource = "baked";
        } else {
            std::printf("font: %s, using the built-in 3x5 ASCII atlas\n", baked.error().message.c_str());
            uiFont = fa::ui::buildBuiltinFont();
        }

        fa::render::TextureDesc fontDesc;
        fontDesc.width = uiFont.width;
        fontDesc.height = uiFont.height;
        fontDesc.format = fa::render::TextureFormat::Rgba8;
        fontDesc.pixels = uiFont.pixels.data();
        fontDesc.debugName = "ui.font";
        if (const fa::core::Status status = renderer->createTexture(fontDesc, uiFont.texture); status.isError()) {
            return status;
        }

        const std::uint8_t white[4] = {255, 255, 255, 255};
        fa::render::TextureDesc whiteDesc;
        whiteDesc.width = 1;
        whiteDesc.height = 1;
        whiteDesc.format = fa::render::TextureFormat::Rgba8;
        whiteDesc.pixels = white;
        whiteDesc.debugName = "ui.white";
        if (const fa::core::Status status = renderer->createTexture(whiteDesc, uiWhiteTexture); status.isError()) {
            renderer->destroyTexture(uiFont.texture);
            uiFont.texture = fa::render::TextureHandle{};
            return status;
        }

        fa::ui::UiTheme theme;
        // The baked atlas is a 10 px pixel font, so widget text is already legible at 1x.
        // 2x made labels (20 px) as tall as the 16-20 px buttons they sit in, and the
        // centring then pushed their sortY to or above the button's own, so the opaque
        // button quad painted over the label.
        theme.textScale = 1.0f * config.ui.scale;
        theme.padding = 4.0f * config.ui.scale;
        ui = std::make_unique<fa::ui::UiContext>(uiFont, uiWhiteTexture, theme);
        return fa::core::okStatus();
    }

    /// \brief Centres a rectangle in the 640x360 virtual UI space.
    [[nodiscard]] static fa::core::Rect centeredRect(float width, float height) {
        return fa::core::makeRect((640.0f - width) * 0.5f, (360.0f - height) * 0.5f, width, height);
    }

    /// \brief Named catalog texture for UI drawing (menu art); invalid when absent.
    [[nodiscard]] fa::render::TextureHandle uiTexture(std::string_view name) const {
        const auto found = namedTextureHandles.find(std::string(name));
        return found != namedTextureHandles.end() ? found->second : fa::render::TextureHandle{};
    }

    /// \brief Scales a base size with `ui.scale`, then centres it.
    [[nodiscard]] fa::core::Rect scaledCenteredRect(float width, float height) const {
        return centeredRect(width * config.ui.scale, height * config.ui.scale);
    }

    /// \brief Drops the renderer, and with it the GL context, before the window closes.
    ///
    /// Handles go first, then the cache (whose destructor releases the GPU textures),
    /// and only then the renderer those textures belong to.
    void shutdownRenderer() {
#if defined(FA_ENABLE_EDITOR)
        // M26 borrows the scene's map; drop it before the stack destroys that map.
        editor.reset();
#endif
        // The scenes go first: their sprites only hold texture handles, but the stack
        // calls onExit() on the way out, which is where per-scene GPU work would be
        // released.
        stack.clear();
        gameScene = nullptr;
        grassTexture = fa::assets::AssetHandle<fa::assets::TextureAsset>{};
        wallTexture = fa::assets::AssetHandle<fa::assets::TextureAsset>{};
        soilTexture = fa::assets::AssetHandle<fa::assets::TextureAsset>{};
        markerTexture = fa::assets::AssetHandle<fa::assets::TextureAsset>{};
        soilWetTexture = fa::assets::AssetHandle<fa::assets::TextureAsset>{};
        cropSeedTexture = fa::assets::AssetHandle<fa::assets::TextureAsset>{};
        cropSproutTexture = fa::assets::AssetHandle<fa::assets::TextureAsset>{};
        cropYoungTexture = fa::assets::AssetHandle<fa::assets::TextureAsset>{};
        cropMatureTexture = fa::assets::AssetHandle<fa::assets::TextureAsset>{};
        chestTexture = fa::assets::AssetHandle<fa::assets::TextureAsset>{};
        slimeTexture = fa::assets::AssetHandle<fa::assets::TextureAsset>{};
        // Catalog-driven handles must go before the manager that owns their storage.
        namedTextureHandles.clear();
        namedTextureAssets.clear();
        assets.reset();
        uploader.reset();
        fileSystem = nullptr;
        // M09 borrows its handles; drop the context first, then the two textures.
        ui.reset();
        if (renderer != nullptr) {
            renderer->destroyTexture(uiFont.texture);
            renderer->destroyTexture(uiWhiteTexture);
        }
        uiFont.texture = fa::render::TextureHandle{};
        uiWhiteTexture = fa::render::TextureHandle{};
        if (renderer != nullptr) {
            renderer->shutdown();
            renderer.reset();
        }
    }

    /// \brief Stops the audio device and releases the generated clips.
    void shutdownAudio() {
        if (audioEngine != nullptr) {
            audioEngine->shutdown();
            audioEngine.reset();
        }
        toneClip.reset();
        loopClip.reset();
        audioClips.clear();
        audioClipDefs.clear();
        toneHandle = fa::audio::kInvalidSoundHandle;
        loopHandle = fa::audio::kInvalidSoundHandle;
    }

    void onEvents(std::span<const fa::platform::PlatformEvent> events) override {
#if defined(FA_ENABLE_DEBUG_TOOLS)
        // While the console owns the keyboard, every raw gameplay hotkey below is
        // skipped: typing "time" must not trigger I/J/E/F2 side effects.
        const bool consoleGrab = debugTools != nullptr && debugTools->consoleOpen();
#else
        const bool consoleGrab = false;
#endif
#if defined(FA_ENABLE_EDITOR)
        for (const fa::platform::PlatformEvent& event : events) {
            if (event.type != fa::platform::PlatformEventType::KeyDown) {
                continue;
            }
            if (consoleGrab) {
                break;
            }
            if (event.scancode == fa::platform::Scancode::F2) {
                if (editor != nullptr) {
                    editor->toggle();
                }
                continue;
            }
            if (editor == nullptr || !editor->active()) {
                continue;
            }
            switch (event.scancode) {
                case fa::platform::Scancode::Digit1:
                    editor->setTool(fa::editor::EditorTool::Brush);
                    break;
                case fa::platform::Scancode::Digit2:
                    editor->setTool(fa::editor::EditorTool::Rect);
                    break;
                case fa::platform::Scancode::Digit3:
                    editor->setTool(fa::editor::EditorTool::Fill);
                    break;
                case fa::platform::Scancode::Digit4:
                    editor->setTool(fa::editor::EditorTool::Erase);
                    break;
                case fa::platform::Scancode::Digit5:
                    editor->setTool(fa::editor::EditorTool::Picker);
                    break;
                case fa::platform::Scancode::LeftBracket:
                    editor->setLayer(editor->stats().layer - 1);
                    break;
                case fa::platform::Scancode::RightBracket:
                    editor->setLayer(editor->stats().layer + 1);
                    break;
                case fa::platform::Scancode::Z:
                    editor->undo();
                    break;
                case fa::platform::Scancode::Y:
                    editor->redo();
                    break;
                case fa::platform::Scancode::S:
                    saveEditorMap();
                    break;
                case fa::platform::Scancode::L:
                    loadEditorMap();
                    break;
                case fa::platform::Scancode::Escape:
                    editor->setActive(false);
                    break;
                default:
                    break;
            }
        }
#endif
#if defined(FA_ENABLE_EDITOR)
        const bool editorActive = editor != nullptr && editor->active();
#else
        const bool editorActive = false;
#endif
        const bool dialogueWasOpen = gameScene != nullptr && gameScene->narrative().dialogueActive();
#if defined(FA_ENABLE_EDITOR)
        if (editorWindowOpen && !consoleGrab) {
            for (const fa::platform::PlatformEvent& event : events) {
                if (event.type == fa::platform::PlatformEventType::KeyDown &&
                    event.scancode == fa::platform::Scancode::Escape) {
                    closeVisualEditor();
                    break;
                }
            }
        }
#endif
        if (!editorActive && dialogueWasOpen && !consoleGrab) {
            for (const fa::platform::PlatformEvent& event : events) {
                if (event.type != fa::platform::PlatformEventType::KeyDown) {
                    continue;
                }
                switch (event.scancode) {
                    case fa::platform::Scancode::Digit1:
                        chooseDialogue(0);
                        break;
                    case fa::platform::Scancode::Digit2:
                        chooseDialogue(1);
                        break;
                    case fa::platform::Scancode::Digit3:
                        chooseDialogue(2);
                        break;
                    case fa::platform::Scancode::Space:
                        advanceDialogue();
                        break;
                    case fa::platform::Scancode::Escape:
                        cancelDialogue();
                        break;
                    default:
                        break;
                }
            }
        }

        // Escape opens/closes the pause overlay; the browser-style slot panel
        // consumes it first so it never resumes gameplay by accident.
        const bool dialogueOpen = gameScene != nullptr && gameScene->narrative().dialogueActive();
        if (playMode && flow != nullptr && !editorActive && !editorWindowOpen && !dialogueOpen && !dialogueWasOpen &&
            !consoleGrab) {
            for (const fa::platform::PlatformEvent& event : events) {
                if (event.type != fa::platform::PlatformEventType::KeyDown ||
                    event.scancode != fa::platform::Scancode::Escape) {
                    continue;
                }
                if (questLogOpen) {
                    closeQuestLog();
                    continue;
                }
                if (inventoryOpen) {
                    closeInventoryPanel();
                    continue;
                }
                if (shopOpen) {
                    shopOpen = false;
                    static_cast<void>(flow->exitDialogue());
                    continue;
                }
                if (slotsOpen) {
                    slotsOpen = false;
                    continue;
                }
                if (flow->state() == fa::flow::AppState::Playing) {
                    flow->requestPause(true);
                } else if (flow->state() == fa::flow::AppState::Paused) {
                    flow->requestPause(false);
                }
            }
        }

        // I opens the backpack panel; like the shop, it pauses via Dialogue.
        if (playMode && flow != nullptr && !editorActive && !dialogueOpen && !shopOpen && !inventoryOpen &&
            !questLogOpen && !consoleGrab && flow->state() == fa::flow::AppState::Playing) {
            for (const fa::platform::PlatformEvent& event : events) {
                if (event.type == fa::platform::PlatformEventType::KeyDown &&
                    event.scancode == fa::platform::Scancode::I) {
                    openInventoryPanel();
                    break;
                }
            }
        }

        // J opens the quest log; it uses the same Dialogue pause as the backpack.
        if (playMode && flow != nullptr && !editorActive && !dialogueOpen && !shopOpen && !inventoryOpen &&
            !questLogOpen && !consoleGrab && flow->state() == fa::flow::AppState::Playing) {
            for (const fa::platform::PlatformEvent& event : events) {
                if (event.type == fa::platform::PlatformEventType::KeyDown &&
                    event.scancode == fa::platform::Scancode::J) {
                    openQuestLog();
                    break;
                }
            }
        }

        // E fires the character skill (teleport). The weapon skill moved to RMB.
        if (playMode && flow != nullptr && !editorActive && !dialogueOpen && !shopOpen && !inventoryOpen &&
            !questLogOpen && !consoleGrab && flow->state() == fa::flow::AppState::Playing) {
            for (const fa::platform::PlatformEvent& event : events) {
                if (event.type == fa::platform::PlatformEventType::KeyDown &&
                    event.scancode == fa::platform::Scancode::E) {
                    if (gameScene != nullptr) {
                        const std::string error = gameScene->requestTeleport();
                        setPlayMessage(error.empty() ? "瞬移！" : error);
                    }
                    break;
                }
            }
        }

#if defined(FA_ENABLE_DEBUG_TOOLS)
        /// The console swallows text/keys while it is open; everything else still
        /// reaches M07, so closing the console restores gameplay input exactly.
        filteredEvents.clear();
        for (const fa::platform::PlatformEvent& event : events) {
            if (debugTools != nullptr && debugTools->handleEvent(event)) {
                continue;
            }
            filteredEvents.push_back(event);
        }
        input.poll(filteredEvents);
        forwardedEvents += filteredEvents.size();
#else
        input.poll(events);
        forwardedEvents += events.size();
#endif
        for (const fa::platform::PlatformEvent& event : events) {
            switch (event.type) {
                case fa::platform::PlatformEventType::Quit:
                    ++quitEvents;
                    break;
                case fa::platform::PlatformEventType::WindowResized:
                    ++resizeEvents;
                    std::printf("resized -> %dx%d\n", event.width, event.height);
                    break;
                default:
                    break;
            }
        }
    }

    void onFixedUpdate(const fa::time::FrameContext& context) override {
        ++fixedSteps;
        lastTick = context.fixedStepIndex;
        const fa::core::Vec2 axis = input.moveAxis();
        lastMoveAxis = axis;
        if (axis.x != 0.0f || axis.y != 0.0f) {
            ++movingSteps;
        }
        if (gameScene != nullptr) {
            if (playMode) {
                // Play mode uses the real input axis; only the scripted smoke needs
                // an artificial move to keep the physics path exercised in CI.
                gameScene->clearScriptedMove();
            } else {
                const fa::core::Vec2 scripted = (axis.x == 0.0f && axis.y == 0.0f) ? fa::core::Vec2{0.35f, 0.2f} : axis;
                gameScene->setScriptedMove(scripted);
            }
        }
        if (gameplayActive()) {
            if (portalCooldown > 0.0f) {
                portalCooldown = std::max(0.0f, portalCooldown - static_cast<float>(context.fixedDeltaSeconds));
            }
            static_cast<void>(checkPortal());
            if (hazardCooldown > 0.0f) {
                hazardCooldown = std::max(0.0f, hazardCooldown - static_cast<float>(context.fixedDeltaSeconds));
            }
            static_cast<void>(tickHazard());
            // A lethal hazard changes the flow state to GameOver inside tickHazard;
            // do not run one last player/physics step after that.
            if (gameplayActive()) {
                stack.fixedUpdate(context);
                if (gameScene != nullptr) {
                    gameScene->advanceAnimation(static_cast<float>(context.fixedDeltaSeconds));
                    gameScene->advanceCombat(static_cast<float>(context.fixedDeltaSeconds));
                    gameScene->advanceAi(static_cast<float>(context.fixedDeltaSeconds));
                }
                processPlayerActions();
                processCombatEvents();
                checkPlayerDeath();
                if (gameplayActive()) {
                    collectLoot();
                    processNarrativeEvents();
                }
            }
        }
        if (soakMode && fixedSteps % 600 == 0) {
            // Every simulated 10 s: write, reload and reapply the save.
            runSoakCycle();
        }
        if (run != nullptr) {
            run->beginTick(context.fixedStepIndex);
        }
    }

    void onVariableUpdate(const fa::time::FrameContext& context) override {
        lastRealDelta = context.realDeltaSeconds;
        // Keep the run log close to live without rewriting the file every frame.
        if (runLogFile != nullptr && ++logFlushTick >= 60) {
            logFlushTick = 0;
            fa::core::flushLogs();
        }
        if (stressMode && context.realDeltaSeconds > 0.0f) {
            stressFrameTimes.push_back(static_cast<float>(context.realDeltaSeconds));
        }
        if (calendar != nullptr) {
            calendar->advance(static_cast<float>(context.realDeltaSeconds));
            const float now = calendar->timeOfDay();
            if (playMode && economy != nullptr && gameplayActive() && lastEconomyTimeOfDay >= 0.0f) {
                float delta = now - lastEconomyTimeOfDay;
                if (delta < 0.0f) {
                    delta += 24.0f;
                }
                // Accumulate fractional minutes: at 60 FPS a 1 s/game-minute clock
                // advances by only 0.0167 minutes per frame, so rounding here would
                // never reach a whole minute.
                economyMinuteCarry += delta * 60.0f;
                const int minutes = static_cast<int>(economyMinuteCarry);
                if (minutes > 0) {
                    economy->update(minutes);
                    economyMinuteCarry -= static_cast<float>(minutes);
                }
            }
            lastEconomyTimeOfDay = now;
            syncEconomyCalendar();
            // The composition root owns forwarding; counting and clearing here keeps
            // the internal event vector from growing for the life of the process.
            calendarEvents += calendar->events().size();
            calendar->clearEvents();
        }

        if (playMode && playerInventory != nullptr && gameplayActive()) {
            for (std::size_t index = 0; index < m_hotbarActions.size(); ++index) {
                if (!input.wasPressed(m_hotbarActions[index])) {
                    continue;
                }
                static_cast<void>(playerInventory->hotbar().select(static_cast<int>(index)));
                const fa::inventory::ItemStack& selected = playerInventory->hotbar().selectedStack();
                const fa::content::ItemDef* selectedDef = selected.empty() ? nullptr : content.item(selected.id);
                setPlayMessage("栏位 " + std::to_string(index + 1) + ": " +
                               (selectedDef != nullptr ? selectedDef->displayName : "空"));
            }
            const fa::core::Vec2 wheel = input.scroll();
            if (wheel.y > 0.0f) {
                static_cast<void>(playerInventory->hotbar().cycle(-1));
            } else if (wheel.y < 0.0f) {
                static_cast<void>(playerInventory->hotbar().cycle(1));
            }

            if (gameScene != nullptr) {
                const fa::interaction::GridTarget target = gameScene->frontTarget();
                if (target.valid) {
                    gameScene->setCursorWorld(fa::core::center(gameScene->mapTileRect(target.cell)));
                }
            }
        }
        updateCombatFeedback(static_cast<float>(context.realDeltaSeconds));
        if (playMessageTimer > 0.0f) {
            playMessageTimer = std::max(0.0f, playMessageTimer - static_cast<float>(context.realDeltaSeconds));
        }
        if (shopMessageTimer > 0.0f) {
            shopMessageTimer = std::max(0.0f, shopMessageTimer - static_cast<float>(context.realDeltaSeconds));
        }
        if (inventoryMessageTimer > 0.0f) {
            inventoryMessageTimer =
                std::max(0.0f, inventoryMessageTimer - static_cast<float>(context.realDeltaSeconds));
        }
        stack.variableUpdate(context);
    }

    void onRender(const fa::time::FrameContext&) override {
        ++renderedFrames;
        if (audioEngine != nullptr) {
            if (!audioOneShotStopped && renderedFrames >= 2) {
                audioEngine->stop(toneHandle, 0.05f);
                audioOneShotStopped = true;
            }
            audioEngine->update();
        }
        lastMouse = input.mousePosition();
        if (run != nullptr) {
            run->endFrame();
        }
        renderFrame();
#if defined(FA_ENABLE_DEBUG_TOOLS)
        updateDebugStats();
#endif
        input.endFrame();
    }

    /// Declared before `run` so the input system outlives nothing that needs it and
    /// construction order stays obvious.
    fa::input::InputSystem input;
    EventsAndJobsRun* run = nullptr;
    /// Declared first so it is destroyed last: the cache above releases GPU textures
    /// through `uploader`, which reaches into the renderer.
    std::unique_ptr<fa::render::IRenderer> renderer;
    fa::content::ContentDatabase content;
    fa::narrative::NarrativeDatabase narrative;
    fa::economy::ShopDatabase shops;
    fa::assets::AssetCatalog catalog;
    /// Borrowed from the platform, which owns the only file system in the run.
    fa::platform::IFileSystem* fileSystem = nullptr;
    std::unique_ptr<AssetUploaderAdapter> uploader;
    std::unique_ptr<fa::assets::IAssetManager> assets;
    fa::assets::AssetHandle<fa::assets::TextureAsset> grassTexture;
    fa::assets::AssetHandle<fa::assets::TextureAsset> wallTexture;
    fa::assets::AssetHandle<fa::assets::TextureAsset> soilTexture;
    fa::assets::AssetHandle<fa::assets::TextureAsset> markerTexture;
    fa::assets::AssetHandle<fa::assets::TextureAsset> soilWetTexture;
    fa::assets::AssetHandle<fa::assets::TextureAsset> cropSeedTexture;
    fa::assets::AssetHandle<fa::assets::TextureAsset> cropSproutTexture;
    fa::assets::AssetHandle<fa::assets::TextureAsset> cropYoungTexture;
    fa::assets::AssetHandle<fa::assets::TextureAsset> cropMatureTexture;
    fa::assets::AssetHandle<fa::assets::TextureAsset> chestTexture;
    fa::assets::AssetHandle<fa::assets::TextureAsset> slimeTexture;
    /// \brief Catalog-driven handles; scenes only keep the raw GPU handles.
    std::unordered_map<std::string, fa::assets::AssetHandle<fa::assets::TextureAsset>> namedTextureAssets;
    std::unordered_map<std::string, fa::render::TextureHandle> namedTextureHandles;
    /// \brief `data/animations.json`: four-way player walk clip settings.
    PlayerWalkAnimConfig playerWalkConfig;
    /// \brief `data/animations.json`: rowing-in-boat clip settings (riding animation).
    PlayerWalkAnimConfig boatWalkConfig;
    /// \brief `data/animations.json`: idle breathing clip (player.png sheet).
    PlayerWalkAnimConfig idleAnimConfig;
    /// \brief `data/skills.json`: E teleport and Q sword-qi tuning.
    SkillConfig skillConfig;
    /// \brief Session state shared by every map scene; declared before the stack so
    /// it outlives the scenes that borrow it.
    GameState gameState;
    /// Owns the scene graph. Cleared before the renderer goes away: the scenes only
    /// store texture handles, but their sprites should not outlive the GPU textures.
    fa::scene::SceneStack stack;
    GameScene* gameScene = nullptr;
#if defined(FA_ENABLE_EDITOR)
    /// \brief Declared after `stack` so it is destroyed before the scene's map.
    std::unique_ptr<fa::editor::EditorOverlay> editor;
    /// \brief Visual editor opened from the main menu; owns only UI state.
    std::unique_ptr<fa::editor::VisualEditor> visualEditor;
    /// \brief Placement rows the editor is authoring for `editorMapId`.
    std::vector<fa::editor::MapPlacement> editorPlacements;
    /// \brief Map file id the editor session belongs to; scopes the rows it may rewrite.
    std::string editorMapId;
    EditorRunStats editorStats;
#endif
    /// \brief Map file the live scene was loaded from, so the editor saves back to it.
    /// Declared outside the editor guard because `makeGameScene` records it in every build.
    std::string currentMapFile = "maps/farm.json";
    /// \brief True while the M26 visual editor owns the frame; always false without it.
    bool editorWindowOpen = false;
    fa::render::RenderQueue renderQueue;
    std::size_t extractedItems = 0;
    /// M09 holds only borrowed handles; `uiFont` is declared before `ui` so the
    /// context (which references the atlas) is destroyed first.
    fa::ui::FontAtlas uiFont;
    /// \brief Which atlas is live: `baked` (Chinese-capable) or `builtin` (ASCII only).
    const char* fontSource = "builtin";
    fa::render::TextureHandle uiWhiteTexture;
    std::unique_ptr<fa::ui::UiContext> ui;
    bool showHud = true;
    float sliderValue = 2.0f;
    std::size_t uiItems = 0;
    std::size_t uiPresses = 0;
    /// M18 containers outlive the renderer; `content` is declared before them, so
    /// they are destroyed first and never keep a dangling database pointer.
    std::unique_ptr<fa::inventory::PlayerInventory> playerInventory;
    /// \brief M24 wallet/shop/crafting runtime over the player backpack.
    std::unique_ptr<fa::economy::EconomyService> economy;
    EconomyRunStats economyStats;
    /// The handler must outlive the service that stores a non-owning pointer to it.
    std::unique_ptr<ConsumableUseHandler> consumable;
    std::unique_ptr<fa::inventory::ItemUseService> itemUse;
    InventoryRunStats inventoryStats;
    /// \brief M08 state. The engine is shut down explicitly before the platform.
    std::unique_ptr<fa::audio::AudioEngine> audioEngine;
    std::shared_ptr<const fa::audio::AudioClip> toneClip;
    std::shared_ptr<const fa::audio::AudioClip> loopClip;
    /// \brief `data/audio.json` content: definitions plus generated PCM.
    std::unordered_map<std::string, AudioClipDef> audioClipDefs;
    std::unordered_map<std::string, std::shared_ptr<const fa::audio::AudioClip>> audioClips;
    fa::audio::SoundHandle toneHandle = fa::audio::kInvalidSoundHandle;
    fa::audio::SoundHandle loopHandle = fa::audio::kInvalidSoundHandle;
    bool audioOneShotStopped = false;
    /// \brief M14 slots; the service borrows the platform file system.
    std::unique_ptr<fa::save::SaveService> saveService;
    SaveRunStats saveStats;
    /// \brief M22 calendar and placeholder point lights.
    std::unique_ptr<fa::world::WorldCalendar> calendar;
    fa::world::LightSet lights;
    std::uint64_t calendarEvents = 0;
    /// \brief Last sampled game time, used to advance processing jobs by minutes.
    float lastEconomyTimeOfDay = -1.0f;
    /// \brief Fractional game minutes not yet handed to `EconomyService::update`.
    float economyMinuteCarry = 0.0f;
    /// \brief M25 state machine; declared after its borrowed services.
    std::unique_ptr<fa::flow::GameFlow> flow;
    FlowRunStats flowStats;
#if defined(FA_ENABLE_DEBUG_TOOLS)
    /// \brief M27 facade; absent in Release because the project reference is absent.
    std::unique_ptr<fa::debug::DebugTools> debugTools;
    /// \brief Events that were not consumed by the console, forwarded to M07.
    std::vector<fa::platform::PlatformEvent> filteredEvents;
#endif
    std::array<fa::input::ActionId, 9> m_hotbarActions{};
    fa::core::Rng m_lootRng{0x5eed1234ULL};
    std::string playMessage;
    float playMessageTimer = 0.0f;
    /// \brief Penalty applied by the most recent death, shown on the GameOver panel.
    int lastDeathPenalty = 0;
    std::string lastDeathReason = "荆棘";
    /// \brief Rising damage numbers; app-local because they never enter a save.
    std::vector<FloatingDamage> damageTexts;
    int quitEvents = 0;
    int resizeEvents = 0;
    int renderedFrames = 0;
    std::uint64_t forwardedEvents = 0;
    std::uint64_t movingSteps = 0;
    std::uint64_t fixedSteps = 0;
    std::uint64_t lastTick = 0;
    fa::core::Vec2 lastMoveAxis{};
    fa::core::Vec2 lastMouse{};
    double lastRealDelta = 0.0;

    /// \brief Cost of the most recent frame, for the summary the caller prints.
    const fa::render::RenderStats& lastRenderStats() const { return lastStats; }

    /// \brief Cache counters, snapshotted before the manager is torn down.
    fa::assets::AssetStats assetStats() const { return assets != nullptr ? assets->stats() : fa::assets::AssetStats{}; }

    /// \brief Content counters; the database outlives the renderer.
    fa::content::ContentStats contentStats() const { return content.stats(); }

    /// \brief One concrete content query for the summary: the turnip stack size.
    int contentSampleStack() const {
        const fa::content::ItemDef* item = content.item("item.turnip");
        return item != nullptr ? item->maxStack : 0;
    }

    /// \brief Scene-side counters, snapshotted before the stack is cleared.
    fa::scene::SceneStackStats sceneStackStats() const { return stack.stats(); }
    fa::scene::WorldStats sceneWorldStats() const {
        return gameScene != nullptr ? gameScene->world().stats() : fa::scene::WorldStats{};
    }
    std::size_t sceneGroundTiles() const { return gameScene != nullptr ? gameScene->groundTiles() : 0; }
    fa::tilemap::TilemapStats sceneTilemapStats() const {
        return gameScene != nullptr ? gameScene->tilemapStats() : fa::tilemap::TilemapStats{};
    }
    fa::tilemap::TileExtractStats sceneTileExtractStats() const {
        return gameScene != nullptr ? gameScene->tileExtractStats() : fa::tilemap::TileExtractStats{};
    }
    InteractionRunStats sceneInteractionStats() const {
        return gameScene != nullptr ? gameScene->interactionRunStats() : InteractionRunStats{};
    }
    fa::player::PlayerStats scenePlayerStats() const {
        return gameScene != nullptr ? gameScene->playerStats() : fa::player::PlayerStats{};
    }
    fa::player::PlayerAnimationParams scenePlayerAnimation() const {
        return gameScene != nullptr ? gameScene->playerAnimation() : fa::player::PlayerAnimationParams{};
    }
    InventoryRunStats sceneInventoryStats() const { return inventoryStats; }
    EconomyRunStats sceneEconomyStats() const { return economyStats; }
    AnimationRunStats sceneAnimationStats() const {
        return gameScene != nullptr ? gameScene->animationRunStats() : AnimationRunStats{};
    }
    CombatRunStats sceneCombatStats() const {
        return gameScene != nullptr ? gameScene->combatRunStats() : CombatRunStats{};
    }
    FarmingRunStats sceneFarmingStats() const {
        if (gameScene == nullptr) {
            return FarmingRunStats{};
        }
        if (playMode) {
            FarmingRunStats live;
            live.stats = gameScene->farmingStats();
            return live;
        }
        return gameScene->farmingRunStats();
    }
    AiRunStats sceneAiStats() const { return gameScene != nullptr ? gameScene->aiRunStats() : AiRunStats{}; }
    NarrativeRunStats sceneNarrativeStats() const {
        return gameScene != nullptr ? gameScene->narrativeRunStats() : NarrativeRunStats{};
    }
#if defined(FA_ENABLE_EDITOR)
    EditorRunStats sceneEditorStats() const {
        EditorRunStats result = editorStats;
        if (editor != nullptr) {
            const fa::editor::EditorStats stats = editor->stats();
            result.active = stats.active;
            result.tool = stats.tool;
            result.layer = stats.layer;
            result.brush = stats.brush.value;
            result.undoDepth = stats.undoDepth;
            result.redoDepth = stats.redoDepth;
            result.previewCells = stats.previewCells;
            result.commands = stats.commands;
            result.undos = stats.undos;
            result.redos = stats.redos;
            result.dirty = stats.dirty;
            result.mapWidth = stats.mapWidth;
            result.mapHeight = stats.mapHeight;
        }
        return result;
    }
#endif
    fa::audio::AudioStats audioStats() const {
        return audioEngine != nullptr ? audioEngine->stats() : fa::audio::AudioStats{};
    }
    float scenePlayerStamina() const { return gameScene != nullptr ? gameScene->playerStamina() : 0.0f; }
    fa::physics::PhysicsStats scenePhysicsStats() const {
        return gameScene != nullptr ? gameScene->physicsStats() : fa::physics::PhysicsStats{};
    }
    float sceneProbeDistance() const { return gameScene != nullptr ? gameScene->probeDistance() : -1.0f; }
    fa::core::Vec2 sceneMarkerPosition() const {
        return gameScene != nullptr ? gameScene->markerPosition() : fa::core::Vec2{};
    }
    fa::core::Vec2 cameraPosition() const { return camera.position(); }
    fa::render::CameraFollowConfig cameraFollowConfig() const { return camera.followConfig(); }
    std::size_t sceneSystemCount() const { return gameScene != nullptr ? gameScene->systems().size() : 0; }
    /// Includes the extended table so the marker can tell a baked CJK atlas apart from the
    /// built-in 95-glyph ASCII fallback.
    std::size_t uiFontGlyphCount() const { return uiFont.glyphs.size() + uiFont.extended.size(); }
    int uiAtlasWidth() const { return uiFont.width; }
    int uiAtlasHeight() const { return uiFont.height; }
    std::size_t uiDrawnItems() const { return uiItems; }
    std::size_t uiPressCount() const { return uiPresses; }
    bool uiHudVisible() const { return showHud; }
    float uiSliderValue() const { return sliderValue; }

private:
    /// \brief Draws the backpack panel: one stack moves per click.
    static constexpr std::array<const char*, 7> kInventoryFilters{
        "全部", "种子", "作物", "食物", "工具", "武器", "其他"};

    /// \brief Whether \p def belongs to the selected backpack filter.
    [[nodiscard]] static bool itemMatchesInventoryFilter(const fa::content::ItemDef* def, int filter) {
        if (filter <= 0) {
            return true;
        }
        const std::string_view category = def != nullptr ? std::string_view(def->category) : std::string_view{};
        switch (filter) {
            case 1:
                return category == "seed";
            case 2:
                return category == "crop";
            case 3:
                return category == "food";
            case 4:
                return category == "tool";
            case 5:
                return category == "weapon";
            case 6:
                return category != "seed" && category != "crop" && category != "food" && category != "tool" &&
                       category != "weapon";
            default:
                return true;
        }
    }

    /// \brief The catalog texture that icons \p itemId, or an invalid handle.
    ///
    /// `item.<x>` maps to `tex.item.<x>`. The three seeds have no dedicated icon and
    /// reuse their `tex.crop.<x>_seed` planting sprite, which is the same pixels.
    [[nodiscard]] fa::render::TextureHandle itemIconTexture(std::string_view itemId) const {
        static constexpr std::string_view kPrefix = "item.";
        if (!itemId.starts_with(kPrefix)) {
            return {};
        }
        const std::string_view suffix = itemId.substr(kPrefix.size());
        for (const std::string_view table : {std::string_view("tex.item."), std::string_view("tex.crop.")}) {
            const auto found = namedTextureHandles.find(std::string(table) + std::string(suffix));
            if (found != namedTextureHandles.end() && found->second.isValid()) {
                return found->second;
            }
        }
        return {};
    }

    /// \brief Draws one inventory row: 16 px icon, then `name xN`.
    ///
    /// Rows without art get the magenta placeholder block from the art policy so a
    /// missing `tex.item.*` entry is visible instead of looking like an intentional gap.
    void inventoryRow(float iconX,
                      float textX,
                      float rowY,
                      float textWidth,
                      const fa::inventory::ItemStack& itemStack,
                      const fa::ui::TextStyle& label) {
        const fa::render::TextureHandle icon = itemIconTexture(itemStack.id.name());
        const fa::core::Rect slot{iconX, rowY, 16.0f, 16.0f};
        if (icon.isValid()) {
            ui->icon(slot, icon);
        } else {
            ui->panel(slot, fa::core::Color{1.0f, 0.0f, 1.0f, 1.0f});
        }
        const fa::content::ItemDef* def = content.item(itemStack.id);
        char line[64]{};
        std::snprintf(line,
                      sizeof(line),
                      "%s x%d",
                      def != nullptr ? def->displayName.c_str() : itemStack.id.name().c_str(),
                      itemStack.count);
        ui->text(line, fa::core::makeRect(textX, rowY + 4.0f, textWidth, 8.0f), label);
    }

    void buildInventoryPanel(const fa::ui::UiTheme& theme) {
        if (!inventoryOpen || ui == nullptr || flow == nullptr || playerInventory == nullptr) {
            return;
        }
        if (flow->state() != fa::flow::AppState::Dialogue) {
            inventoryOpen = false;
            return;
        }

        const float scale = config.ui.scale;
        fa::ui::TextStyle title;
        title.color = theme.text;
        title.scale = 2.0f * scale;
        fa::ui::TextStyle label;
        label.color = theme.text;
        label.scale = scale;

        // Sized for the 640x360 virtual screen: the old 540x300 panel covered 84% of it.
        const fa::core::Rect panel = centeredRect(420.0f, 250.0f);
        fa::ui::TextStyle dim = label;
        dim.color = theme.textDisabled;
        static constexpr float kMargin = 10.0f;
        static constexpr float kColPitch = 200.0f;
        static constexpr float kColWidth = 194.0f;
        static constexpr float kRowPitch = 16.0f;
        static constexpr float kRowsTop = 74.0f;
        static constexpr float kActionsY = 224.0f;
        const float colX[2] = {panel.x + kMargin, panel.x + kMargin + kColPitch};
        ui->panel(panel, theme.panel);
        ui->border(panel, theme.panelBorder);
        ui->text("背包", fa::core::makeRect(panel.x + kMargin, panel.y + 6.0f, 200.0f, 20.0f), title);

        for (std::size_t index = 0; index < kInventoryFilters.size(); ++index) {
            char id[24]{};
            std::snprintf(id, sizeof(id), "inv.filter.%zu", index);
            if (ui->button(fa::ui::UiId::fromName(id),
                           kInventoryFilters[index],
                           fa::core::makeRect(
                               panel.x + kMargin + static_cast<float>(index) * 56.0f, panel.y + 30.0f, 54.0f, 16.0f))
                    .pressed) {
                ++uiPresses;
                inventoryCategory = static_cast<int>(index);
                inventoryPage = 0;
            }
        }

        std::vector<int> bagSlots;
        fa::inventory::Inventory& bag = playerInventory->backpack();
        for (int slot = 0; slot < bag.slotCount(); ++slot) {
            const fa::inventory::ItemStack& itemStack = bag.at(slot);
            if (!itemStack.empty() && itemMatchesInventoryFilter(content.item(itemStack.id), inventoryCategory)) {
                bagSlots.push_back(slot);
            }
        }
        constexpr int kPageSize = 8;
        const int pageCount = std::max(1, (static_cast<int>(bagSlots.size()) + kPageSize - 1) / kPageSize);
        if (const fa::core::Vec2 wheel = input.scroll(); wheel.y > 0.0f) {
            --inventoryPage;
        } else if (wheel.y < 0.0f) {
            ++inventoryPage;
        }
        inventoryPage = std::clamp(inventoryPage, 0, pageCount - 1);

        char filterText[72]{};
        const int filterIndex = std::clamp(inventoryCategory, 0, static_cast<int>(kInventoryFilters.size()) - 1);
        std::snprintf(filterText,
                      sizeof(filterText),
                      "筛选 %s   第 %d/%d 页",
                      kInventoryFilters[static_cast<std::size_t>(filterIndex)],
                      inventoryPage + 1,
                      pageCount);
        ui->text(filterText, fa::core::makeRect(panel.x + kMargin, panel.y + 50.0f, 200.0f, 10.0f), label);
        ui->text("背包 → 快捷栏", fa::core::makeRect(colX[0], panel.y + 62.0f, kColWidth, 10.0f), label);
        ui->text("快捷栏 → 背包", fa::core::makeRect(colX[1], panel.y + 62.0f, kColWidth, 10.0f), label);

        int moveBag = -1;
        int moveHot = -1;
        constexpr float kTextX = 18.0f;
        constexpr float kMoveWidth = 44.0f;
        constexpr float kTextWidth = kColWidth - kTextX - kMoveWidth;
        float bagY = panel.y + kRowsTop;
        int shown = 0;
        const int first = inventoryPage * kPageSize;
        for (int index = first; index < static_cast<int>(bagSlots.size()) && shown < kPageSize; ++index, ++shown) {
            const int slot = bagSlots[static_cast<std::size_t>(index)];
            const fa::inventory::ItemStack& itemStack = bag.at(slot);
            inventoryRow(colX[0], colX[0] + kTextX, bagY, kTextWidth, itemStack, label);

            char id[32]{};
            std::snprintf(id, sizeof(id), "inv.bag.%d", slot);
            if (ui->button(fa::ui::UiId::fromName(id),
                           "移动",
                           fa::core::makeRect(colX[0] + kColWidth - kMoveWidth, bagY, kMoveWidth, 16.0f))
                    .pressed) {
                ++uiPresses;
                moveBag = slot;
            }
            bagY += kRowPitch;
        }
        if (shown == 0) {
            ui->text("空", fa::core::makeRect(colX[0], bagY + 2.0f, kColWidth, 10.0f), label);
        }

        float hotY = panel.y + kRowsTop;
        int hotShown = 0;
        fa::inventory::Inventory& hotbarInventory = playerInventory->hotbar().inventory();
        for (int slot = 0; slot < hotbarInventory.slotCount() && hotShown < 9; ++slot) {
            const fa::inventory::ItemStack& itemStack = hotbarInventory.at(slot);
            if (itemStack.empty()) {
                continue;
            }
            inventoryRow(colX[1], colX[1] + kTextX, hotY, kTextWidth, itemStack, label);

            char id[32]{};
            std::snprintf(id, sizeof(id), "inv.hot.%d", slot);
            if (ui->button(fa::ui::UiId::fromName(id),
                           "移动",
                           fa::core::makeRect(colX[1] + kColWidth - kMoveWidth, hotY, kMoveWidth, 16.0f))
                    .pressed) {
                ++uiPresses;
                moveHot = slot;
            }
            hotY += kRowPitch;
            ++hotShown;
        }
        if (hotShown == 0) {
            ui->text("空", fa::core::makeRect(colX[1], hotY + 2.0f, kColWidth, 10.0f), label);
        }

        if (ui->button(fa::ui::UiId::fromName("inv.prev"),
                       "上一页",
                       fa::core::makeRect(panel.x + kMargin, panel.y + kActionsY, 54.0f, 16.0f))
                .pressed) {
            ++uiPresses;
            inventoryPage = std::max(0, inventoryPage - 1);
        }
        if (ui->button(fa::ui::UiId::fromName("inv.next"),
                       "下一页",
                       fa::core::makeRect(panel.x + 68.0f, panel.y + kActionsY, 54.0f, 16.0f))
                .pressed) {
            ++uiPresses;
            inventoryPage = std::min(pageCount - 1, inventoryPage + 1);
        }
        ui->text("J / ESC 关闭", fa::core::makeRect(panel.x + 130.0f, panel.y + kActionsY + 3.0f, 150.0f, 10.0f), dim);
        if (ui->button(fa::ui::UiId::fromName("inv.close"),
                       "关闭",
                       fa::core::makeRect(panel.x + 356.0f, panel.y + kActionsY, 54.0f, 16.0f))
                .pressed) {
            ++uiPresses;
            closeInventoryPanel();
            return;
        }

        if (moveBag >= 0) {
            const fa::core::Result<int> moved = playerInventory->moveToHotbar(moveBag, 1);
            setInventoryMessage(moved.isOk() && moved.value() > 0 ? "已移到快捷栏" : "快捷栏已满");
        }
        if (moveHot >= 0) {
            const fa::core::Result<int> moved = playerInventory->moveToBackpack(moveHot, 1);
            setInventoryMessage(moved.isOk() && moved.value() > 0 ? "已移到背包" : "背包已满");
        }
        if (inventoryMessageTimer > 0.0f && !inventoryMessage.empty()) {
            fa::ui::TextStyle message = label;
            message.color = theme.accent;
            ui->text(inventoryMessage, fa::core::makeRect(panel.x + 150.0f, panel.y + 240.0f, 250.0f, 8.0f), message);
        }
    }

    /// \brief Draws the quest log: active quests first, then completed ones.
    void buildQuestLogPanel(const fa::ui::UiTheme& theme) {
        if (!questLogOpen || ui == nullptr || flow == nullptr || gameScene == nullptr) {
            return;
        }
        if (flow->state() != fa::flow::AppState::Dialogue) {
            questLogOpen = false;
            return;
        }

        const float scale = config.ui.scale;
        fa::ui::TextStyle title;
        title.color = theme.text;
        title.scale = 2.0f * scale;
        fa::ui::TextStyle label;
        label.color = theme.text;
        label.scale = scale;
        fa::ui::TextStyle dim = label;
        dim.color = theme.textDisabled;

        const fa::core::Rect panel = centeredRect(520.0f, 280.0f);
        ui->panel(panel, theme.panel);
        ui->border(panel, theme.panelBorder);
        ui->text("任务日志", fa::core::makeRect(panel.x + 12.0f, panel.y + 8.0f, 300.0f, 16.0f), title);

        const fa::narrative::NarrativeService& narrativeState = gameScene->narrative();
        std::vector<fa::narrative::QuestState> states = narrativeState.questStates();
        std::stable_sort(states.begin(),
                         states.end(),
                         [](const fa::narrative::QuestState& lhs, const fa::narrative::QuestState& rhs) {
                             const int rankLeft = lhs.active ? 0 : (lhs.completed ? 1 : 2);
                             const int rankRight = rhs.active ? 0 : (rhs.completed ? 1 : 2);
                             return rankLeft < rankRight;
                         });

        float y = panel.y + 34.0f;
        int rendered = 0;
        for (const fa::narrative::QuestState& state : states) {
            if (rendered >= 3 || y > panel.y + 214.0f) {
                break;
            }
            const fa::narrative::QuestDef* def =
                narrativeState.database() != nullptr ? narrativeState.database()->quest(state.id) : nullptr;
            if (def == nullptr) {
                continue;
            }
            char header[128]{};
            std::snprintf(header,
                          sizeof(header),
                          "%s  %s",
                          state.active ? "进行中" : (state.completed ? "已完成" : "未知"),
                          def->title.c_str());
            fa::ui::TextStyle headerStyle = state.completed ? dim : label;
            ui->text(header, fa::core::makeRect(panel.x + 12.0f, y, panel.w - 24.0f, 8.0f), headerStyle);
            y += 13.0f;
            ui->text(fa::ui::fitToCodepoints(def->description, 20),
                     fa::core::makeRect(panel.x + 20.0f, y, panel.w - 32.0f, 10.0f),
                     dim);
            y += 12.0f;
            for (std::size_t index = 0; index < def->objectives.size() && index < 4; ++index) {
                const fa::narrative::QuestObjective& objective = def->objectives[index];
                const int progress = index < state.progress.size() ? state.progress[index] : 0;
                const fa::content::ItemDef* target = content.item(objective.target);
                const std::string_view kindName = fa::narrative::toString(objective.kind);
                char line[128]{};
                std::snprintf(line,
                              sizeof(line),
                              "  - %.*s %s  %d/%d",
                              static_cast<int>(kindName.size()),
                              kindName.data(),
                              target != nullptr ? target->displayName.c_str() : objective.target.name().c_str(),
                              progress,
                              objective.required);
                ui->text(line, fa::core::makeRect(panel.x + 24.0f, y, panel.w - 36.0f, 8.0f), dim);
                y += 11.0f;
            }
            y += 8.0f;
            ++rendered;
        }
        if (rendered == 0) {
            ui->text("暂无任务 - 去找村长", fa::core::makeRect(panel.x + 12.0f, y, panel.w - 24.0f, 8.0f), dim);
        }

        if (ui->button(fa::ui::UiId::fromName("quest.close"),
                       "关闭 [J]",
                       fa::core::makeRect(panel.x + 412.0f, panel.y + 244.0f, 96.0f, 18.0f))
                .pressed) {
            ++uiPresses;
            closeQuestLog();
            return;
        }
        ui->text("J / ESC 关闭", fa::core::makeRect(panel.x + 12.0f, panel.y + 250.0f, 180.0f, 8.0f), dim);
    }

    /// \brief Draws the merchant panel while it is open.
    void buildShopPanel(const fa::ui::UiTheme& theme) {
        if (!shopOpen || ui == nullptr || flow == nullptr) {
            return;
        }
        if (flow->state() != fa::flow::AppState::Dialogue) {
            shopOpen = false;
            return;
        }

        const float scale = config.ui.scale;
        fa::ui::TextStyle title;
        title.color = theme.text;
        title.scale = 2.0f * scale;
        title.align = fa::ui::TextAlign::Center;
        fa::ui::TextStyle label;
        label.color = theme.text;
        label.scale = scale;

        const fa::core::Rect panel = centeredRect(420.0f, 250.0f);
        ui->panel(panel, theme.panel);
        ui->border(panel, theme.panelBorder);
        ui->text("杂货店", fa::core::makeRect(panel.x + 10.0f, panel.y + 8.0f, panel.w - 20.0f, 16.0f), title);

        const fa::content::ContentId seed = fa::content::ContentId::fromName("item.turnip_seed");
        const fa::content::ContentId turnip = fa::content::ContentId::fromName("item.turnip");
        const fa::content::ContentId soup = fa::content::ContentId::fromName("item.turnip_soup");
        const fa::economy::ShopDef* shop = shops.shop(shopGeneralId());

        const std::string seasonName =
            economy != nullptr ? std::string(seasonLabel(fa::content::toString(economy->season()))) : "春";
        char goldText[96]{};
        std::snprintf(goldText,
                      sizeof(goldText),
                      "金币 %d    季节 %s",
                      economy != nullptr ? economy->gold() : 0,
                      seasonName.c_str());
        ui->text(goldText, fa::core::makeRect(panel.x + 12.0f, panel.y + 30.0f, panel.w - 24.0f, 8.0f), label);

        const auto press = [this](const char* id, const char* text, float x, float y, float w) {
            const bool pressed =
                ui->button(fa::ui::UiId::fromName(id), text, fa::core::makeRect(x, y, w, 18.0f)).pressed;
            if (pressed) {
                ++uiPresses;
            }
            return pressed;
        };

        ui->text("购买", fa::core::makeRect(panel.x + 12.0f, panel.y + 44.0f, 60.0f, 8.0f), label);
        float stockY = panel.y + 56.0f;
        if (shop != nullptr) {
            for (std::size_t index = 0; index < shop->stock.size(); ++index) {
                const fa::economy::ShopEntry& entry = shop->stock[index];
                const fa::content::ItemDef* def = content.item(entry.item);
                const bool available = economy != nullptr && economy->seasonAllows(entry);
                const int price = economy != nullptr ? economy->buyPrice(*shop, entry, 1) : entry.price;
                const int remaining = economy != nullptr ? economy->remaining(shop->id, entry.item) : -1;
                char line[96]{};
                if (!available) {
                    std::snprintf(line,
                                  sizeof(line),
                                  "%s  非当季",
                                  def != nullptr ? def->displayName.c_str() : entry.item.name().c_str());
                } else if (remaining >= 0) {
                    std::snprintf(line,
                                  sizeof(line),
                                  "%s  %d金  库存 %d",
                                  def != nullptr ? def->displayName.c_str() : entry.item.name().c_str(),
                                  price,
                                  remaining);
                } else {
                    std::snprintf(line,
                                  sizeof(line),
                                  "%s  %dg",
                                  def != nullptr ? def->displayName.c_str() : entry.item.name().c_str(),
                                  price);
                }
                fa::ui::TextStyle row = label;
                if (!available) {
                    row.color = theme.textDisabled;
                }
                ui->text(line, fa::core::makeRect(panel.x + 12.0f, stockY, 280.0f, 8.0f), row);
                if (available) {
                    char id[32]{};
                    std::snprintf(id, sizeof(id), "shop.buy.%zu", index);
                    if (press(id, "买 1", panel.x + 320.0f, stockY - 4.0f, 88.0f)) {
                        const fa::core::Status status = buyFromShop(entry.item, 1);
                        setShopMessage(status.isOk() ? "已买 1" : status.error().message);
                    }
                }
                stockY += 20.0f;
            }
        }

        const int turnipCount = playerInventory != nullptr ? playerInventory->backpack().countOf(turnip) +
                                                                 playerInventory->hotbar().inventory().countOf(turnip)
                                                           : 0;
        char turnipText[96]{};
        std::snprintf(turnipText, sizeof(turnipText), "萝卜 %d   卖 1 = %d金", turnipCount, shopSellPrice(turnip));
        ui->text(turnipText, fa::core::makeRect(panel.x + 12.0f, stockY, 280.0f, 8.0f), label);
        if (press("shop.sell", "卖 1", panel.x + 320.0f, stockY - 4.0f, 88.0f)) {
            const fa::core::Status status = sellToShop(turnip, 1);
            setShopMessage(status.isOk() ? "已卖出 1 萝卜" : status.error().message);
        }
        stockY += 22.0f;

        const int soupCount = playerInventory != nullptr ? playerInventory->backpack().countOf(soup) +
                                                               playerInventory->hotbar().inventory().countOf(soup)
                                                         : 0;
        const std::size_t readyJobs = economy != nullptr ? economy->readyJobCount() : 0;
        char soupText[96]{};
        std::snprintf(soupText, sizeof(soupText), "厨房  汤 %d   可取 %zu", soupCount, readyJobs);
        ui->text(soupText, fa::core::makeRect(panel.x + 12.0f, stockY, panel.w - 24.0f, 8.0f), label);
        stockY += 14.0f;
        if (press("shop.cook", "煮汤（2 萝卜）", panel.x + 12.0f, stockY, 190.0f)) {
            const fa::core::Status status = craftTurnipSoup();
            setShopMessage(status.isOk() ? "烹饪中 30 分" : status.error().message);
        }
        if (press("shop.collect", "收取", panel.x + 210.0f, stockY, 90.0f)) {
            const fa::core::Status status = collectReadyCraft();
            setShopMessage(status.isOk() ? "已收取汤" : status.error().message);
        }
        if (press("shop.close", "关闭", panel.x + 320.0f, stockY, 88.0f)) {
            shopOpen = false;
            static_cast<void>(flow->exitDialogue());
            return;
        }

        if (shopMessageTimer > 0.0f && !shopMessage.empty()) {
            fa::ui::TextStyle message = label;
            message.color = theme.accent;
            ui->text(fa::ui::fitToCodepoints(shopMessage, 30),
                     fa::core::makeRect(panel.x + 12.0f, panel.y + 220.0f, panel.w - 24.0f, 10.0f),
                     message);
        }
    }

    /// \brief Draws the main menu, pause overlay and the save-slot list.
    void buildFlowMenus(const fa::ui::UiTheme& theme, fa::flow::AppState state) {
        if (!playMode || flow == nullptr || ui == nullptr) {
            return;
        }
        const bool showMainMenu = state == fa::flow::AppState::MainMenu;
        const bool showPause = state == fa::flow::AppState::Paused;
        const bool showGameOver = state == fa::flow::AppState::GameOver;
        if (!showMainMenu && !showPause && !showGameOver && !slotsOpen) {
            return;
        }

        const float scale = config.ui.scale;
        fa::ui::TextStyle title;
        title.color = theme.text;
        title.scale = 2.0f * scale;
        title.align = fa::ui::TextAlign::Center;
        fa::ui::TextStyle label;
        label.color = theme.text;
        label.scale = scale;
        fa::ui::TextStyle centeredLabel = label;
        centeredLabel.align = fa::ui::TextAlign::Center;
        fa::ui::TextStyle dim = centeredLabel;
        dim.color = theme.textDisabled;

        const auto press = [this](const char* id, const char* text, float x, float y, float w, float h) {
            const bool pressed = ui->button(fa::ui::UiId::fromName(id), text, fa::core::makeRect(x, y, w, h)).pressed;
            if (pressed) {
                ++uiPresses;
            }
            return pressed;
        };
        const auto findSlot = [this](std::string_view name) -> const fa::save::SaveMetadata* {
            for (const fa::save::SaveMetadata& meta : slotList) {
                if (meta.slotName == name) {
                    return &meta;
                }
            }
            return nullptr;
        };

        if (showMainMenu && !slotsOpen) {
            // Main menu: generated art (swap `textures/ui/menu_bg.png` /
            // `menu_button.png` to retheme) + centred title + horizontal buttons.
            const fa::render::TextureHandle bg = uiTexture("tex.ui.menu_bg");
            const fa::render::TextureHandle btnTex = uiTexture("tex.ui.menu_button");
            const fa::core::Rect full = fa::core::makeRect(0.0f, 0.0f, 640.0f, 360.0f);
            if (bg.isValid()) {
                ui->icon(full, bg);
            } else {
                ui->panel(full, theme.panel);
            }

            ui->text("Florian's Adventure", fa::core::makeRect(0.0f, 40.0f, 640.0f, 16.0f), title);
            char dayText[48]{};
            std::snprintf(dayText, sizeof(dayText), "春季 第 %d 天", calendar != nullptr ? calendar->date().day : 1);
            ui->text(dayText, fa::core::makeRect(0.0f, 64.0f, 640.0f, 8.0f), centeredLabel);

            constexpr float kButtonW = 112.0f;
            constexpr float kButtonH = 24.0f;
            constexpr float kButtonGap = 10.0f;
#if defined(FA_ENABLE_EDITOR)
            constexpr int kButtonCount = 5;
#else
            constexpr int kButtonCount = 4;
#endif
            const float rowWidth = static_cast<float>(kButtonCount) * (kButtonW + kButtonGap) - kButtonGap;
            float buttonX = (640.0f - rowWidth) * 0.5f;
            constexpr float kButtonY = 286.0f;
            const auto menuButton = [this, &btnTex](const char* id, const char* text, float x) {
                const bool pressed =
                    ui->button(
                          fa::ui::UiId::fromName(id), text, fa::core::makeRect(x, kButtonY, kButtonW, kButtonH), btnTex)
                        .pressed;
                if (pressed) {
                    ++uiPresses;
                }
                return pressed;
            };
            if (menuButton("menu.new", "开始新游戏", buttonX)) {
                static_cast<void>(flow->startNewGame("play"));
            }
            buttonX += kButtonW + kButtonGap;
            if (menuButton("menu.continue", "继续", buttonX)) {
                continueGame();
            }
            buttonX += kButtonW + kButtonGap;
            if (menuButton("menu.slots", "存档槽", buttonX)) {
                refreshSlots();
                slotsOpen = true;
            }
            buttonX += kButtonW + kButtonGap;
#if defined(FA_ENABLE_EDITOR)
            if (menuButton("menu.editor", "地图编辑器", buttonX)) {
                openVisualEditor();
            }
            buttonX += kButtonW + kButtonGap;
#endif
            if (menuButton("menu.quit", "退出", buttonX)) {
                flow->requestQuit();
                if (requestAppQuit) {
                    requestAppQuit();
                }
            }
            ui->text("G 上床睡觉", fa::core::makeRect(0.0f, 344.0f, 640.0f, 8.0f), dim);
        }

        if (showPause && !slotsOpen) {
            const fa::core::Rect panel = centeredRect(240.0f, 160.0f);
            ui->panel(panel, theme.panel);
            ui->border(panel, theme.panelBorder);
            ui->text("已暂停", fa::core::makeRect(panel.x + 12.0f, panel.y + 8.0f, panel.w - 24.0f, 16.0f), title);

            const float buttonWidth = 150.0f;
            const float buttonX = panel.x + (panel.w - buttonWidth) * 0.5f;
            if (press("pause.resume", "继续", buttonX, panel.y + 36.0f, buttonWidth, 20.0f)) {
                flow->requestPause(false);
            }
            if (press("pause.save", "保存", buttonX, panel.y + 60.0f, buttonWidth, 20.0f)) {
                static_cast<void>(flow->saveGame());
                refreshSlots();
            }
            if (press("pause.slots", "存档槽", buttonX, panel.y + 84.0f, buttonWidth, 20.0f)) {
                refreshSlots();
                slotsOpen = true;
            }
            if (press("pause.menu", "主菜单", buttonX, panel.y + 108.0f, buttonWidth, 20.0f)) {
                static_cast<void>(flow->enterMainMenu());
                slotsOpen = false;
            }
            ui->text("ESC 继续", fa::core::makeRect(panel.x + 12.0f, panel.y + 138.0f, panel.w - 24.0f, 8.0f), dim);
        }

        if (showGameOver && !slotsOpen) {
            const fa::core::Rect panel = centeredRect(300.0f, 200.0f);
            ui->panel(panel, theme.panel);
            ui->border(panel, theme.panelBorder);
            ui->text("你晕倒了", fa::core::makeRect(panel.x + 8.0f, panel.y + 10.0f, panel.w - 16.0f, 16.0f), title);

            char reasonText[64]{};
            std::snprintf(reasonText, sizeof(reasonText), "%s  -%d 金币", lastDeathReason.c_str(), lastDeathPenalty);
            ui->text(
                reasonText, fa::core::makeRect(panel.x + 8.0f, panel.y + 36.0f, panel.w - 16.0f, 8.0f), centeredLabel);
            ui->text("惩罚已扣除", fa::core::makeRect(panel.x + 8.0f, panel.y + 48.0f, panel.w - 16.0f, 8.0f), dim);

            const float buttonWidth = 180.0f;
            const float buttonX = panel.x + (panel.w - buttonWidth) * 0.5f;
            if (press("death.respawn", "在床边复活", buttonX, panel.y + 68.0f, buttonWidth, 20.0f)) {
                static_cast<void>(respawnFromDeath());
            }
            if (press("death.load", "读取自动存档", buttonX, panel.y + 94.0f, buttonWidth, 20.0f)) {
                static_cast<void>(loadFromDeath());
            }
            if (press("death.menu", "主菜单", buttonX, panel.y + 120.0f, buttonWidth, 20.0f)) {
                static_cast<void>(mainMenuFromDeath());
            }
            ui->text("选择如何继续", fa::core::makeRect(panel.x + 8.0f, panel.y + 154.0f, panel.w - 16.0f, 8.0f), dim);
        }

        if (slotsOpen) {
            const fa::core::Rect panel = centeredRect(480.0f, 240.0f);
            ui->panel(panel, theme.panel);
            ui->border(panel, theme.panelBorder);
            ui->text("存档槽", fa::core::makeRect(panel.x + 10.0f, panel.y + 8.0f, 300.0f, 16.0f), title);

            std::vector<std::string> names;
            names.emplace_back(fa::flow::kAutosaveSlot);
            for (int index = 1; index <= fa::flow::kManualSlotCount; ++index) {
                names.push_back(fa::flow::manualSlotName(index));
            }

            const bool canSave = !showMainMenu;
            float y = panel.y + 42.0f;
            for (std::size_t index = 0; index < names.size(); ++index) {
                const std::string& name = names[index];
                const fa::save::SaveMetadata* meta = findSlot(name);
                char line[64]{};
                if (meta != nullptr) {
                    std::snprintf(line, sizeof(line), "%s  第 %d 天", name.c_str(), meta->inGameDay);
                } else {
                    std::snprintf(line, sizeof(line), "%s  空", name.c_str());
                }
                ui->text(line, fa::core::makeRect(panel.x + 12.0f, y, 270.0f, 8.0f), meta != nullptr ? label : dim);

                char loadId[32]{};
                std::snprintf(loadId, sizeof(loadId), "slot.load.%zu", index);
                if (press(loadId, "读取", panel.x + 300.0f, y - 4.0f, 58.0f, 18.0f)) {
                    loadSlot(name);
                    return;
                }
                if (canSave && index > 0) {
                    char saveId[32]{};
                    std::snprintf(saveId, sizeof(saveId), "slot.save.%zu", index);
                    if (press(saveId, "保存", panel.x + 366.0f, y - 4.0f, 58.0f, 18.0f)) {
                        saveSlot(name);
                    }
                }
                y += 30.0f;
            }

            if (press("slots.back", "返回", panel.x + 10.0f, panel.y + 206.0f, 70.0f, 20.0f)) {
                slotsOpen = false;
            }
        }
    }

    /// \brief Builds the M09 HUD and the widget row, then appends their draw items.
    void buildHud(const fa::core::Vec2& cursor) {
        if (ui == nullptr) {
            return;
        }

        fa::ui::UiInputState state;
        state.mousePosition = camera.worldToViewport(cursor);
        if (fa::input::MouseDevice* mouse = input.mouse(); mouse != nullptr) {
            state.mouseDown = mouse->held(fa::platform::MouseButton::Left);
            state.mousePressed = mouse->pressed(fa::platform::MouseButton::Left);
            state.mouseReleased = mouse->released(fa::platform::MouseButton::Left);
        }
        const fa::core::Rect visible = camera.visibleRect();
        ui->setOrigin(fa::core::Vec2{visible.x, visible.y});
        ui->beginFrame(state);

        const fa::ui::UiTheme& theme = ui->theme();
        const fa::flow::AppState menuState = flow != nullptr ? flow->state() : fa::flow::AppState::Boot;
#if defined(FA_ENABLE_EDITOR)
        if (editorWindowOpen && visualEditor != nullptr) {
            std::vector<std::string> textureIds;
            textureIds.reserve(catalog.size());
            for (const fa::assets::AssetEntry& entry : catalog.entries()) {
                if (entry.kind == fa::assets::AssetKind::Texture) {
                    textureIds.emplace_back(entry.id.name());
                }
            }
            const fa::core::Rect screen = fa::core::makeRect(0.0f, 0.0f, visible.w, visible.h);
            visualEditor->draw(*ui, screen, textureIds, [this](std::string_view) { return saveVisualEditorMap(); });
            if (visualEditor->closeRequested()) {
                closeVisualEditor();
            }
            uiItems = ui->itemCount();
            ui->submit(renderQueue);
            return;
        }
#endif
        const bool inMenu = playMode && flow != nullptr && menuState != fa::flow::AppState::Playing;
        if (showHud && !inMenu) {
            // Halved footprint (118x50): one-scale text on a 9 px line grid. The baked
            // font floors text to scale 1, so the title drops from 2x to 1x instead.
            const fa::core::Rect panel = fa::core::makeRect(8.0f, 8.0f, 118.0f, 50.0f);
            ui->panel(panel, theme.panel);
            ui->border(panel, theme.panelBorder);

            fa::ui::TextStyle title;
            title.color = theme.text;
            title.scale = config.ui.scale;
            // The game's name is a proper noun and stays English, like the window title.
            ui->text("Florian's Adventure", fa::core::makeRect(11.0f, 10.0f, 100.0f, 10.0f), title);

            fa::ui::TextStyle label;
            label.color = theme.text;
            label.scale = config.ui.scale;
            ui->text("生命", fa::core::makeRect(11.0f, 21.0f, 20.0f, 10.0f), label);
            const float health = gameScene != nullptr ? gameScene->playerHealth() : 0.0f;
            const float maxHealth = gameScene != nullptr ? gameScene->playerMaxHealth() : 100.0f;
            ui->progressBar(fa::core::makeRect(34.0f, 23.0f, 72.0f, 4.0f),
                            health,
                            0.0f,
                            maxHealth,
                            fa::core::Color{0.85f, 0.30f, 0.30f, 1.0f});

            ui->text("体力", fa::core::makeRect(11.0f, 30.0f, 20.0f, 10.0f), label);
            const float stamina = gameScene != nullptr ? gameScene->playerStamina() : 0.0f;
            const float maxStamina = gameScene != nullptr ? gameScene->playerMaxStamina() : 100.0f;
            ui->progressBar(fa::core::makeRect(34.0f, 32.0f, 72.0f, 4.0f), stamina, 0.0f, maxStamina, theme.accent);

            // The selected slot lives in the bottom hotbar now; gold belongs with the rest
            // of the player status, so it moved here from the middle of the screen.
            if (economy != nullptr) {
                char goldText[32]{};
                std::snprintf(goldText, sizeof(goldText), "金币 %d", economy->gold());
                ui->text(goldText, fa::core::makeRect(11.0f, 39.0f, 100.0f, 10.0f), label);
            }

            if (playMode && calendar != nullptr) {
                const std::string seasonName(seasonLabel(fa::world::toString(calendar->season())));
                const char* region = gameScene != nullptr && gameScene->isForest() ? "林地" : "农场";
                char clockText[80]{};
                std::snprintf(clockText,
                              sizeof(clockText),
                              "第%d天 %02d:%02d %s %s",
                              calendar->date().day,
                              calendar->hour(),
                              calendar->minute(),
                              seasonName.c_str(),
                              region);
                ui->text(clockText, fa::core::makeRect(11.0f, 48.0f, 104.0f, 10.0f), label);
            }
        }

        // Bottom hotbar: every slot with its item icon, stack count, key digit and a
        // highlight on the selected one. Replaces the "SLOT n/m NAME xN" text line.
        if (playMode && showHud && !inMenu && playerInventory != nullptr) {
            fa::inventory::Inventory& hotbar = playerInventory->hotbar().inventory();
            const int slotCount = hotbar.slotCount();
            const int selectedSlot = playerInventory->hotbar().selected();
            static constexpr float kCell = 30.0f;
            static constexpr float kGap = 3.0f;
            const float totalWidth = static_cast<float>(slotCount) * (kCell + kGap) - kGap;
            const float left = (visible.w - totalWidth) * 0.5f;
            // Room for the key digits under the cells: the bar sits 20 px off the
            // bottom edge so the numbers stay on screen.
            const float top = visible.h - kCell - 20.0f;

            fa::ui::TextStyle slotStyle;
            slotStyle.color = theme.text;
            slotStyle.scale = config.ui.scale;

            for (int slot = 0; slot < slotCount; ++slot) {
                const float x = left + static_cast<float>(slot) * (kCell + kGap);
                const fa::core::Rect cell = fa::core::makeRect(x, top, kCell, kCell);
                ui->panel(cell, theme.panel);

                const fa::inventory::ItemStack& itemStack = hotbar.at(slot);
                if (!itemStack.empty()) {
                    const fa::render::TextureHandle icon = itemIconTexture(itemStack.id.name());
                    const fa::core::Rect iconRect = fa::core::makeRect(x + 7.0f, top + 3.0f, 16.0f, 16.0f);
                    if (icon.isValid()) {
                        ui->icon(iconRect, icon);
                    } else {
                        ui->panel(iconRect, fa::core::Color{1.0f, 0.0f, 1.0f, 1.0f});
                    }

                    // The 10 px font cannot fit inside the icon row, so the stack count sits
                    // on its own line at the bottom of the cell.
                    char count[12]{};
                    std::snprintf(count, sizeof(count), "%d", itemStack.count);
                    fa::ui::TextStyle countStyle = slotStyle;
                    countStyle.align = fa::ui::TextAlign::Right;
                    ui->text(count, fa::core::makeRect(x + 2.0f, top + 19.0f, kCell - 5.0f, 10.0f), countStyle);
                }

                const bool selected = slot == selectedSlot;
                ui->border(cell, selected ? theme.accent : theme.panelBorder, selected ? 2.0f : 1.0f);

                char key[4]{};
                std::snprintf(key, sizeof(key), "%d", (slot + 1) % 10);
                ui->text(key, fa::core::makeRect(x + 10.0f, top + kCell + 2.0f, kCell, 10.0f), slotStyle);
            }
        }

        if (playMode && !inMenu && !damageTexts.empty()) {
            fa::ui::TextStyle damageStyle;
            damageStyle.scale = config.ui.scale;
            damageStyle.align = fa::ui::TextAlign::Center;
            const fa::core::Rect view = camera.visibleRect();
            for (const FloatingDamage& text : damageTexts) {
                if (text.remaining <= 0.0f || text.position.x < view.x || text.position.y < view.y ||
                    text.position.x > view.x + view.w || text.position.y > view.y + view.h) {
                    continue;
                }
                const float alpha = std::min(1.0f, text.remaining / kDamageTextSeconds);
                damageStyle.color = text.onPlayer ? fa::core::Color{1.0f, 0.30f, 0.30f, alpha}
                                                  : fa::core::Color{1.0f, 0.82f, 0.25f, alpha};
                char line[16]{};
                std::snprintf(line, sizeof(line), "-%d", text.amount);
                ui->text(line,
                         fa::core::makeRect(text.position.x - 24.0f, text.position.y - 8.0f, 48.0f, 8.0f),
                         damageStyle);
            }
        }

        // The M09 widget showcase (HUD toggle + UI scale slider). It is not part of the
        // F3 debug overlay; it sat in the middle of the screen and covered the play field,
        // so it now sits in the bottom-right corner and stays interactive there. Bottom-left
        // is reserved for the M27 console/log panels.
        if (!inMenu) {
            static_cast<void>(ui->checkbox(fa::ui::UiId::fromName("hud"),
                                           "界面",
                                           showHud,
                                           fa::core::makeRect(visible.w - 158.0f, visible.h - 46.0f, 150.0f, 16.0f)));
            static_cast<void>(ui->slider(fa::ui::UiId::fromName("scale"),
                                         sliderValue,
                                         1.0f,
                                         4.0f,
                                         fa::core::makeRect(visible.w - 158.0f, visible.h - 26.0f, 150.0f, 10.0f)));
        }

        if (playMode && !inMenu) {
            // The help panel explains how the game resolves tools, weapons and
            // keys - judgement info a player should not need. It belongs to
            // `--debug`; a plain `--play` session only ever shows the quest line
            // and the transient message, which are game feedback, not internals.
            std::string questText;
            if (gameScene != nullptr && gameScene->isFarm()) {
                const fa::narrative::NarrativeService& dialogueState = gameScene->narrative();
                for (const fa::narrative::QuestState& questState : dialogueState.activeQuests()) {
                    const fa::narrative::QuestDef* definition =
                        dialogueState.database() != nullptr ? dialogueState.database()->quest(questState.id) : nullptr;
                    if (definition != nullptr) {
                        questText = "任务：" + definition->title;
                        break;
                    }
                }
                if (questText.empty() &&
                    dialogueState.isQuestCompleted(fa::content::ContentId::fromName("quest.turnip"))) {
                    questText = "任务已完成";
                }
            }

            fa::ui::TextStyle style;
            style.color = theme.text;
            style.scale = config.ui.scale;

            if (debugMode) {
                // Stacked under the status panel on the left edge, halved to match it:
                // 118x66, 9 px line grid, compact key hints.
                const fa::core::Rect panel = fa::core::makeRect(8.0f, 62.0f, 118.0f, 66.0f);
                ui->panel(panel, theme.panel);
                ui->border(panel, theme.panelBorder);

                const fa::inventory::ItemStack& selected =
                    playerInventory != nullptr ? playerInventory->hotbar().selectedStack() : fa::inventory::ItemStack{};
                const fa::content::ItemDef* selectedDef = selected.empty() ? nullptr : content.item(selected.id);
                char toolText[64]{};
                std::snprintf(toolText,
                              sizeof(toolText),
                              "工具 %s",
                              selectedDef != nullptr ? selectedDef->displayName.c_str() : "空");
                ui->text(toolText, fa::core::makeRect(panel.x + 4.0f, panel.y + 2.0f, 110.0f, 10.0f), style);
                ui->text(
                    "1-9选栏 J任务 G交互", fa::core::makeRect(panel.x + 4.0f, panel.y + 11.0f, 110.0f, 10.0f), style);

                float messageY = panel.y + 20.0f;
                if (!questText.empty()) {
                    ui->text(fa::ui::fitToCodepoints(questText, 10),
                             fa::core::makeRect(panel.x + 4.0f, panel.y + 20.0f, 110.0f, 10.0f),
                             style);
                    messageY = panel.y + 29.0f;
                }

                if (playMessageTimer > 0.0f && !playMessage.empty()) {
                    fa::ui::TextStyle messageStyle = style;
                    messageStyle.color = theme.accent;
                    ui->text(fa::ui::fitToCodepoints(playMessage, 10),
                             fa::core::makeRect(panel.x + 4.0f, messageY, 110.0f, 10.0f),
                             messageStyle);
                }

                // The weapon line follows the selected slot: that is what LMB/G swings
                // and what RMB charges the sword qi with.
                const fa::content::ItemDef* weaponDef = nullptr;
                if (playerInventory != nullptr) {
                    const fa::inventory::ItemStack& held = playerInventory->hotbar().selectedStack();
                    const fa::content::ItemDef* heldDef = held.empty() ? nullptr : content.item(held.id);
                    if (heldDef != nullptr && (heldDef->category == "weapon" || heldDef->damage > 0)) {
                        weaponDef = heldDef;
                    }
                }
                char weaponText[64]{};
                std::snprintf(weaponText,
                              sizeof(weaponText),
                              "武器 %s 伤害 %d",
                              weaponDef != nullptr ? weaponDef->displayName.c_str() : "空手",
                              weaponDef != nullptr ? std::max(1, weaponDef->damage) : 0);
                ui->text(weaponText, fa::core::makeRect(panel.x + 4.0f, panel.y + 38.0f, 110.0f, 8.0f), style);
                ui->text(
                    "I背包 G吃 左键攻击", fa::core::makeRect(panel.x + 4.0f, panel.y + 47.0f, 110.0f, 8.0f), style);
                ui->text("E瞬移 右键剑气", fa::core::makeRect(panel.x + 4.0f, panel.y + 56.0f, 110.0f, 8.0f), style);
            } else if (!questText.empty() || (playMessageTimer > 0.0f && !playMessage.empty())) {
                // Plain play feedback: quest progress and the transient message,
                // without any tool/weapon/key judgement lines.
                const fa::core::Rect panel = fa::core::makeRect(8.0f, 62.0f, 118.0f, 28.0f);
                ui->panel(panel, theme.panel);
                ui->border(panel, theme.panelBorder);
                float messageY = panel.y + 2.0f;
                if (!questText.empty()) {
                    ui->text(fa::ui::fitToCodepoints(questText, 10),
                             fa::core::makeRect(panel.x + 4.0f, messageY, 110.0f, 10.0f),
                             style);
                    messageY += 11.0f;
                }
                if (playMessageTimer > 0.0f && !playMessage.empty()) {
                    fa::ui::TextStyle messageStyle = style;
                    messageStyle.color = theme.accent;
                    ui->text(fa::ui::fitToCodepoints(playMessage, 10),
                             fa::core::makeRect(panel.x + 4.0f, messageY, 110.0f, 10.0f),
                             messageStyle);
                }
            }
        }

#if defined(FA_ENABLE_DEBUG_TOOLS)
        if (debugTools != nullptr) {
            debugTools->draw(*ui, camera.visibleRect());
        }
#endif
#if defined(FA_ENABLE_EDITOR)
        if (editor != nullptr) {
            editor->draw(*ui);
        }
#endif
        if (gameScene != nullptr) {
            const fa::narrative::NarrativeService& dialogueState = gameScene->narrative();
            const fa::narrative::DialogueNode* node = dialogueState.currentNode();
            if (dialogueState.dialogueActive() && node != nullptr) {
                const fa::core::Rect panel = fa::core::makeRect(32.0f, 248.0f, 576.0f, 100.0f);
                ui->panel(panel, theme.panel);
                ui->border(panel, theme.panelBorder);
                fa::ui::TextStyle style;
                style.color = theme.text;
                style.scale = config.ui.scale;
                ui->text(node->speaker, fa::core::makeRect(panel.x + 8.0f, panel.y + 5.0f, 300.0f, 7.0f), style);
                ui->text(node->text, fa::core::makeRect(panel.x + 8.0f, panel.y + 17.0f, 560.0f, 7.0f), style);

                const std::vector<std::size_t> choices = dialogueState.availableChoices();
                if (choices.empty()) {
                    ui->text("[回车] 继续", fa::core::makeRect(panel.x + 8.0f, panel.y + 34.0f, 300.0f, 7.0f), style);
                } else {
                    float y = panel.y + 34.0f;
                    for (std::size_t index = 0; index < choices.size(); ++index) {
                        const std::string line = std::to_string(index + 1) + ") " + node->choices[choices[index]].text;
                        ui->text(line, fa::core::makeRect(panel.x + 8.0f, y, 560.0f, 7.0f), style);
                        y += 11.0f;
                    }
                }
            }
        }
        buildInventoryPanel(theme);
        buildShopPanel(theme);
        buildQuestLogPanel(theme);
        buildFlowMenus(theme, menuState);
        uiItems = ui->itemCount();
        ui->submit(renderQueue);
    }

    /// \brief The whole M06 frame contract: beginFrame, submit, endFrame.
    void renderFrame() {
        if (renderer == nullptr || !renderer->isReady()) {
            return;
        }

        // M05 drains finished background loads and uploads their pixels here, on the
        // thread that owns the GL context.
        if (assets != nullptr) {
            assets->update();
        }
        const auto uploaded = [](const fa::assets::AssetHandle<fa::assets::TextureAsset>& handle) {
            const fa::assets::TextureAsset* asset = handle.get();
            return asset != nullptr && asset->gpuTexture != 0;
        };
        if (!uploaded(grassTexture) || !uploaded(wallTexture) || !uploaded(soilTexture) || !uploaded(markerTexture)) {
            return;
        }

        if (!cameraReady) {
            // The map bounds, so a bigger map later needs no change here.
            camera.setBounds(
                gameScene != nullptr
                    ? gameScene->mapBounds()
                    : fa::core::makeRect(0.0f, 0.0f, 40.0f * fa::render::kTileSize, 23.0f * fa::render::kTileSize));
            // A small deadzone avoids distracting micro-movement while walking;
            // 10/s easing keeps the follow smooth without lagging behind.
            camera.setFollowConfig(fa::render::CameraFollowConfig{fa::core::Vec2{48.0f, 32.0f}, 10.0f, 0.0f});
            camera.setPosition(gameScene != nullptr ? gameScene->markerCenter() : fa::core::Vec2{320.0f, 200.0f});
            cameraReady = true;
        }
        if (gameScene != nullptr && !editorWindowOpen) {
            camera.follow(gameScene->markerCenter(), static_cast<float>(lastRealDelta));
        }

        renderer->beginFrame(camera);

        // Mouse position proves the window -> virtual -> world chain: the viewport
        // strips the letterbox, the camera removes its own offset.
        const fa::render::Viewport viewport = renderer->viewport();
        const fa::core::Vec2 cursor = camera.viewportToWorld(viewport.windowToVirtual(lastMouse));
        if (gameScene != nullptr && !playMode) {
            gameScene->setCursorWorld(cursor);
        }

#if defined(FA_ENABLE_EDITOR)
        if (editorWindowOpen && visualEditor != nullptr) {
            const fa::core::Rect visible = camera.visibleRect();
            const fa::core::Rect mapArea =
                fa::editor::VisualEditor::mapRect(fa::core::makeRect(0.0f, 0.0f, visible.w, visible.h));
            const fa::core::Vec2 uiCursor = camera.worldToViewport(cursor);
            fa::editor::VisualEditorInput editorInput;
            editorInput.cursorScreen = uiCursor;
            const bool overMap = uiCursor.x >= mapArea.x && uiCursor.y >= mapArea.y + 22.0f &&
                                 uiCursor.x < mapArea.x + mapArea.w && uiCursor.y < mapArea.y + mapArea.h;
            if (overMap) {
                if (fa::input::MouseDevice* mouse = input.mouse(); mouse != nullptr) {
                    editorInput.primaryPressed = mouse->pressed(fa::platform::MouseButton::Left);
                    editorInput.secondaryPressed = mouse->pressed(fa::platform::MouseButton::Right);
                }
            }
            visualEditor->update(editorInput, mapArea);
        }
#endif

        // M12 -> M11 -> M06: the extraction system culls tiles against the camera,
        // the sprite extractor appends entities, the queue sorts itself, and the
        // renderer takes the whole batch at once.
        if (gameScene != nullptr) {
            gameScene->setViewBounds(camera.visibleRect());
        }
        renderQueue.clear();
#if defined(FA_ENABLE_EDITOR)
        if (editorWindowOpen && visualEditor != nullptr) {
            // The editor draws its own tile view so a resized or repainted map is
            // visible immediately, before the next scene reload.
            drawEditorMap();
        } else {
            stack.renderExtract(renderQueue);
        }
#else
        stack.renderExtract(renderQueue);
#endif
        extractedItems = renderQueue.size();

#if defined(FA_ENABLE_EDITOR)
        if (editor != nullptr) {
            fa::editor::EditorFrameInput editorInput;
            editorInput.cursorWorld = cursor;
            editorInput.cursorScreen = camera.worldToViewport(cursor);
            if (fa::input::MouseDevice* mouse = input.mouse(); mouse != nullptr) {
                editorInput.primaryDown = mouse->held(fa::platform::MouseButton::Left);
                editorInput.primaryPressed = mouse->pressed(fa::platform::MouseButton::Left);
                editorInput.primaryReleased = mouse->released(fa::platform::MouseButton::Left);
                editorInput.secondaryPressed = mouse->pressed(fa::platform::MouseButton::Right);
            }
            editor->update(editorInput);
        }
#endif

        // M09 appends its HUD and widget quads after the world; the queue sort puts
        // them on RenderLayer::UI, so they draw last. Debug shapes go straight to the
        // renderer and share the same final sort.
        buildHud(cursor);

        // M19: farmland and crops are plain data, so the composition root turns
        // them into tinted quads. Tilled cells sit below actors; crops use the
        // object layer and the cell's bottom edge as the depth key.
        if (gameScene != nullptr && gameScene->isFarm() && uiWhiteTexture.isValid()) {
            for (const fa::farming::FarmTileEntry& entry : gameScene->farmingTiles()) {
                const fa::core::Vec2 cellPosition = gameScene->farmingCellPosition(entry.cell);
                if (entry.tile.tilled) {
                    fa::render::DrawItem soil;
                    const fa::render::TextureHandle wet = gameScene->textureForName("tex.ground.soil_wet");
                    if (entry.tile.watered && wet.isValid()) {
                        soil.texture = wet;
                        soil.tint = fa::core::Color{1.0f, 1.0f, 1.0f, 1.0f};
                    } else {
                        soil.texture = uiWhiteTexture;
                        soil.tint = entry.tile.watered ? fa::core::Color{0.16f, 0.22f, 0.34f, 0.72f}
                                                       : fa::core::Color{0.34f, 0.23f, 0.13f, 0.55f};
                    }
                    soil.position = cellPosition;
                    soil.size = fa::core::Vec2{16.0f, 16.0f};
                    soil.layer = fa::render::RenderLayer::GroundDetail;
                    soil.sortY = cellPosition.y;
                    renderQueue.submit(soil);
                }
                if (entry.hasCrop) {
                    fa::render::DrawItem crop;
                    const fa::content::CropDef* cropDef = content.crop(entry.crop.crop);
                    fa::assets::AssetId spriteId;
                    if (cropDef != nullptr) {
                        if (entry.crop.growthDays <= 0.0f && cropDef->seedSprite.isValid()) {
                            spriteId = cropDef->seedSprite;
                        } else if (!cropDef->stageSprites.empty()) {
                            const std::size_t index = static_cast<std::size_t>(std::max(
                                0, std::min(entry.crop.stage, static_cast<int>(cropDef->stageSprites.size()) - 1)));
                            spriteId = cropDef->stageSprites[index];
                        }
                    }
                    const fa::render::TextureHandle cropTexture =
                        spriteId.isValid() ? gameScene->textureForAsset(spriteId) : fa::render::TextureHandle{};
                    if (cropTexture.isValid()) {
                        crop.texture = cropTexture;
                        crop.position = cellPosition;
                        crop.size = fa::core::Vec2{16.0f, 16.0f};
                        crop.tint = entry.crop.dead ? fa::core::Color{0.55f, 0.50f, 0.45f, 1.0f}
                                                    : fa::core::Color{1.0f, 1.0f, 1.0f, 1.0f};
                    } else {
                        crop.texture = uiWhiteTexture;
                        crop.position = fa::core::Vec2{cellPosition.x + 3.0f, cellPosition.y + 4.0f};
                        crop.size = fa::core::Vec2{10.0f, 10.0f};
                        if (entry.crop.dead) {
                            crop.tint = fa::core::Color{0.45f, 0.38f, 0.32f, 0.95f};
                        } else if (entry.crop.harvestable) {
                            crop.tint = fa::core::Color{1.0f, 0.85f, 0.25f, 1.0f};
                        } else {
                            crop.tint = fa::core::Color{
                                0.25f + 0.12f * static_cast<float>(entry.crop.stage), 0.65f, 0.25f, 1.0f};
                        }
                    }
                    crop.layer = fa::render::RenderLayer::Objects;
                    crop.sortY = cellPosition.y + 16.0f;
                    renderQueue.submit(crop);
                }
            }
        }

        // M22: a full-screen darkening quad plus one translucent quad per point
        // light. Both land on RenderLayer::Weather, below the UI, so the HUD stays
        // at full brightness.
        if (calendar != nullptr && uiWhiteTexture.isValid()) {
            const fa::core::Rect visible = camera.visibleRect();
            const float darkness = fa::core::clamp01(1.0f - calendar->ambientIntensity()) * 0.85f;
            if (darkness > 0.001f) {
                fa::render::DrawItem shade;
                shade.texture = uiWhiteTexture;
                shade.position = fa::core::Vec2{visible.x, visible.y};
                shade.size = fa::core::Vec2{visible.w, visible.h};
                shade.tint = fa::core::Color{0.0f, 0.0f, 0.0f, darkness};
                shade.layer = fa::render::RenderLayer::Weather;
                shade.sortY = -100000.0f;
                renderQueue.submit(shade);
            }
            for (const fa::world::PointLight& light : lights.lights()) {
                fa::render::DrawItem glow;
                glow.texture = uiWhiteTexture;
                glow.position = fa::core::Vec2{light.position.x - light.radius, light.position.y - light.radius};
                glow.size = fa::core::Vec2{light.radius * 2.0f, light.radius * 2.0f};
                glow.tint = fa::core::Color{
                    light.color.r, light.color.g, light.color.b, fa::core::clamp01(light.intensity) * 0.35f};
                glow.layer = fa::render::RenderLayer::Weather;
                glow.sortY = light.position.y;
                glow.entityId = light.handle;
                renderQueue.submit(glow);
            }
        }
        renderQueue.sort();
        renderer->submitAll(renderQueue.items());

        // The cursor marker (yellow 16x16 box + green chain-check line) proves the
        // window -> virtual -> world coordinate chain, but it is a debug tool: it
        // stays hidden in every mode unless F6 / `cursor.show` turns it on.
#if defined(FA_ENABLE_DEBUG_TOOLS)
        const bool cursorMarkerVisible = debugTools != nullptr && debugTools->cursorVisible();
#else
        const bool cursorMarkerVisible = false;
#endif
        if (cursorMarkerVisible) {
            renderer->drawDebugRect(fa::core::makeRect(cursor.x - 8.0f, cursor.y - 8.0f, 16.0f, 16.0f),
                                    fa::core::Color{1.0f, 1.0f, 0.2f, 1.0f});
            renderer->drawDebugLine(fa::core::Vec2{cursor.x, cursor.y},
                                    fa::core::Vec2{cursor.x + 24.0f, cursor.y - 16.0f},
                                    fa::core::Color{0.2f, 1.0f, 0.6f, 1.0f});
        }
        // The facing-tile interaction highlight (green = reachable, red = not) is
        // a debug aid too: it shares the F6 cursor-marker switch and is gone by
        // default in every mode.
        if (cursorMarkerVisible && gameScene != nullptr) {
            const fa::core::Rect targetRect = gameScene->interactionTargetRect();
            if (!fa::core::isEmpty(targetRect)) {
                renderer->drawDebugRect(targetRect, gameScene->interactionTargetColor());
            }
        }
#if defined(FA_ENABLE_EDITOR)
        if (editor != nullptr && editor->active() && gameScene != nullptr) {
            const fa::tilemap::TileCoord hover = editor->hover();
            if (gameScene->editableMap().contains(hover.x, hover.y)) {
                renderer->drawDebugRect(gameScene->mapTileRect(hover), fa::core::Color{1.0f, 0.85f, 0.2f, 1.0f}, 2.0f);
            }
            for (const fa::tilemap::TileCoord& cell : editor->previewCells()) {
                renderer->drawDebugRect(gameScene->mapTileRect(cell), fa::core::Color{0.3f, 0.8f, 1.0f, 0.8f});
            }
        }
#endif

#if defined(FA_ENABLE_DEBUG_TOOLS)
        /// F3 (stats) also shows each AI agent and the target it has locked: a dim
        /// box on the agent, a line to the target, and an orange target box while
        /// the target is visible through M21's line of sight.
        if (debugTools != nullptr && debugTools->statsVisible()) {
            // Cyan marks the camera's follow target and the offset from the view
            // centre, which makes the deadzone visible while walking.
            if (gameScene != nullptr && debugTools->stats().camera.following) {
                const fa::core::Vec2 followCenter = gameScene->markerCenter();
                const fa::core::Color cameraColor{0.2f, 0.9f, 1.0f, 0.9f};
                renderer->drawDebugRect(fa::core::makeRect(followCenter.x - 5.0f, followCenter.y - 5.0f, 10.0f, 10.0f),
                                        cameraColor);
                renderer->drawDebugLine(camera.position(), followCenter, cameraColor, 1.0f);
            }

            for (const fa::debug::DebugAiTargetInfo& info : debugTools->stats().aiTargets) {
                if (!info.hasAgentPosition) {
                    continue;
                }
                const fa::core::Color agentColor{0.55f, 0.75f, 1.0f, 0.9f};
                renderer->drawDebugRect(
                    fa::core::makeRect(info.agentPosition.x - 4.0f, info.agentPosition.y - 4.0f, 8.0f, 8.0f),
                    agentColor);
                if (!info.hasTargetPosition) {
                    continue;
                }
                const fa::core::Color targetColor = info.targetVisible ? fa::core::Color{1.0f, 0.55f, 0.2f, 1.0f}
                                                                       : fa::core::Color{0.65f, 0.65f, 0.65f, 0.9f};
                renderer->drawDebugLine(info.agentPosition, info.targetPosition, targetColor, 1.0f);
                renderer->drawDebugRect(
                    fa::core::makeRect(info.targetPosition.x - 4.0f, info.targetPosition.y - 4.0f, 8.0f, 8.0f),
                    targetColor);
            }
        }

        /// F5 toggles collision boxes. M13 produces the shapes, M27 adapts them to
        /// M06's debug draw calls; the renderer still owns the actual quads.
        if (debugTools != nullptr && debugTools->physicsVisible() && gameScene != nullptr) {
            const std::vector<fa::physics::DebugShape> shapes = gameScene->physicsDebugShapes();
            fa::debug::drawPhysicsShapes(*renderer, shapes);
        }
#endif

        renderer->endFrame();
        lastStats = renderer->stats();
    }

    fa::render::Camera2D camera;
    fa::render::RenderStats lastStats;
    bool cameraReady = false;
};

/// \brief Moves the process into the repo root when the launcher left us somewhere else.
///
/// Visual Studio starts the debugger in `src/app` and a double-click starts beside the exe,
/// while every content path (`config/`, `data/`, `assets/`, `saves/`) is relative to the
/// repo root. A wrong directory never fails loudly: `data/catalog.json` is simply missing,
/// the inline smoke catalog takes over, and the run shows colour blocks instead of the PNGs.
/// The root is the nearest ancestor holding a `data` directory.
void adoptProjectRoot(fa::platform::IFileSystem& fileSystem) {
    static constexpr std::string_view kMarker = "data";
    if (fileSystem.exists(kMarker)) {
        return;  // already inside the repo root
    }

    const std::string started = fileSystem.currentPath();
    std::string dir = started;
    for (int depth = 0; depth < 8; ++depth) {
        const std::size_t separator = dir.find_last_of("/\\");
        if (separator == std::string::npos || separator == 0u) {
            break;
        }
        dir = dir.substr(0, separator);
        if (fileSystem.exists(dir + "/" + std::string(kMarker)) && fileSystem.setCurrentPath(dir)) {
            std::printf(
                "root: working directory %s is not the repo root, switched to %s\n", started.c_str(), dir.c_str());
            return;
        }
    }
    std::printf("root: no data/ above %s, falling back to built-in defaults\n", started.c_str());
}

}  // namespace

int main(int argc, char** argv) {
    const Options options = parseOptions(argc, argv);
    if (options.showHelp) {
        printUsage();
        return 0;
    }
    printBanner();

    std::unique_ptr<fa::platform::IPlatform> platform = fa::platform::createPlatform();
    adoptProjectRoot(platform->files());

    EventsAndJobsRun run;
    SmokeSink sink;
    sink.run = &run;
    sink.playMode = options.playMode;
    sink.debugMode = options.debugMode;
    sink.playTest = options.playTest;
    sink.initializeConfig(*platform);
    if (options.width > 0 && options.height > 0) {
        sink.config.window.width = options.width;
        sink.config.window.height = options.height;
        std::printf("window override: %dx%d\n", options.width, options.height);
    }

    fa::platform::WindowDesc desc;
    desc.title = "Florian's Adventure";
    desc.width = sink.config.window.width;
    desc.height = sink.config.window.height;
    desc.minWidth = sink.config.window.minWidth;
    desc.minHeight = sink.config.window.minHeight;
    desc.resizable = sink.config.window.resizable;
    desc.highDpi = sink.config.window.highDpi;
    desc.hidden = !options.visibleWindow;

    const fa::core::Status initialized = platform->initialize(desc);
    if (initialized.isError()) {
        std::printf("platform init failed (%d): %s\n", initialized.error().code, initialized.error().message.c_str());
        return 1;
    }

    // Only the player modes write a run log: an automation run must not leave files behind
    // on a CI runner, and its console output is already the artifact.
    std::unique_ptr<fa::app::RunLogFileSink> runLog;
    if (options.isPlayerMode()) {
        const std::string path = fa::app::runLogFilePath(sink.config.paths.data + "/log");
        const std::string header = std::string("Florian's Adventure run log - mode ") +
                                   (options.debugMode ? "debug" : "play") +
                                   (options.visibleWindow ? ", window" : ", hidden");
        runLog = std::make_unique<fa::app::RunLogFileSink>(platform->files(), path, header);
        sink.runLogFile = runLog.get();
        if (runLog->ok()) {
            std::printf("log file: %s\n", runLog->path().c_str());
        } else {
            std::printf("log file: cannot write %s, console only\n", runLog->path().c_str());
        }
    }
    if (options.debugMode) {
        fa::core::setLogLevel(fa::core::LogLevel::Debug);
    }

    const fa::platform::IWindow& window = platform->window();
    std::printf(
        "window %dx%d px, dpi scale %.2f\n", window.width(), window.height(), static_cast<double>(window.dpiScale()));

    if (options.playMode) {
#if defined(FA_ENABLE_DEBUG_TOOLS)
        if (options.debugMode) {
            std::printf("play mode: WASD move, 1-9 hotbar, wheel cycle, LMB/G use, RMB sword qi, E teleport\n");
            std::printf("           console `, F3 stats, F4 log, F5 physics, F6 cursor marker,\n");
            std::printf("           time.addDays N / time.set HH:MM\n");
            std::printf("debug mode: stats + physics/AI overlays + help panel on at start, log level Debug\n");
        }
#endif
    }

    const fa::core::Status contentStatus = sink.initializeContent();
    if (contentStatus.isError()) {
        std::printf(
            "content init failed (%d): %s\n", contentStatus.error().code, contentStatus.error().message.c_str());
        platform->shutdown();
        return 1;
    }

    const fa::core::Status inventoryStatus = sink.initializeInventory(options.playMode);
    if (inventoryStatus.isError()) {
        std::printf(
            "inventory init failed (%d): %s\n", inventoryStatus.error().code, inventoryStatus.error().message.c_str());
        platform->shutdown();
        return 1;
    }

    sink.initializeEconomy(!options.playMode);
    sink.initializeSave(*platform);
    sink.initializeCalendar(options.playMode);

    /// Audio is optional in exactly the same way as GL: a CI image without a
    /// sound card must still finish the smoke run.
    const fa::core::Status audioStatus = sink.initializeAudio(*platform);
    if (audioStatus.isError()) {
        std::printf("audio init failed (%d): %s\n", audioStatus.error().code, audioStatus.error().message.c_str());
        std::printf("  continuing without audio\n");
    }

#if defined(FA_ENABLE_DEBUG_TOOLS)
    // Debug info belongs to `--debug` and to the automation runs that assert on it.
    // A plain `--play` session creates no debug tools at all: no F3/F4/F5/F6 panels
    // and no console.
    if (!options.playMode || options.debugMode || options.playTest) {
        sink.initializeDebug();
    }
#endif

    // A hidden window may still get a software GL context, but CI images that have
    // none must not fail the smoke run; a visible run has to be able to draw.
    const fa::core::Status renderStatus =
        sink.initializeRenderer(*platform, options.visibleWindow && sink.config.window.vsync);
    if (renderStatus.isError()) {
        std::printf("render init failed (%d): %s\n", renderStatus.error().code, renderStatus.error().message.c_str());
        if (options.visibleWindow) {
            platform->shutdown();
            return 1;
        }
        std::printf("  continuing without rendering (hidden run)\n");
    } else {
        // The cache needs a renderer to upload through, so it only comes up once the
        // GL context exists.
        const fa::core::Status assetStatus = sink.initializeAssets(*platform);
        if (assetStatus.isError()) {
            std::printf("asset init failed (%d): %s\n", assetStatus.error().code, assetStatus.error().message.c_str());
            if (options.visibleWindow) {
                sink.shutdownRenderer();
                platform->shutdown();
                return 1;
            }
            std::printf("  continuing without assets (hidden run)\n");
        }

        // M09 needs only a renderer: it uploads a generated atlas, not an asset file.
        const fa::core::Status uiStatus = sink.initializeUi(*platform);
        if (uiStatus.isError()) {
            std::printf("ui init failed (%d): %s\n", uiStatus.error().code, uiStatus.error().message.c_str());
            if (options.visibleWindow) {
                sink.shutdownRenderer();
                platform->shutdown();
                return 1;
            }
            std::printf("  continuing without UI (hidden run)\n");
        }
    }

    sink.initializeFlow();
    if (options.soakSeconds > 0) {
        sink.beginSoak();
    }
    if (options.stressSprites > 0) {
        sink.beginStress(options.stressSprites);
    }

    fa::time::FrameLoopConfig config;
    /// A hidden smoke run needs a few frames; a visible window runs until closed.
    config.maxFrames = options.frames > 0 ? static_cast<std::uint64_t>(options.frames)
                                          : (options.playTest ? 1u : (options.visibleWindow ? 0u : 3u));
    /// Software pacing only for headless runs; a real window is paced by vsync (M06).
    config.pacingFramesPerSecond = options.visibleWindow ? 0 : fa::time::kFixedStepsPerSecond;

    fa::time::FrameLoop loop(*platform, sink, config);
    sink.requestAppQuit = [&loop] { loop.requestQuit(); };
    const double startedSeconds = platform->monotonicSeconds();
    const int exitCode = loop.run();
    const double elapsedSeconds = platform->monotonicSeconds() - startedSeconds;

    const fa::time::FixedStepClock& clock = loop.clock();
    const bool eventsJobsOk = run.flush();
    bool playTestOk = true;
    if (options.playTest) {
        playTestOk = sink.runPlaySelfTest();
    }
    const bool soakOk = sink.finishSoak();
    const bool stressOk = sink.finishStress();
    if (!options.playMode) {
        /// M14 runs after the scene has lived for the whole smoke run.
        sink.runSaveSmoke();
        /// M25 exercises the application flow once the smoke state is complete.
        sink.runFlowSmoke();
    }
    // Snapshot what the summary reports: the strings and the stats die with the context.
    std::string glVersionText;
    std::string glRendererText;
    int liveTextures = 0;
    if (sink.renderer != nullptr) {
        glVersionText.assign(sink.renderer->glVersion());
        glRendererText.assign(sink.renderer->glRenderer());
        liveTextures = sink.renderer->liveTextureCount();
    }
    const fa::render::RenderStats renderStats = sink.lastRenderStats();
    const fa::core::Vec2 cameraPosition = sink.cameraPosition();
    const fa::render::CameraFollowConfig cameraFollow = sink.cameraFollowConfig();
    const fa::assets::AssetStats assetStats = sink.assetStats();
    const fa::content::ContentStats contentStats = sink.contentStats();
    const int contentStack = sink.contentSampleStack();
    const std::size_t catalogEntries = sink.catalog.size();
    // Scene counters die with the stack, which shutdownRenderer() clears first.
    const fa::scene::SceneStackStats stackStats = sink.sceneStackStats();
    const fa::scene::WorldStats worldStats = sink.sceneWorldStats();
    const fa::core::Vec2 marker = sink.sceneMarkerPosition();
    const std::size_t systemCount = sink.sceneSystemCount();
    const std::size_t groundTiles = sink.sceneGroundTiles();
    const fa::tilemap::TilemapStats mapStats = sink.sceneTilemapStats();
    const fa::tilemap::TileExtractStats tileStats = sink.sceneTileExtractStats();
    const InteractionRunStats interactionStats = sink.sceneInteractionStats();
    const fa::player::PlayerStats playerStats = sink.scenePlayerStats();
    const fa::player::PlayerAnimationParams playerAnim = sink.scenePlayerAnimation();
    const float playerStamina = sink.scenePlayerStamina();
    // Probe first: the ray is a real query and its counter belongs in the summary.
    const float probeDistance = sink.sceneProbeDistance();
    const fa::physics::PhysicsStats physicsStats = sink.scenePhysicsStats();
    const std::size_t uiGlyphs = sink.uiFontGlyphCount();
    const int uiAtlasWidth = sink.uiAtlasWidth();
    const int uiAtlasHeight = sink.uiAtlasHeight();
    const std::size_t uiItems = sink.uiDrawnItems();
    const std::size_t uiPresses = sink.uiPressCount();
    const bool hudVisible = sink.uiHudVisible();
    const float uiSlider = sink.uiSliderValue();
    const InventoryRunStats inventoryStats = sink.sceneInventoryStats();
    const EconomyRunStats economyStats = sink.sceneEconomyStats();
    const AnimationRunStats animStats = sink.sceneAnimationStats();
    const CombatRunStats combatStats = sink.sceneCombatStats();
    const FarmingRunStats farmingStats = sink.sceneFarmingStats();
    const AiRunStats aiStats = sink.sceneAiStats();
    const NarrativeRunStats narrativeStats = sink.sceneNarrativeStats();
    const FlowRunStats flowStats = sink.flowRunStats();
#if defined(FA_ENABLE_EDITOR)
    const EditorRunStats editorStats = sink.sceneEditorStats();
#endif
    const fa::audio::AudioStats audioStats = sink.audioStats();
    const SaveRunStats saveStats = sink.saveRunStats();
    const CalendarRunStats calendarStats = sink.calendarStats();
#if defined(FA_ENABLE_DEBUG_TOOLS)
    const std::size_t debugCommands = sink.debugCommandCount();
    const std::size_t debugCvars = sink.debugCVarCount();
    const fa::debug::DebugStats debugSnapshot = sink.debugSnapshot();
    sink.shutdownDebug();
#endif
    sink.shutdownFlow();
    sink.shutdownSave();
    sink.shutdownCalendar();
    /// Audio and the GL context both belong to the platform window, so they go
    /// before the platform shuts it down.
    sink.shutdownAudio();
    sink.shutdownRenderer();
    platform->shutdown();

    if (exitCode != 0) {
        std::printf("frame loop failed (%d): the platform clock is unavailable\n", exitCode);
        return exitCode;
    }

    if (!eventsJobsOk) {
        std::printf("events/jobs smoke failed: the final job reported an error\n");
        return 1;
    }

    // A plain `--play` session is the game, not a test report: the m02..m27 module
    // dump belongs to the automation runs and to `--debug`. Failures above still
    // print; a clean play session just exits quietly.
    if (options.playMode && !options.debugMode && !options.playTest) {
        return (playTestOk && soakOk && stressOk) ? 0 : 1;
    }

    std::printf("ran %llu frame(s) in %.3fs\n", static_cast<unsigned long long>(loop.frameCount()), elapsedSeconds);
    std::printf("  fixed steps: %llu (tick %llu, step %.4fs), dropped: %llu\n",
                static_cast<unsigned long long>(sink.fixedSteps),
                static_cast<unsigned long long>(sink.lastTick),
                clock.fixedStepSeconds(),
                static_cast<unsigned long long>(clock.droppedSteps()));
    std::printf("  simulated: %.3fs, last real delta: %.4fs\n", clock.simulatedSeconds(), sink.lastRealDelta);
    if (elapsedSeconds > 0.0) {
        std::printf("  average: %.1f fps\n", static_cast<double>(sink.renderedFrames) / elapsedSeconds);
    }
    std::printf("  events: quit %d, resize %d\n", sink.quitEvents, sink.resizeEvents);
    std::printf("  m03: %llu tick event(s) (last tick %llu), %llu upload event(s) worth %d\n",
                static_cast<unsigned long long>(run.tickEvents),
                static_cast<unsigned long long>(run.lastTick),
                static_cast<unsigned long long>(run.uploadEvents),
                run.uploadedValue);
    const std::string_view contextName = fa::input::toString(sink.input.activeContext());
    std::printf("  m07: %llu event(s) forwarded, context %.*s, moving %llu step(s), "
                "axis (%.2f, %.2f), mouse (%g, %g)\n",
                static_cast<unsigned long long>(sink.forwardedEvents),
                static_cast<int>(contextName.size()),
                contextName.data(),
                static_cast<unsigned long long>(sink.movingSteps),
                static_cast<double>(sink.lastMoveAxis.x),
                static_cast<double>(sink.lastMoveAxis.y),
                static_cast<double>(sink.lastMouse.x),
                static_cast<double>(sink.lastMouse.y));
    if (audioStats.sampleRate == 0 || audioStats.channels == 0) {
        std::printf("  m08: audio unavailable\n");
    } else {
        std::printf("  m08: %d voice(s), %llu played, %llu stopped, %llu dropped, "
                    "%d Hz, %d channel(s), %llu mixed frame(s)\n",
                    audioStats.activeVoices,
                    static_cast<unsigned long long>(audioStats.played),
                    static_cast<unsigned long long>(audioStats.stopped),
                    static_cast<unsigned long long>(audioStats.dropped),
                    audioStats.sampleRate,
                    audioStats.channels,
                    static_cast<unsigned long long>(audioStats.mixedFrames));
    }
#if defined(FA_ENABLE_DEBUG_TOOLS)
    std::printf("  m27: %zu console command(s), %zu cvar(s), camera %s, %zu ai target(s), "
                "F3 stats / F4 log / ` console\n",
                debugCommands,
                debugCvars,
                debugSnapshot.camera.valid ? "ok" : "none",
                debugSnapshot.aiTargets.size());
#else
    std::printf("  m27: disabled (release build)\n");
#endif
    if (uiGlyphs == 0) {
        std::printf("  m09: UI unavailable (no font atlas)\n");
    } else {
        std::printf("  m09: %zu glyph(s) in a %dx%d atlas, %zu UI item(s), %zu press(es), HUD %s, slider %.2f\n",
                    uiGlyphs,
                    uiAtlasWidth,
                    uiAtlasHeight,
                    uiItems,
                    uiPresses,
                    hudVisible ? "on" : "off",
                    static_cast<double>(uiSlider));
    }
    std::printf("  m10: %llu update(s), frame %zu/%zu, %llu event(s), last %s@%zu, %s\n",
                static_cast<unsigned long long>(animStats.updates),
                animStats.frameIndex + 1,
                animStats.frameCount,
                static_cast<unsigned long long>(animStats.events),
                animStats.lastEvent.c_str(),
                animStats.lastEventFrame,
                animStats.finished ? "finished" : "looping");
    if (glVersionText.empty()) {
        std::printf("  m06: renderer unavailable (no GL context)\n");
    } else {
        std::printf("  m06: %s on %s\n", glVersionText.c_str(), glRendererText.c_str());
        std::printf("       %u draw call(s), %u quad(s), %u vertex/vertices, %u texture switch(es), "
                    "%u dropped, %d live texture(s)\n",
                    renderStats.drawCalls,
                    renderStats.quads,
                    renderStats.vertices,
                    renderStats.textureSwitches,
                    renderStats.droppedItems,
                    liveTextures);
        std::printf("       camera (%.1f, %.1f), follow deadzone %.1fx%.1f, %.1f/s, cap %.1f px/s\n",
                    static_cast<double>(cameraPosition.x),
                    static_cast<double>(cameraPosition.y),
                    static_cast<double>(cameraFollow.deadzone.x),
                    static_cast<double>(cameraFollow.deadzone.y),
                    static_cast<double>(cameraFollow.smoothing),
                    static_cast<double>(cameraFollow.maxSpeed));
    }
    std::printf("  m05: %zu catalog entr(ies), %zu texture(s) cached, %zu placeholder(s), %zu CPU byte(s), "
                "%zu live GPU texture(s), %zu failed\n",
                catalogEntries,
                assetStats.textureCount,
                assetStats.placeholderCount,
                assetStats.cpuBytes,
                assetStats.liveGpuTextures,
                assetStats.failedCount);
    std::printf("  m11: %zu scene depth, %zu system(s), %zu live entit(ies) (%llu created / %llu destroyed) over "
                "%zu component type(s), %zu component(s), %zu extracted item(s)\n",
                stackStats.depth,
                systemCount,
                worldStats.liveEntities,
                static_cast<unsigned long long>(worldStats.createdTotal),
                static_cast<unsigned long long>(worldStats.destroyedTotal),
                worldStats.componentTypes,
                worldStats.components,
                sink.extractedItems);
    std::printf("       stack: %llu push(es), %llu pop(s), %llu replace(s), %llu pause(s), %llu resume(s)\n",
                static_cast<unsigned long long>(stackStats.pushes),
                static_cast<unsigned long long>(stackStats.pops),
                static_cast<unsigned long long>(stackStats.replacements),
                static_cast<unsigned long long>(stackStats.pauses),
                static_cast<unsigned long long>(stackStats.resumes));
    std::printf("       marker at (%g, %g)\n", static_cast<double>(marker.x), static_cast<double>(marker.y));
    std::printf("  m12: %dx%d map, %d px tiles, %zu cell(s), %zu layer(s), %zu def(s), %zu non-empty, "
                "%zu solid, %zu tillable, %zu area(s), %zu portal(s)\n",
                mapStats.width,
                mapStats.height,
                mapStats.tileSize,
                groundTiles,
                mapStats.layers,
                mapStats.tileDefs,
                mapStats.nonEmptyTiles,
                mapStats.solidTiles,
                mapStats.tillableTiles,
                mapStats.areas,
                mapStats.portals);
    std::printf("       %zu tile item(s) submitted, %zu culled, %zu empty, %zu without texture\n",
                tileStats.submitted,
                tileStats.culled,
                tileStats.skippedEmpty,
                tileStats.skippedNoTexture);
    std::printf("  m13: %zu collider(s), %zu trigger, %llu move(s), %llu blocked, %zu cell(s) "
                "(worst bucket %zu)\n",
                physicsStats.bodies,
                physicsStats.triggerBodies,
                static_cast<unsigned long long>(physicsStats.moves),
                static_cast<unsigned long long>(physicsStats.blockedMoves),
                physicsStats.cells,
                physicsStats.maxCellOccupancy);
    std::printf("       %llu step(s), %llu overlap quer(ies), %llu ray(s), %llu trigger begin(s) / "
                "%llu end(s), %zu event(s) last step\n",
                static_cast<unsigned long long>(physicsStats.steps),
                static_cast<unsigned long long>(physicsStats.overlapQueries),
                static_cast<unsigned long long>(physicsStats.rayQueries),
                static_cast<unsigned long long>(physicsStats.triggerBegins),
                static_cast<unsigned long long>(physicsStats.triggerEnds),
                physicsStats.triggerEvents);
    std::printf("       nearest wall straight ahead: %g px\n", static_cast<double>(probeDistance));
    if (options.playMode) {
        std::printf("  m14: play slot %s, %llu save(s), %llu autosave(s), day %d\n",
                    flowStats.stats.slot.empty() ? "-" : flowStats.stats.slot.c_str(),
                    static_cast<unsigned long long>(flowStats.stats.saves),
                    static_cast<unsigned long long>(flowStats.stats.autosaves),
                    flowStats.stats.day);
    } else if (!saveStats.ok) {
        std::printf("  m14: save/load unavailable (%s)\n", saveStats.error.c_str());
    } else {
        std::printf("  m14: slot %s, v%d, scene %s, day %d, %.2fs, %zu entities, marker (%g, %g), "
                    "stamina %.1f, %zu slot(s), backup %s\n",
                    saveStats.slot.c_str(),
                    saveStats.version,
                    saveStats.scene.c_str(),
                    saveStats.day,
                    saveStats.playSeconds,
                    saveStats.entities,
                    static_cast<double>(saveStats.marker.x),
                    static_cast<double>(saveStats.marker.y),
                    static_cast<double>(saveStats.stamina),
                    saveStats.slots,
                    saveStats.recovered ? "used" : "no");
    }
    std::printf("  m15: %zu item(s), %zu crop(s), %zu recipe(s), %zu enemy(ies), %zu loot table(s), "
                "%zu error(s), schema %d, content v%llu, turnip stack %d\n",
                contentStats.items,
                contentStats.crops,
                contentStats.recipes,
                contentStats.enemies,
                contentStats.lootTables,
                contentStats.validationErrors,
                contentStats.schemaVersion,
                static_cast<unsigned long long>(contentStats.contentVersion),
                contentStack);
    const char* targetKind = !interactionStats.targetValid     ? "invalid"
                             : interactionStats.targetIsEntity ? "entity"
                                                               : "tile";
    const std::string_view playerStateName = fa::player::toString(playerAnim.state);
    const std::string_view playerFacingName = fa::player::toString(playerAnim.facing);
    std::printf("  m16: %.*s, facing %.*s, speed %.1f, stamina %.1f, %llu update(s), "
                "%llu move / %llu idle, %llu dodge(s), %llu action(s)\n",
                static_cast<int>(playerStateName.size()),
                playerStateName.data(),
                static_cast<int>(playerFacingName.size()),
                playerFacingName.data(),
                static_cast<double>(playerAnim.speed),
                static_cast<double>(playerStamina),
                static_cast<unsigned long long>(playerStats.updates),
                static_cast<unsigned long long>(playerStats.moveSteps),
                static_cast<unsigned long long>(playerStats.idleSteps),
                static_cast<unsigned long long>(playerStats.dodges),
                static_cast<unsigned long long>(playerStats.actions));
    std::printf("  m17: target cell (%d, %d), %s, %.1f px, %s; %zu tool use(s), %zu placement(s), "
                "%zu event(s), %zu occupied cell(s), preview %s -> %s\n",
                interactionStats.targetCell.x,
                interactionStats.targetCell.y,
                targetKind,
                static_cast<double>(interactionStats.targetDistance),
                interactionStats.targetReachable ? "reachable" : "blocked",
                interactionStats.toolUses,
                interactionStats.placements,
                interactionStats.events,
                interactionStats.occupiedCells,
                interactionStats.previewBefore ? "valid" : "invalid",
                interactionStats.previewAfter ? "valid" : "invalid");
    const char* toolName = inventoryStats.toolEquipped ? inventoryStats.toolId.name().c_str() : "none";
    std::printf("  m18: backpack %d item(s) in %d slot(s), hotbar %d item(s), selected %d, tool %s, "
                "soup uses %d, %zu inventory event(s), %zu use event(s)\n",
                inventoryStats.backpackItems,
                inventoryStats.backpackSlots,
                inventoryStats.hotbarItems,
                inventoryStats.selected,
                toolName,
                inventoryStats.soupUses,
                inventoryStats.inventoryEvents,
                inventoryStats.useEvents);
    std::printf("  m19: %zu tilled, %zu crop(s), %zu harvestable, %zu dead; "
                "%llu till(s)/%llu water(s)/%llu plant(s)/%llu harvest(s), %llu item(s), "
                "stage %d%s, greenhouse %s, winter wilt %s, seed %d, %zu event(s)\n",
                farmingStats.stats.tilled,
                farmingStats.stats.crops,
                farmingStats.stats.harvestable,
                farmingStats.stats.dead,
                static_cast<unsigned long long>(farmingStats.stats.tills),
                static_cast<unsigned long long>(farmingStats.stats.waters),
                static_cast<unsigned long long>(farmingStats.stats.plants),
                static_cast<unsigned long long>(farmingStats.stats.harvests),
                static_cast<unsigned long long>(farmingStats.stats.harvestItems),
                farmingStats.stage,
                farmingStats.harvestable ? " (ready)" : "",
                farmingStats.greenhouseOk ? "ok" : "bad",
                farmingStats.wiltedOk ? "ok" : "bad",
                farmingStats.seedCount,
                farmingStats.stats.events);
    std::printf("  m20: %llu attack(s), %llu hit(s), %llu damage, %llu kill(s), "
                "%llu projectile(s)/%llu hit(s)/%llu wall hit(s), %llu status tick(s), %zu event(s)\n",
                static_cast<unsigned long long>(combatStats.attacks),
                static_cast<unsigned long long>(combatStats.attackHits),
                static_cast<unsigned long long>(combatStats.damageDealt),
                static_cast<unsigned long long>(combatStats.kills),
                static_cast<unsigned long long>(combatStats.projectiles),
                static_cast<unsigned long long>(combatStats.projectileHits),
                static_cast<unsigned long long>(combatStats.projectileWallHits),
                static_cast<unsigned long long>(combatStats.statusTicks),
                combatStats.damageEvents + combatStats.deathEvents + combatStats.projectileEvents +
                    combatStats.statusEvents);
    const std::string_view aiStateName = fa::ai::toString(aiStats.state);
    std::printf("  m21: %.*s, target %s (%s), %zu path node(s), %zu agent(s), "
                "%llu perception(s), %llu target(s), %llu path(s), %llu move(s), "
                "%llu attack(s), %llu state change(s)\n",
                static_cast<int>(aiStateName.size()),
                aiStateName.data(),
                aiStats.targetValid ? "valid" : "none",
                aiStats.targetVisible ? "visible" : "hidden",
                aiStats.pathNodes,
                aiStats.stats.agents,
                static_cast<unsigned long long>(aiStats.stats.perceptions),
                static_cast<unsigned long long>(aiStats.stats.targetsFound),
                static_cast<unsigned long long>(aiStats.stats.pathsFound),
                static_cast<unsigned long long>(aiStats.stats.moves),
                static_cast<unsigned long long>(aiStats.stats.attacks),
                static_cast<unsigned long long>(aiStats.stats.stateChanges));
    const std::string_view seasonName = fa::world::toString(calendarStats.season);
    const std::string_view weatherName = fa::world::toString(calendarStats.today);
    const std::string_view tomorrowName = fa::world::toString(calendarStats.tomorrow);
    std::printf("  m22: year %d %.*s day %d %02d:%02d, %.*s -> %.*s, ambient %.2f, "
                "auto-water %s, growth x%.2f, %llu day(s)/%llu hour(s)/%llu season(s)/"
                "%llu weather change(s), %zu event(s)\n",
                calendarStats.year,
                static_cast<int>(seasonName.size()),
                seasonName.data(),
                calendarStats.day,
                calendarStats.hour,
                calendarStats.minute,
                static_cast<int>(weatherName.size()),
                weatherName.data(),
                static_cast<int>(tomorrowName.size()),
                tomorrowName.data(),
                static_cast<double>(calendarStats.ambientIntensity),
                calendarStats.autoWater ? "yes" : "no",
                static_cast<double>(calendarStats.growth),
                static_cast<unsigned long long>(calendarStats.days),
                static_cast<unsigned long long>(calendarStats.hours),
                static_cast<unsigned long long>(calendarStats.seasons),
                static_cast<unsigned long long>(calendarStats.weatherChanges),
                calendarStats.events);
    const char* questStatus = narrativeStats.questCompleted ? "completed"
                              : narrativeStats.questActive  ? "active"
                                                            : "none";
    std::printf("  m23: %s, node %s, %zu choice(s), affinity %d, quest %s, seed %d, "
                "%llu dialogue(s) [%llu ended], %llu node(s), %llu choice(s), "
                "%llu gift(s), %zu event(s)\n",
                narrativeStats.active ? "active" : "off",
                narrativeStats.node.empty() ? "-" : narrativeStats.node.c_str(),
                narrativeStats.choices,
                narrativeStats.affinity,
                questStatus,
                narrativeStats.seeds,
                static_cast<unsigned long long>(narrativeStats.dialoguesStarted),
                static_cast<unsigned long long>(narrativeStats.dialoguesEnded),
                static_cast<unsigned long long>(narrativeStats.nodes),
                static_cast<unsigned long long>(narrativeStats.choicesMade),
                static_cast<unsigned long long>(narrativeStats.gifts),
                narrativeStats.events);
    std::printf("  m24: gold %d (+%d settled), seed %d, turnip %d, soup %d, "
                "buy %s, sell %s, craft %s, collect %s, ship %s; "
                "%llu bought / %llu sold / %llu crafted / %llu shipped, "
                "%d spent / %d earned, %zu event(s)\n",
                economyStats.gold,
                economyStats.settledGold,
                economyStats.seeds,
                economyStats.turnips,
                economyStats.soups,
                economyStats.buyOk ? "ok" : "fail",
                economyStats.sellOk ? "ok" : "fail",
                economyStats.craftOk ? "ok" : "fail",
                economyStats.collectOk ? "ok" : "fail",
                economyStats.shipOk ? "ok" : "fail",
                static_cast<unsigned long long>(economyStats.stats.bought),
                static_cast<unsigned long long>(economyStats.stats.sold),
                static_cast<unsigned long long>(economyStats.stats.crafted),
                static_cast<unsigned long long>(economyStats.stats.shipped),
                economyStats.stats.goldSpent,
                economyStats.stats.goldEarned,
                economyStats.stats.events);
    const std::string_view flowStateName = fa::flow::toString(flowStats.stats.state);
    std::printf("  m25: %.*s, slot %s, day %d, %.2fs, dirty %s; "
                "%llu new / %llu load(s) (%llu failed) / %llu save(s) / %llu autosave(s), "
                "%llu day end(s), %llu pause(s)/%llu resume(s), %llu death(s)/%llu respawn(s), "
                "%llu menu return(s), %llu quit(s), %llu transition(s), %zu event(s); "
                "new %s, pause %s, save %s, day %s, over %s, respawn %s, menu %s, load %s, quit %s\n",
                static_cast<int>(flowStateName.size()),
                flowStateName.data(),
                flowStats.stats.slot.empty() ? "-" : flowStats.stats.slot.c_str(),
                flowStats.stats.day,
                flowStats.stats.playSeconds,
                flowStats.stats.dirty ? "yes" : "no",
                static_cast<unsigned long long>(flowStats.stats.newGames),
                static_cast<unsigned long long>(flowStats.stats.loads),
                static_cast<unsigned long long>(flowStats.stats.loadFailures),
                static_cast<unsigned long long>(flowStats.stats.saves),
                static_cast<unsigned long long>(flowStats.stats.autosaves),
                static_cast<unsigned long long>(flowStats.stats.dayEnds),
                static_cast<unsigned long long>(flowStats.stats.pauses),
                static_cast<unsigned long long>(flowStats.stats.resumes),
                static_cast<unsigned long long>(flowStats.stats.deaths),
                static_cast<unsigned long long>(flowStats.stats.respawns),
                static_cast<unsigned long long>(flowStats.stats.menuReturns),
                static_cast<unsigned long long>(flowStats.stats.quitRequests),
                static_cast<unsigned long long>(flowStats.stats.transitions),
                flowStats.stats.events,
                flowStats.newGameOk ? "ok" : "fail",
                flowStats.pauseOk ? "ok" : "fail",
                flowStats.saveOk ? "ok" : "fail",
                flowStats.dayEndOk ? "ok" : "fail",
                flowStats.gameOverOk ? "ok" : "fail",
                flowStats.respawnOk ? "ok" : "fail",
                flowStats.menuOk ? "ok" : "fail",
                flowStats.loadOk ? "ok" : "fail",
                flowStats.quitOk ? "ok" : "fail");
#if defined(FA_ENABLE_EDITOR)
    const std::string_view editorToolName = fa::editor::toString(editorStats.tool);
    std::printf("  m26: %s, tool %.*s, layer %d, brush %u, undo %zu / redo %zu, "
                "%llu command(s), dirty %s, map %dx%d, save %s, load %s\n",
                editorStats.active ? "active" : "off",
                static_cast<int>(editorToolName.size()),
                editorToolName.data(),
                editorStats.layer,
                editorStats.brush,
                editorStats.undoDepth,
                editorStats.redoDepth,
                static_cast<unsigned long long>(editorStats.commands),
                editorStats.dirty ? "yes" : "no",
                editorStats.mapWidth,
                editorStats.mapHeight,
                editorStats.saveOk ? "ok" : "fail",
                editorStats.loadOk ? "ok" : "fail");
#else
    std::printf("  m26: disabled (release build)\n");
#endif
#if defined(FA_ENABLE_DEBUG_TOOLS)
    std::printf("M02+M03+M04+M05+M06+M07+M08+M09+M10+M11+M12+M13+M14+M15+M16+M17+M18+M19+M20+M21+M22+M23+M24+M25+M26+"
                "M27 smoke test OK\n");
#else
    std::printf("M02+M03+M04+M05+M06+M07+M08+M09+M10+M11+M12+M13+M14+M15+M16+M17+M18+M19+M20+M21+M22+M23+M24+M25 smoke "
                "test OK\n");
#endif
    return (playTestOk && soakOk && stressOk) ? 0 : 1;
}
