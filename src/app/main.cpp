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
#include <fa/platform/FileSystem.hpp>
#include <fa/platform/Platform.hpp>
#include <fa/render/IRenderer.hpp>
#include <fa/render/RenderTypes.hpp>
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

/// \brief Temporary sink: counts events and reports the frame timing it observes.
///
/// Stands in for the layers that will own this work later (M03 events, M06 render)
/// so the loop itself can be verified from the app. It also owns the M07 input system
/// and drives it in the order the module contract requires:
/// `poll(events)` in onEvents, read actions during the updates, `endFrame()` in onRender.
///
/// M06 is driven in the same hook: one texture is uploaded at startup and every frame
/// draws a full screen of ground tiles plus a debug overlay, which is a small but real
/// exercise of batching, sorting, the viewport mapping and the debug helpers.
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
        return fa::core::okStatus();
    }

    /// \brief Drops the renderer, and with it the GL context, before the window closes.
    ///
    /// Handles go first, then the cache (whose destructor releases the GPU textures),
    /// and only then the renderer those textures belong to.
    void shutdownRenderer() {
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
        if (demo != nullptr) {
            demo->beginTick(context.fixedStepIndex);
        }
    }

    void onVariableUpdate(const fa::time::FrameContext& context) override {
        lastRealDelta = context.realDeltaSeconds;
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
        const fa::render::TextureHandle groundHandle{ground->gpuTexture};
        const fa::render::TextureHandle markerHandle{marker->gpuTexture};

        if (!cameraReady) {
            // A 40x23 tile plot: wider than the viewport, half a tile taller, so the
            // vertical clamp actually moves the camera.
            camera.setBounds(fa::core::makeRect(0.0f, 0.0f, 40.0f * fa::render::kTileSize,
                                                23.0f * fa::render::kTileSize));
            cameraReady = true;
        }
        camera.setPosition(fa::core::Vec2{320.0f + static_cast<float>(renderedFrames % 5), 200.0f});

        renderer->beginFrame(camera);

        const fa::core::Rect visible = camera.visibleRect();
        const int firstRow = static_cast<int>(visible.y) / fa::render::kTileSize;
        const int lastRow = static_cast<int>(visible.y + visible.h) / fa::render::kTileSize;
        constexpr int kColumns = 40;
        for (int row = firstRow; row <= lastRow; ++row) {
            for (int column = 0; column < kColumns; ++column) {
                fa::render::DrawItem item;
                item.texture = groundHandle;
                item.position = fa::core::Vec2{static_cast<float>(column * fa::render::kTileSize),
                                               static_cast<float>(row * fa::render::kTileSize)};
                item.size = fa::core::Vec2{static_cast<float>(fa::render::kTileSize),
                                           static_cast<float>(fa::render::kTileSize)};
                item.layer = fa::render::RenderLayer::Ground;
                item.sortY = item.position.y + item.size.y;
                item.entityId = static_cast<std::uint32_t>(row * kColumns + column + 1);
                renderer->submit(item);
            }
        }

        // Mouse position proves the window -> virtual -> world chain: the viewport
        // strips the letterbox, the camera removes its own offset.
        const fa::render::Viewport viewport = renderer->viewport();
        const fa::core::Vec2 cursor = camera.viewportToWorld(viewport.windowToVirtual(lastMouse));

        fa::render::DrawItem markerItem;
        markerItem.texture = markerHandle;
        markerItem.position = fa::core::Vec2{cursor.x - 4.0f, cursor.y - 4.0f};
        markerItem.size = fa::core::Vec2{8.0f, 8.0f};
        markerItem.tint = fa::core::Color{1.0f, 0.35f, 0.35f, 1.0f};
        markerItem.layer = fa::render::RenderLayer::Actors;
        markerItem.sortY = cursor.y;
        renderer->submit(markerItem);

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
    std::printf("M02+M03+M04+M05+M06+M07 smoke test OK\n");
    return 0;
}
