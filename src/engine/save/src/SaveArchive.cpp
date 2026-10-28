#include "fa/save/SaveArchive.hpp"

#include <nlohmann/json.hpp>

#include <utility>

#include <fa/save/SaveError.hpp>

namespace fa::save {

struct SaveArchive::Impl {
    nlohmann::json value = nullptr;
};

SaveArchive::SaveArchive() : m_impl(std::make_unique<Impl>()) {}

SaveArchive::SaveArchive(const SaveArchive& other) {
    if (other.m_impl) {
        m_impl = std::make_unique<Impl>(*other.m_impl);
    }
}

SaveArchive& SaveArchive::operator=(const SaveArchive& other) {
    if (this != &other) {
        if (other.m_impl) {
            m_impl = std::make_unique<Impl>(*other.m_impl);
        } else {
            m_impl.reset();
        }
    }
    return *this;
}

SaveArchive::SaveArchive(SaveArchive&&) noexcept = default;
SaveArchive& SaveArchive::operator=(SaveArchive&&) noexcept = default;
SaveArchive::~SaveArchive() = default;

SaveArchive SaveArchive::object() {
    SaveArchive archive;
    archive.m_impl->value = nlohmann::json::object();
    return archive;
}

SaveArchive SaveArchive::array() {
    SaveArchive archive;
    archive.m_impl->value = nlohmann::json::array();
    return archive;
}

core::Result<SaveArchive> SaveArchive::parse(std::string_view text) {
    nlohmann::json value = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
    if (value.is_discarded()) {
        return core::Result<SaveArchive>::err(save_error::kParseFailed, "invalid JSON text");
    }
    SaveArchive archive;
    archive.m_impl->value = std::move(value);
    return core::Result<SaveArchive>::ok(std::move(archive));
}

bool SaveArchive::isNull() const noexcept {
    return !m_impl || m_impl->value.is_null();
}

bool SaveArchive::isObject() const noexcept {
    return m_impl && m_impl->value.is_object();
}

bool SaveArchive::isArray() const noexcept {
    return m_impl && m_impl->value.is_array();
}

std::size_t SaveArchive::size() const noexcept {
    if (!m_impl) {
        return 0;
    }
    return m_impl->value.is_array() || m_impl->value.is_object() ? m_impl->value.size() : 0;
}

bool SaveArchive::has(std::string_view key) const {
    if (!m_impl || !m_impl->value.is_object()) {
        return false;
    }
    return m_impl->value.find(std::string(key)) != m_impl->value.end();
}

SaveArchive SaveArchive::child(std::string_view key) const {
    if (!m_impl || !m_impl->value.is_object()) {
        return SaveArchive{};
    }
    const auto found = m_impl->value.find(std::string(key));
    if (found == m_impl->value.end()) {
        return SaveArchive{};
    }
    SaveArchive result;
    result.m_impl->value = *found;
    return result;
}

std::vector<std::string> SaveArchive::keys() const {
    std::vector<std::string> result;
    if (!m_impl || !m_impl->value.is_object()) {
        return result;
    }
    result.reserve(m_impl->value.size());
    for (auto it = m_impl->value.begin(); it != m_impl->value.end(); ++it) {
        result.push_back(it.key());
    }
    return result;
}

SaveArchive SaveArchive::at(std::size_t index) const {
    if (!m_impl || !m_impl->value.is_array() || index >= m_impl->value.size()) {
        return SaveArchive{};
    }
    SaveArchive result;
    result.m_impl->value = m_impl->value[index];
    return result;
}

void SaveArchive::push(SaveArchive value) {
    ensureArray();
    if (value.m_impl) {
        m_impl->value.push_back(std::move(value.m_impl->value));
    } else {
        m_impl->value.push_back(nullptr);
    }
}

void SaveArchive::setChild(std::string_view key, SaveArchive value) {
    ensureObject();
    if (value.m_impl) {
        m_impl->value[std::string(key)] = std::move(value.m_impl->value);
    } else {
        m_impl->value[std::string(key)] = nullptr;
    }
}

void SaveArchive::setBool(std::string_view key, bool value) {
    ensureObject();
    m_impl->value[std::string(key)] = value;
}

void SaveArchive::setInt(std::string_view key, int value) {
    ensureObject();
    m_impl->value[std::string(key)] = value;
}

void SaveArchive::setInt64(std::string_view key, std::int64_t value) {
    ensureObject();
    m_impl->value[std::string(key)] = value;
}

void SaveArchive::setFloat(std::string_view key, float value) {
    ensureObject();
    m_impl->value[std::string(key)] = value;
}

void SaveArchive::setDouble(std::string_view key, double value) {
    ensureObject();
    m_impl->value[std::string(key)] = value;
}

void SaveArchive::setString(std::string_view key, std::string_view value) {
    ensureObject();
    m_impl->value[std::string(key)] = std::string(value);
}

void SaveArchive::setVec2(std::string_view key, core::Vec2 value) {
    SaveArchive object = SaveArchive::object();
    object.setFloat("x", value.x);
    object.setFloat("y", value.y);
    setChild(key, std::move(object));
}

void SaveArchive::setRect(std::string_view key, core::Rect value) {
    SaveArchive object = SaveArchive::object();
    object.setFloat("x", value.x);
    object.setFloat("y", value.y);
    object.setFloat("w", value.w);
    object.setFloat("h", value.h);
    setChild(key, std::move(object));
}

bool SaveArchive::getBool(std::string_view key, bool& out) const {
    if (!m_impl || !m_impl->value.is_object()) {
        return false;
    }
    const auto found = m_impl->value.find(std::string(key));
    if (found == m_impl->value.end() || !found->is_boolean()) {
        return false;
    }
    out = found->get<bool>();
    return true;
}

bool SaveArchive::getInt(std::string_view key, int& out) const {
    if (!m_impl || !m_impl->value.is_object()) {
        return false;
    }
    const auto found = m_impl->value.find(std::string(key));
    if (found == m_impl->value.end() || (!found->is_number_integer() && !found->is_number_unsigned())) {
        return false;
    }
    out = found->get<int>();
    return true;
}

bool SaveArchive::getInt64(std::string_view key, std::int64_t& out) const {
    if (!m_impl || !m_impl->value.is_object()) {
        return false;
    }
    const auto found = m_impl->value.find(std::string(key));
    if (found == m_impl->value.end() || (!found->is_number_integer() && !found->is_number_unsigned())) {
        return false;
    }
    out = found->get<std::int64_t>();
    return true;
}

bool SaveArchive::getFloat(std::string_view key, float& out) const {
    if (!m_impl || !m_impl->value.is_object()) {
        return false;
    }
    const auto found = m_impl->value.find(std::string(key));
    if (found == m_impl->value.end() || !found->is_number()) {
        return false;
    }
    out = found->get<float>();
    return true;
}

bool SaveArchive::getDouble(std::string_view key, double& out) const {
    if (!m_impl || !m_impl->value.is_object()) {
        return false;
    }
    const auto found = m_impl->value.find(std::string(key));
    if (found == m_impl->value.end() || !found->is_number()) {
        return false;
    }
    out = found->get<double>();
    return true;
}

bool SaveArchive::getString(std::string_view key, std::string& out) const {
    if (!m_impl || !m_impl->value.is_object()) {
        return false;
    }
    const auto found = m_impl->value.find(std::string(key));
    if (found == m_impl->value.end() || !found->is_string()) {
        return false;
    }
    out = found->get<std::string>();
    return true;
}

bool SaveArchive::getVec2(std::string_view key, core::Vec2& out) const {
    const SaveArchive object = child(key);
    return object.getFloat("x", out.x) && object.getFloat("y", out.y);
}

bool SaveArchive::getRect(std::string_view key, core::Rect& out) const {
    const SaveArchive object = child(key);
    return object.getFloat("x", out.x) && object.getFloat("y", out.y) && object.getFloat("w", out.w) &&
           object.getFloat("h", out.h);
}

std::string SaveArchive::dump() const {
    if (!m_impl) {
        return "null";
    }
    return m_impl->value.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
}

void SaveArchive::ensureObject() {
    if (!m_impl) {
        m_impl = std::make_unique<Impl>();
    }
    if (!m_impl->value.is_object()) {
        m_impl->value = nlohmann::json::object();
    }
}

void SaveArchive::ensureArray() {
    if (!m_impl) {
        m_impl = std::make_unique<Impl>();
    }
    if (!m_impl->value.is_array()) {
        m_impl->value = nlohmann::json::array();
    }
}

}  // namespace fa::save
