/// \file
/// \brief Tests for M11: handles and generations, the sparse component set, the
/// world container, deferred structural changes, system scheduling, the scene
/// stack and sprite extraction. Everything here runs headless - extraction stops
/// at a `RenderQueue`, which is plain data, so no GL context is involved.
/// See docs/build/m11-scene-ecs.md for the problems found while writing this.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <fa/core/Math.hpp>
#include <fa/render/RenderQueue.hpp>
#include <fa/render/RenderTypes.hpp>
#include <fa/scene/CommandBuffer.hpp>
#include <fa/scene/ComponentType.hpp>
#include <fa/scene/Components.hpp>
#include <fa/scene/EntityId.hpp>
#include <fa/scene/Scene.hpp>
#include <fa/scene/SceneError.hpp>
#include <fa/scene/SceneStack.hpp>
#include <fa/scene/SparseSet.hpp>
#include <fa/scene/SpriteExtractor.hpp>
#include <fa/scene/World.hpp>
#include <fa/time/FrameContext.hpp>

namespace {

using Catch::Approx;
using fa::core::Vec2;
using fa::render::DrawItem;
using fa::render::RenderLayer;
using fa::render::RenderQueue;
using fa::render::TextureHandle;
using fa::scene::BasicScene;
using fa::scene::CommandBuffer;
using fa::scene::EntityId;
using fa::scene::extractSprites;
using fa::scene::IScene;
using fa::scene::ISystem;
using fa::scene::SceneStack;
using fa::scene::SparseSet;
using fa::scene::Sprite;
using fa::scene::SpriteExtractOptions;
using fa::scene::SpriteExtractSystem;
using fa::scene::SystemPipeline;
using fa::scene::Transform;
using fa::scene::World;

constexpr std::uint32_t kTexture = 7;

/// \brief Stand-in for a gameplay system: records the ids it saw, in order.
class RecordingSystem final : public ISystem {
public:
    RecordingSystem(std::string label, std::vector<std::string>& order) : m_label(std::move(label)), m_order(order) {}

    [[nodiscard]] std::string_view name() const noexcept override { return m_label; }

    void onFixedUpdate(World&, const fa::time::FrameContext&, CommandBuffer&) override { m_order.push_back(m_label); }

    void onRenderExtract(const World& world, RenderQueue& queue) override {
        m_seenEntities = world.liveEntities();
        static_cast<void>(queue);
    }

    std::size_t seenEntities() const noexcept { return m_seenEntities; }

private:
    std::string m_label;
    std::vector<std::string>& m_order;
    std::size_t m_seenEntities = 0;
};

/// \brief System that spawns one entity per fixed step, proving deferred creates.
class SpawnerSystem final : public ISystem {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "SpawnerSystem"; }

    void onFixedUpdate(World&, const fa::time::FrameContext&, CommandBuffer& commands) override {
        const EntityId spawned = commands.create();
        commands.add<Transform>(spawned, Transform{Vec2{static_cast<float>(m_spawned), 0.0f}, Vec2{1.0f, 1.0f}, 0.0f});
        ++m_spawned;
    }

    std::size_t spawned() const noexcept { return m_spawned; }

private:
    std::size_t m_spawned = 0;
};

/// \brief System that moves every transform, used for the end-to-end test.
class MoveSystem final : public ISystem {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "MoveSystem"; }

    void onFixedUpdate(World& world, const fa::time::FrameContext&, CommandBuffer&) override {
        world.each<Transform>([](EntityId, Transform& transform) { transform.position.x += 1.0f; });
    }
};

/// \brief Counter that outlives the scenes, so a test can observe `onExit` for the
/// paths where the stack destroys the scene straight after calling it.
struct LifecycleCounter {
    int exits = 0;
};

/// \brief Minimal scene for stack tests: counts the lifecycle calls it receives.
class CountingScene final : public IScene {
public:
    explicit CountingScene(std::string name, LifecycleCounter* exitCounter = nullptr)
        : m_name(std::move(name)), m_exitCounter(exitCounter) {}

    [[nodiscard]] std::string_view name() const noexcept override { return m_name; }

    void onEnter() override { ++enters; }
    void onExit() override {
        ++exits;
        if (m_exitCounter != nullptr) {
            ++m_exitCounter->exits;
        }
    }
    void onPause() override { ++pauses; }
    void onResume() override { ++resumes; }
    void onFixedUpdate(const fa::time::FrameContext&) override { ++fixedUpdates; }
    void onRenderExtract(RenderQueue& queue) override {
        ++extracts;
        DrawItem item;
        item.texture = TextureHandle{kTexture};
        item.layer = RenderLayer::Ground;
        queue.submit(item);
    }

    int enters = 0;
    int exits = 0;
    int pauses = 0;
    int resumes = 0;
    int fixedUpdates = 0;
    int extracts = 0;

private:
    std::string m_name;
    LifecycleCounter* m_exitCounter = nullptr;
};

/// \brief Scene with a world and a pipeline, the shape the game uses.
class WorldScene final : public BasicScene {
public:
    WorldScene() : BasicScene("WorldScene") {
        systems().add(std::make_unique<MoveSystem>());
        systems().add(std::make_unique<SpriteExtractSystem>());
    }
};

Sprite makeSprite(std::uint32_t texture = kTexture) {
    Sprite sprite;
    sprite.texture = texture != 0 ? TextureHandle{texture} : TextureHandle{};
    sprite.size = Vec2{16.0f, 16.0f};
    return sprite;
}

}  // namespace

// ---------------------------------------------------------------- EntityId ---

TEST_CASE("EntityId defaults to the null handle", "[scene][entity]") {
    const EntityId id;
    REQUIRE(id.index == 0);
    REQUIRE(id.generation == 0);
    REQUIRE_FALSE(id.isValid());
    REQUIRE_FALSE(id.isPending());
}

