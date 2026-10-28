/// \file
/// \brief Tests for M27: console parsing/history, typed CVars, log capture, stats
/// formatting, entity snapshots, overlay drawing and event routing. Everything is
/// headless; no window, renderer or audio device is opened.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fa/core/Log.hpp>
#include <fa/core/Math.hpp>
#include <fa/core/Result.hpp>
#include <fa/debug/DebugCVar.hpp>
#include <fa/debug/DebugConsole.hpp>
#include <fa/debug/DebugInspector.hpp>
#include <fa/debug/DebugLog.hpp>
#include <fa/debug/DebugStats.hpp>
#include <fa/debug/DebugTools.hpp>
#include <fa/physics/Collider.hpp>
#include <fa/platform/FileSystem.hpp>
#include <fa/platform/PlatformEvent.hpp>
#include <fa/render/RenderTypes.hpp>
#include <fa/scene/Components.hpp>
#include <fa/scene/World.hpp>
#include <fa/ui/Font.hpp>
#include <fa/ui/Ui.hpp>

namespace {

using Catch::Approx;

using fa::core::Status;
using fa::debug::ConsoleCommand;
using fa::debug::CVarRegistry;
using fa::debug::DebugConsole;
using fa::debug::DebugLogSink;
using fa::debug::DebugStats;
using fa::debug::DebugTools;
using fa::debug::formatInspection;
using fa::debug::formatLogLine;
using fa::debug::formatStats;
using fa::debug::inspectWorld;
using fa::debug::splitCommandLine;
using fa::debug::trimCommandLine;

/// \brief In-memory IFileSystem for CVar save/load.
class MemoryFileSystem final : public fa::platform::IFileSystem {
public:
    std::map<std::string, std::string, std::less<>> files;

    [[nodiscard]] bool exists(std::string_view path) const override { return files.find(path) != files.end(); }
    [[nodiscard]] bool isDirectory(std::string_view) const override { return false; }
    fa::core::Result<std::string> readText(std::string_view path) override {
        const auto found = files.find(path);
        if (found == files.end()) {
            return fa::core::Result<std::string>::err(fa::core::error_code::kNotFound, "missing");
        }
        return fa::core::Result<std::string>::ok(found->second);
    }
    fa::core::Result<std::string> readBinary(std::string_view path) override { return readText(path); }
    Status writeText(std::string_view path, std::string_view content) override {
        files[std::string(path)] = std::string(content);
        return fa::core::okStatus();
    }
    Status writeTextAtomic(std::string_view path, std::string_view content) override {
        return writeText(path, content);
    }
    Status createDirectories(std::string_view) override { return fa::core::okStatus(); }
    fa::core::Result<std::vector<std::string>> listDirectory(std::string_view) override {
        return fa::core::Result<std::vector<std::string>>::ok({});
    }
    Status remove(std::string_view path) override {
        files.erase(std::string(path));
        return fa::core::okStatus();
    }
};

}  // namespace

TEST_CASE("DebugConsole splits quoted command lines", "[debug]") {
    const std::vector<std::string> tokens = splitCommandLine("give \"item.turnip seed\" 3");
    REQUIRE(tokens.size() == 3);
    CHECK(tokens[0] == "give");
    CHECK(tokens[1] == "item.turnip seed");
    CHECK(tokens[2] == "3");

    const std::vector<std::string> escaped = splitCommandLine("echo hello\\ world");
    REQUIRE(escaped.size() == 2);
    CHECK(escaped[1] == "hello world");

    CHECK(trimCommandLine("  a b  ") == "a b");
    CHECK(trimCommandLine("   ").empty());
    CHECK(splitCommandLine("   ").empty());
}

TEST_CASE("DebugConsole registers and executes commands", "[debug]") {
    DebugConsole console;
    int calls = 0;
    REQUIRE(console.registerCommand(
        ConsoleCommand{"sum", "add two integers", [&calls](DebugConsole& out, std::span<const std::string> args) {
                           ++calls;
                           out.print(std::to_string(std::stoi(args[0]) + std::stoi(args[1])));
                       }}));
    CHECK_FALSE(console.registerCommand(
        ConsoleCommand{"sum", "duplicate", [](DebugConsole&, std::span<const std::string>) {}}));

    CHECK(console.execute("sum 2 3"));
    CHECK(calls == 1);
    REQUIRE_FALSE(console.lines().empty());
    CHECK(console.lines().back() == "5");
    REQUIRE(console.history().size() == 1);
    CHECK(console.history()[0] == "sum 2 3");

    CHECK_FALSE(console.execute("missing"));
    CHECK(console.lines().back().find("unknown command") != std::string::npos);

    static_cast<void>(console.execute("help"));
    bool sawSum = false;
    for (const std::string& line : console.lines()) {
        if (line.find("sum") != std::string::npos) {
            sawSum = true;
        }
    }
    CHECK(sawSum);

    static_cast<void>(console.execute("clear"));
    CHECK(console.lines().empty());
}

