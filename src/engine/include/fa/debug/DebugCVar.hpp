#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <fa/core/Result.hpp>

namespace fa::platform {
class IFileSystem;
}

namespace fa::debug {

/// \brief Type of a runtime configuration value.
enum class CVarType : std::uint8_t {
    Bool,
    Int,
    Float,
    String,
};

/// \brief Type-tagged CVar payload.
struct CVarValue {
    CVarType type = CVarType::Bool;
    bool boolean = false;
    int integer = 0;
    float real = 0.0f;
    std::string text;

    /// \brief Stable text form, also used by save/load.
    [[nodiscard]] std::string toString() const;
};

/// \brief Copy of one CVar for listing.
struct CVarInfo {
    std::string name;
    std::string help;
    CVarType type = CVarType::Bool;
    std::string value;
};

/// \brief Small typed CVar registry with immediate change callbacks.
///
/// Lookup is a linear scan on purpose: a development build has tens of CVars, and
/// avoiding `std::unordered_map` keeps this module free of extra exception paths.
class CVarRegistry {
public:
    using ChangeCallback = std::function<void()>;

    [[nodiscard]] bool
    registerBool(std::string name, bool defaultValue, std::string help, ChangeCallback onChange = {});
    [[nodiscard]] bool registerInt(std::string name, int defaultValue, std::string help, ChangeCallback onChange = {});
    [[nodiscard]] bool
    registerFloat(std::string name, float defaultValue, std::string help, ChangeCallback onChange = {});
    [[nodiscard]] bool
    registerString(std::string name, std::string defaultValue, std::string help, ChangeCallback onChange = {});

    /// \brief Borrowed value, or nullptr for an unknown name. Stable until the next
    /// registration.
    [[nodiscard]] const CVarValue* get(std::string_view name) const noexcept;

    /// \brief Parses \p value according to the CVar type and applies it.
    /// \return false for an unknown name or an unparsable value.
    [[nodiscard]] bool set(std::string_view name, std::string_view value);

    [[nodiscard]] std::vector<CVarInfo> list() const;
    [[nodiscard]] std::size_t size() const noexcept { return m_entries.size(); }
    void clear() noexcept { m_entries.clear(); }

    /// \brief Writes `name=value` lines.
    [[nodiscard]] core::Status save(platform::IFileSystem& files, std::string_view path) const;

    /// \brief Applies `name=value` lines, ignoring unknown names and comments.
    [[nodiscard]] core::Status load(platform::IFileSystem& files, std::string_view path);

private:
    struct Entry {
        std::string name;
        std::string help;
        CVarValue value;
        ChangeCallback onChange;
    };

    [[nodiscard]] bool add(Entry entry);
    [[nodiscard]] Entry* find(std::string_view name) noexcept;

    std::vector<Entry> m_entries;
};

}  // namespace fa::debug
