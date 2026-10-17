#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace fa::input {

/// Why a frame's input is being read. Each context owns its own ActionMap, so
/// opening a menu makes gameplay actions stop firing even though the physical keys
/// are still held - that is the "menu open => character stops" acceptance test
/// in docs/modules/07-input.md, achieved without any per-action gating.
enum class InputContext : std::uint8_t {
    Gameplay,
    UI,
    Dialogue,
    Editor,
    TextInput,
    Count,
};

inline constexpr std::size_t kInputContextCount = static_cast<std::size_t>(InputContext::Count);

/// Stable on-disk spelling used as the JSON key and in logs.
std::string_view toString(InputContext context) noexcept;

/// Inverse of \ref toString. Case sensitive; returns InputContext::Gameplay when the
/// text is not a context name.
InputContext inputContextFromName(std::string_view name) noexcept;

/// Last-in-first-out stack of active contexts. Only the top entry is queried, so a
/// pushed UI context fully masks the Gameplay map beneath it.
///
/// The stack is never empty: popping the base entry re-pushes Gameplay rather than
/// leaving the system without a map. Duplicate pushes are allowed on purpose - two
/// overlapping panels must each pop their own entry.
class InputContextStack {
public:
    InputContextStack();

    void push(InputContext context) noexcept;

    /// Pops the top entry. Returns the context that is now active (or Gameplay when
    /// the stack had a single entry and was reset).
    InputContext pop() noexcept;

    /// Pops only when the top entry is `context`; returns false otherwise. This is
    /// the safe form for "the panel I pushed is closing, nobody else's entry".
    bool tryPop(InputContext context) noexcept;

    InputContext top() const noexcept;

    /// True when `context` is anywhere in the stack, top or buried.
    bool contains(InputContext context) const noexcept;

    std::size_t size() const noexcept { return m_stack.size(); }

    /// Collapses to a single Gameplay entry.
    void clear() noexcept;

private:
    std::vector<InputContext> m_stack;
};

}  // namespace fa::input
