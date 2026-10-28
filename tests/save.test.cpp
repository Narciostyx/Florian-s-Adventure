/// \file
/// \brief Tests for M14: JSON archive round-trips, save slots, atomic backup
/// recovery, version migration and component serializer registration. Everything
/// uses an in-memory IFileSystem, so no real files are written.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fa/core/Math.hpp>
#include <fa/core/Result.hpp>
#include <fa/platform/FileSystem.hpp>
#include <fa/save/ComponentSerializer.hpp>
#include <fa/save/SaveArchive.hpp>
#include <fa/save/SaveError.hpp>
#include <fa/save/SaveService.hpp>
#include <fa/save/SaveTypes.hpp>
#include <fa/scene/World.hpp>

namespace {

using Catch::Approx;

using fa::core::Status;
using fa::save::ComponentSerializerRegistry;
using fa::save::IComponentSerializer;
using fa::save::kSaveVersion;
using fa::save::registerDefaultMigrations;
using fa::save::SaveArchive;
using fa::save::SaveContext;
using fa::save::SaveFile;
using fa::save::SaveMetadata;
using fa::save::SaveService;
namespace save_error = fa::save::save_error;

/// \brief In-memory IFileSystem with just enough directory support for save slots.
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
    fa::core::Result<std::vector<std::string>> listDirectory(std::string_view path) override {
        std::vector<std::string> names;
        const std::string prefix = std::string(path) + "/";
        for (const auto& [name, content] : files) {
            static_cast<void>(content);
            if (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0) {
                const std::string rest = name.substr(prefix.size());
                if (rest.find('/') == std::string::npos) {
                    names.push_back(rest);
                }
            }
        }
        return fa::core::Result<std::vector<std::string>>::ok(std::move(names));
    }
    Status remove(std::string_view path) override {
        files.erase(std::string(path));
        return fa::core::okStatus();
    }
};

SaveContext sampleContext(std::string slot, int day, double seconds) {
    SaveContext context;
    context.slotName = std::move(slot);
    context.sceneId = "farm";
    context.inGameDay = day;
    context.playSeconds = seconds;
    context.world.setInt("entities", 7);
    context.world.setVec2("marker", fa::core::Vec2{10.0f, 20.0f});
    context.player.setFloat("stamina", 55.5f);
    context.inventories.setInt("seeds", 21);
    context.quests.setString("active", "intro");
    context.economy.setInt("gold", 120);
    return context;
}

class FakeSerializer final : public IComponentSerializer {
public:
    std::string_view name() const noexcept override { return "fake"; }
    void save(const fa::scene::World&, SaveArchive& out) const override { out.setInt("value", savedValue); }
    void load(fa::scene::World&, const SaveArchive& in) const override {
        static_cast<void>(in.getInt("value", loadedValue));
    }

    int savedValue = 0;
    mutable int loadedValue = 0;
};

}  // namespace

TEST_CASE("SaveArchive round-trips primitives, vectors and rects", "[save]") {
    SaveArchive archive = SaveArchive::object();
    archive.setBool("b", true);
    archive.setInt("i", 42);
    archive.setInt64("i64", 1234567890123);
    archive.setFloat("f", 1.5f);
    archive.setDouble("d", 2.25);
    archive.setString("s", "hello");
    archive.setVec2("v", fa::core::Vec2{3.0f, 4.0f});
    archive.setRect("r", fa::core::Rect{1.0f, 2.0f, 3.0f, 4.0f});

    const std::string text = archive.dump();
    fa::core::Result<SaveArchive> parsed = SaveArchive::parse(text);
    REQUIRE(parsed.isOk());
    const SaveArchive loaded = std::move(parsed).take();

    bool boolean = false;
    CHECK(loaded.getBool("b", boolean));
    CHECK(boolean);
    int integer = 0;
    CHECK(loaded.getInt("i", integer));
    CHECK(integer == 42);
    std::int64_t large = 0;
    CHECK(loaded.getInt64("i64", large));
    CHECK(large == 1234567890123);
    float real = 0.0f;
    CHECK(loaded.getFloat("f", real));
    CHECK(real == Approx(1.5f));
    double precise = 0.0;
    CHECK(loaded.getDouble("d", precise));
    CHECK(precise == Approx(2.25));

    std::string textValue;
    CHECK(loaded.getString("s", textValue));
    CHECK(textValue == "hello");
    fa::core::Vec2 vector;
    CHECK(loaded.getVec2("v", vector));
    CHECK(vector.x == Approx(3.0f));
    CHECK(vector.y == Approx(4.0f));
    fa::core::Rect rect;
    CHECK(loaded.getRect("r", rect));
    CHECK(rect.x == Approx(1.0f));
    CHECK(rect.h == Approx(4.0f));
    CHECK(loaded.keys().size() == 8);

    CHECK(SaveArchive::parse("not json").isError());
    CHECK(SaveArchive::parse("not json").error().code == save_error::kParseFailed);
}