TEST_CASE("DebugConsole edits input and walks history", "[debug]") {
    DebugConsole console;
    REQUIRE(console.registerCommand(
        ConsoleCommand{"echo2", "echo first arg", [](DebugConsole& out, std::span<const std::string> args) {
                           out.print(args.empty() ? std::string{} : args[0]);
                       }}));

    console.setInput("echo2 one");
    CHECK(console.submitInput());
    console.setInput("echo2 two");
    CHECK(console.submitInput());
    CHECK(console.input().empty());

    console.historyPrevious();
    CHECK(console.input() == "echo2 two");
    console.historyPrevious();
    CHECK(console.input() == "echo2 one");
    console.historyNext();
    CHECK(console.input() == "echo2 two");
    console.historyNext();
    CHECK(console.input().empty());

    console.setInput("abc");
    console.backspace();
    CHECK(console.input() == "ab");
    console.appendInput("\r\n");
    CHECK(console.input() == "ab");
}

TEST_CASE("CVarRegistry stores typed values and fires callbacks", "[debug]") {
    CVarRegistry registry;
    int changes = 0;

    REQUIRE(registry.registerBool("debug.overlay", true, "overlay", [&changes] { ++changes; }));
    REQUIRE(registry.registerInt("debug.level", 2, "level"));
    REQUIRE(registry.registerFloat("debug.scale", 1.5f, "scale"));
    REQUIRE(registry.registerString("debug.name", "florian", "name"));
    CHECK_FALSE(registry.registerInt("debug.level", 0, "duplicate"));

    REQUIRE(registry.get("debug.overlay") != nullptr);
    CHECK(registry.get("debug.overlay")->boolean);

    CHECK(registry.set("debug.overlay", "false"));
    CHECK(changes == 1);
    CHECK_FALSE(registry.get("debug.overlay")->boolean);

    CHECK(registry.set("debug.level", "7"));
    CHECK(registry.get("debug.level")->integer == 7);
    CHECK_FALSE(registry.set("debug.level", "not-a-number"));

    CHECK(registry.set("debug.scale", "2.25"));
    CHECK(registry.get("debug.scale")->real == Approx(2.25f));

    CHECK(registry.set("debug.name", "flora"));
    CHECK(registry.get("debug.name")->text == "flora");
    CHECK_FALSE(registry.set("missing", "1"));
    CHECK(registry.size() == 4);
}

TEST_CASE("CVarRegistry saves and loads through IFileSystem", "[debug]") {
    MemoryFileSystem files;
    CVarRegistry registry;
    REQUIRE(registry.registerInt("debug.level", 1, "level"));
    REQUIRE(registry.registerString("debug.name", "florian", "name"));

    REQUIRE(registry.save(files, "cvars.cfg").isOk());
    CHECK(files.files["cvars.cfg"] == "debug.level=1\ndebug.name=florian\n");

    CVarRegistry loaded;
    REQUIRE(loaded.registerInt("debug.level", 0, "level"));
    REQUIRE(loaded.registerString("debug.name", "", "name"));
    REQUIRE(loaded.load(files, "cvars.cfg").isOk());
    CHECK(loaded.get("debug.level")->integer == 1);
    CHECK(loaded.get("debug.name")->text == "florian");

    files.files["cvars.cfg"] = "# comment\nunknown=1\ndebug.level=9\n";
    REQUIRE(loaded.load(files, "cvars.cfg").isOk());
    CHECK(loaded.get("debug.level")->integer == 9);
}

