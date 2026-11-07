#pragma once

#include <limits>
#include <string>

#include <nlohmann/json.hpp>

namespace fa::core::jsonread {

/// \file
/// Shared, type-checked JSON field readers for the data-driven modules (M15/M12/M24/M23).
///
/// These replace four byte-identical anonymous-namespace copies in ContentDatabase,
/// TilemapLoader, ShopDatabase and NarrativeDatabase. Every function is non-throwing:
/// wrong types or out-of-range integers return false instead of raising
/// nlohmann::type_error, so a malformed document degrades to a validation error.

/// \brief Reads a JSON integer value into \p out, rejecting non-numbers and anything
/// outside `int` range (floats are not accepted; use `readFloatValue` for those).
[[nodiscard]] inline bool readIntValue(const nlohmann::json& value, int& out) {
    if (value.is_number_unsigned()) {
        const unsigned long long raw = value.get<unsigned long long>();
        if (raw > static_cast<unsigned long long>(std::numeric_limits<int>::max())) {
            return false;
        }
        out = static_cast<int>(raw);
        return true;
    }
    if (value.is_number_integer()) {
        const long long raw = value.get<long long>();
        if (raw < static_cast<long long>(std::numeric_limits<int>::min()) ||
            raw > static_cast<long long>(std::numeric_limits<int>::max())) {
            return false;
        }
        out = static_cast<int>(raw);
        return true;
    }
    return false;
}

/// \brief Reads any JSON number into \p out as float.
[[nodiscard]] inline bool readFloatValue(const nlohmann::json& value, float& out) {
    if (!value.is_number()) {
        return false;
    }
    out = value.get<float>();
    return true;
}

/// \brief Reads a JSON string value.
[[nodiscard]] inline bool readStringValue(const nlohmann::json& value, std::string& out) {
    if (!value.is_string()) {
        return false;
    }
    out = value.get<std::string>();
    return true;
}

/// \brief Reads a JSON boolean value.
[[nodiscard]] inline bool readBoolValue(const nlohmann::json& value, bool& out) {
    if (!value.is_boolean()) {
        return false;
    }
    out = value.get<bool>();
    return true;
}

/// \brief Field wrappers: false when the object lacks \p key or the value is mis-typed.
/// @{
[[nodiscard]] inline bool readIntField(const nlohmann::json& object, const char* key, int& out) {
    const auto found = object.find(key);
    return found != object.end() && readIntValue(*found, out);
}

[[nodiscard]] inline bool readFloatField(const nlohmann::json& object, const char* key, float& out) {
    const auto found = object.find(key);
    return found != object.end() && readFloatValue(*found, out);
}

[[nodiscard]] inline bool readStringField(const nlohmann::json& object, const char* key, std::string& out) {
    const auto found = object.find(key);
    return found != object.end() && readStringValue(*found, out);
}

[[nodiscard]] inline bool readBoolField(const nlohmann::json& object, const char* key, bool& out) {
    const auto found = object.find(key);
    return found != object.end() && readBoolValue(*found, out);
}
/// @}

}  // namespace fa::core::jsonread