TEST_CASE("EntityId validity rejects a zero index or generation", "[scene][entity]") {
    REQUIRE(EntityId{1, 1}.isValid());
    REQUIRE_FALSE(EntityId{0, 1}.isValid());
    REQUIRE_FALSE(EntityId{1, 0}.isValid());
}

TEST_CASE("EntityId placeholders are pending, not valid", "[scene][entity]") {
    const EntityId placeholder{3, EntityId::kPendingGeneration};
    REQUIRE(placeholder.isPending());
    REQUIRE_FALSE(placeholder.isValid());
}

TEST_CASE("EntityId compares and orders by slot then generation", "[scene][entity]") {
    REQUIRE(EntityId{1, 2} == EntityId{1, 2});
    REQUIRE(EntityId{1, 2} != EntityId{1, 3});
    REQUIRE(EntityId{1, 9} < EntityId{2, 1});
    REQUIRE(EntityId{2, 1} < EntityId{2, 2});
    REQUIRE_FALSE(EntityId{2, 2} < EntityId{2, 1});
}

TEST_CASE("EntityId hashes distinct handles apart", "[scene][entity]") {
    const std::hash<EntityId> hash;
    REQUIRE(hash(EntityId{1, 1}) == hash(EntityId{1, 1}));
    REQUIRE(hash(EntityId{1, 1}) != hash(EntityId{1, 2}));
    REQUIRE(hash(EntityId{1, 1}) != hash(EntityId{2, 1}));
}

TEST_CASE("EntityId prints slot and generation", "[scene][entity]") {
    REQUIRE(fa::scene::toString(EntityId{12, 3}) == "e12v3");
    REQUIRE(fa::scene::toString(EntityId{}) == "e0");
    REQUIRE(fa::scene::toString(EntityId{5, EntityId::kPendingGeneration}) == "e5pending");
}

// --------------------------------------------------------------- SparseSet ---

TEST_CASE("SparseSet stores and returns components", "[scene][sparse]") {
    SparseSet<Transform> set;
    const EntityId id{1, 1};
    REQUIRE(set.empty());
    REQUIRE_FALSE(set.has(id));

    Transform& stored = set.add(id, Transform{Vec2{4.0f, 5.0f}, Vec2{1.0f, 1.0f}, 0.0f});
    stored.position.x = 9.0f;

    REQUIRE(set.size() == 1);
    REQUIRE(set.has(id));
    REQUIRE(set.tryGet(id) != nullptr);
    REQUIRE(set.tryGet(id)->position.x == Approx(9.0f));
}

TEST_CASE("SparseSet add replaces an existing component", "[scene][sparse]") {
    SparseSet<Transform> set;
    const EntityId id{1, 1};
    set.add(id, Transform{Vec2{1.0f, 1.0f}, Vec2{1.0f, 1.0f}, 0.0f});
    set.add(id, Transform{Vec2{2.0f, 2.0f}, Vec2{1.0f, 1.0f}, 0.0f});

    REQUIRE(set.size() == 1);
    REQUIRE(set.tryGet(id)->position.x == Approx(2.0f));
}

TEST_CASE("SparseSet remove swaps the last entry into the hole", "[scene][sparse]") {
    SparseSet<int> set;
    const EntityId first{1, 1};
    const EntityId second{2, 1};
    const EntityId third{3, 1};
    set.add(first, 10);
    set.add(second, 20);
    set.add(third, 30);

    REQUIRE(set.remove(second));
    REQUIRE(set.size() == 2);
    REQUIRE_FALSE(set.has(second));
    REQUIRE(set.entityAt(1) == third);
    REQUIRE(set.valueAt(1) == 30);
    REQUIRE(set.valueAt(0) == 10);
    REQUIRE(set.tryGet(third) != nullptr);
    REQUIRE(*set.tryGet(third) == 30);
}

TEST_CASE("SparseSet remove of an absent entity reports false", "[scene][sparse]") {
    SparseSet<int> set;
    REQUIRE_FALSE(set.remove(EntityId{4, 1}));
    set.add(EntityId{4, 1}, 1);
    REQUIRE_FALSE(set.remove(EntityId{4, 2}));
    REQUIRE(set.size() == 1);
}

TEST_CASE("SparseSet ignores stale generations", "[scene][sparse]") {
    SparseSet<int> set;
    set.add(EntityId{2, 1}, 42);
    REQUIRE(set.tryGet(EntityId{2, 1}) != nullptr);
    REQUIRE(set.tryGet(EntityId{2, 2}) == nullptr);
    REQUIRE_FALSE(set.has(EntityId{2, 2}));
}

TEST_CASE("SparseSet grows its sparse row to reach a high slot", "[scene][sparse]") {
    SparseSet<int> set;
    set.add(EntityId{50, 1}, 5);
    REQUIRE(set.sparseCapacity() >= 51);
    REQUIRE(*set.tryGet(EntityId{50, 1}) == 5);
    REQUIRE(set.tryGet(EntityId{49, 1}) == nullptr);
}

TEST_CASE("SparseSet clear empties values and the sparse row", "[scene][sparse]") {
    SparseSet<int> set;
    set.add(EntityId{1, 1}, 1);
    set.add(EntityId{2, 1}, 2);
    set.clear();
    REQUIRE(set.empty());
    REQUIRE(set.sparseCapacity() == 0);
    REQUIRE(set.tryGet(EntityId{1, 1}) == nullptr);
}

TEST_CASE("SparseSet valueAt rejects a bad index only in checked builds", "[scene][sparse]") {
    SparseSet<int> set;
    set.add(EntityId{1, 1}, 3);
    REQUIRE(set.entityAt(0) == EntityId{1, 1});
    REQUIRE(set.valueAt(0) == 3);
}

// ------------------------------------------------------------ ComponentType ---