TEST_CASE("DebugLogSink filters and keeps the newest lines", "[debug]") {
    DebugLogSink sink(3);
    sink.setMinimumLevel(fa::core::LogLevel::Info);

    sink.write(fa::core::LogLevel::Debug, fa::core::LogCategory::Core, "hidden");
    sink.write(fa::core::LogLevel::Info, fa::core::LogCategory::Platform, "one");
    sink.write(fa::core::LogLevel::Warn, fa::core::LogCategory::Asset, "two");
    sink.write(fa::core::LogLevel::Error, fa::core::LogCategory::Audio, "three");
    sink.write(fa::core::LogLevel::Info, fa::core::LogCategory::Ui, "four");

    const std::vector<fa::debug::DebugLogLine> lines = sink.snapshot();
    REQUIRE(lines.size() == 3);
    CHECK(lines[0].message == "two");
    CHECK(lines[1].message == "three");
    CHECK(lines[2].message == "four");
    CHECK(sink.dropped() == 1);
    CHECK(formatLogLine(lines[0]) == "[warn][asset] two");

    sink.clear();
    CHECK(sink.snapshot().empty());
}

TEST_CASE("DebugStats formats a readable snapshot", "[debug]") {
    DebugStats stats;
    stats.fps = 60.0f;
    stats.frameMs = 16.7f;
    stats.entities = 3;
    stats.drawCalls = 12;
    stats.audioVoices = 2;
    stats.audioSampleRate = 48000;

    const std::vector<std::string> lines = formatStats(stats);
    REQUIRE(lines.size() >= 9);
    CHECK(lines[0] == "FA DEBUG");
    CHECK(lines[1].find("fps 60.0") != std::string::npos);
    CHECK(lines[1].find("frame 16.7") != std::string::npos);
    CHECK(lines[3].find("entities 3") != std::string::npos);
    CHECK(lines[4].find("12 calls") != std::string::npos);
    CHECK(lines[8].find("2 voices") != std::string::npos);
    CHECK(lines.back() == "ai targets none");

    stats.camera.valid = true;
    stats.camera.position = fa::core::Vec2{320.0f, 180.0f};
    stats.camera.viewportSize = fa::core::Vec2{640.0f, 360.0f};
    stats.camera.followTarget = fa::scene::EntityId{2u, 1u};
    stats.camera.following = true;
    stats.camera.deadzoneX = 48.0f;
    stats.camera.deadzoneY = 32.0f;
    stats.camera.smoothing = 10.0f;

    fa::debug::DebugAiTargetInfo ai;
    ai.agent = fa::scene::EntityId{4u, 1u};
    ai.target = fa::scene::EntityId{2u, 1u};
    ai.hasTarget = true;
    ai.targetVisible = true;
    ai.state = "chase";
    ai.agentPosition = fa::core::Vec2{400.0f, 200.0f};
    ai.targetPosition = fa::core::Vec2{320.0f, 180.0f};
    ai.hasAgentPosition = true;
    ai.hasTargetPosition = true;
    stats.aiTargets.push_back(ai);
    stats.hasPlayerPosition = true;
    stats.playerPosition = fa::core::Vec2{112.5f, 80.5f};
    stats.hasPlayerCell = true;
    stats.playerCell = fa::core::Vec2{7.0f, 5.0f};

    const std::vector<std::string> extended = formatStats(stats);
    bool foundCamera = false;
    bool foundFollow = false;
    bool foundAgent = false;
    bool foundPlayer = false;
    for (const std::string& line : extended) {
        if (line.find("player (112.5, 80.5)") != std::string::npos && line.find("tile (7, 5)") != std::string::npos) {
            foundPlayer = true;
        }
        if (line.find("camera (320.0, 180.0)") != std::string::npos &&
            line.find("view 640.0x360.0") != std::string::npos) {
            foundCamera = true;
        }
        if (line.find("follow player e2v1") != std::string::npos && line.find("dz 48.0x32.0") != std::string::npos) {
            foundFollow = true;
        }
        if (line.find("e4v1 chase -> e2v1 vis") != std::string::npos) {
            foundAgent = true;
        }
    }
    CHECK(foundCamera);
    CHECK(foundFollow);
    CHECK(foundAgent);
    CHECK(foundPlayer);
}

