#include "fa/debug/DebugCVar.hpp"

#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>

#include <fa/platform/FileSystem.hpp>

namespace fa::debug {
namespace {

[[nodiscard]] std::string trimLine(std::string_view line) {
    std::size_t begin = 0;
    while (begin < line.size() && (line[begin] == ' ' || line[begin] == '\t' || line[begin] == '\r')) {
        ++begin;
    }
    std::size_t end = line.size();
    while (end > begin && (line[end - 1] == ' ' || line[end - 1] == '\t' || line[end - 1] == '\r')) {
        --end;
    }
    return std::string(line.substr(begin, end - begin));
}

[[nodiscard]] bool parseBool(std::string_view text, bool& out) {
    if (text == "true" || text == "1" || text == "yes" || text == "on") {
        out = true;
        return true;
    }
    if (text == "false" || text == "0" || text == "no" || text == "off") {
        out = false;
        return true;
    }
    return false;
}

[[nodiscard]] bool parseInt(std::string_view text, int& out) {
    const std::string value(text);
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0') {
        return false;
    }
    out = static_cast<int>(parsed);
    return true;
}

[[nodiscard]] bool parseFloat(std::string_view text, float& out) {
    const std::string value(text);
    char* end = nullptr;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0' || !std::isfinite(parsed)) {
        return false;
    }
    out = parsed;
    return true;
}

}  // namespace

std::string CVarValue::toString() const {
    switch (type) {
        case CVarType::Bool:
            return boolean ? "true" : "false";
        case CVarType::Int:
            return std::to_string(integer);
        case CVarType::Float:
            return std::to_string(real);
        case CVarType::String:
            return text;
    }
    return {};
}

bool CVarRegistry::registerBool(std::string name, bool defaultValue, std::string help, ChangeCallback onChange) {
    return add(Entry{
        std::move(name), std::move(help), CVarValue{CVarType::Bool, defaultValue, 0, 0.0f, {}}, std::move(onChange)});
}

bool CVarRegistry::registerInt(std::string name, int defaultValue, std::string help, ChangeCallback onChange) {
    return add(Entry{std::move(name),
                     std::move(help),
                     CVarValue{CVarType::Int, false, defaultValue, 0.0f, {}},
                     std::move(onChange)});
}

bool CVarRegistry::registerFloat(std::string name, float defaultValue, std::string help, ChangeCallback onChange) {
    return add(Entry{
        std::move(name), std::move(help), CVarValue{CVarType::Float, false, 0, defaultValue, {}}, std::move(onChange)});
}

bool CVarRegistry::registerString(std::string name,
                                  std::string defaultValue,
                                  std::string help,
                                  ChangeCallback onChange) {
    return add(Entry{std::move(name),
                     std::move(help),
                     CVarValue{CVarType::String, false, 0, 0.0f, std::move(defaultValue)},
                     std::move(onChange)});
}

const CVarValue* CVarRegistry::get(std::string_view name) const noexcept {
    for (const Entry& entry : m_entries) {
        if (entry.name == name) {
            return &entry.value;
        }
    }
    return nullptr;
}

bool CVarRegistry::set(std::string_view name, std::string_view value) {
    Entry* entry = find(name);
    if (entry == nullptr) {
        return false;
    }

    CVarValue parsed = entry->value;
    switch (entry->value.type) {
        case CVarType::Bool:
            if (!parseBool(value, parsed.boolean)) {
                return false;
            }
            break;
        case CVarType::Int:
            if (!parseInt(value, parsed.integer)) {
                return false;
            }
            break;
        case CVarType::Float:
            if (!parseFloat(value, parsed.real)) {
                return false;
            }
            break;
        case CVarType::String:
            parsed.text = std::string(value);
            break;
    }

    entry->value = std::move(parsed);
    if (entry->onChange) {
        entry->onChange();
    }
    return true;
}

std::vector<CVarInfo> CVarRegistry::list() const {
    std::vector<CVarInfo> result;
    result.reserve(m_entries.size());
    for (const Entry& entry : m_entries) {
        result.push_back(CVarInfo{entry.name, entry.help, entry.value.type, entry.value.toString()});
    }
    return result;
}

core::Status CVarRegistry::save(platform::IFileSystem& files, std::string_view path) const {
    std::string text;
    for (const Entry& entry : m_entries) {
        text += entry.name;
        text.push_back('=');
        text += entry.value.toString();
        text.push_back('\n');
    }
    return files.writeTextAtomic(path, text);
}

core::Status CVarRegistry::load(platform::IFileSystem& files, std::string_view path) {
    const core::Result<std::string> read = files.readText(path);
    if (read.isError()) {
        return core::Status::err(read.error());
    }

    std::string_view remaining = read.value();
    while (!remaining.empty()) {
        const std::size_t newline = remaining.find('\n');
        const std::string_view rawLine = newline == std::string_view::npos ? remaining : remaining.substr(0, newline);
        remaining = newline == std::string_view::npos ? std::string_view{} : remaining.substr(newline + 1);

        const std::string line = trimLine(rawLine);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string name = trimLine(std::string_view(line).substr(0, equals));
        const std::string value = trimLine(std::string_view(line).substr(equals + 1));
        static_cast<void>(set(name, value));
    }
    return core::okStatus();
}

bool CVarRegistry::add(Entry entry) {
    if (entry.name.empty() || find(entry.name) != nullptr) {
        return false;
    }
    m_entries.push_back(std::move(entry));
    return true;
}

CVarRegistry::Entry* CVarRegistry::find(std::string_view name) noexcept {
    for (Entry& entry : m_entries) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

}  // namespace fa::debug
