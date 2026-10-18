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
#include <fa/events/EventBus.hpp>
#include <fa/events/JobSystem.hpp>
#include <fa/input/Input.hpp>
#include <fa/physics/Collider.hpp>
#include <fa/physics/CollisionLayer.hpp>
#include <fa/physics/PhysicsSystem.hpp>
#include <fa/physics/PhysicsWorld.hpp>
#include <fa/physics/TileCollision.hpp>
#include <fa/platform/FileSystem.hpp>
#include <fa/platform/Platform.hpp>
#include <fa/render/IRenderer.hpp>
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
#include <fa/time/FrameContext.hpp>
#include <fa/time/FrameLoop.hpp>

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
    {"id": "tex.actor.player", "kind": "texture", "size": [16, 24],
     "color": "#e8c39e", "altColor": "#c79a74"},
    {"id": "tex.marker", "kind": "texture", "size": [8, 8], "color": "#ffffff"}
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

/// \brief The demo world: a tile map with a solid wall, a trigger, and one body.
///
/// This is the smallest scene that shows M11 and M13 working together. The ground and
/// the marker are entities, the marker's motion comes from `PhysicsSystem` integrating
/// a `Velocity`, and a wall column stops it part way across the plot - so the smoke run
/// proves the solver rather than just that a number changed. A trigger volume on the
/// path reports one enter and one leave.
class DemoScene final : public fa::scene::BasicScene {
public:
    static constexpr int kColumns = 40;
    static constexpr int kRows = 23;
    /// \brief Column of the one-tile wall the marker drives into.
    static constexpr int kWallColumn = 22;
    static constexpr float kMarkerSpeed = 120.0f;
    static constexpr float kMarkerSize = 8.0f;

    DemoScene() : BasicScene("demo") {
        m_tiles.resize(kColumns, kRows, static_cast<float>(fa::render::kTileSize));
        for (int row = 0; row < kRows; ++row) {
            static_cast<void>(m_tiles.setSolid(kWallColumn, row, true));
        }
        m_tiles.setBorderSolid(true);
    }

    /// \brief Hands the scene the textures its entities draw with.
    ///
    /// Only the asset manager can produce these, and it needs a GL context, so the
    /// scene is configured right before it is pushed.
    void setTextures(fa::render::TextureHandle ground, fa::render::TextureHandle marker) {
        m_ground = ground;
        m_marker = marker;
    }

    /// \brief Turns the input axis into the marker's velocity.
    ///
    /// The scene owns the intent, `PhysicsWorld` is the only writer of position. That
    /// split is what lets M16 replace this method later without touching the solver.
    void setMoveAxis(fa::core::Vec2 axis) {
        fa::physics::Velocity* velocity = world().tryGet<fa::physics::Velocity>(m_markerEntity);
        if (velocity != nullptr) {
            velocity->linear = fa::core::Vec2{axis.x * kMarkerSpeed, axis.y * kMarkerSpeed};
        }
    }

    void onEnter() override {
        if (!m_built) {
            buildWorld();
            m_built = true;
        }
        auto physics = std::make_unique<fa::physics::PhysicsSystem>(&m_tiles, 64.0f);
        m_physics = physics.get();
        systems().add(std::move(physics));
        systems().add(std::make_unique<fa::scene::SpriteExtractSystem>());
    }

    void onExit() override {
        // The physics system borrows `m_tiles`, so it has to die before the grid does.
        systems().clear();
        m_physics = nullptr;
    }

    /// \brief The marker, so the caller can prove the world really moved.
    [[nodiscard]] fa::core::Vec2 markerPosition() const {
        const fa::scene::Transform* transform = world().tryGet<fa::scene::Transform>(m_markerEntity);
        return transform != nullptr ? transform->position : fa::core::Vec2{};
    }

    [[nodiscard]] std::size_t groundTiles() const noexcept { return m_groundTiles; }

    [[nodiscard]] std::size_t solidTiles() const noexcept { return m_tiles.solidCount(); }