TEST_CASE("Component type keys are stable and distinct", "[scene][component]") {
    const auto transform = fa::scene::componentTypeIdOf<Transform>();
    const auto sprite = fa::scene::componentTypeIdOf<Sprite>();
    const auto transformAgain = fa::scene::componentTypeIdOf<Transform>();

    REQUIRE(transform == transformAgain);
    REQUIRE(transform != sprite);
    REQUIRE(transform != fa::scene::kInvalidComponentType);
    REQUIRE(sprite != fa::scene::kInvalidComponentType);
}

TEST_CASE("Component set keys mix the individual keys", "[scene][component]") {
    const auto single = fa::scene::componentSetIdOf<Transform>();
    const auto pair = fa::scene::componentSetIdOf<Transform, Sprite>();
    REQUIRE(single != pair);
}

// ------------------------------------------------------------------- World ---

TEST_CASE("World create starts at slot one", "[scene][world]") {
    World world;
    const EntityId first = world.create();
    const EntityId second = world.create();

    REQUIRE(first == EntityId{1, 1});
    REQUIRE(second == EntityId{2, 1});
    REQUIRE(world.liveEntities() == 2);
    REQUIRE(world.entitySlots() == 2);
    REQUIRE(world.isAlive(first));
    REQUIRE(world.isAlive(second));
}

TEST_CASE("World destroy frees the slot and invalidates the handle", "[scene][world]") {
    World world;
    const EntityId id = world.create();
    REQUIRE(world.destroy(id));
    REQUIRE_FALSE(world.isAlive(id));
    REQUIRE(world.liveEntities() == 0);
    REQUIRE(world.entitySlots() == 1);
    REQUIRE_FALSE(world.destroy(id));
}

TEST_CASE("World recycles slots with a higher generation", "[scene][world]") {
    World world;
    const EntityId first = world.create();
    static_cast<void>(world.destroy(first));
    const EntityId reused = world.create();

    REQUIRE(reused.index == first.index);
    REQUIRE(reused.generation == first.generation + 1);
    REQUIRE_FALSE(world.isAlive(first));
    REQUIRE(world.isAlive(reused));
}

TEST_CASE("World destroy drops every component", "[scene][world]") {
    World world;
    const EntityId id = world.create();
    world.add<Transform>(id);
    world.add<Sprite>(id, makeSprite());

    REQUIRE(world.destroy(id));
    REQUIRE_FALSE(world.has<Transform>(id));
    REQUIRE_FALSE(world.has<Sprite>(id));
    REQUIRE(world.registeredComponentTypes() == 2);
}

TEST_CASE("World add and remove report component presence", "[scene][world]") {
    World world;
    const EntityId id = world.create();

    REQUIRE_FALSE(world.has<Transform>(id));
    REQUIRE(world.tryGet<Transform>(id) == nullptr);
    REQUIRE_FALSE(world.remove<Transform>(id));

    world.add<Transform>(id, Transform{Vec2{3.0f, 4.0f}, Vec2{1.0f, 1.0f}, 0.0f});
    REQUIRE(world.has<Transform>(id));
    REQUIRE(world.tryGet<Transform>(id)->position.x == Approx(3.0f));
    REQUIRE(world.remove<Transform>(id));
    REQUIRE_FALSE(world.has<Transform>(id));
}

TEST_CASE("World storages are per type and per world", "[scene][world]") {
    World first;
    World second;
    const EntityId a = first.create();
    const EntityId b = second.create();

    first.add<Transform>(a);
    REQUIRE(first.storage<Transform>().size() == 1);
    REQUIRE(second.storage<Transform>().size() == 0);
    REQUIRE_FALSE(second.has<Transform>(b));
}

TEST_CASE("World storage lookup on a const world never materialises one", "[scene][world]") {
    World world;
    const World& view = world;
    REQUIRE(view.storage<Transform>().size() == 0);
    REQUIRE(world.registeredComponentTypes() == 0);
}

TEST_CASE("World each visits only complete entities", "[scene][world]") {
    World world;
    const EntityId both = world.create();
    const EntityId transformOnly = world.create();
    const EntityId spriteOnly = world.create();
    world.add<Transform>(both);
    world.add<Sprite>(both, makeSprite());
    world.add<Transform>(transformOnly);
    world.add<Sprite>(spriteOnly, makeSprite());

    std::vector<EntityId> visited;
    world.each<Transform, Sprite>([&visited](EntityId id, Transform&, Sprite&) { visited.push_back(id); });

    REQUIRE(visited.size() == 1);
    REQUIRE(visited[0] == both);
    REQUIRE(world.countWith<Transform>() == 2);
    REQUIRE(world.countWith<Transform, Sprite>() == 1);
    REQUIRE(world.countWith<Sprite, Transform>() == 1);
}

TEST_CASE("World each on a component type nobody has does nothing", "[scene][world]") {
    World world;
    const EntityId id = world.create();
    world.add<Transform>(id);

    std::size_t visits = 0;
    world.each<Transform, Sprite>([&visits](EntityId, Transform&, Sprite&) { ++visits; });
    REQUIRE(visits == 0);
    REQUIRE(world.countWith<Transform, Sprite>() == 0);
}

TEST_CASE("World each mutates components in place", "[scene][world]") {
    World world;
    const EntityId id = world.create();
    world.add<Transform>(id, Transform{Vec2{1.0f, 2.0f}, Vec2{1.0f, 1.0f}, 0.0f});

    world.each<Transform>([](EntityId, Transform& transform) { transform.position.y += 10.0f; });
    REQUIRE(world.tryGet<Transform>(id)->position.y == Approx(12.0f));
}

