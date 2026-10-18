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
#include <fa/assets/ITextureUploader.hpp>
#include <fa/core/Result.hpp>
#include <fa/core/Version.hpp>
#include <fa/content/ContentDatabase.hpp>
#include <fa/events/EventBus.hpp>
#include <fa/events/JobSystem.hpp>
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

/// \brief Records the one tool call the smoke run dispatches.
class CountingToolHandler final : public fa::interaction::IToolHandler {
public:
    [[nodiscard]] bool handles(fa::interaction::ToolKind tool) const noexcept override {
        return tool == fa::interaction::ToolKind::Hoe;
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
    explicit DemoScene(fa::input::IInput& input)
        : BasicScene("demo"), m_input(input), m_interaction(m_map, &m_placement) {
        static_cast<void>(m_map.reset(kColumns, kRows, fa::render::kTileSize));
        static_cast<void>(m_placement.resize(kColumns, kRows));
        m_interaction.addHandler(m_toolHandler);
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
            m_built = true;
        }

        auto player = std::make_unique<fa::player::PlayerSystem>(m_input);
        m_playerSystem = player.get();
        systems().add(std::move(player));

        auto physics = std::make_unique<fa::physics::PhysicsSystem>(&m_solids, 64.0f);
        m_physics = physics.get();
        systems().add(std::move(physics));

        auto tiles = std::make_unique<fa::tilemap::TilemapExtractSystem>(m_map);
        m_tileSystem = tiles.get();
        systems().add(std::move(tiles));
        systems().add(std::make_unique<fa::scene::SpriteExtractSystem>());
    }

    void onExit() override {
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

    [[nodiscard]] std::size_t groundTiles() const noexcept { return m_map.cellCount(); }

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
        use.tool = fa::interaction::ToolKind::Hoe;
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
    /// Declared after its map, grid and handler so those outlive it.
    fa::interaction::InteractionService m_interaction;
    fa::scene::EntityId m_markerEntity;
    fa::scene::EntityId m_crateEntity;
    fa::scene::EntityId m_placedEntity;
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
        }
        return status;
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
        auto scene = std::make_unique<DemoScene>(input);
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
        return fa::core::okStatus();
    }

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

    void onEvents(std::span<const fa::platform::PlatformEvent> events) override {
        input.poll(events);
        forwardedEvents += events.size();
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
        if (demo != nullptr) {
            demo->beginTick(context.fixedStepIndex);
        }
    }

    void onVariableUpdate(const fa::time::FrameContext& context) override {
        lastRealDelta = context.realDeltaSeconds;
        stack.variableUpdate(context);
    }

    void onRender(const fa::time::FrameContext&) override {
        ++renderedFrames;
        lastMouse = input.mousePosition();
        if (demo != nullptr) {
            demo->endFrame();
        }
        renderFrame();
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
    /// The handler must outlive the service that stores a non-owning pointer to it.
    std::unique_ptr<ConsumableUseHandler> consumable;
    std::unique_ptr<fa::inventory::ItemUseService> itemUse;
    InventoryDemoStats inventoryDemo;
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

        // M09 appends its HUD and widget quads after the world; the queue sort puts
        // them on RenderLayer::UI, so they draw last. Debug shapes go straight to the
        // renderer and share the same final sort.
        buildHud(cursor);
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
    // The context belongs to the renderer and dies with the window, so it has to go
    // before the platform shuts that window down.
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
    if (uiGlyphs == 0) {
        std::printf("  m09: UI unavailable (no font atlas)\n");
    } else {
        std::printf("  m09: %zu glyph(s) in a %dx%d atlas, %zu UI item(s), %zu press(es), HUD %s, slider %.2f\n",
                    uiGlyphs, uiAtlasWidth, uiAtlasHeight, uiItems, uiPresses, hudVisible ? "on" : "off",
                    static_cast<double>(uiSlider));
    }
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
    std::printf("M02+M03+M04+M05+M06+M07+M09+M11+M12+M13+M15+M16+M17+M18 smoke test OK\n");
    return 0;
}