TEST_CASE("DebugInspector snapshots transforms, sprites and colliders", "[debug]") {
    fa::scene::World world;
    const fa::scene::EntityId id = world.create();
    fa::scene::Transform transform;
    transform.position = fa::core::Vec2{10.0f, 20.0f};
    static_cast<void>(world.add<fa::scene::Transform>(id, transform));

    fa::scene::Sprite sprite;
    sprite.size = fa::core::Vec2{16.0f, 24.0f};
    static_cast<void>(world.add<fa::scene::Sprite>(id, sprite));

    fa::physics::Collider collider;
    collider.offset = fa::core::Vec2{3.0f, 18.0f};
    collider.size = fa::core::Vec2{10.0f, 6.0f};
    static_cast<void>(world.add<fa::physics::Collider>(id, collider));

    static_cast<void>(world.create());

    const std::vector<fa::debug::EntityInspection> entities = inspectWorld(world);
    REQUIRE(entities.size() == 1);
    CHECK(entities[0].id == id);
    CHECK(entities[0].hasTransform);
    CHECK(entities[0].hasSprite);
    CHECK(entities[0].hasCollider);
    CHECK(entities[0].position.x == Approx(10.0f));
    CHECK(entities[0].spriteSize.y == Approx(24.0f));
    CHECK(entities[0].collider.x == Approx(13.0f));
    CHECK(entities[0].collider.y == Approx(38.0f));
    CHECK(entities[0].collider.w == Approx(10.0f));
    CHECK(entities[0].collider.h == Approx(6.0f));
    CHECK(formatInspection(entities[0]).find("collider") != std::string::npos);

    CHECK(inspectWorld(world, 0).empty());
}

TEST_CASE("DebugOverlay queues panels through UiContext", "[debug]") {
    const fa::ui::FontAtlas font = fa::ui::buildBuiltinFont();
    fa::ui::UiContext ui(font, fa::render::TextureHandle{1});
    ui.beginFrame(fa::ui::UiInputState{});
    ui.setOrigin(fa::core::Vec2{0.0f, 0.0f});

    DebugTools tools;
    DebugStats stats;
    stats.entities = 2;
    tools.setStats(stats);
    tools.setConsoleOpen(true);
    static_cast<void>(tools.console().execute("help"));
    tools.setLogVisible(true);
    tools.log().write(fa::core::LogLevel::Info, fa::core::LogCategory::Core, "hello");

    tools.draw(ui, fa::core::makeRect(0.0f, 0.0f, 640.0f, 360.0f));
    CHECK(ui.itemCount() > 0);
}

TEST_CASE("DebugTools routes keys and text without leaking to gameplay", "[debug]") {
    DebugTools tools;
    fa::platform::PlatformEvent event;

    event.type = fa::platform::PlatformEventType::KeyDown;
    event.scancode = fa::platform::Scancode::F3;
    CHECK(tools.handleEvent(event));
    CHECK_FALSE(tools.statsVisible());
    CHECK(tools.handleEvent(event));
    CHECK(tools.statsVisible());

    event.scancode = fa::platform::Scancode::F4;
    CHECK(tools.handleEvent(event));
    CHECK(tools.logVisible());

    event.scancode = fa::platform::Scancode::F5;
    CHECK(tools.handleEvent(event));
    CHECK(tools.physicsVisible());

    event.scancode = fa::platform::Scancode::Grave;
    CHECK(tools.handleEvent(event));
    CHECK(tools.consoleOpen());

    event.type = fa::platform::PlatformEventType::TextInput;
    event.text = "help";
    CHECK(tools.handleEvent(event));
    CHECK(tools.console().input() == "help");

    event.type = fa::platform::PlatformEventType::KeyDown;
    event.scancode = fa::platform::Scancode::Enter;
    CHECK(tools.handleEvent(event));
    CHECK(tools.console().input().empty());
    CHECK(tools.console().history().size() == 1);

    event.scancode = fa::platform::Scancode::Escape;
    CHECK(tools.handleEvent(event));
    CHECK_FALSE(tools.consoleOpen());

    event.scancode = fa::platform::Scancode::A;
    CHECK_FALSE(tools.handleEvent(event));

    event.type = fa::platform::PlatformEventType::TextInput;
    event.text = "x";
    CHECK_FALSE(tools.handleEvent(event));
}

TEST_CASE("DebugTools exposes CVar commands", "[debug]") {
    DebugTools tools;
    REQUIRE(tools.cvars().registerInt("debug.level", 1, "level"));

    CHECK(tools.console().execute("cvar.set debug.level 5"));
    REQUIRE(tools.cvars().get("debug.level") != nullptr);
    CHECK(tools.cvars().get("debug.level")->integer == 5);

    CHECK(tools.console().execute("cvar.get debug.level"));
    CHECK(tools.console().lines().back().find("debug.level = 5") != std::string::npos);

    CHECK(tools.console().execute("cvar.set debug.level nope"));
    CHECK(tools.cvars().get("debug.level")->integer == 5);
}