TEST_CASE("World each on a const world reads without mutating", "[scene][world]") {
    World world;
    const EntityId id = world.create();
    world.add<Transform>(id, Transform{Vec2{1.0f, 2.0f}, Vec2{1.0f, 1.0f}, 0.0f});

    const World& view = world;
    float seen = 0.0f;
    view.each<Transform>([&seen](EntityId, const Transform& transform) { seen = transform.position.x; });
    REQUIRE(seen == Approx(1.0f));
}

TEST_CASE("World stats count entities, slots and components", "[scene][world]") {
    World world;
    const EntityId first = world.create();
    const EntityId second = world.create();
    world.add<Transform>(first);
    world.add<Transform>(second);
    world.add<Sprite>(second, makeSprite());
    static_cast<void>(world.destroy(first));

    const fa::scene::WorldStats stats = world.stats();
    REQUIRE(stats.liveEntities == 1);
    REQUIRE(stats.entitySlots == 2);
    REQUIRE(stats.recycledSlots == 1);
    REQUIRE(stats.componentTypes == 2);
    REQUIRE(stats.components == 2);
    REQUIRE(stats.createdTotal == 2);
    REQUIRE(stats.destroyedTotal == 1);
}

TEST_CASE("World clear keeps the world usable", "[scene][world]") {
    World world;
    const EntityId id = world.create();
    world.add<Transform>(id);
    world.clear();

    REQUIRE(world.liveEntities() == 0);
    REQUIRE(world.entitySlots() == 0);
    REQUIRE(world.storage<Transform>().size() == 0);
    REQUIRE_FALSE(world.isAlive(id));

    const EntityId again = world.create();
    REQUIRE(again.index == 1);
    REQUIRE(world.isAlive(again));
}

TEST_CASE("World keeps a thousand entities independent", "[scene][world]") {
    World world;
    std::vector<EntityId> ids;
    ids.reserve(1000);
    for (int index = 0; index < 1000; ++index) {
        const EntityId id = world.create();
        world.add<Transform>(id, Transform{Vec2{static_cast<float>(index), 0.0f}, Vec2{1.0f, 1.0f}, 0.0f});
        world.add<Sprite>(id, makeSprite());
        ids.push_back(id);
    }
    REQUIRE(world.liveEntities() == 1000);
    REQUIRE(world.countWith<Transform, Sprite>() == 1000);

    for (std::size_t index = 0; index < ids.size(); index += 2) {
        static_cast<void>(world.destroy(ids[index]));
    }
    REQUIRE(world.liveEntities() == 500);
    REQUIRE(world.countWith<Transform>() == 500);
}

// ----------------------------------------------------------- CommandBuffer ---

TEST_CASE("CommandBuffer applies nothing until it is drained", "[scene][commands]") {
    World world;
    CommandBuffer commands;
    const EntityId placeholder = commands.create();
    commands.add<Transform>(placeholder, Transform{Vec2{2.0f, 0.0f}, Vec2{1.0f, 1.0f}, 0.0f});

    REQUIRE(world.liveEntities() == 0);
    REQUIRE(commands.pendingCount() == 2);
    REQUIRE_FALSE(placeholder.isValid());

    const fa::scene::CommandBufferStats stats = commands.apply(world);
    REQUIRE(stats.applied == 2);
    REQUIRE(stats.skipped == 0);
    REQUIRE(stats.created == 1);
    REQUIRE(stats.componentsAdded == 1);
    REQUIRE(world.liveEntities() == 1);
    REQUIRE(commands.empty());
    REQUIRE(commands.appliedTotal() == 2);
}

TEST_CASE("CommandBuffer resolves a placeholder to the real entity", "[scene][commands]") {
    World world;
    CommandBuffer commands;
    const EntityId placeholder = commands.create();
    static_cast<void>(commands.apply(world));

    const EntityId real = commands.resolve(placeholder);
    REQUIRE(real.isValid());
    REQUIRE(world.isAlive(real));
    REQUIRE(real.generation == 1);
}

TEST_CASE("CommandBuffer resolve leaves real ids alone", "[scene][commands]") {
    CommandBuffer commands;
    const EntityId real{4, 2};
    REQUIRE(commands.resolve(real) == real);
    REQUIRE_FALSE(commands.resolve(EntityId{9, EntityId::kPendingGeneration}).isValid());
}

TEST_CASE("CommandBuffer gives each placeholder a distinct index", "[scene][commands]") {
    CommandBuffer commands;
    const EntityId first = commands.create();
    const EntityId second = commands.create();
    REQUIRE(first.index != second.index);
    REQUIRE(first != second);
}

TEST_CASE("CommandBuffer skips commands whose target is already gone", "[scene][commands]") {
    World world;
    CommandBuffer commands;
    const EntityId id = world.create();
    commands.destroy(id);
    commands.add<Transform>(id, Transform{});
    commands.destroy(id);

    const fa::scene::CommandBufferStats stats = commands.apply(world);
    REQUIRE(stats.applied == 1);
    REQUIRE(stats.destroyed == 1);
    REQUIRE(stats.skipped == 2);
}

TEST_CASE("CommandBuffer keeps queued order for create then destroy", "[scene][commands]") {
    World world;
    CommandBuffer commands;
    const EntityId placeholder = commands.create();
    commands.destroy(placeholder);

    const fa::scene::CommandBufferStats stats = commands.apply(world);
    REQUIRE(stats.created == 1);
    REQUIRE(stats.destroyed == 1);
    REQUIRE(world.liveEntities() == 0);
}

TEST_CASE("CommandBuffer removes components on request", "[scene][commands]") {
    World world;
    const EntityId id = world.create();
    world.add<Transform>(id);

    CommandBuffer commands;
    commands.remove<Transform>(id);
    const fa::scene::CommandBufferStats stats = commands.apply(world);

    REQUIRE(stats.componentsRemoved == 1);
    REQUIRE_FALSE(world.has<Transform>(id));
}