    [[nodiscard]] fa::physics::PhysicsStats physicsStats() const {
        return m_physics != nullptr ? m_physics->physics().stats() : fa::physics::PhysicsStats{};
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
        const float tile = static_cast<float>(fa::render::kTileSize);
        for (int row = 0; row < kRows; ++row) {
            for (int column = 0; column < kColumns; ++column) {
                const fa::scene::EntityId id = world().create();
                fa::scene::Transform transform;
                transform.position = fa::core::Vec2{static_cast<float>(column) * tile,
                                                     static_cast<float>(row) * tile};
                static_cast<void>(world().add<fa::scene::Transform>(id, transform));

                fa::scene::Sprite sprite;
                sprite.texture = m_ground;
                sprite.size = fa::core::Vec2{tile, tile};
                sprite.layer = fa::render::RenderLayer::Ground;
                // Solid cells get a darker tint, so the wall is visible and not only felt.
                sprite.tint = m_tiles.isSolid(column, row) ? fa::core::Color{0.35f, 0.4f, 0.55f, 1.0f}
                                                           : fa::core::Color{1.0f, 1.0f, 1.0f, 1.0f};
                static_cast<void>(world().add<fa::scene::Sprite>(id, sprite));
                ++m_groundTiles;
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
    }

    fa::render::TextureHandle m_ground;
    fa::render::TextureHandle m_marker;
    /// Declared before the systems are created so it outlives them: `PhysicsSystem`
    /// borrows the grid, and `onExit` clears the pipeline before this member dies.
    fa::physics::TileGrid m_tiles;
    fa::scene::EntityId m_markerEntity;
    fa::physics::PhysicsSystem* m_physics = nullptr;
    std::size_t m_groundTiles = 0;
    bool m_built = false;
};

/// \brief Temporary sink: counts events and reports the frame timing it observes.
///
/// Stands in for the layers that will own this work later (M03 events, M06 render)
/// so the loop itself can be verified from the app. It also owns the M07 input system
/// and drives it in the order the module contract requires:
/// `poll(events)` in onEvents, read actions during the updates, `endFrame()` in onRender.
///
/// M06 is driven in the same hook: one texture is uploaded at startup and every frame
/// draws a full screen of ground tiles plus a debug overlay, which is a small but real
/// exercise of batching, sorting, the viewport mapping and the debug helpers. The draw
/// items themselves come from M11: the scene stack extracts the world into a render
/// queue and the renderer takes the queue in one submitAll() call.
class SmokeSink final : public fa::time::IFrameSink {
public:
    SmokeSink() {
        input.addDevice(std::make_unique<fa::input::KeyboardDevice>());
        input.addDevice(std::make_unique<fa::input::MouseDevice>());
        input.applyDefaultBindings();
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

    /// \brief Loads the catalog and uploads the two textures the frame draws.
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

        fa::core::Result<fa::assets::AssetHandle<fa::assets::TextureAsset>> ground =
            assets->loadTexture(fa::assets::AssetId::fromName("tex.ground.grass"));
        if (ground.isError()) {
            return fa::core::errorStatus(ground.error().code, ground.error().message);
        }
        groundTexture = std::move(ground).take();

        fa::core::Result<fa::assets::AssetHandle<fa::assets::TextureAsset>> marker =
            assets->loadTexture(fa::assets::AssetId::fromName("tex.marker"));
        if (marker.isError()) {
            return fa::core::errorStatus(marker.error().code, marker.error().message);
        }
        markerTexture = std::move(marker).take();

        if (!groundTexture || !markerTexture) {
            return fa::core::errorStatus(fa::assets::asset_error::kUploadFailed, "a smoke texture stayed unusable");
        }

        const fa::assets::TextureAsset* groundAsset = groundTexture.get();
        const fa::assets::TextureAsset* markerAsset = markerTexture.get();
        if (groundAsset == nullptr || markerAsset == nullptr) {
            return fa::core::errorStatus(fa::assets::asset_error::kUploadFailed, "a smoke texture stayed unusable");
        }

        // M11 takes over from here: the textures go into a scene, and the scene becomes
        // the only source of draw items.
        auto scene = std::make_unique<DemoScene>();
        scene->setTextures(fa::render::TextureHandle{groundAsset->gpuTexture},
                           fa::render::TextureHandle{markerAsset->gpuTexture});
        demoScene = scene.get();
        const fa::core::Status pushed = stack.push(std::move(scene));
        if (pushed.isError()) {
            demoScene = nullptr;
            return pushed;
        }
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
        groundTexture = fa::assets::AssetHandle<fa::assets::TextureAsset>{};
        markerTexture = fa::assets::AssetHandle<fa::assets::TextureAsset>{};
        assets.reset();
        uploader.reset();
        fileSystem = nullptr;
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
            demoScene->setMoveAxis(scripted);
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
    fa::assets::AssetCatalog catalog;
    /// Borrowed from the platform, which owns the only file system in the run.
    fa::platform::IFileSystem* fileSystem = nullptr;
    std::unique_ptr<AssetUploaderAdapter> uploader;
    std::unique_ptr<fa::assets::IAssetManager> assets;
    fa::assets::AssetHandle<fa::assets::TextureAsset> groundTexture;
    fa::assets::AssetHandle<fa::assets::TextureAsset> markerTexture;
    /// Owns the scene graph. Cleared before the renderer goes away: the scenes only
    /// store texture handles, but their sprites should not outlive the GPU textures.
    fa::scene::SceneStack stack;
    DemoScene* demoScene = nullptr;
    fa::render::RenderQueue renderQueue;
    std::size_t extractedItems = 0;
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

    /// \brief Scene-side counters, snapshotted before the stack is cleared.
    fa::scene::SceneStackStats sceneStackStats() const { return stack.stats(); }
    fa::scene::WorldStats sceneWorldStats() const {
        return demoScene != nullptr ? demoScene->world().stats() : fa::scene::WorldStats{};
    }
    std::size_t sceneGroundTiles() const { return demoScene != nullptr ? demoScene->groundTiles() : 0; }
    std::size_t sceneSolidTiles() const { return demoScene != nullptr ? demoScene->solidTiles() : 0; }
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

private:
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
        const fa::assets::TextureAsset* ground = groundTexture.get();
        const fa::assets::TextureAsset* marker = markerTexture.get();
        if (ground == nullptr || marker == nullptr || ground->gpuTexture == 0 || marker->gpuTexture == 0) {
            return;
        }

        if (!cameraReady) {
            // A 40x23 tile plot: wider than the viewport, half a tile taller, so the
            // vertical clamp actually moves the camera.
            camera.setBounds(fa::core::makeRect(0.0f, 0.0f, 40.0f * fa::render::kTileSize,
                                                23.0f * fa::render::kTileSize));
            cameraReady = true;
        }
        camera.setPosition(fa::core::Vec2{320.0f + static_cast<float>(renderedFrames % 5), 200.0f});

        renderer->beginFrame(camera);

        // M11 -> M06: the scene stack extracts every layer into one queue (bottom scene
        // first, so a paused world still shows through a menu), the queue sorts itself,
        // and the renderer takes the whole batch at once.
        renderQueue.clear();
        stack.renderExtract(renderQueue);
        renderQueue.sort();
        renderer->submitAll(renderQueue.items());
        extractedItems = renderQueue.size();

        // Mouse position proves the window -> virtual -> world chain: the viewport
        // strips the letterbox, the camera removes its own offset.
        const fa::render::Viewport viewport = renderer->viewport();
        const fa::core::Vec2 cursor = camera.viewportToWorld(viewport.windowToVirtual(lastMouse));

        renderer->drawDebugRect(fa::core::makeRect(cursor.x - 8.0f, cursor.y - 8.0f, 16.0f, 16.0f),
                                fa::core::Color{1.0f, 1.0f, 0.2f, 1.0f});
        renderer->drawDebugLine(fa::core::Vec2{cursor.x, cursor.y},
                                fa::core::Vec2{cursor.x + 24.0f, cursor.y - 16.0f},
                                fa::core::Color{0.2f, 1.0f, 0.6f, 1.0f});

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
    const std::size_t catalogEntries = sink.catalog.size();
    // Scene counters die with the stack, which shutdownRenderer() clears first.
    const fa::scene::SceneStackStats stackStats = sink.sceneStackStats();
    const fa::scene::WorldStats worldStats = sink.sceneWorldStats();
    const fa::core::Vec2 marker = sink.sceneMarkerPosition();
    const std::size_t systemCount = sink.sceneSystemCount();
    const std::size_t groundTiles = sink.sceneGroundTiles();
    const std::size_t solidTiles = sink.sceneSolidTiles();
    // Probe first: the ray is a real query and its counter belongs in the summary.
    const float probeDistance = sink.sceneProbeDistance();
    const fa::physics::PhysicsStats physicsStats = sink.scenePhysicsStats();
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
    std::printf("       %zu ground tile(s), %zu solid, marker at (%g, %g)\n", groundTiles, solidTiles,
                static_cast<double>(marker.x), static_cast<double>(marker.y));
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
    std::printf("M02+M03+M04+M05+M06+M07+M11+M13 smoke test OK\n");
    return 0;
}
