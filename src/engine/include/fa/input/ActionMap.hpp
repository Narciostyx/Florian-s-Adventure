#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <fa/core/Result.hpp>
#include <fa/input/Action.hpp>
#include <fa/input/Binding.hpp>
#include <fa/input/InputContext.hpp>

namespace fa::input {

/// Action -> bindings for one input context. Pure data: it never reads devices, so
/// it can be authored, saved and tested without a window. InputSystem evaluates it.
class ActionMap {
public:
    /// Adds a binding unless the action already has an identical one. Several
    /// bindings per action are the norm: they are OR-ed, and for axes the reading
    /// with the largest magnitude wins.
    void bind(ActionId action, Binding binding);

    /// Removes the first binding matching every field of `binding`.
    bool unbind(ActionId action, const Binding& binding);

    /// Removes everything the action is bound to, keeping the key allocated.
    void clearAction(ActionId action);

    void clear() noexcept;

    /// Empty when the action has no bindings. Action ids that were never bound are
    /// not materialized by a lookup.
    std::span<const Binding> bindings(ActionId action) const noexcept;

    std::size_t actionCount() const noexcept { return m_bindings.size(); }
    bool empty() const noexcept { return m_bindings.empty(); }

    /// Sorted by id so callers get a stable order without needing the registry.
    std::vector<ActionId> actions() const;

private:
    std::unordered_map<ActionId, std::vector<Binding>> m_bindings;
};

/// One context's bindings as `{ "<action name>": ["Key:A", "MouseButton:Left"] }`.
/// Action keys are author-facing names looked up in `names`; actions without a
/// registered name are skipped rather than serialized as a bare number.
std::string saveActionMapToJson(const ActionMap& map, const ActionRegistry& names);

/// Replaces `out` with the parsed bindings and interns every action name into
/// `names`. Unknown binding syntax and unreadable entries are skipped, so one
/// hand-edited typo cannot wipe a player's whole layout. Returns an error only when
/// the document is not an object.
fa::core::Status loadActionMapFromJson(std::string_view json, ActionMap& out, ActionRegistry& names);

/// Whole layout, i.e. every context in one document:
///     { "version": 1, "contexts": { "Gameplay": { ... }, "UI": { ... } } }
/// `maps` is indexed by InputContext, so it must have kInputContextCount entries;
/// `toString(InputContext)` supplies the keys. Contexts with no bindings are
/// omitted to keep the file readable.
std::string saveAllActionMapsToJson(std::span<const ActionMap> maps, const ActionRegistry& names);

/// Contexts missing from the document keep their current bindings, so a partial
/// file is a merge, not a reset.
fa::core::Status loadAllActionMapsFromJson(std::string_view json, std::span<ActionMap> maps, ActionRegistry& names);

}  // namespace fa::input