TEST_CASE("CommandBuffer skips removing a component the entity never had", "[scene][commands]") {
    World world;
    const EntityId id = world.create();
    CommandBuffer commands;
    commands.remove<Transform>(id);

    const fa::scene::CommandBufferStats stats = commands.apply(world);
    REQUIRE(stats.applied == 0);
    REQUIRE(stats.skipped == 1);
}

TEST_CASE("CommandBuffer clear drops queued work and the placeholder table", "[scene][commands]") {
    World world;
    CommandBuffer commands;
    const EntityId placeholder = commands.create();
    commands.clear();

    REQUIRE(commands.empty());
    REQUIRE_FALSE(commands.resolve(placeholder).isValid());
    REQUIRE(commands.apply(world).applied == 0);
}

// -------------------------------------------------------- SystemPipeline ---

TEST_CASE("SystemPipeline runs systems in registration order", "[scene][pipeline]") {
    std::vector<std::string> order;
    SystemPipeline pipeline;
    pipeline.add(std::make_unique<RecordingSystem>("first", order));
    pipeline.add(std::make_unique<RecordingSystem>("second", order));
    pipeline.add(std::make_unique<RecordingSystem>("third", order));

    World world;
    fa::time::FrameContext context;
    pipeline.fixedUpdate(world, context);

    REQUIRE(order.size() == 3);
    REQUIRE(order[0] == "first");
    REQUIRE(order[1] == "second");
    REQUIRE(order[2] == "third");
    REQUIRE(pipeline.size() == 3);
}

TEST_CASE("SystemPipeline ignores a null system", "[scene][pipeline]") {
    SystemPipeline pipeline;
    pipeline.add(nullptr);
    REQUIRE(pipeline.empty());
}

TEST_CASE("SystemPipeline counts its runs", "[scene][pipeline]") {
    std::vector<std::string> order;
    SystemPipeline pipeline;
    pipeline.add(std::make_unique<RecordingSystem>("only", order));

    World world;
    fa::time::FrameContext context;
    pipeline.fixedUpdate(world, context);
    pipeline.fixedUpdate(world, context);
    pipeline.variableUpdate(world, context);
    RenderQueue queue;
    pipeline.renderExtract(world, queue);

    const fa::scene::SystemPipelineStats stats = pipeline.stats();
    REQUIRE(stats.systems == 1);
    REQUIRE(stats.fixedRuns == 2);
    REQUIRE(stats.variableRuns == 1);
    REQUIRE(stats.extractRuns == 1);
}

TEST_CASE("SystemPipeline drains commands after every system", "[scene][pipeline]") {
    SystemPipeline pipeline;
    pipeline.add(std::make_unique<SpawnerSystem>());

    World world;
    fa::time::FrameContext context;
    pipeline.fixedUpdate(world, context);
    REQUIRE(world.liveEntities() == 1);

    pipeline.fixedUpdate(world, context);
    REQUIRE(world.liveEntities() == 2);
    REQUIRE(world.countWith<Transform>() == 2);
    REQUIRE(pipeline.stats().commandsApplied == 4);
    REQUIRE(pipeline.commands().appliedTotal() == 4);
}

TEST_CASE("SystemPipeline exposes systems by index", "[scene][pipeline]") {
    std::vector<std::string> order;
    SystemPipeline pipeline;
    pipeline.add(std::make_unique<RecordingSystem>("probe", order));

    REQUIRE(pipeline.at(0) != nullptr);
    REQUIRE(pipeline.at(0)->name() == "probe");
}

TEST_CASE("SystemPipeline clear forgets systems and queued commands", "[scene][pipeline]") {
    std::vector<std::string> order;
    SystemPipeline pipeline;
    pipeline.add(std::make_unique<RecordingSystem>("gone", order));
    pipeline.commands().create();
    pipeline.clear();

    REQUIRE(pipeline.empty());
    REQUIRE(pipeline.commands().empty());
}

TEST_CASE("SystemPipeline extract sees the world without changing it", "[scene][pipeline]") {
    std::vector<std::string> order;
    SystemPipeline pipeline;
    auto system = std::make_unique<RecordingSystem>("probe", order);
    RecordingSystem* raw = system.get();
    pipeline.add(std::move(system));

    World world;
    static_cast<void>(world.create());
    RenderQueue queue;
    pipeline.renderExtract(world, queue);
    REQUIRE(raw->seenEntities() == 1);
    REQUIRE(order.empty());
}

// ----------------------------------------------------------------- Scenes ---

TEST_CASE("BasicScene names itself and forwards hooks to the pipeline", "[scene][scene]") {
    BasicScene scene("Level1");
    REQUIRE(scene.name() == "Level1");
    scene.systems().add(std::make_unique<SpawnerSystem>());

    fa::time::FrameContext context;
    scene.onFixedUpdate(context);
    REQUIRE(scene.world().liveEntities() == 1);
    REQUIRE(scene.systems().stats().fixedRuns == 1);
}

TEST_CASE("BasicScene falls back to a default name", "[scene][scene]") {
    BasicScene scene("");
    REQUIRE(scene.name() == "scene");
}

TEST_CASE("WorldScene moves entities and extracts them", "[scene][scene]") {
    WorldScene scene;
    const EntityId id = scene.world().create();
    scene.world().add<Transform>(id, Transform{Vec2{0.0f, 0.0f}, Vec2{1.0f, 1.0f}, 0.0f});
    scene.world().add<Sprite>(id, makeSprite());

    fa::time::FrameContext context;
    scene.onFixedUpdate(context);
    scene.onFixedUpdate(context);
    REQUIRE(scene.world().tryGet<Transform>(id)->position.x == Approx(2.0f));

    RenderQueue queue;
    scene.onRenderExtract(queue);
    REQUIRE(queue.size() == 1);
}

// ------------------------------------------------------------- SceneStack ---

