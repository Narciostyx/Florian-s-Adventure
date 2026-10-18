/// \file
/// \brief Composition root: the only translation unit that knows about every engine module.
///
/// Wires M02 (platform), M03 (events/jobs), M04 (frame loop), M06 (render) and
/// M07 (input) so window, event, timing, drawing and input behave end to end.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fa/assets/AssetCatalog.hpp>
#include <fa/assets/AssetError.hpp>
#include <fa/assets/AssetHandle.hpp>
#include <fa/assets/AssetId.hpp>
#include <fa/assets/AssetManager.hpp>
#include <fa/assets/AssetTypes.hpp>
#include <fa/assets/ImageData.hpp>
#include <fa/anim/AnimationClip.hpp>
#include <fa/anim/AnimationService.hpp>
#include <fa/anim/AnimationStateMachine.hpp>
#include <fa/anim/Animator.hpp>
#include <fa/assets/ITextureUploader.hpp>
#include <fa/audio/AudioClip.hpp>
#include <fa/audio/AudioEngine.hpp>
#include <fa/audio/AudioTypes.hpp>
#include <fa/combat/CombatService.hpp>
#include <fa/combat/CombatTypes.hpp>
#if defined(FA_ENABLE_DEBUG_TOOLS)
#include <fa/debug/DebugConsole.hpp>
#include <fa/debug/DebugCVar.hpp>
#include <fa/debug/DebugDraw.hpp>
#include <fa/debug/DebugInspector.hpp>
#include <fa/debug/DebugLog.hpp>
#include <fa/debug/DebugStats.hpp>
#include <fa/debug/DebugTools.hpp>
#endif
#include <fa/core/Result.hpp>
#include <fa/core/Version.hpp>
#include <fa/content/ContentDatabase.hpp>
#include <fa/economy/EconomyService.hpp>
#include <fa/economy/ShopDatabase.hpp>
#include <fa/ai/AiService.hpp>
#include <fa/ai/AiTypes.hpp>
#if defined(FA_ENABLE_EDITOR)
#include <fa/editor/EditorOverlay.hpp>
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
#include <fa/player/PlayerController.hpp>
#include <fa/player/PlayerSystem.hpp>
#include <fa/player/PlayerTypes.hpp>
#include <fa/platform/FileSystem.hpp>
#include <fa/platform/Platform.hpp>
#include <fa/platform/PlatformError.hpp>
#include <fa/save/SaveArchive.hpp>
#include <fa/save/SaveError.hpp>
#include <fa/save/SaveService.hpp>
#include <fa/save/SaveTypes.hpp>
#include <fa/world/Calendar.hpp>
#include <fa/world/Lighting.hpp>
#include <fa/render/IRenderer.hpp>
#include <fa/render/RenderError.hpp>
#include <fa/render/RenderQueue.hpp>
#include <fa/render/RenderTypes.hpp>
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

namespace {

struct Options {
    /// A visible window is opt-in so CI and automated runs never block.
    bool visibleWindow = false;
    /// 0 means "run until the user closes the window".
    int frames = 0;
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--window") {
            options.visibleWindow = true;
        } else if (arg == "--frames" && i + 1 < argc) {
            options.frames = std::atoi(argv[++i]);
        }
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
constexpr std::string_view kCatalogText = R"({
  "assets": [
    {"id": "tex.ground.grass", "kind": "texture", "size": [16, 16], "tile": 8,
     "color": "#4a8250", "altColor": "#5c8a42"},
    {"id": "tex.ground.soil", "kind": "texture", "size": [16, 16], "tile": 4,
     "color": "#6b4a2f", "altColor": "#7d5a3a"},
    {"id": "tex.ground.wall", "kind": "texture", "size": [16, 16], "tile": 4,
     "color": "#3d465e", "altColor": "#55627e"},
    {"id": "tex.actor.player", "kind": "texture", "size": [16, 24],
     "color": "#e8c39e", "altColor": "#c79a74"},
    {"id": "tex.marker", "kind": "texture", "size": [8, 8], "color": "#ffffff"}
  ]
})";

/// \brief Small content pack used by the smoke run: one crop chain and one enemy.
///
/// It is inline on purpose: M15 takes text, not a path, so the smoke run proves
/// loading and reference validation without adding asset files. The ids are stable
/// and namespaced exactly as a real `data/` pack would be.
constexpr std::string_view kContentText = R"({
  "schema_version": 1,
  "items": [
    {"id": "item.turnip_seed", "display_name": "Turnip Seed", "category": "seed",
     "max_stack": 99, "sell_price": 5, "tags": ["seed", "spring"]},
    {"id": "item.turnip", "display_name": "Turnip", "category": "crop",
     "max_stack": 99, "sell_price": 35, "tags": ["crop", "spring"]},
    {"id": "item.turnip_soup", "display_name": "Turnip Soup", "category": "food",
     "max_stack": 10, "sell_price": 120},
    {"id": "item.hoe", "display_name": "Hoe", "category": "tool",
     "max_stack": 1, "sell_price": 0, "tags": ["tool"]}
  ],
  "crops": [
    {"id": "crop.turnip", "seed_item": "item.turnip_seed", "harvest_item": "item.turnip",
     "stage_days": [1, 2, 2],
     "stage_sprites": ["tex.ground.soil", "tex.ground.grass", "tex.ground.grass"],
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
    ]}
  ]
})";

/// \brief One NPC, one branching dialogue and one collection quest for M23.
constexpr std::string_view kNarrativeText = R"({
  "schema_version": 1,
  "npcs": [
    {"id": "npc.elder", "name": "Elder", "liked_items": ["item.turnip"]}
  ],
  "dialogues": [
    {"id": "dialogue.elder", "npc": "npc.elder", "start": "greet", "nodes": [
      {"id": "greet", "speaker": "Elder",
       "text": "The soil remembers every season. Will you help?",
       "choices": [
         {"text": "I will bring a turnip.", "next": "quest"},
         {"text": "Not now.", "next": "bye"}
       ]},
      {"id": "quest", "speaker": "Elder",
       "text": "Bring me one turnip and I will trade seeds.",
       "actions": [{"kind": "start_quest", "id": "quest.turnip"}],
       "next": "bye"},
      {"id": "bye", "speaker": "Elder",
       "text": "Come back when the sun is high.", "next": ""}
    ]}
  ],
  "quests": [
    {"id": "quest.turnip", "title": "A Turnip for the Elder",
     "description": "Bring one turnip.",
     "objectives": [{"kind": "collect", "target": "item.turnip", "required": 1}],
     "rewards": [{"item": "item.turnip_seed", "amount": 2}], "gold": 10}
  ]
})";