TEST_CASE("SaveArchive arrays and copies are independent", "[save]") {
    SaveArchive array = SaveArchive::array();
    array.push(SaveArchive::object());
    SaveArchive first = array.at(0);
    first.setInt("value", 1);
    array.push(first);
    REQUIRE(array.size() == 2);
    CHECK(array.at(0).isObject());

    SaveArchive original = SaveArchive::object();
    original.setInt("x", 1);
    SaveArchive copy = original;
    copy.setInt("x", 2);
    int value = 0;
    CHECK(original.getInt("x", value));
    CHECK(value == 1);
    CHECK(copy.getInt("x", value));
    CHECK(value == 2);
}

TEST_CASE("SaveService round-trips a slot", "[save]") {
    MemoryFileSystem files;
    SaveService service(files);

    REQUIRE(service.save("slot_1", sampleContext("slot_1", 3, 123.5)).isOk());
    CHECK(files.exists("saves/slot_1.json"));

    const fa::core::Result<SaveFile> loaded = service.load("slot_1");
    REQUIRE(loaded.isOk());
    const SaveFile& file = loaded.value();
    CHECK(file.meta.saveVersion == kSaveVersion);
    CHECK(file.meta.slotName == "slot_1");
    CHECK(file.meta.sceneId == "farm");
    CHECK(file.meta.inGameDay == 3);
    CHECK(file.meta.playSeconds == Approx(123.5));
    CHECK_FALSE(file.recoveredFromBackup);

    int entities = 0;
    CHECK(file.world.getInt("entities", entities));
    CHECK(entities == 7);
    fa::core::Vec2 marker;
    CHECK(file.world.getVec2("marker", marker));
    CHECK(marker.x == Approx(10.0f));
    float stamina = 0.0f;
    CHECK(file.player.getFloat("stamina", stamina));
    CHECK(stamina == Approx(55.5f));
    int seeds = 0;
    CHECK(file.inventories.getInt("seeds", seeds));
    CHECK(seeds == 21);
}

TEST_CASE("SaveService recovers a corrupt primary from the backup", "[save]") {
    MemoryFileSystem files;
    SaveService service(files);

    REQUIRE(service.save("slot_1", sampleContext("slot_1", 1, 10.0)).isOk());
    REQUIRE(service.save("slot_1", sampleContext("slot_1", 2, 20.0)).isOk());
    CHECK(files.exists("saves/slot_1.json.bak"));

    files.files["saves/slot_1.json"] = "{ broken";
    const fa::core::Result<SaveFile> loaded = service.load("slot_1");
    REQUIRE(loaded.isOk());
    CHECK(loaded.value().recoveredFromBackup);
    CHECK(loaded.value().meta.inGameDay == 1);
    CHECK(loaded.value().meta.playSeconds == Approx(10.0));
}

TEST_CASE("SaveService lists slots, marks corrupt files and deletes", "[save]") {
    MemoryFileSystem files;
    SaveService service(files);
    REQUIRE(service.save("slot_1", sampleContext("slot_1", 1, 1.0)).isOk());
    REQUIRE(service.save("slot_2", sampleContext("slot_2", 2, 2.0)).isOk());
    files.files["saves/broken.json"] = "nonsense";

    const std::vector<SaveMetadata> slots = service.listSlots();
    REQUIRE(slots.size() == 3);
    bool sawSlot1 = false;
    bool sawBroken = false;
    for (const SaveMetadata& metadata : slots) {
        if (metadata.slotName == "slot_1") {
            sawSlot1 = metadata.readable;
        }
        if (metadata.slotName == "broken") {
            sawBroken = !metadata.readable;
        }
    }
    CHECK(sawSlot1);
    CHECK(sawBroken);

    CHECK(service.deleteSlot("slot_1").isOk());
    CHECK_FALSE(files.exists("saves/slot_1.json"));
    CHECK(service.deleteSlot("missing").isError());
    CHECK(service.deleteSlot("../bad").error().code == save_error::kInvalidSlot);

    CHECK(SaveService::isValidSlotName("slot_1"));
    CHECK_FALSE(SaveService::isValidSlotName(""));
    CHECK_FALSE(SaveService::isValidSlotName("../slot"));
    CHECK_FALSE(SaveService::isValidSlotName(std::string(65, 'a')));
}