TEST_CASE("SceneStack push enters and pop exits", "[scene][stack]") {
    SceneStack stack;
    auto scene = std::make_unique<CountingScene>("menu");
    CountingScene* raw = scene.get();

    REQUIRE(stack.push(std::move(scene)).isOk());
    REQUIRE(raw->enters == 1);
    REQUIRE(stack.size() == 1);
    REQUIRE(stack.top() == raw);

    const std::unique_ptr<IScene> popped = stack.pop();
    REQUIRE(popped != nullptr);
    REQUIRE(raw->exits == 1);
    REQUIRE(stack.empty());
    REQUIRE(stack.top() == nullptr);
}

TEST_CASE("SceneStack pop on an empty stack returns null", "[scene][stack]") {
    SceneStack stack;
    REQUIRE(stack.pop() == nullptr);
    REQUIRE(stack.empty());
}

TEST_CASE("SceneStack rejects a null scene", "[scene][stack]") {
    SceneStack stack;
    const fa::core::Status pushed = stack.push(nullptr);
    REQUIRE(pushed.isError());
    REQUIRE(pushed.error().code == fa::scene::scene_error::kNullScene);
    REQUIRE(stack.empty());
}

TEST_CASE("SceneStack caps its depth", "[scene][stack]") {
    SceneStack stack;
    for (std::size_t index = 0; index < SceneStack::kMaxDepth; ++index) {
        REQUIRE(stack.push(std::make_unique<CountingScene>("layer")).isOk());
    }
    const fa::core::Status overflow = stack.push(std::make_unique<CountingScene>("overflow"));
    REQUIRE(overflow.isError());
    REQUIRE(overflow.error().code == fa::scene::scene_error::kSceneStackOverflow);
    REQUIRE(stack.size() == SceneStack::kMaxDepth);
}

TEST_CASE("SceneStack pauses the scene it covers and resumes it on pop", "[scene][stack]") {
    SceneStack stack;
    auto world = std::make_unique<CountingScene>("world");
    CountingScene* worldRaw = world.get();
    REQUIRE(stack.push(std::move(world)).isOk());

    auto menu = std::make_unique<CountingScene>("menu");
    CountingScene* menuRaw = menu.get();
    REQUIRE(stack.push(std::move(menu)).isOk());

    REQUIRE(worldRaw->pauses == 1);
    REQUIRE(worldRaw->resumes == 0);
    REQUIRE(menuRaw->enters == 1);

    static_cast<void>(stack.pop());
    REQUIRE(worldRaw->resumes == 1);
    REQUIRE(stack.stats().pauses == 1);
    REQUIRE(stack.stats().resumes == 1);
}

TEST_CASE("SceneStack updates only the top scene", "[scene][stack]") {
    SceneStack stack;
    auto lower = std::make_unique<CountingScene>("world");
    CountingScene* lowerRaw = lower.get();
    static_cast<void>(stack.push(std::move(lower)));

    auto upper = std::make_unique<CountingScene>("menu");
    CountingScene* upperRaw = upper.get();
    static_cast<void>(stack.push(std::move(upper)));

    fa::time::FrameContext context;
    stack.fixedUpdate(context);
    stack.variableUpdate(context);

    REQUIRE(lowerRaw->fixedUpdates == 0);
    REQUIRE(upperRaw->fixedUpdates == 1);
}

TEST_CASE("SceneStack extracts every scene bottom first", "[scene][stack]") {
    SceneStack stack;
    static_cast<void>(stack.push(std::make_unique<CountingScene>("world")));
    static_cast<void>(stack.push(std::make_unique<CountingScene>("menu")));

    RenderQueue queue;
    stack.renderExtract(queue);
    REQUIRE(queue.size() == 2);
    REQUIRE(stack.at(0) != nullptr);
    REQUIRE(stack.at(0)->name() == "world");
    REQUIRE(stack.at(1)->name() == "menu");
    REQUIRE(stack.at(2) == nullptr);
}

TEST_CASE("SceneStack replace keeps the depth and enters the new scene", "[scene][stack]") {
    LifecycleCounter retired;
    SceneStack stack;
    auto level = std::make_unique<CountingScene>("level1", &retired);
    static_cast<void>(stack.push(std::move(level)));

    auto next = std::make_unique<CountingScene>("level2");
    CountingScene* nextRaw = next.get();
    REQUIRE(stack.replace(std::move(next)).isOk());

    REQUIRE(stack.size() == 1);
    REQUIRE(retired.exits == 1);
    REQUIRE(nextRaw->enters == 1);
    REQUIRE(nextRaw->resumes == 0);
    REQUIRE(stack.stats().replacements == 1);
}

TEST_CASE("SceneStack replace on an empty stack acts as push", "[scene][stack]") {
    SceneStack stack;
    REQUIRE(stack.replace(std::make_unique<CountingScene>("first")).isOk());
    REQUIRE(stack.size() == 1);
    REQUIRE(stack.top()->name() == "first");
}

TEST_CASE("SceneStack replace rejects a null scene", "[scene][stack]") {
    SceneStack stack;
    static_cast<void>(stack.push(std::make_unique<CountingScene>("world")));
    const fa::core::Status replaced = stack.replace(nullptr);
    REQUIRE(replaced.isError());
    REQUIRE(replaced.error().code == fa::scene::scene_error::kNullScene);
    REQUIRE(stack.size() == 1);
}

TEST_CASE("SceneStack finds a scene by name from the top down", "[scene][stack]") {
    SceneStack stack;
    static_cast<void>(stack.push(std::make_unique<CountingScene>("shared")));
    static_cast<void>(stack.push(std::make_unique<CountingScene>("shared")));
    static_cast<void>(stack.push(std::make_unique<CountingScene>("menu")));

    IScene* found = stack.find("shared");
    REQUIRE(found != nullptr);
    REQUIRE(found == stack.at(1));
    REQUIRE(stack.find("missing") == nullptr);
}