/// \brief One general store for the M24 smoke sequence.
constexpr std::string_view kShopText = R"({
  "schema_version": 1,
  "shops": [
    {"id": "shop.general", "name": "General Store",
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
class EventsAndJobsDemo final {
public:
    EventsAndJobsDemo() {
        m_tickSubscription = fa::events::Subscription(m_bus, m_bus.subscribe<TickCompleted>([this](const TickCompleted& event) {
                                  lastTick = event.tick;
                                  ++tickEvents;
                              }));
        m_uploadSubscription = fa::events::Subscription(m_bus, m_bus.subscribe<UploadFinished>([this](const UploadFinished& event) {
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
struct InteractionDemoStats {
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
struct FarmingDemoStats {
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
struct EconomyDemoStats {
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

/// \brief Snapshot of the scripted M23 dialogue/quest sequence.
struct NarrativeDemoStats {
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

/// \brief Snapshot of the scripted M21 agent for the smoke summary.
struct AiDemoStats {
    fa::ai::AiState state = fa::ai::AiState::Idle;
    bool targetValid = false;
    bool targetVisible = false;
    std::size_t pathNodes = 0;
    fa::ai::AiStats stats;
};

#if defined(FA_ENABLE_EDITOR)
/// \brief Snapshot of the M26 editor and its save/load round-trip.
struct EditorDemoStats {
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
struct InventoryDemoStats {
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
struct SaveDemoStats {
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
struct AnimationDemoStats {
    std::uint64_t updates = 0;
    std::uint64_t events = 0;
    std::size_t frameIndex = 0;
    std::size_t frameCount = 0;
    std::size_t lastEventFrame = 0;
    std::string lastEvent;
    bool finished = false;
};

/// \brief Counters for the scripted M20 combat sequence.
struct CombatDemoStats {
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
struct CalendarDemoStats {
    int year = 1;
    fa::world::Season season = fa::world::Season::Spring;
    int day = 1;
    int hour = 6;
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

/// \brief The demo world: a tile map with a solid wall, a trigger, and one body.
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
class DemoScene final : public fa::scene::BasicScene {
public:
    static constexpr int kColumns = 40;
    static constexpr int kRows = 23;
    /// \brief Column of the one-tile wall the marker drives into.
    static constexpr int kWallColumn = 22;
    static constexpr float kMarkerSize = 8.0f;

    static constexpr std::uint16_t kGrassTile = 1;
    static constexpr std::uint16_t kWallTile = 2;
    static constexpr std::uint16_t kSoilTile = 3;

    /// \brief Cell of the interactable crate, reachable from the marker's start.
    static constexpr int kCrateColumn = 19;
    static constexpr int kCrateRow = 12;
    /// \brief Free cell the demo places a prop into.
    static constexpr int kPlacedColumn = 19;
    static constexpr int kPlacedRow = 11;
    static constexpr std::uint32_t kCratePayload = 7;
    static constexpr std::uint32_t kToolPayload = 42;
    static constexpr std::uint32_t kPlacementPayload = 9;

    DemoScene() = delete;
    DemoScene(fa::input::IInput& input, const fa::content::ContentDatabase& content,
              const fa::narrative::NarrativeDatabase& narrative)
        : BasicScene("demo"), m_input(input), m_interaction(m_map, &m_placement) {
        m_farming.setContentDatabase(&content);
        m_farmInventory.setContentDatabase(&content);
        m_farmingTools.setDebrisLayer(&m_farmDebris);
        m_narrative.setDatabase(&narrative);
        m_narrative.setInventory(&m_farmInventory);
        m_narrative.setSeason(fa::content::Season::Spring);
        static_cast<void>(m_map.reset(kColumns, kRows, fa::render::kTileSize));
        static_cast<void>(m_placement.resize(kColumns, kRows));
        m_interaction.addHandler(m_toolHandler);
        m_interaction.addHandler(m_farmingTools);
    }

    /// \brief Hands the scene the textures the map and the marker draw with.
    ///
    /// Only the asset manager can produce these, and it needs a GL context, so the
    /// scene is configured right before it is pushed. The map stores plain handles;
    /// `setTileTexture` is the same binding path a JSON-loaded map will use.
    void setTextures(fa::render::TextureHandle grass, fa::render::TextureHandle wall,
                     fa::render::TextureHandle soil, fa::render::TextureHandle marker) {
        m_grass = grass;
        m_wall = wall;
        m_soil = soil;
        m_marker = marker;
    }

    /// \brief Turns the scripted/input axis into the controller's intent.
    ///
    /// The scene no longer writes `Velocity`: `PlayerSystem` owns that write, and
    /// `PhysicsSystem` still owns the position. The split is what lets M16 replace
    /// the old direct-drive demo without touching the solver.
    void setScriptedMove(fa::core::Vec2 axis) {
        if (m_playerSystem != nullptr) {
            m_playerSystem->setScriptedMove(axis);
        }
    }

    void onEnter() override {
        if (!m_built) {
            buildWorld();
            runInteractionDemo();
            runFarmingDemo();
            runNarrativeDemo();
            m_built = true;
        }

        auto player = std::make_unique<fa::player::PlayerSystem>(m_input);
        m_playerSystem = player.get();
        systems().add(std::move(player));

        auto physics = std::make_unique<fa::physics::PhysicsSystem>(&m_solids, 64.0f);
        m_physics = physics.get();
        systems().add(std::move(physics));
        m_combat = std::make_unique<fa::combat::CombatService>(m_physics->physics());
        m_ai = std::make_unique<fa::ai::AiService>(m_map, m_physics->physics(), m_combat.get());

        auto tiles = std::make_unique<fa::tilemap::TilemapExtractSystem>(m_map);
        m_tileSystem = tiles.get();
        systems().add(std::move(tiles));
        systems().add(std::make_unique<fa::scene::SpriteExtractSystem>());
    }

    void onExit() override {
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
    [[nodiscard]] const InteractionDemoStats& interactionDemoStats() const noexcept { return m_demoStats; }

    /// \brief Counters for the scripted M19 sequence.
    [[nodiscard]] const FarmingDemoStats& farmingDemoStats() const noexcept { return m_farmingDemo; }

    /// \brief Farmland/crop overlay entries, drawn by the composition root.
    [[nodiscard]] std::span<const fa::farming::FarmTileEntry> farmingTiles() const noexcept {
        return m_farming.entries();
    }

    [[nodiscard]] fa::core::Vec2 farmingCellPosition(fa::tilemap::TileCoord cell) const noexcept {
        return m_map.gridToWorld(cell);
    }

    [[nodiscard]] fa::narrative::NarrativeService& narrative() noexcept { return m_narrative; }
    [[nodiscard]] const fa::narrative::NarrativeService& narrative() const noexcept { return m_narrative; }

    [[nodiscard]] NarrativeDemoStats narrativeDemoStats() const {
        NarrativeDemoStats result;
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
    [[nodiscard]] AnimationDemoStats animationDemoStats() const { return m_animStats; }

    /// \brief Public entry point for the composition root's fixed step.
    void advanceAnimation(float deltaSeconds) { updateAnimation(deltaSeconds); }

    /// \brief M20 counters for the smoke summary.
    [[nodiscard]] CombatDemoStats combatDemoStats() const { return m_combatStats; }
    void advanceCombat(float deltaSeconds) { updateCombat(deltaSeconds); }

    /// \brief M21 snapshot for the smoke summary.
    [[nodiscard]] AiDemoStats aiDemoStats() const {
        AiDemoStats result;
        if (m_ai != nullptr) {
            result.stats = m_ai->stats();
            result.pathNodes = m_ai->pathNodeCount(m_aiAgentEntity);
        }
        if (const fa::ai::AiAgent* agent = world().tryGet<fa::ai::AiAgent>(m_aiAgentEntity);
            agent != nullptr) {
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
            m_physics->physics().raycast(world(), origin, fa::core::Vec2{1.0f, 0.0f}, 100.0f,
                                         fa::physics::maskOf(fa::physics::CollisionLayer::World));
        return hit.has_value() ? hit->distance : -1.0f;
    }

private:
    void buildWorld() {
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
        for (int row = 0; row < kRows; ++row) {
            static_cast<void>(m_map.setTile(kWallColumn, row, 0, fa::tilemap::TileId{kWallTile}));
        }
        // A tilled patch proves the tillable flag travels with the layer data and that
        // more than one tile definition can be drawn from the same map.
        for (int row = 8; row <= 11; ++row) {
            for (int column = 4; column <= 7; ++column) {
                static_cast<void>(m_map.setTile(column, row, 0, fa::tilemap::TileId{kSoilTile}));
            }
        }

        // M19 reads the greenhouse property from the map, not from a hard-coded cell.
        fa::tilemap::MapArea greenhouse;
        greenhouse.areaId = 1;
        greenhouse.kind = fa::tilemap::AreaKind::Greenhouse;
        greenhouse.bounds = fa::tilemap::TileBounds{7, 11, 1, 1};
        m_map.addArea(greenhouse);

        m_markerEntity = world().create();
        fa::scene::Transform marker;
        marker.position = fa::core::Vec2{320.0f, 200.0f};
        static_cast<void>(world().add<fa::scene::Transform>(m_markerEntity, marker));

        fa::scene::Sprite sprite;
        sprite.texture = m_marker;
        sprite.size = fa::core::Vec2{kMarkerSize, kMarkerSize};
        sprite.tint = fa::core::Color{1.0f, 0.35f, 0.35f, 1.0f};
        sprite.layer = fa::render::RenderLayer::Actors;
        static_cast<void>(world().add<fa::scene::Sprite>(m_markerEntity, sprite));

        fa::physics::Collider body;
        body.size = fa::core::Vec2{kMarkerSize, kMarkerSize};
        body.layer = fa::physics::CollisionLayer::Player;
        body.mask =
            fa::physics::maskOf(fa::physics::CollisionLayer::World) | fa::physics::CollisionLayer::Trigger;
        static_cast<void>(world().add<fa::physics::Collider>(m_markerEntity, body));
        static_cast<void>(world().add<fa::physics::Velocity>(m_markerEntity, fa::physics::Velocity{}));
        fa::player::PlayerController::attach(world(), m_markerEntity);

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

        // M17: one crate to target and one prop entity whose cell the demo claims.
        m_crateEntity = world().create();
        fa::scene::Transform crateTransform;
        crateTransform.position = m_map.gridToWorld(fa::tilemap::TileCoord{kCrateColumn, kCrateRow});
        static_cast<void>(world().add<fa::scene::Transform>(m_crateEntity, crateTransform));

        fa::scene::Sprite crateSprite;
        crateSprite.texture = m_soil;
        crateSprite.size = fa::core::Vec2{static_cast<float>(fa::render::kTileSize),
                                          static_cast<float>(fa::render::kTileSize)};
        crateSprite.layer = fa::render::RenderLayer::Objects;
        static_cast<void>(world().add<fa::scene::Sprite>(m_crateEntity, crateSprite));

        fa::interaction::Interactable crate;
        crate.area = fa::core::makeRect(0.0f, 0.0f, static_cast<float>(fa::render::kTileSize),
                                        static_cast<float>(fa::render::kTileSize));
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

        buildAnimation();
        buildCombat();
        buildAi();
        buildNarrative();
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
        m_animStats = AnimationDemoStats{};
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

        const auto makeDummy = [this](fa::core::Vec2 position, int health, std::string_view loot)
            -> fa::scene::EntityId {
            const fa::scene::EntityId id = world().create();
            fa::scene::Transform transform;
            transform.position = position;
            static_cast<void>(world().add<fa::scene::Transform>(id, transform));

            fa::scene::Sprite sprite;
            sprite.texture = m_wall;
            sprite.size = fa::core::Vec2{16.0f, 16.0f};
            sprite.layer = fa::render::RenderLayer::Objects;
            sprite.tint = fa::core::Color{1.0f, 0.55f, 0.55f, 1.0f};
            static_cast<void>(world().add<fa::scene::Sprite>(id, sprite));

            fa::physics::Collider collider;
            collider.size = fa::core::Vec2{16.0f, 16.0f};
            collider.layer = fa::physics::CollisionLayer::Enemy;
            collider.mask = fa::physics::maskOf(fa::physics::CollisionLayer::Player) |
                            fa::physics::CollisionLayer::PlayerAttack;
            static_cast<void>(world().add<fa::physics::Collider>(id, collider));

            fa::player::Health hp;
            hp.current = health;
            hp.max = health;
            static_cast<void>(world().add<fa::player::Health>(id, hp));

            fa::combat::LootSource lootSource;
            lootSource.lootTable = std::string(loot);
            static_cast<void>(world().add<fa::combat::LootSource>(id, lootSource));
            return id;
        };

        m_combatDummy1 = makeDummy(fa::core::Vec2{300.0f, 220.0f}, 20, "loot.slime");
        m_combatDummy2 = makeDummy(fa::core::Vec2{300.0f, 240.0f}, 30, "loot.slime");
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
        if (m_combatStats.updates == 30 || m_combatStats.updates == 50) {
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

        if (m_combatStats.updates == 30 && marker != nullptr) {
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

        if (m_combatStats.updates == 35) {
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
        m_combat->clearEvents();
    }

    /// \brief Advances M10 on the fixed step and copies the current source rect.
    void updateAnimation(float deltaSeconds) {
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
    void runInteractionDemo() {
        const fa::tilemap::TileCoord crateCell{kCrateColumn, kCrateRow};
        const fa::interaction::GridTarget target =
            m_interaction.pickTarget(world(), m_markerEntity, fa::core::center(m_map.tileRect(crateCell)));

        m_demoStats.targetValid = target.valid;
        m_demoStats.targetReachable = target.reachable;
        m_demoStats.targetIsEntity = target.entity.isValid();
        m_demoStats.targetDistance = target.distance;
        m_demoStats.targetCell = target.cell;

        fa::interaction::InteractionRequest use;
        use.actor = m_markerEntity;
        use.target = target;
        use.tool = fa::interaction::ToolKind::Sword;
        use.payload = kToolPayload;
        m_demoStats.toolUsed = m_interaction.interact(world(), use).isOk();
        m_demoStats.toolUses = m_toolHandler.uses;

        fa::interaction::PlacementRequest place;
        place.actor = m_markerEntity;
        place.owner = m_placedEntity;
        place.cell = fa::tilemap::TileCoord{kPlacedColumn, kPlacedRow};
        place.size = fa::core::Vec2{static_cast<float>(fa::render::kTileSize),
                                    static_cast<float>(fa::render::kTileSize)};
        place.payload = kPlacementPayload;

        m_demoStats.previewBefore = m_interaction.previewPlacement(world(), place).valid;
        m_demoStats.placementOk = m_interaction.requestPlacement(world(), place).isOk();
        if (m_demoStats.placementOk) {
            ++m_demoStats.placements;
        }
        m_demoStats.previewAfter = m_interaction.previewPlacement(world(), place).valid;
        m_demoStats.events = m_interaction.events().size();
        m_demoStats.occupiedCells = m_placement.occupiedCells();
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
    void runFarmingDemo() {
        using fa::content::ContentId;
        using fa::content::Season;
        using fa::interaction::ToolKind;

        m_farmingDemo = FarmingDemoStats{};
        const ContentId seed = ContentId::fromName("item.turnip_seed");
        const ContentId crop = ContentId::fromName("item.turnip");
        m_farming.setCurrentSeason(Season::Spring);
        m_farmingTools.setSelectedSeed(seed);
        static_cast<void>(m_farmInventory.add(fa::inventory::ItemStack{seed, 5, -1}));

        const fa::tilemap::TileCoord plot{4, 8};
        m_farmingDemo.hoeOk = useFarmingTool(plot, ToolKind::Hoe).isOk();
        m_farmingDemo.seedOk = useFarmingTool(plot, ToolKind::Seed).isOk();
        m_farmingDemo.waterOk = useFarmingTool(plot, ToolKind::WateringCan).isOk();
        for (int day = 0; day < 5; ++day) {
            if (day > 0) {
                static_cast<void>(useFarmingTool(plot, ToolKind::WateringCan));
            }
            m_farming.onDayChanged(fa::farming::FarmingDayContext{Season::Spring, false, 1.0f});
        }
        if (const fa::farming::CropState* grown = m_farming.cropAt(plot); grown != nullptr) {
            m_farmingDemo.stage = grown->stage;
            m_farmingDemo.harvestable = grown->harvestable;
        }
        m_farmingDemo.harvestOk = useFarmingTool(plot, ToolKind::Harvest).isOk();
        m_farmingDemo.harvestCount = m_farmInventory.countOf(crop);

        m_farming.setCurrentSeason(Season::Spring);
        const fa::tilemap::TileCoord greenhouse{7, 11};
        static_cast<void>(useFarmingTool(greenhouse, ToolKind::Hoe));
        static_cast<void>(useFarmingTool(greenhouse, ToolKind::Seed));
        static_cast<void>(useFarmingTool(greenhouse, ToolKind::WateringCan));
        m_farming.onDayChanged(fa::farming::FarmingDayContext{Season::Winter, false, 1.0f});
        if (const fa::farming::CropState* surviving = m_farming.cropAt(greenhouse); surviving != nullptr) {
            m_farmingDemo.greenhouseOk = !surviving->dead;
        }

        m_farming.setCurrentSeason(Season::Spring);
        const fa::tilemap::TileCoord outdoor{6, 9};
        static_cast<void>(useFarmingTool(outdoor, ToolKind::Hoe));
        static_cast<void>(useFarmingTool(outdoor, ToolKind::Seed));
        static_cast<void>(useFarmingTool(outdoor, ToolKind::WateringCan));
        m_farming.onDayChanged(fa::farming::FarmingDayContext{Season::Winter, false, 1.0f});
        if (const fa::farming::CropState* wilted = m_farming.cropAt(outdoor); wilted != nullptr) {
            m_farmingDemo.wiltedOk = wilted->dead;
        }

        m_farmingDemo.seedCount = m_farmInventory.countOf(seed);
        m_farmingDemo.stats = m_farming.stats();
    }

    /// \brief Creates the one NPC the narrative demo talks to.
    void buildNarrative() {
        m_npcEntity = world().create();
        fa::scene::Transform transform;
        transform.position = fa::core::Vec2{326.0f, 190.0f};
        static_cast<void>(world().add<fa::scene::Transform>(m_npcEntity, transform));

        fa::scene::Sprite sprite;
        sprite.texture = m_marker;
        sprite.size = fa::core::Vec2{16.0f, 16.0f};
        sprite.tint = fa::core::Color{1.0f, 0.78f, 0.30f, 1.0f};
        sprite.layer = fa::render::RenderLayer::Actors;
        static_cast<void>(world().add<fa::scene::Sprite>(m_npcEntity, sprite));

        fa::interaction::Interactable interactable;
        interactable.area = fa::core::makeRect(0.0f, 0.0f, 16.0f, 16.0f);
        interactable.priority = 1;
        interactable.payload = 23;
        static_cast<void>(world().add<fa::interaction::Interactable>(m_npcEntity, interactable));
    }

    /// \brief Runs one full dialogue/quest/gift sequence, then leaves a dialogue open.
    void runNarrativeDemo() {
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
    /// \brief Borrowed; the scene's PlayerSystem reads the same input as the app.
    fa::input::IInput& m_input;
    /// Declared before the systems are created so it outlives them: both systems
    /// borrow it, and `onExit` clears the pipeline before this member dies.
    fa::tilemap::Tilemap m_map;
    /// Non-owning bridge from the map to `PhysicsSystem`; declared after `m_map`.
    fa::tilemap::TilemapSolids m_solids{m_map};
    fa::interaction::PlacementGrid m_placement;
    CountingToolHandler m_toolHandler;
    /// \brief M19 state. The inventory is the demo's seed/harvest sink.
    fa::farming::FarmingService m_farming;
    fa::inventory::Inventory m_farmInventory{4, 99, nullptr};
    fa::farming::DebrisLayer m_farmDebris;
    fa::farming::FarmingToolHandler m_farmingTools{m_farming, m_farmInventory};
    FarmingDemoStats m_farmingDemo;
    /// \brief M23 dialogue/quest state and the NPC entity it talks to.
    fa::narrative::NarrativeService m_narrative;
    fa::scene::EntityId m_npcEntity;
    /// Declared after its map, grid and handlers so those outlive it.
    fa::interaction::InteractionService m_interaction;
    fa::scene::EntityId m_markerEntity;
    fa::scene::EntityId m_crateEntity;
    fa::scene::EntityId m_placedEntity;
    fa::scene::EntityId m_animEntity;
    fa::anim::AnimationService m_animService;
    fa::anim::Animator m_animator;
    AnimationDemoStats m_animStats;
    std::unique_ptr<fa::combat::CombatService> m_combat;
    fa::scene::EntityId m_combatDummy1;
    fa::scene::EntityId m_combatDummy2;
    CombatDemoStats m_combatStats;
    /// \brief M21 service and the scripted agent/decoy pair.
    std::unique_ptr<fa::ai::AiService> m_ai;
    fa::scene::EntityId m_aiAgentEntity;
    fa::scene::EntityId m_aiTargetEntity;
    AiDemoStats m_aiDemo;
    fa::interaction::GridTarget m_cursorTarget;
    InteractionDemoStats m_demoStats;
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
/// in `DemoScene::onEnter()`.
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
            static_cast<void>(input.bind(fa::input::InputContext::Gameplay, action,
                                         fa::input::Binding::fromKey(key)));
        };
        const auto bindMouse = [this](std::string_view name, fa::platform::MouseButton button) {
            const fa::input::ActionId action = input.registerAction(name);
            static_cast<void>(input.bind(fa::input::InputContext::Gameplay, action,
                                         fa::input::Binding::fromMouseButton(button)));
        };
        bindKey(fa::player::kActionNameRun, fa::platform::Scancode::LeftShift);
        bindKey(fa::player::kActionNameDodge, fa::platform::Scancode::Space);
        bindKey(fa::player::kActionNameInteract, fa::platform::Scancode::E);
        bindKey(fa::player::kActionNameInventory, fa::platform::Scancode::Tab);
        bindMouse(fa::player::kActionNameUseTool, fa::platform::MouseButton::Left);
        bindMouse(fa::player::kActionNameAttack, fa::platform::MouseButton::Right);
    }

    /// \brief Creates the GL context the smoke run draws with.
    ///
    /// \return The failure from initialize(); the caller decides whether a headless
    /// run may continue without rendering.
    fa::core::Status initializeRenderer(fa::platform::IPlatform& platform, bool vsync) {
        renderer = fa::render::createRenderer();

        fa::render::RendererDesc desc;
        desc.vsync = vsync;
        const fa::core::Status status = renderer->initialize(platform, desc);
        if (status.isError()) {
            renderer.reset();
        }
        return status;
    }

    /// \brief Loads the inline content pack and prints every collected error.
    fa::core::Status initializeContent() {
        const fa::core::Status status = content.loadDocument(kContentText);
        if (status.isError()) {
            for (const fa::content::ContentError& error : content.errors()) {
                std::printf("content error %d [%s]: %s\n", error.code, error.context.c_str(),
                            error.message.c_str());
            }
            return status;
        }
        const fa::core::Status narrativeStatus = narrative.loadDocument(kNarrativeText);
        if (narrativeStatus.isError()) {
            std::printf("narrative init failed (%d): %s\n", narrativeStatus.error().code,
                        narrativeStatus.error().message.c_str());
            return narrativeStatus;
        }
        const fa::core::Status shopStatus = shops.loadDocument(kShopText);
        if (shopStatus.isError()) {
            std::printf("shop init failed (%d): %s\n", shopStatus.error().code,
                        shopStatus.error().message.c_str());
        }
        return shopStatus;
    }

    /// \brief Loads the catalog and uploads the four textures the frame draws.
    ///
    /// M05 only caches and uploads; it never talks to the renderer directly, so this
    /// is where `AssetUploaderAdapter` ties the two modules together.
    fa::core::Status initializeAssets(fa::platform::IPlatform& platform) {
        if (renderer == nullptr) {
            return fa::core::errorStatus(fa::assets::asset_error::kNotInitialized, "no renderer to upload through");
        }
        const fa::core::Status catalogStatus = catalog.loadFromText(kCatalogText);
        if (catalogStatus.isError()) {
            return catalogStatus;
        }
        fileSystem = &platform.files();
        uploader = std::make_unique<AssetUploaderAdapter>(*renderer);

        fa::assets::AssetManagerDesc desc;
        desc.fileSystem = fileSystem;
        desc.catalog = &catalog;
        desc.uploader = uploader.get();
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

        const fa::assets::TextureAsset* grassAsset = grassTexture.get();
        const fa::assets::TextureAsset* wallAsset = wallTexture.get();
        const fa::assets::TextureAsset* soilAsset = soilTexture.get();
        const fa::assets::TextureAsset* markerAsset = markerTexture.get();
        if (grassAsset == nullptr || wallAsset == nullptr || soilAsset == nullptr || markerAsset == nullptr) {
            return fa::core::errorStatus(fa::assets::asset_error::kUploadFailed, "a smoke texture stayed unusable");
        }

        // M11 takes over from here: the textures go into a scene, and the scene becomes
        // the only source of draw items.
        auto scene = std::make_unique<DemoScene>(input, content, narrative);
        scene->setTextures(fa::render::TextureHandle{grassAsset->gpuTexture},
                           fa::render::TextureHandle{wallAsset->gpuTexture},
                           fa::render::TextureHandle{soilAsset->gpuTexture},
                           fa::render::TextureHandle{markerAsset->gpuTexture});
        demoScene = scene.get();
        const fa::core::Status pushed = stack.push(std::move(scene));
        if (pushed.isError()) {
            demoScene = nullptr;
            return pushed;
        }

#if defined(FA_ENABLE_EDITOR)
        editor = std::make_unique<fa::editor::EditorOverlay>(demoScene->editableMap());
        runEditorDemo();
#endif
        return fa::core::okStatus();
    }

#if defined(FA_ENABLE_EDITOR)
    /// \brief Saves the live map through M26 and proves the runtime loader reads it.
    void runEditorDemo() {
        editorDemo = EditorDemoStats{};
        if (editor == nullptr || fileSystem == nullptr) {
            return;
        }
        static_cast<void>(fileSystem->createDirectories("saves"));
        const std::string text = editor->mapText();
        const fa::core::Status saved = fileSystem->writeTextAtomic("saves/editor-map.json", text);
        editorDemo.saveOk = saved.isOk();
        const fa::core::Result<fa::tilemap::Tilemap> loaded =
            fa::tilemap::loadTilemapFromJson(text);
        editorDemo.loadOk = loaded.isOk() && loaded.value().width() == demoScene->editableMap().width() &&
                            loaded.value().height() == demoScene->editableMap().height();
    }

    /// \brief Saves the current editor map to the editor slot.
    void saveEditorMap() {
        if (editor == nullptr || fileSystem == nullptr) {
            return;
        }
        static_cast<void>(fileSystem->createDirectories("saves"));
        editorDemo.saveOk =
            fileSystem->writeTextAtomic("saves/editor-map.json", editor->mapText()).isOk();
    }

    /// \brief Reads the editor slot and validates it with the runtime loader.
    void loadEditorMap() {
        if (editor == nullptr || fileSystem == nullptr) {
            return;
        }
        const fa::core::Result<std::string> text = fileSystem->readText("saves/editor-map.json");
        if (text.isError()) {
            editorDemo.loadOk = false;
            return;
        }
        const fa::core::Result<fa::tilemap::Tilemap> loaded = fa::tilemap::loadTilemapFromJson(text.value());
        editorDemo.loadOk = loaded.isOk();
    }
#endif

    /// \brief Builds the player containers and runs the one M18 demo sequence.
    ///
    /// The sequence exercises stacking (120 seeds become 99 + 21), quick transfer to
    /// the hotbar, equipping a tool, selection changes and one food use. It is
    /// deliberately data-only: no world entity is resolved, so the synthetic
    /// `EntityId` is just recorded in the use event.
    fa::core::Status initializeInventory() {
        playerInventory = std::make_unique<fa::inventory::PlayerInventory>(20, 10, &content);
        itemUse = std::make_unique<fa::inventory::ItemUseService>(content);
        consumable = std::make_unique<ConsumableUseHandler>();
        itemUse->registerHandler(*consumable);

        const auto add = [this](std::string_view name, int count) -> fa::core::Status {
            const auto result = playerInventory->backpack().add(
                fa::inventory::ItemStack{fa::content::ContentId::fromName(name), count, -1});
            if (result.isError()) {
                return fa::core::Status::err(result.error());
            }
            if (result.value() != 0) {
                return fa::core::errorStatus(fa::inventory::inventory_error::kInventoryFull,
                                             "the demo backpack is too small");
            }
            return fa::core::okStatus();
        };

        if (const fa::core::Status status = add("item.turnip_seed", 120); status.isError()) {
            return status;
        }
        if (const fa::core::Status status = add("item.turnip", 5); status.isError()) {
            return status;
        }
        if (const fa::core::Status status = add("item.turnip_soup", 3); status.isError()) {
            return status;
        }
        if (const fa::core::Status status = add("item.hoe", 1); status.isError()) {
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
        if (const auto equipped =
                playerInventory->equipment().equip(fa::inventory::EquipmentSlot::Tool, hoe.value());
            equipped.isError()) {
            return fa::core::Status::err(equipped.error());
        }

        static_cast<void>(playerInventory->hotbar().select(0));
        static_cast<void>(playerInventory->hotbar().cycle(1));
        static_cast<void>(playerInventory->hotbar().cycle(-1));

        const fa::scene::EntityId player{1u, 1u};
        if (const fa::core::Status used =
                itemUse->use(playerInventory->hotbar().inventory(), 1, player);
            used.isError()) {
            return used;
        }

        const fa::inventory::Inventory& backpack = playerInventory->backpack();
        const fa::inventory::Inventory& hotbar = playerInventory->hotbar().inventory();
        const fa::inventory::ItemStack& tool =
            playerInventory->equipment().at(fa::inventory::EquipmentSlot::Tool);

        inventoryDemo.backpackItems = backpack.totalItems();
        inventoryDemo.backpackSlots = backpack.usedSlots();
        inventoryDemo.hotbarItems = hotbar.totalItems();
        inventoryDemo.selected = playerInventory->hotbar().selected();
        inventoryDemo.toolEquipped = !tool.empty();
        inventoryDemo.toolId = tool.id;
        inventoryDemo.soupUses = consumable->uses;
        inventoryDemo.inventoryEvents = backpack.events().size() + hotbar.events().size();
        inventoryDemo.useEvents = itemUse->events().size();
        return fa::core::okStatus();
    }

    /// \brief Runs the M24 buy/sell/craft/ship sequence on the player backpack.
    void initializeEconomy() {
        if (playerInventory == nullptr) {
            return;
        }
        economy = std::make_unique<fa::economy::EconomyService>(&content, &shops);
        economy->setGold(100);
        economy->setSeason(fa::content::Season::Spring);

        fa::inventory::Inventory& bag = playerInventory->backpack();
        const fa::content::ContentId shop = fa::content::ContentId::fromName("shop.general");
        const fa::content::ContentId seed = fa::content::ContentId::fromName("item.turnip_seed");
        const fa::content::ContentId turnip = fa::content::ContentId::fromName("item.turnip");
        const fa::content::ContentId soup = fa::content::ContentId::fromName("item.turnip_soup");
        const fa::content::ContentId recipe = fa::content::ContentId::fromName("recipe.turnip_soup");

        economyDemo = EconomyDemoStats{};
        economyDemo.buyOk = economy->buy(shop, seed, 3, bag).isOk();
        economyDemo.sellOk = economy->sell(shop, turnip, 2, bag).isOk();
        economyDemo.craftOk = economy->startCraft(recipe, "kitchen", bag).isOk();
        economy->update(30);
        economyDemo.collectOk = economy->collectCraft(0, bag).isOk();
        economyDemo.shipOk = economy->ship(soup, 1, bag).isOk();
        economyDemo.settledGold = economy->onDayChanged();

        economyDemo.stats = economy->stats();
        economyDemo.gold = economyDemo.stats.gold;
        economyDemo.seeds = bag.countOf(seed);
        economyDemo.turnips = bag.countOf(turnip);
        economyDemo.soups = bag.countOf(soup) + playerInventory->hotbar().inventory().countOf(soup);
    }

    /// \brief Creates the M14 save service rooted at the ignored `saves/` directory.
    void initializeSave(fa::platform::IPlatform& platform) {
        saveService = std::make_unique<fa::save::SaveService>(platform.files(), "saves");
    }

    /// \brief Creates the M22 calendar and two placeholder point lights.
    ///
    /// The demo runs the clock fast (0.01 real seconds per game minute) so the
    /// 60-frame smoke shows an hour change; the product default is 0.7.
    void initializeCalendar() {
        calendar = std::make_unique<fa::world::WorldCalendar>(20261018ULL);
        calendar->setRealSecondsPerGameMinute(0.01f);
        calendarEvents = 0;

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

    [[nodiscard]] CalendarDemoStats calendarStats() const {
        CalendarDemoStats stats;
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

    /// \brief Builds a snapshot of the current demo state and writes one slot.
    fa::core::Status saveToSlot(std::string_view slot) {
        if (saveService == nullptr) {
            return fa::core::errorStatus(fa::save::save_error::kIoFailure, "save service is not initialized");
        }

        fa::save::SaveContext context;
        context.slotName = std::string(slot);
        context.sceneId = "demo";
        context.inGameDay = 1;
        context.playSeconds = static_cast<double>(fixedSteps) / 60.0;

        const fa::scene::WorldStats world = sceneWorldStats();
        context.world.setInt("entities", static_cast<int>(world.liveEntities));
        context.world.setVec2("marker", sceneMarkerPosition());
        context.player.setFloat("stamina", scenePlayerStamina());
        if (playerInventory != nullptr) {
            context.inventories.setInt("backpackItems", playerInventory->backpack().totalItems());
            context.inventories.setInt("backpackSlots", playerInventory->backpack().usedSlots());
            context.inventories.setInt("hotbarItems", playerInventory->hotbar().inventory().totalItems());
            context.inventories.setInt("selected", playerInventory->hotbar().selected());
        }
        return saveService->save(slot, context);
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
    void runSaveDemo() {
        saveDemo = SaveDemoStats{};
        if (saveService == nullptr || demoScene == nullptr) {
            saveDemo.error = "no scene or save service";
            return;
        }

        const fa::core::Status saved = saveToSlot("autosave");
        if (saved.isError()) {
            saveDemo.error = saved.error().message;
            return;
        }

        const fa::core::Result<fa::save::SaveFile> loaded = loadFromSlot("autosave");
        if (loaded.isError()) {
            saveDemo.error = loaded.error().message;
            return;
        }

        const fa::save::SaveFile& file = loaded.value();
        saveDemo.ok = true;
        saveDemo.recovered = file.recoveredFromBackup;
        saveDemo.slot = file.meta.slotName;
        saveDemo.scene = file.meta.sceneId;
        saveDemo.version = file.meta.saveVersion;
        saveDemo.day = file.meta.inGameDay;
        saveDemo.playSeconds = file.meta.playSeconds;
        int entities = 0;
        static_cast<void>(file.world.getInt("entities", entities));
        saveDemo.entities = static_cast<std::size_t>(entities > 0 ? entities : 0);
        static_cast<void>(file.world.getVec2("marker", saveDemo.marker));
        static_cast<void>(file.player.getFloat("stamina", saveDemo.stamina));
        saveDemo.slots = saveService->listSlots().size();
    }

    void shutdownSave() { saveService.reset(); }

    [[nodiscard]] SaveDemoStats saveDemoStats() const { return saveDemo; }

    /// \brief Opens the platform audio device and starts the scripted M08 voices.
    ///
    /// v1 has no decoder, so the clips are generated here: a 0.25 s 440 Hz tone
    /// followed by 0.1 s of silence, plus a looping 220 Hz tone. The one-shot is
    /// stopped after two rendered frames in onRender().
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

        fa::audio::AudioClip tone = fa::audio::makeTone(440.0f, 0.25f);
        const fa::audio::AudioClip silence = fa::audio::makeSilence(0.1f);
        tone.samples.insert(tone.samples.end(), silence.samples.begin(), silence.samples.end());
        toneClip = std::make_shared<fa::audio::AudioClip>(std::move(tone));
        loopClip = std::make_shared<fa::audio::AudioClip>(fa::audio::makeTone(220.0f, 0.5f));

        audioEngine->setBusVolume(fa::audio::AudioBus::Master, 0.8f);
        audioEngine->setBusVolume(fa::audio::AudioBus::SFX, 0.7f);

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

#if defined(FA_ENABLE_DEBUG_TOOLS)
    /// \brief Creates the M27 console/overlay and registers app-level commands.
    ///
    /// The commands intentionally go through public APIs only: `teleport` writes a
    /// Transform, `give` uses the M18 backpack, `entities` uses the M27 inspector.
    void initializeDebug() {
        debugTools = std::make_unique<fa::debug::DebugTools>();
        debugTools->attachLogSink();

        static_cast<void>(debugTools->registerCommand(fa::debug::ConsoleCommand{
            "entities", "list up to 16 entities in the demo scene",
            [this](fa::debug::DebugConsole& console, std::span<const std::string>) {
                if (demoScene == nullptr) {
                    console.print("no scene");
                    return;
                }
                const std::vector<fa::debug::EntityInspection> entities =
                    fa::debug::inspectWorld(demoScene->world(), 16);
                console.print("entities: " + std::to_string(entities.size()));
                for (const fa::debug::EntityInspection& entity : entities) {
                    console.print(fa::debug::formatInspection(entity));
                }
            }}));

        static_cast<void>(debugTools->registerCommand(fa::debug::ConsoleCommand{
            "teleport", "teleport the marker: teleport <x> <y>",
            [this](fa::debug::DebugConsole& console, std::span<const std::string> args) {
                if (args.size() != 2 || demoScene == nullptr) {
                    console.print("usage: teleport <x> <y>");
                    return;
                }
                const float x = static_cast<float>(std::atof(args[0].c_str()));
                const float y = static_cast<float>(std::atof(args[1].c_str()));
                demoScene->teleportMarker(fa::core::Vec2{x, y});
                console.print("marker moved");
            }}));

        static_cast<void>(debugTools->registerCommand(fa::debug::ConsoleCommand{
            "give", "give an item: give <item.id> <count>",
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

        static_cast<void>(debugTools->registerCommand(fa::debug::ConsoleCommand{
            "save", "save the demo: save [slot]",
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
            "load", "load a slot: load [slot]",
            [this](fa::debug::DebugConsole& console, std::span<const std::string> args) {
                const std::string slot = args.empty() ? std::string("autosave") : args[0];
                const fa::core::Result<fa::save::SaveFile> loaded = loadFromSlot(slot);
                if (loaded.isError()) {
                    console.print("load failed: " + loaded.error().message);
                    return;
                }
                const fa::save::SaveFile& file = loaded.value();
                console.print("loaded " + file.meta.slotName + " v" + std::to_string(file.meta.saveVersion) +
                              " day " + std::to_string(file.meta.inGameDay));
                fa::core::Vec2 marker;
                if (demoScene != nullptr && file.world.getVec2("marker", marker)) {
                    demoScene->teleportMarker(marker);
                    console.print("marker restored");
                }
            }}));

        static_cast<void>(debugTools->registerCommand(fa::debug::ConsoleCommand{
            "slots", "list save slots",
            [this](fa::debug::DebugConsole& console, std::span<const std::string>) {
                if (saveService == nullptr) {
                    console.print("save service unavailable");
                    return;
                }
                for (const fa::save::SaveMetadata& metadata : saveService->listSlots()) {
                    console.print(metadata.slotName + (metadata.readable ? " readable" : " corrupt") +
                                  " day " + std::to_string(metadata.inGameDay));
                }
            }}));

        static_cast<void>(debugTools->registerCommand(fa::debug::ConsoleCommand{
            "delete", "delete a slot: delete <slot>",
            [this](fa::debug::DebugConsole& console, std::span<const std::string> args) {
                if (args.size() != 1 || saveService == nullptr) {
                    console.print("usage: delete <slot>");
                    return;
                }
                const fa::core::Status status = saveService->deleteSlot(args[0]);
                console.print(status.isOk() ? "deleted " + args[0] : "delete failed: " + status.error().message);
            }}));
    }

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
        stats.liveTextures =
            renderer != nullptr ? static_cast<std::uint32_t>(renderer->liveTextureCount()) : 0u;
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
    [[nodiscard]] std::size_t debugCVarCount() const {
        return debugTools != nullptr ? debugTools->cvars().size() : 0;
    }
#endif

    /// \brief Uploads the built-in font atlas and the 1x1 white quad texture.
    ///
    /// M09 owns no GL objects: the composition root creates both textures and hands
    /// M09 plain handles, exactly like the asset pipeline hands handles to sprites.
    fa::core::Status initializeUi() {
        if (renderer == nullptr) {
            return fa::core::errorStatus(fa::render::render_error::kNotInitialized,
                                         "no renderer to upload the font");
        }
        uiFont = fa::ui::buildBuiltinFont();

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

        ui = std::make_unique<fa::ui::UiContext>(uiFont, uiWhiteTexture);
        return fa::core::okStatus();
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
        demoScene = nullptr;
        grassTexture = fa::assets::AssetHandle<fa::assets::TextureAsset>{};
        wallTexture = fa::assets::AssetHandle<fa::assets::TextureAsset>{};
        soilTexture = fa::assets::AssetHandle<fa::assets::TextureAsset>{};
        markerTexture = fa::assets::AssetHandle<fa::assets::TextureAsset>{};
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
        toneHandle = fa::audio::kInvalidSoundHandle;
        loopHandle = fa::audio::kInvalidSoundHandle;
    }

    void onEvents(std::span<const fa::platform::PlatformEvent> events) override {
#if defined(FA_ENABLE_EDITOR)
        for (const fa::platform::PlatformEvent& event : events) {
            if (event.type != fa::platform::PlatformEventType::KeyDown) {
                continue;
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
        if (!editorActive && demoScene != nullptr && demoScene->narrative().dialogueActive()) {
            for (const fa::platform::PlatformEvent& event : events) {
                if (event.type != fa::platform::PlatformEventType::KeyDown) {
                    continue;
                }
                switch (event.scancode) {
                    case fa::platform::Scancode::Digit1:
                        static_cast<void>(demoScene->narrative().choose(0));
                        break;
                    case fa::platform::Scancode::Digit2:
                        static_cast<void>(demoScene->narrative().choose(1));
                        break;
                    case fa::platform::Scancode::Digit3:
                        static_cast<void>(demoScene->narrative().choose(2));
                        break;
                    case fa::platform::Scancode::Enter:
                    case fa::platform::Scancode::Space:
                        static_cast<void>(demoScene->narrative().advance());
                        break;
                    case fa::platform::Scancode::Escape:
                        static_cast<void>(demoScene->narrative().endDialogue());
                        break;
                    default:
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
        if (demoScene != nullptr) {
            // A headless run never presses a key, so the marker also gets a slow
            // scripted drift. It keeps the system pipeline's movement path exercised
            // in CI, where the input axis stays at zero.
            const fa::core::Vec2 scripted = axis.x != 0.0f || axis.y != 0.0f ? axis
                                                                            : fa::core::Vec2{0.35f, 0.2f};
            demoScene->setScriptedMove(scripted);
        }
        stack.fixedUpdate(context);
        if (demoScene != nullptr) {
            demoScene->advanceAnimation(static_cast<float>(context.fixedDeltaSeconds));
            demoScene->advanceCombat(static_cast<float>(context.fixedDeltaSeconds));
            demoScene->advanceAi(static_cast<float>(context.fixedDeltaSeconds));
        }
        if (demo != nullptr) {
            demo->beginTick(context.fixedStepIndex);
        }
    }

    void onVariableUpdate(const fa::time::FrameContext& context) override {
        lastRealDelta = context.realDeltaSeconds;
        if (calendar != nullptr) {
            calendar->advance(static_cast<float>(context.realDeltaSeconds));
            // The composition root owns forwarding; counting and clearing here keeps
            // the internal event vector from growing for the life of the process.
            calendarEvents += calendar->events().size();
            calendar->clearEvents();
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
        if (demo != nullptr) {
            demo->endFrame();
        }
        renderFrame();
#if defined(FA_ENABLE_DEBUG_TOOLS)
        updateDebugStats();
#endif
        input.endFrame();
    }

    /// Declared before `demo` so the input system outlives nothing that needs it and
    /// construction order stays obvious.
    fa::input::InputSystem input;
    EventsAndJobsDemo* demo = nullptr;
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
    /// Owns the scene graph. Cleared before the renderer goes away: the scenes only
    /// store texture handles, but their sprites should not outlive the GPU textures.
    fa::scene::SceneStack stack;
    DemoScene* demoScene = nullptr;
#if defined(FA_ENABLE_EDITOR)
    /// \brief Declared after `stack` so it is destroyed before the scene's map.
    std::unique_ptr<fa::editor::EditorOverlay> editor;
    EditorDemoStats editorDemo;
#endif
    fa::render::RenderQueue renderQueue;
    std::size_t extractedItems = 0;
    /// M09 holds only borrowed handles; `uiFont` is declared before `ui` so the
    /// context (which references the atlas) is destroyed first.
    fa::ui::FontAtlas uiFont;
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
    EconomyDemoStats economyDemo;
    /// The handler must outlive the service that stores a non-owning pointer to it.
    std::unique_ptr<ConsumableUseHandler> consumable;
    std::unique_ptr<fa::inventory::ItemUseService> itemUse;
    InventoryDemoStats inventoryDemo;
    /// \brief M08 state. The engine is shut down explicitly before the platform.
    std::unique_ptr<fa::audio::AudioEngine> audioEngine;
    std::shared_ptr<const fa::audio::AudioClip> toneClip;
    std::shared_ptr<const fa::audio::AudioClip> loopClip;
    fa::audio::SoundHandle toneHandle = fa::audio::kInvalidSoundHandle;
    fa::audio::SoundHandle loopHandle = fa::audio::kInvalidSoundHandle;
    bool audioOneShotStopped = false;
    /// \brief M14 slots; the service borrows the platform file system.
    std::unique_ptr<fa::save::SaveService> saveService;
    SaveDemoStats saveDemo;
    /// \brief M22 calendar and placeholder point lights.
    std::unique_ptr<fa::world::WorldCalendar> calendar;
    fa::world::LightSet lights;
    std::uint64_t calendarEvents = 0;
#if defined(FA_ENABLE_DEBUG_TOOLS)
    /// \brief M27 facade; absent in Release because the project reference is absent.
    std::unique_ptr<fa::debug::DebugTools> debugTools;
    /// \brief Events that were not consumed by the console, forwarded to M07.
    std::vector<fa::platform::PlatformEvent> filteredEvents;
#endif
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
    fa::assets::AssetStats assetStats() const {
        return assets != nullptr ? assets->stats() : fa::assets::AssetStats{};
    }

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
        return demoScene != nullptr ? demoScene->world().stats() : fa::scene::WorldStats{};
    }
    std::size_t sceneGroundTiles() const { return demoScene != nullptr ? demoScene->groundTiles() : 0; }
    fa::tilemap::TilemapStats sceneTilemapStats() const {
        return demoScene != nullptr ? demoScene->tilemapStats() : fa::tilemap::TilemapStats{};
    }
    fa::tilemap::TileExtractStats sceneTileExtractStats() const {
        return demoScene != nullptr ? demoScene->tileExtractStats() : fa::tilemap::TileExtractStats{};
    }
    InteractionDemoStats sceneInteractionStats() const {
        return demoScene != nullptr ? demoScene->interactionDemoStats() : InteractionDemoStats{};
    }
    fa::player::PlayerStats scenePlayerStats() const {
        return demoScene != nullptr ? demoScene->playerStats() : fa::player::PlayerStats{};
    }
    fa::player::PlayerAnimationParams scenePlayerAnimation() const {
        return demoScene != nullptr ? demoScene->playerAnimation() : fa::player::PlayerAnimationParams{};
    }
    InventoryDemoStats sceneInventoryStats() const { return inventoryDemo; }
    EconomyDemoStats sceneEconomyStats() const { return economyDemo; }
    AnimationDemoStats sceneAnimationStats() const {
        return demoScene != nullptr ? demoScene->animationDemoStats() : AnimationDemoStats{};
    }
    CombatDemoStats sceneCombatStats() const {
        return demoScene != nullptr ? demoScene->combatDemoStats() : CombatDemoStats{};
    }
    FarmingDemoStats sceneFarmingStats() const {
        return demoScene != nullptr ? demoScene->farmingDemoStats() : FarmingDemoStats{};
    }
    AiDemoStats sceneAiStats() const {
        return demoScene != nullptr ? demoScene->aiDemoStats() : AiDemoStats{};
    }
    NarrativeDemoStats sceneNarrativeStats() const {
        return demoScene != nullptr ? demoScene->narrativeDemoStats() : NarrativeDemoStats{};
    }
#if defined(FA_ENABLE_EDITOR)
    EditorDemoStats sceneEditorStats() const {
        EditorDemoStats result = editorDemo;
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
    float scenePlayerStamina() const { return demoScene != nullptr ? demoScene->playerStamina() : 0.0f; }
    fa::physics::PhysicsStats scenePhysicsStats() const {
        return demoScene != nullptr ? demoScene->physicsStats() : fa::physics::PhysicsStats{};
    }
    float sceneProbeDistance() const { return demoScene != nullptr ? demoScene->probeDistance() : -1.0f; }
    fa::core::Vec2 sceneMarkerPosition() const {
        return demoScene != nullptr ? demoScene->markerPosition() : fa::core::Vec2{};
    }
    std::size_t sceneSystemCount() const {
        return demoScene != nullptr ? demoScene->systems().size() : 0;
    }
    std::size_t uiFontGlyphCount() const { return uiFont.glyphs.size(); }
    int uiAtlasWidth() const { return uiFont.width; }
    int uiAtlasHeight() const { return uiFont.height; }
    std::size_t uiDrawnItems() const { return uiItems; }
    std::size_t uiPressCount() const { return uiPresses; }
    bool uiHudVisible() const { return showHud; }
    float uiSliderValue() const { return sliderValue; }

private:
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
        if (showHud) {
            const fa::core::Rect panel = fa::core::makeRect(8.0f, 8.0f, 236.0f, 62.0f);
            ui->panel(panel, theme.panel);
            ui->border(panel, theme.panelBorder);

            fa::ui::TextStyle title;
            title.color = theme.text;
            title.scale = 2.0f;
            ui->text("FLORIAN'S ADVENTURE", fa::core::makeRect(14.0f, 13.0f, 220.0f, 14.0f), title);

            fa::ui::TextStyle label;
            label.color = theme.text;
            label.scale = 1.0f;
            ui->text("STAMINA", fa::core::makeRect(14.0f, 33.0f, 44.0f, 7.0f), label);
            const float stamina = demoScene != nullptr ? demoScene->playerStamina() : 0.0f;
            ui->progressBar(fa::core::makeRect(62.0f, 34.0f, 108.0f, 5.0f), stamina, 0.0f, 100.0f,
                            theme.accent);

            char info[48]{};
            const fa::core::Vec2 marker = sceneMarkerPosition();
            std::snprintf(info, sizeof(info), "MARKER %d %d", static_cast<int>(marker.x),
                          static_cast<int>(marker.y));
            ui->text(info, fa::core::makeRect(14.0f, 46.0f, 180.0f, 7.0f), label);

            if (playerInventory != nullptr) {
                const fa::inventory::ItemStack& selected = playerInventory->hotbar().selectedStack();
                const fa::content::ItemDef* selectedDef =
                    selected.empty() ? nullptr : content.item(selected.id);
                char hotbarText[64]{};
                std::snprintf(hotbarText, sizeof(hotbarText), "SLOT %d/%d  %s x%d",
                              playerInventory->hotbar().selected() + 1,
                              playerInventory->hotbar().inventory().slotCount(),
                              selectedDef != nullptr ? selectedDef->displayName.c_str() : "EMPTY",
                              selected.count);
                ui->text(hotbarText, fa::core::makeRect(14.0f, 58.0f, 220.0f, 7.0f), label);
            }
        }

        if (ui->button(fa::ui::UiId::fromName("pause"), "PAUSE", fa::core::makeRect(254.0f, 8.0f, 96.0f, 16.0f))
                .pressed) {
            ++uiPresses;
        }
        static_cast<void>(ui->checkbox(fa::ui::UiId::fromName("hud"), "HUD", showHud,
                                       fa::core::makeRect(254.0f, 30.0f, 150.0f, 16.0f)));
        static_cast<void>(ui->slider(fa::ui::UiId::fromName("scale"), sliderValue, 1.0f, 4.0f,
                                     fa::core::makeRect(254.0f, 52.0f, 150.0f, 10.0f)));
        if (economy != nullptr) {
            fa::ui::TextStyle goldStyle;
            goldStyle.color = theme.text;
            goldStyle.scale = 1.0f;
            char goldText[32]{};
            std::snprintf(goldText, sizeof(goldText), "GOLD %d", economy->gold());
            ui->text(goldText, fa::core::makeRect(254.0f, 74.0f, 150.0f, 7.0f), goldStyle);
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
        if (demoScene != nullptr) {
            const fa::narrative::NarrativeService& dialogueState = demoScene->narrative();
            const fa::narrative::DialogueNode* node = dialogueState.currentNode();
            if (dialogueState.dialogueActive() && node != nullptr) {
                const fa::core::Rect panel = fa::core::makeRect(32.0f, 248.0f, 576.0f, 100.0f);
                ui->panel(panel, theme.panel);
                ui->border(panel, theme.panelBorder);
                fa::ui::TextStyle style;
                style.color = theme.text;
                style.scale = 1.0f;
                ui->text(node->speaker,
                         fa::core::makeRect(panel.x + 8.0f, panel.y + 5.0f, 300.0f, 7.0f), style);
                ui->text(node->text,
                         fa::core::makeRect(panel.x + 8.0f, panel.y + 17.0f, 560.0f, 7.0f), style);

                const std::vector<std::size_t> choices = dialogueState.availableChoices();
                if (choices.empty()) {
                    ui->text("[ENTER] CONTINUE",
                             fa::core::makeRect(panel.x + 8.0f, panel.y + 34.0f, 300.0f, 7.0f), style);
                } else {
                    float y = panel.y + 34.0f;
                    for (std::size_t index = 0; index < choices.size(); ++index) {
                        const std::string line = std::to_string(index + 1) + ") " +
                                                 node->choices[choices[index]].text;
                        ui->text(line, fa::core::makeRect(panel.x + 8.0f, y, 560.0f, 7.0f), style);
                        y += 11.0f;
                    }
                }
            }
        }
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
            camera.setBounds(demoScene != nullptr ? demoScene->mapBounds()
                                                  : fa::core::makeRect(0.0f, 0.0f, 40.0f * fa::render::kTileSize,
                                                                       23.0f * fa::render::kTileSize));
            cameraReady = true;
        }
        camera.setPosition(fa::core::Vec2{320.0f + static_cast<float>(renderedFrames % 5), 200.0f});

        renderer->beginFrame(camera);

        // M12 -> M11 -> M06: the extraction system culls tiles against the camera,
        // the sprite extractor appends entities, the queue sorts itself, and the
        // renderer takes the whole batch at once.
        if (demoScene != nullptr) {
            demoScene->setViewBounds(camera.visibleRect());
        }
        renderQueue.clear();
        stack.renderExtract(renderQueue);
        extractedItems = renderQueue.size();

        // Mouse position proves the window -> virtual -> world chain: the viewport
        // strips the letterbox, the camera removes its own offset.
        const fa::render::Viewport viewport = renderer->viewport();
        const fa::core::Vec2 cursor = camera.viewportToWorld(viewport.windowToVirtual(lastMouse));
        if (demoScene != nullptr) {
            demoScene->setCursorWorld(cursor);
        }
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
        if (demoScene != nullptr && uiWhiteTexture.isValid()) {
            for (const fa::farming::FarmTileEntry& entry : demoScene->farmingTiles()) {
                const fa::core::Vec2 cellPosition = demoScene->farmingCellPosition(entry.cell);
                if (entry.tile.tilled) {
                    fa::render::DrawItem soil;
                    soil.texture = uiWhiteTexture;
                    soil.position = cellPosition;
                    soil.size = fa::core::Vec2{16.0f, 16.0f};
                    soil.tint = entry.tile.watered
                                    ? fa::core::Color{0.16f, 0.22f, 0.34f, 0.72f}
                                    : fa::core::Color{0.34f, 0.23f, 0.13f, 0.55f};
                    soil.layer = fa::render::RenderLayer::GroundDetail;
                    soil.sortY = cellPosition.y;
                    renderQueue.submit(soil);
                }
                if (entry.hasCrop) {
                    fa::render::DrawItem crop;
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
                glow.position = fa::core::Vec2{light.position.x - light.radius,
                                               light.position.y - light.radius};
                glow.size = fa::core::Vec2{light.radius * 2.0f, light.radius * 2.0f};
                glow.tint = fa::core::Color{light.color.r, light.color.g, light.color.b,
                                            fa::core::clamp01(light.intensity) * 0.35f};
                glow.layer = fa::render::RenderLayer::Weather;
                glow.sortY = light.position.y;
                glow.entityId = light.handle;
                renderQueue.submit(glow);
            }
        }
        renderQueue.sort();
        renderer->submitAll(renderQueue.items());

        renderer->drawDebugRect(fa::core::makeRect(cursor.x - 8.0f, cursor.y - 8.0f, 16.0f, 16.0f),
                                fa::core::Color{1.0f, 1.0f, 0.2f, 1.0f});
        renderer->drawDebugLine(fa::core::Vec2{cursor.x, cursor.y},
                                fa::core::Vec2{cursor.x + 24.0f, cursor.y - 16.0f},
                                fa::core::Color{0.2f, 1.0f, 0.6f, 1.0f});
        if (demoScene != nullptr) {
            const fa::core::Rect targetRect = demoScene->interactionTargetRect();
            if (!fa::core::isEmpty(targetRect)) {
                renderer->drawDebugRect(targetRect, demoScene->interactionTargetColor());
            }
        }
#if defined(FA_ENABLE_EDITOR)
        if (editor != nullptr && editor->active() && demoScene != nullptr) {
            const fa::tilemap::TileCoord hover = editor->hover();
            if (demoScene->editableMap().contains(hover.x, hover.y)) {
                renderer->drawDebugRect(demoScene->mapTileRect(hover),
                                        fa::core::Color{1.0f, 0.85f, 0.2f, 1.0f}, 2.0f);
            }
            for (const fa::tilemap::TileCoord& cell : editor->previewCells()) {
                renderer->drawDebugRect(demoScene->mapTileRect(cell),
                                        fa::core::Color{0.3f, 0.8f, 1.0f, 0.8f});
            }
        }
#endif

#if defined(FA_ENABLE_DEBUG_TOOLS)
        /// F5 toggles collision boxes. M13 produces the shapes, M27 adapts them to
        /// M06's debug draw calls; the renderer still owns the actual quads.
        if (debugTools != nullptr && debugTools->physicsVisible() && demoScene != nullptr) {
            const std::vector<fa::physics::DebugShape> shapes = demoScene->physicsDebugShapes();
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

}  // namespace

int main(int argc, char** argv) {
    const Options options = parseOptions(argc, argv);
    printBanner();

    std::unique_ptr<fa::platform::IPlatform> platform = fa::platform::createPlatform();

    fa::platform::WindowDesc desc;
    desc.title = "Florian's Adventure";
    desc.width = 1280;
    desc.height = 720;
    desc.hidden = !options.visibleWindow;

    const fa::core::Status initialized = platform->initialize(desc);
    if (initialized.isError()) {
        std::printf("platform init failed (%d): %s\n", initialized.error().code, initialized.error().message.c_str());
        return 1;
    }

    const fa::platform::IWindow& window = platform->window();
    std::printf("window %dx%d px, dpi scale %.2f\n", window.width(), window.height(),
                static_cast<double>(window.dpiScale()));

    EventsAndJobsDemo demo;

    SmokeSink sink;
    sink.demo = &demo;

    const fa::core::Status contentStatus = sink.initializeContent();
    if (contentStatus.isError()) {
        std::printf("content init failed (%d): %s\n", contentStatus.error().code,
                    contentStatus.error().message.c_str());
        platform->shutdown();
        return 1;
    }

    const fa::core::Status inventoryStatus = sink.initializeInventory();
    if (inventoryStatus.isError()) {
        std::printf("inventory init failed (%d): %s\n", inventoryStatus.error().code,
                    inventoryStatus.error().message.c_str());
        platform->shutdown();
        return 1;
    }

    sink.initializeEconomy();
    sink.initializeSave(*platform);
    sink.initializeCalendar();

    /// Audio is optional in exactly the same way as GL: a CI image without a
    /// sound card must still finish the smoke run.
    const fa::core::Status audioStatus = sink.initializeAudio(*platform);
    if (audioStatus.isError()) {
        std::printf("audio init failed (%d): %s\n", audioStatus.error().code,
                    audioStatus.error().message.c_str());
        std::printf("  continuing without audio\n");
    }

#if defined(FA_ENABLE_DEBUG_TOOLS)
    sink.initializeDebug();
#endif

    // A hidden window may still get a software GL context, but CI images that have
    // none must not fail the smoke run; a visible run has to be able to draw.
    const fa::core::Status renderStatus = sink.initializeRenderer(*platform, options.visibleWindow);
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
        const fa::core::Status uiStatus = sink.initializeUi();
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

    fa::time::FrameLoopConfig config;
    /// A hidden smoke run needs a few frames; a visible window runs until closed.
    config.maxFrames = options.frames > 0 ? static_cast<std::uint64_t>(options.frames)
                                         : (options.visibleWindow ? 0u : 3u);
    /// Software pacing only for headless runs; a real window is paced by vsync (M06).
    config.pacingFramesPerSecond = options.visibleWindow ? 0 : fa::time::kFixedStepsPerSecond;

    fa::time::FrameLoop loop(*platform, sink, config);
    const double startedSeconds = platform->monotonicSeconds();
    const int exitCode = loop.run();
    const double elapsedSeconds = platform->monotonicSeconds() - startedSeconds;

    const fa::time::FixedStepClock& clock = loop.clock();
    const bool eventsJobsOk = demo.flush();
    /// M14 runs after the scene has lived for the whole smoke run.
    sink.runSaveDemo();
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
    const InteractionDemoStats interactionStats = sink.sceneInteractionStats();
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
    const InventoryDemoStats inventoryStats = sink.sceneInventoryStats();
    const EconomyDemoStats economyStats = sink.sceneEconomyStats();
    const AnimationDemoStats animStats = sink.sceneAnimationStats();
    const CombatDemoStats combatStats = sink.sceneCombatStats();
    const FarmingDemoStats farmingStats = sink.sceneFarmingStats();
    const AiDemoStats aiStats = sink.sceneAiStats();
    const NarrativeDemoStats narrativeStats = sink.sceneNarrativeStats();
#if defined(FA_ENABLE_EDITOR)
    const EditorDemoStats editorStats = sink.sceneEditorStats();
#endif
    const fa::audio::AudioStats audioStats = sink.audioStats();
    const SaveDemoStats saveDemo = sink.saveDemoStats();
    const CalendarDemoStats calendarStats = sink.calendarStats();
#if defined(FA_ENABLE_DEBUG_TOOLS)
    const std::size_t debugCommands = sink.debugCommandCount();
    const std::size_t debugCvars = sink.debugCVarCount();
    sink.shutdownDebug();
#endif
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

    std::printf("ran %llu frame(s) in %.3fs\n", static_cast<unsigned long long>(loop.frameCount()), elapsedSeconds);
    std::printf("  fixed steps: %llu (tick %llu, step %.4fs), dropped: %llu\n",
                static_cast<unsigned long long>(sink.fixedSteps), static_cast<unsigned long long>(sink.lastTick),
                clock.fixedStepSeconds(), static_cast<unsigned long long>(clock.droppedSteps()));
    std::printf("  simulated: %.3fs, last real delta: %.4fs\n", clock.simulatedSeconds(), sink.lastRealDelta);
    if (elapsedSeconds > 0.0) {
        std::printf("  average: %.1f fps\n", static_cast<double>(sink.renderedFrames) / elapsedSeconds);
    }
    std::printf("  events: quit %d, resize %d\n", sink.quitEvents, sink.resizeEvents);
    std::printf("  m03: %llu tick event(s) (last tick %llu), %llu upload event(s) worth %d\n",
                static_cast<unsigned long long>(demo.tickEvents), static_cast<unsigned long long>(demo.lastTick),
                static_cast<unsigned long long>(demo.uploadEvents), demo.uploadedValue);
    const std::string_view contextName = fa::input::toString(sink.input.activeContext());
    std::printf("  m07: %llu event(s) forwarded, context %.*s, moving %llu step(s), "
                "axis (%.2f, %.2f), mouse (%g, %g)\n",
                static_cast<unsigned long long>(sink.forwardedEvents), static_cast<int>(contextName.size()),
                contextName.data(), static_cast<unsigned long long>(sink.movingSteps),
                static_cast<double>(sink.lastMoveAxis.x), static_cast<double>(sink.lastMoveAxis.y),
                static_cast<double>(sink.lastMouse.x), static_cast<double>(sink.lastMouse.y));
    if (audioStats.sampleRate == 0 || audioStats.channels == 0) {
        std::printf("  m08: audio unavailable\n");
    } else {
        std::printf("  m08: %d voice(s), %llu played, %llu stopped, %llu dropped, "
                    "%d Hz, %d channel(s), %llu mixed frame(s)\n",
                    audioStats.activeVoices, static_cast<unsigned long long>(audioStats.played),
                    static_cast<unsigned long long>(audioStats.stopped),
                    static_cast<unsigned long long>(audioStats.dropped), audioStats.sampleRate,
                    audioStats.channels, static_cast<unsigned long long>(audioStats.mixedFrames));
    }
#if defined(FA_ENABLE_DEBUG_TOOLS)
    std::printf("  m27: %zu console command(s), %zu cvar(s), F3 stats / F4 log / ` console\n",
                debugCommands, debugCvars);
#else
    std::printf("  m27: disabled (release build)\n");
#endif
    if (uiGlyphs == 0) {
        std::printf("  m09: UI unavailable (no font atlas)\n");
    } else {
        std::printf("  m09: %zu glyph(s) in a %dx%d atlas, %zu UI item(s), %zu press(es), HUD %s, slider %.2f\n",
                    uiGlyphs, uiAtlasWidth, uiAtlasHeight, uiItems, uiPresses, hudVisible ? "on" : "off",
                    static_cast<double>(uiSlider));
    }
    std::printf("  m10: %llu update(s), frame %zu/%zu, %llu event(s), last %s@%zu, %s\n",
                static_cast<unsigned long long>(animStats.updates), animStats.frameIndex + 1,
                animStats.frameCount, static_cast<unsigned long long>(animStats.events),
                animStats.lastEvent.c_str(), animStats.lastEventFrame, animStats.finished ? "finished" : "looping");
    if (glVersionText.empty()) {
        std::printf("  m06: renderer unavailable (no GL context)\n");
    } else {
        std::printf("  m06: %s on %s\n", glVersionText.c_str(), glRendererText.c_str());
        std::printf("       %u draw call(s), %u quad(s), %u vertex/vertices, %u texture switch(es), "
                    "%u dropped, %d live texture(s)\n",
                    renderStats.drawCalls, renderStats.quads, renderStats.vertices, renderStats.textureSwitches,
                    renderStats.droppedItems, liveTextures);
    }
    std::printf("  m05: %zu catalog entr(ies), %zu texture(s) cached, %zu placeholder(s), %zu CPU byte(s), "
                "%zu live GPU texture(s), %zu failed\n",
                catalogEntries, assetStats.textureCount, assetStats.placeholderCount, assetStats.cpuBytes,
                assetStats.liveGpuTextures, assetStats.failedCount);
    std::printf("  m11: %zu scene depth, %zu system(s), %zu live entit(ies) (%llu created / %llu destroyed) over "
                "%zu component type(s), %zu component(s), %zu extracted item(s)\n",
                stackStats.depth, systemCount, worldStats.liveEntities,
                static_cast<unsigned long long>(worldStats.createdTotal),
                static_cast<unsigned long long>(worldStats.destroyedTotal), worldStats.componentTypes,
                worldStats.components, sink.extractedItems);
    std::printf("       stack: %llu push(es), %llu pop(s), %llu replace(s), %llu pause(s), %llu resume(s)\n",
                static_cast<unsigned long long>(stackStats.pushes),
                static_cast<unsigned long long>(stackStats.pops),
                static_cast<unsigned long long>(stackStats.replacements),
                static_cast<unsigned long long>(stackStats.pauses),
                static_cast<unsigned long long>(stackStats.resumes));
    std::printf("       marker at (%g, %g)\n", static_cast<double>(marker.x), static_cast<double>(marker.y));
    std::printf("  m12: %dx%d map, %d px tiles, %zu cell(s), %zu layer(s), %zu def(s), %zu non-empty, "
                "%zu solid, %zu tillable, %zu area(s), %zu portal(s)\n",
                mapStats.width, mapStats.height, mapStats.tileSize, groundTiles, mapStats.layers, mapStats.tileDefs,
                mapStats.nonEmptyTiles, mapStats.solidTiles, mapStats.tillableTiles, mapStats.areas,
                mapStats.portals);
    std::printf("       %zu tile item(s) submitted, %zu culled, %zu empty, %zu without texture\n",
                tileStats.submitted, tileStats.culled, tileStats.skippedEmpty, tileStats.skippedNoTexture);
    std::printf("  m13: %zu collider(s), %zu trigger, %llu move(s), %llu blocked, %zu cell(s) "
                "(worst bucket %zu)\n",
                physicsStats.bodies, physicsStats.triggerBodies,
                static_cast<unsigned long long>(physicsStats.moves),
                static_cast<unsigned long long>(physicsStats.blockedMoves), physicsStats.cells,
                physicsStats.maxCellOccupancy);
    std::printf("       %llu step(s), %llu overlap quer(ies), %llu ray(s), %llu trigger begin(s) / "
                "%llu end(s), %zu event(s) last step\n",
                static_cast<unsigned long long>(physicsStats.steps),
                static_cast<unsigned long long>(physicsStats.overlapQueries),
                static_cast<unsigned long long>(physicsStats.rayQueries),
                static_cast<unsigned long long>(physicsStats.triggerBegins),
                static_cast<unsigned long long>(physicsStats.triggerEnds), physicsStats.triggerEvents);
    std::printf("       nearest wall straight ahead: %g px\n", static_cast<double>(probeDistance));
    if (!saveDemo.ok) {
        std::printf("  m14: save/load unavailable (%s)\n", saveDemo.error.c_str());
    } else {
        std::printf("  m14: slot %s, v%d, scene %s, day %d, %.2fs, %zu entities, marker (%g, %g), "
                    "stamina %.1f, %zu slot(s), backup %s\n",
                    saveDemo.slot.c_str(), saveDemo.version, saveDemo.scene.c_str(), saveDemo.day,
                    saveDemo.playSeconds, saveDemo.entities, static_cast<double>(saveDemo.marker.x),
                    static_cast<double>(saveDemo.marker.y), static_cast<double>(saveDemo.stamina),
                    saveDemo.slots, saveDemo.recovered ? "used" : "no");
    }
    std::printf("  m15: %zu item(s), %zu crop(s), %zu recipe(s), %zu enemy(ies), %zu loot table(s), "
                "%zu error(s), schema %d, content v%llu, turnip stack %d\n",
                contentStats.items, contentStats.crops, contentStats.recipes, contentStats.enemies,
                contentStats.lootTables, contentStats.validationErrors, contentStats.schemaVersion,
                static_cast<unsigned long long>(contentStats.contentVersion), contentStack);
    const char* targetKind = !interactionStats.targetValid ? "invalid"
                             : interactionStats.targetIsEntity ? "entity"
                                                               : "tile";
    const std::string_view playerStateName = fa::player::toString(playerAnim.state);
    const std::string_view playerFacingName = fa::player::toString(playerAnim.facing);
    std::printf("  m16: %.*s, facing %.*s, speed %.1f, stamina %.1f, %llu update(s), "
                "%llu move / %llu idle, %llu dodge(s), %llu action(s)\n",
                static_cast<int>(playerStateName.size()), playerStateName.data(),
                static_cast<int>(playerFacingName.size()), playerFacingName.data(),
                static_cast<double>(playerAnim.speed), static_cast<double>(playerStamina),
                static_cast<unsigned long long>(playerStats.updates),
                static_cast<unsigned long long>(playerStats.moveSteps),
                static_cast<unsigned long long>(playerStats.idleSteps),
                static_cast<unsigned long long>(playerStats.dodges),
                static_cast<unsigned long long>(playerStats.actions));
    std::printf("  m17: target cell (%d, %d), %s, %.1f px, %s; %zu tool use(s), %zu placement(s), "
                "%zu event(s), %zu occupied cell(s), preview %s -> %s\n",
                interactionStats.targetCell.x, interactionStats.targetCell.y, targetKind,
                static_cast<double>(interactionStats.targetDistance),
                interactionStats.targetReachable ? "reachable" : "blocked", interactionStats.toolUses,
                interactionStats.placements, interactionStats.events, interactionStats.occupiedCells,
                interactionStats.previewBefore ? "valid" : "invalid",
                interactionStats.previewAfter ? "valid" : "invalid");
    const char* toolName = inventoryStats.toolEquipped ? inventoryStats.toolId.name().c_str() : "none";
    std::printf("  m18: backpack %d item(s) in %d slot(s), hotbar %d item(s), selected %d, tool %s, "
                "soup uses %d, %zu inventory event(s), %zu use event(s)\n",
                inventoryStats.backpackItems, inventoryStats.backpackSlots, inventoryStats.hotbarItems,
                inventoryStats.selected, toolName, inventoryStats.soupUses, inventoryStats.inventoryEvents,
                inventoryStats.useEvents);
    std::printf("  m19: %zu tilled, %zu crop(s), %zu harvestable, %zu dead; "
                "%llu till(s)/%llu water(s)/%llu plant(s)/%llu harvest(s), %llu item(s), "
                "stage %d%s, greenhouse %s, winter wilt %s, seed %d, %zu event(s)\n",
                farmingStats.stats.tilled, farmingStats.stats.crops, farmingStats.stats.harvestable,
                farmingStats.stats.dead, static_cast<unsigned long long>(farmingStats.stats.tills),
                static_cast<unsigned long long>(farmingStats.stats.waters),
                static_cast<unsigned long long>(farmingStats.stats.plants),
                static_cast<unsigned long long>(farmingStats.stats.harvests),
                static_cast<unsigned long long>(farmingStats.stats.harvestItems), farmingStats.stage,
                farmingStats.harvestable ? " (ready)" : "", farmingStats.greenhouseOk ? "ok" : "bad",
                farmingStats.wiltedOk ? "ok" : "bad", farmingStats.seedCount, farmingStats.stats.events);
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
                static_cast<int>(aiStateName.size()), aiStateName.data(),
                aiStats.targetValid ? "valid" : "none", aiStats.targetVisible ? "visible" : "hidden",
                aiStats.pathNodes, aiStats.stats.agents,
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
                calendarStats.year, static_cast<int>(seasonName.size()), seasonName.data(), calendarStats.day,
                calendarStats.hour, calendarStats.minute, static_cast<int>(weatherName.size()),
                weatherName.data(), static_cast<int>(tomorrowName.size()), tomorrowName.data(),
                static_cast<double>(calendarStats.ambientIntensity), calendarStats.autoWater ? "yes" : "no",
                static_cast<double>(calendarStats.growth),
                static_cast<unsigned long long>(calendarStats.days),
                static_cast<unsigned long long>(calendarStats.hours),
                static_cast<unsigned long long>(calendarStats.seasons),
                static_cast<unsigned long long>(calendarStats.weatherChanges), calendarStats.events);
    const char* questStatus = narrativeStats.questCompleted ? "completed"
                             : narrativeStats.questActive ? "active"
                                                          : "none";
    std::printf("  m23: %s, node %s, %zu choice(s), affinity %d, quest %s, seed %d, "
                "%llu dialogue(s) [%llu ended], %llu node(s), %llu choice(s), "
                "%llu gift(s), %zu event(s)\n",
                narrativeStats.active ? "active" : "off",
                narrativeStats.node.empty() ? "-" : narrativeStats.node.c_str(), narrativeStats.choices,
                narrativeStats.affinity, questStatus, narrativeStats.seeds,
                static_cast<unsigned long long>(narrativeStats.dialoguesStarted),
                static_cast<unsigned long long>(narrativeStats.dialoguesEnded),
                static_cast<unsigned long long>(narrativeStats.nodes),
                static_cast<unsigned long long>(narrativeStats.choicesMade),
                static_cast<unsigned long long>(narrativeStats.gifts), narrativeStats.events);
    std::printf("  m24: gold %d (+%d settled), seed %d, turnip %d, soup %d, "
                "buy %s, sell %s, craft %s, collect %s, ship %s; "
                "%llu bought / %llu sold / %llu crafted / %llu shipped, "
                "%d spent / %d earned, %zu event(s)\n",
                economyStats.gold, economyStats.settledGold, economyStats.seeds, economyStats.turnips,
                economyStats.soups, economyStats.buyOk ? "ok" : "fail", economyStats.sellOk ? "ok" : "fail",
                economyStats.craftOk ? "ok" : "fail", economyStats.collectOk ? "ok" : "fail",
                economyStats.shipOk ? "ok" : "fail",
                static_cast<unsigned long long>(economyStats.stats.bought),
                static_cast<unsigned long long>(economyStats.stats.sold),
                static_cast<unsigned long long>(economyStats.stats.crafted),
                static_cast<unsigned long long>(economyStats.stats.shipped), economyStats.stats.goldSpent,
                economyStats.stats.goldEarned, economyStats.stats.events);
#if defined(FA_ENABLE_EDITOR)
    const std::string_view editorToolName = fa::editor::toString(editorStats.tool);
    std::printf("  m26: %s, tool %.*s, layer %d, brush %u, undo %zu / redo %zu, "
                "%llu command(s), dirty %s, map %dx%d, save %s, load %s\n",
                editorStats.active ? "active" : "off", static_cast<int>(editorToolName.size()),
                editorToolName.data(), editorStats.layer, editorStats.brush, editorStats.undoDepth,
                editorStats.redoDepth, static_cast<unsigned long long>(editorStats.commands),
                editorStats.dirty ? "yes" : "no", editorStats.mapWidth, editorStats.mapHeight,
                editorStats.saveOk ? "ok" : "fail", editorStats.loadOk ? "ok" : "fail");
#else
    std::printf("  m26: disabled (release build)\n");
#endif
#if defined(FA_ENABLE_DEBUG_TOOLS)
    std::printf("M02+M03+M04+M05+M06+M07+M08+M09+M10+M11+M12+M13+M14+M15+M16+M17+M18+M19+M20+M21+M22+M23+M24+M26+M27 smoke test OK\n");
#else
    std::printf("M02+M03+M04+M05+M06+M07+M08+M09+M10+M11+M12+M13+M14+M15+M16+M17+M18+M19+M20+M21+M22+M23+M24 smoke test OK\n");
#endif
    return 0;
}
