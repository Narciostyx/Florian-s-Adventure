#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "fa/scene/EntityId.hpp"
#include "fa/scene/World.hpp"

namespace fa::scene {

/// \file
/// Deferred structural changes.
///
/// A system may not create or destroy entities while `World::each` is walking the
/// slot array: the arrays it is iterating would move underneath it. Per
/// `docs/architecture/conventions.md` section 9 every structural change instead goes
/// through this buffer and lands at one well-defined moment - right after the hook
/// that queued it returns. That also makes a hook's effect on the world readable in
/// one place, and lets a test drive a frame without worrying about interleaving.

/// \brief What one `apply()` did, for the debug overlay and tests.
struct CommandBufferStats {
    std::size_t applied = 0;
    std::size_t skipped = 0;
    std::size_t created = 0;
    std::size_t destroyed = 0;
    std::size_t componentsAdded = 0;
    std::size_t componentsRemoved = 0;
};

enum class CommandKind { Create, Destroy, AddComponent, RemoveComponent };

namespace detail {

/// \brief Maps a deferred-create placeholder onto the real entity, leaving real ids alone.
[[nodiscard]] EntityId resolvePending(EntityId id,
                                      const std::unordered_map<std::uint32_t, EntityId>& resolved) noexcept;

class CommandBase {
public:
    CommandBase() = default;
    virtual ~CommandBase() = default;
    CommandBase(const CommandBase&) = delete;
    CommandBase& operator=(const CommandBase&) = delete;
    CommandBase(CommandBase&&) = delete;
    CommandBase& operator=(CommandBase&&) = delete;

    [[nodiscard]] virtual CommandKind kind() const noexcept = 0;
    /// \return true when the command did something, false when its target was gone.
    virtual bool run(World& world, std::unordered_map<std::uint32_t, EntityId>& resolved) = 0;
};

class CreateCommand final : public CommandBase {
public:
    explicit CreateCommand(EntityId placeholder) noexcept : m_placeholder(placeholder) {}

    [[nodiscard]] CommandKind kind() const noexcept override { return CommandKind::Create; }
    bool run(World& world, std::unordered_map<std::uint32_t, EntityId>& resolved) override;

private:
    EntityId m_placeholder;
};

class DestroyCommand final : public CommandBase {
public:
    explicit DestroyCommand(EntityId target) noexcept : m_target(target) {}

    [[nodiscard]] CommandKind kind() const noexcept override { return CommandKind::Destroy; }
    bool run(World& world, std::unordered_map<std::uint32_t, EntityId>& resolved) override;

private:
    EntityId m_target;
};

template <typename T>
class AddComponentCommand final : public CommandBase {
public:
    AddComponentCommand(EntityId target, T value) : m_target(target), m_value(std::move(value)) {}

    [[nodiscard]] CommandKind kind() const noexcept override { return CommandKind::AddComponent; }

    bool run(World& world, std::unordered_map<std::uint32_t, EntityId>& resolved) override {
        const EntityId target = resolvePending(m_target, resolved);
        if (!target.isValid() || !world.isAlive(target)) {
            return false;
        }
        world.add<T>(target, std::move(m_value));
        return true;
    }

private:
    EntityId m_target;
    T m_value;
};

template <typename T>
class RemoveComponentCommand final : public CommandBase {
public:
    explicit RemoveComponentCommand(EntityId target) noexcept : m_target(target) {}

    [[nodiscard]] CommandKind kind() const noexcept override { return CommandKind::RemoveComponent; }

    bool run(World& world, std::unordered_map<std::uint32_t, EntityId>& resolved) override {
        const EntityId target = resolvePending(m_target, resolved);
        if (!target.isValid() || !world.isAlive(target)) {
            return false;
        }
        return world.remove<T>(target);
    }

private:
    EntityId m_target;
};

}  // namespace detail

/// \brief Queue of structural commands, drained by the scheduler after each hook.
///
/// Queued order is preserved: a create followed by an add lands both, and the add
/// sees the real entity. Commands whose target died earlier in the same buffer are
/// skipped and counted, never asserted - a system may legitimately queue work for an
/// entity another system just removed.
class CommandBuffer {
public:
    CommandBuffer() = default;
    CommandBuffer(const CommandBuffer&) = delete;
    CommandBuffer& operator=(const CommandBuffer&) = delete;
    CommandBuffer(CommandBuffer&&) = delete;
    CommandBuffer& operator=(CommandBuffer&&) = delete;
    ~CommandBuffer() = default;

    /// \brief Queues a create and returns a placeholder usable by later queued commands.
    ///
    /// The placeholder is not a real entity: `isAlive` is false for it and it must not
    /// be handed to `World`. Use `resolve()` (or read the id out of `apply()`'s world)
    /// to learn the real handle once the buffer has been drained.
    EntityId create();

    void destroy(EntityId target) noexcept;

    template <typename T>
    void add(EntityId target, T value) {
        m_commands.push_back(std::make_unique<detail::AddComponentCommand<T>>(target, std::move(value)));
    }

    template <typename T>
    void remove(EntityId target) {
        m_commands.push_back(std::make_unique<detail::RemoveComponentCommand<T>>(target));
    }

    /// \brief Runs every queued command in order and empties the queue.
    ///
    /// The placeholder table survives so that `resolve()` keeps answering for ids that
    /// were created earlier; `clear()` is what throws that away.
    CommandBufferStats apply(World& world);

    /// \brief Real handle behind a placeholder, or the input unchanged when unknown.
    [[nodiscard]] EntityId resolve(EntityId id) const noexcept;

    [[nodiscard]] std::size_t pendingCount() const noexcept { return m_commands.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_commands.empty(); }

    /// \brief Total commands run since construction, for the smoke summary.
    [[nodiscard]] std::uint64_t appliedTotal() const noexcept { return m_appliedTotal; }

    /// \brief Drops queued commands and the placeholder table.
    void clear() noexcept;

private:
    std::vector<std::unique_ptr<detail::CommandBase>> m_commands;
    std::unordered_map<std::uint32_t, EntityId> m_resolved;
    std::uint32_t m_nextPlaceholder = 0;
    std::uint64_t m_appliedTotal = 0;
};

}  // namespace fa::scene