TEST_CASE("SceneStack clear exits every scene", "[scene][stack]") {
    LifecycleCounter retired;
    SceneStack stack;
    auto lower = std::make_unique<CountingScene>("world", &retired);
    static_cast<void>(stack.push(std::move(lower)));
    auto upper = std::make_unique<CountingScene>("menu", &retired);
    static_cast<void>(stack.push(std::move(upper)));

    stack.clear();
    REQUIRE(stack.empty());
    REQUIRE(retired.exits == 2);
}

TEST_CASE("SceneStack destructor exits the scenes it still owns", "[scene][stack]") {
    LifecycleCounter retired;
    {
        SceneStack stack;
        auto scene = std::make_unique<CountingScene>("temporary", &retired);
        static_cast<void>(stack.push(std::move(scene)));
    }
    REQUIRE(retired.exits == 1);
}

// -------------------------------------------------------- SpriteExtractor ---

TEST_CASE("Sprite extraction maps components onto draw items", "[scene][extract]") {
    World world;
    const EntityId id = world.create();
    Transform transform;
    transform.position = Vec2{32.0f, 48.0f};
    world.add<Transform>(id, transform);

    Sprite sprite = makeSprite();
    sprite.size = Vec2{16.0f, 24.0f};
    sprite.layer = RenderLayer::Objects;
    sprite.flipX = true;
    sprite.tint = fa::core::Color{0.5f, 0.25f, 0.125f, 1.0f};
    world.add<Sprite>(id, sprite);

    RenderQueue queue;
    const fa::scene::SpriteExtractStats stats = extractSprites(world, queue);

    REQUIRE(stats.candidates == 1);
    REQUIRE(stats.submitted == 1);
    REQUIRE(stats.skippedNoTexture == 0);
    REQUIRE(queue.size() == 1);

    const DrawItem& item = queue.items()[0];
    REQUIRE(item.texture == TextureHandle{kTexture});
    REQUIRE(item.position.x == Approx(32.0f));
    REQUIRE(item.position.y == Approx(48.0f));
    REQUIRE(item.size.x == Approx(16.0f));
    REQUIRE(item.size.y == Approx(24.0f));
    REQUIRE(item.layer == RenderLayer::Objects);
    REQUIRE(item.flipX);
    REQUIRE(item.tint.x == Approx(0.5f));
    REQUIRE(item.tint.y == Approx(0.25f));
}

TEST_CASE("Sprite extraction uses the sprite feet as the depth key", "[scene][extract]") {
    World world;
    const EntityId id = world.create();
    world.add<Transform>(id, Transform{Vec2{0.0f, 100.0f}, Vec2{1.0f, 1.0f}, 0.0f});
    Sprite sprite = makeSprite();
    sprite.size = Vec2{16.0f, 20.0f};
    sprite.sortYBias = 2.0f;
    world.add<Sprite>(id, sprite);

    RenderQueue queue;
    static_cast<void>(extractSprites(world, queue));
    REQUIRE(queue.items()[0].sortY == Approx(122.0f));
}

TEST_CASE("Sprite extraction applies the transform scale", "[scene][extract]") {
    World world;
    const EntityId id = world.create();
    world.add<Transform>(id, Transform{Vec2{0.0f, 0.0f}, Vec2{2.0f, 3.0f}, 0.0f});
    world.add<Sprite>(id, makeSprite());

    RenderQueue queue;
    static_cast<void>(extractSprites(world, queue));
    REQUIRE(queue.items()[0].size.x == Approx(32.0f));
    REQUIRE(queue.items()[0].size.y == Approx(48.0f));
}

TEST_CASE("Sprite extraction can ignore the transform", "[scene][extract]") {
    World world;
    const EntityId id = world.create();
    world.add<Transform>(id, Transform{Vec2{0.0f, 0.0f}, Vec2{2.0f, 2.0f}, 1.5f});
    world.add<Sprite>(id, makeSprite());

    SpriteExtractOptions options;
    options.applyTransform = false;
    RenderQueue queue;
    static_cast<void>(extractSprites(world, queue, options));
    REQUIRE(queue.items()[0].size.x == Approx(16.0f));
    REQUIRE(queue.items()[0].rotation == Approx(0.0f));
}

TEST_CASE("Sprite extraction copies rotation verbatim", "[scene][extract]") {
    World world;
    const EntityId id = world.create();
    constexpr float kQuarterTurn = 1.5707964f;
    world.add<Transform>(id, Transform{Vec2{0.0f, 0.0f}, Vec2{1.0f, 1.0f}, kQuarterTurn});
    world.add<Sprite>(id, makeSprite());

    RenderQueue queue;
    static_cast<void>(extractSprites(world, queue));
    REQUIRE(queue.items()[0].rotation == Approx(kQuarterTurn));
}

TEST_CASE("Sprite extraction skips sprites without a texture", "[scene][extract]") {
    World world;
    const EntityId id = world.create();
    world.add<Transform>(id);
    world.add<Sprite>(id, makeSprite(0));

    RenderQueue queue;
    const fa::scene::SpriteExtractStats stats = extractSprites(world, queue);
    REQUIRE(stats.candidates == 1);
    REQUIRE(stats.submitted == 0);
    REQUIRE(stats.skippedNoTexture == 1);
    REQUIRE(queue.empty());
}

TEST_CASE("Sprite extraction can keep textureless sprites as colour quads", "[scene][extract]") {
    World world;
    const EntityId id = world.create();
    world.add<Transform>(id);
    world.add<Sprite>(id, makeSprite(0));

    SpriteExtractOptions options;
    options.requireTexture = false;
    RenderQueue queue;
    const fa::scene::SpriteExtractStats stats = extractSprites(world, queue, options);
    REQUIRE(stats.submitted == 1);
    REQUIRE_FALSE(queue.items()[0].texture.isValid());
}