TEST_CASE("SaveService rejects newer versions and runs migrations", "[save]") {
    MemoryFileSystem files;
    SaveService versionOne(files, "saves", 1);
    REQUIRE(versionOne.save("slot_1", sampleContext("slot_1", 1, 1.0)).isOk());

    SaveService versionTwo(files, "saves", 2);
    versionTwo.registerMigration(1, [](SaveArchive& root) {
        SaveArchive world = root.child("world");
        world.setInt("migrated", 1);
        root.setChild("world", std::move(world));
        return fa::core::okStatus();
    });
    const fa::core::Result<SaveFile> migrated = versionTwo.load("slot_1");
    REQUIRE(migrated.isOk());
    CHECK(migrated.value().meta.saveVersion == 2);
    int marker = 0;
    CHECK(migrated.value().world.getInt("migrated", marker));
    CHECK(marker == 1);

    REQUIRE(versionTwo.save("slot_2", sampleContext("slot_2", 2, 2.0)).isOk());
    const fa::core::Result<SaveFile> tooNew = versionOne.load("slot_2");
    REQUIRE(tooNew.isError());
    CHECK(tooNew.error().code == save_error::kUnsupportedVersion);

    SaveService versionTwoNoMigration(files, "saves", 2);
    const fa::core::Result<SaveFile> missing = versionTwoNoMigration.load("slot_1");
    REQUIRE(missing.isError());
    CHECK(missing.error().code == save_error::kMigrationFailed);
}

TEST_CASE("Default migrations normalize v1 saves", "[save]") {
    MemoryFileSystem files;
    SaveService versionOne(files, "saves", 1);
    SaveContext oldContext;
    oldContext.inGameDay = 1;
    oldContext.world.setString("map", "farm");
    REQUIRE(versionOne.save("old", oldContext).isOk());

    SaveService current(files, "saves");
    registerDefaultMigrations(current);
    const fa::core::Result<SaveFile> loaded = current.load("old");
    REQUIRE(loaded.isOk());
    CHECK(loaded.value().meta.saveVersion == kSaveVersion);
    CHECK(loaded.value().world.child("objects").isArray());
    CHECK(loaded.value().world.child("debris").isArray());
    CHECK(loaded.value().world.child("ai").isArray());
    CHECK(loaded.value().quests.child("affinity").isObject());
    CHECK(loaded.value().quests.child("quests").isArray());
    CHECK(loaded.value().economy.child("shipping").isArray());
    CHECK(loaded.value().inventories.isObject());

    // A file already written by v2 keeps loading without double-normalising.
    REQUIRE(current.save("new", oldContext).isOk());
    const fa::core::Result<SaveFile> reloaded = current.load("new");
    REQUIRE(reloaded.isOk());
    CHECK(reloaded.value().meta.saveVersion == kSaveVersion);
}

TEST_CASE("ComponentSerializerRegistry saves and loads sections", "[save]") {
    fa::scene::World world;
    ComponentSerializerRegistry registry;
    auto serializer = std::make_unique<FakeSerializer>();
    FakeSerializer* raw = serializer.get();
    raw->savedValue = 7;
    REQUIRE(registry.add(std::move(serializer)));
    CHECK_FALSE(registry.add(std::make_unique<FakeSerializer>()));
    CHECK(registry.size() == 1);
    REQUIRE(registry.at(0) != nullptr);

    SaveArchive sections;
    registry.saveAll(world, sections);
    int value = 0;
    CHECK(sections.child("fake").getInt("value", value));
    CHECK(value == 7);

    raw->loadedValue = 0;
    registry.loadAll(world, sections);
    CHECK(raw->loadedValue == 7);
}