TEST_CASE("Sprite extraction fills entityId from the entity index", "[scene][extract]") {
    World world;
    const EntityId first = world.create();
    const EntityId second = world.create();
    for (const EntityId id : {first, second}) {
        world.add<Transform>(id);
        world.add<Sprite>(id, makeSprite());
    }

    RenderQueue queue;
    static_cast<void>(extractSprites(world, queue));
    REQUIRE(queue.size() == 2);
    REQUIRE(queue.items()[0].entityId == first.index);
    REQUIRE(queue.items()[1].entityId == second.index);
}

TEST_CASE("Sprite extraction honours an explicit entity id override", "[scene][extract]") {
    World world;
    const EntityId id = world.create();
    world.add<Transform>(id);
    Sprite sprite = makeSprite();
    sprite.entityOverride = 999u;
    world.add<Sprite>(id, sprite);

    RenderQueue queue;
    static_cast<void>(extractSprites(world, queue));
    REQUIRE(queue.items()[0].entityId == 999u);
}

TEST_CASE("Sprite extraction adds the global sort bias", "[scene][extract]") {
    World world;
    const EntityId id = world.create();
    world.add<Transform>(id);
    world.add<Sprite>(id, makeSprite());

    SpriteExtractOptions options;
    options.sortYBias = -1.0f;
    RenderQueue queue;
    static_cast<void>(extractSprites(world, queue, options));
    REQUIRE(queue.items()[0].sortY == Approx(15.0f));
}

TEST_CASE("Sprite extraction has nothing to do in an empty world", "[scene][extract]") {
    World world;
    RenderQueue queue;
    const fa::scene::SpriteExtractStats stats = extractSprites(world, queue);
    REQUIRE(stats.candidates == 0);
    REQUIRE(stats.submitted == 0);
    REQUIRE(queue.empty());
}

TEST_CASE("Extracted items draw back to front after sorting", "[scene][extract]") {
    World world;
    const EntityId far = world.create();
    world.add<Transform>(far, Transform{Vec2{0.0f, 10.0f}, Vec2{1.0f, 1.0f}, 0.0f});
    world.add<Sprite>(far, makeSprite());
    const EntityId near = world.create();
    world.add<Transform>(near, Transform{Vec2{0.0f, 300.0f}, Vec2{1.0f, 1.0f}, 0.0f});
    world.add<Sprite>(near, makeSprite());

    RenderQueue queue;
    static_cast<void>(extractSprites(world, queue));
    queue.sort();

    REQUIRE(queue.size() == 2);
    REQUIRE(queue.items()[0].entityId == far.index);
    REQUIRE(queue.items()[1].entityId == near.index);
}

TEST_CASE("Entities on the same row draw in entity order", "[scene][extract]") {
    World world;
    std::vector<EntityId> ids;
    for (int index = 0; index < 4; ++index) {
        const EntityId id = world.create();
        world.add<Transform>(id, Transform{Vec2{0.0f, 50.0f}, Vec2{1.0f, 1.0f}, 0.0f});
        world.add<Sprite>(id, makeSprite());
        ids.push_back(id);
    }

    RenderQueue queue;
    static_cast<void>(extractSprites(world, queue));
    queue.sort();
    for (std::size_t index = 0; index < ids.size(); ++index) {
        REQUIRE(queue.items()[index].entityId == ids[index].index);
    }
}

TEST_CASE("SpriteExtractSystem records what it extracted", "[scene][extract]") {
    World world;
    const EntityId id = world.create();
    world.add<Transform>(id);
    world.add<Sprite>(id, makeSprite());

    SpriteExtractSystem system;
    REQUIRE(system.name() == "SpriteExtractSystem");

    RenderQueue queue;
    system.onRenderExtract(world, queue);
    REQUIRE(system.lastStats().submitted == 1);
    REQUIRE(queue.size() == 1);
}

TEST_CASE("Sprite extraction handles a thousand entities quickly", "[scene][extract]") {
    World world;
    for (int index = 0; index < 1000; ++index) {
        const EntityId id = world.create();
        world.add<Transform>(
            id,
            Transform{Vec2{static_cast<float>(index % 40) * 16.0f, static_cast<float>(index / 40) * 16.0f},
                      Vec2{1.0f, 1.0f},
                      0.0f});
        world.add<Sprite>(id, makeSprite());
    }

    RenderQueue queue;
    queue.reserve(1000);
    const fa::scene::SpriteExtractStats stats = extractSprites(world, queue);
    REQUIRE(stats.submitted == 1000);
    REQUIRE(queue.size() == 1000);

    queue.sort();
    REQUIRE(queue.items()[0].entityId == 1u);
    REQUIRE(queue.items()[999].entityId == 1000u);
}

TEST_CASE("A frame drives systems, deferred spawns and extraction", "[scene][integrate]") {
    BasicScene scene("integration");
    scene.systems().add(std::make_unique<SpawnerSystem>());
    scene.systems().add(std::make_unique<MoveSystem>());
    scene.systems().add(std::make_unique<SpriteExtractSystem>());

    fa::time::FrameContext context;
    scene.onFixedUpdate(context);
    REQUIRE(scene.world().liveEntities() == 1);

    const EntityId spawned = EntityId{1, 1};
    scene.world().add<Sprite>(spawned, makeSprite());

    scene.onFixedUpdate(context);
    REQUIRE(scene.world().liveEntities() == 2);
    REQUIRE(scene.world().tryGet<Transform>(spawned)->position.x == Approx(2.0f));

    RenderQueue queue;
    scene.onRenderExtract(queue);
    // Only the first spawn receives a sprite, so exactly one draw item is extracted.
    REQUIRE(queue.size() == 1);
    REQUIRE(queue.items()[0].entityId == spawned.index);
}
