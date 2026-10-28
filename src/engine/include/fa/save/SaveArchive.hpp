#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <fa/core/Math.hpp>
#include <fa/core/Result.hpp>

namespace fa::save {

/// \brief Small JSON-backed value tree used by save sections and component serializers.
///
/// The header deliberately hides `nlohmann::json`: only this module's `.cpp` sees the
/// third-party type, so gameplay and engine headers do not inherit its build flags.
/// Values are copied by value; a default-constructed archive is JSON `null`.
class SaveArchive {
public:
    static SaveArchive object();
    static SaveArchive array();

    /// \brief Parses JSON text without throwing.
    [[nodiscard]] static core::Result<SaveArchive> parse(std::string_view text);

    SaveArchive();
    SaveArchive(const SaveArchive& other);
    SaveArchive& operator=(const SaveArchive& other);
    SaveArchive(SaveArchive&&) noexcept;
    SaveArchive& operator=(SaveArchive&&) noexcept;
    ~SaveArchive();

    [[nodiscard]] bool isNull() const noexcept;
    [[nodiscard]] bool isObject() const noexcept;
    [[nodiscard]] bool isArray() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

    [[nodiscard]] bool has(std::string_view key) const;
    [[nodiscard]] SaveArchive child(std::string_view key) const;
    [[nodiscard]] std::vector<std::string> keys() const;

    [[nodiscard]] SaveArchive at(std::size_t index) const;
    void push(SaveArchive value);
    void setChild(std::string_view key, SaveArchive value);

    void setBool(std::string_view key, bool value);
    void setInt(std::string_view key, int value);
    void setInt64(std::string_view key, std::int64_t value);
    void setFloat(std::string_view key, float value);
    void setDouble(std::string_view key, double value);
    void setString(std::string_view key, std::string_view value);
    void setVec2(std::string_view key, core::Vec2 value);
    void setRect(std::string_view key, core::Rect value);

    [[nodiscard]] bool getBool(std::string_view key, bool& out) const;
    [[nodiscard]] bool getInt(std::string_view key, int& out) const;
    [[nodiscard]] bool getInt64(std::string_view key, std::int64_t& out) const;
    [[nodiscard]] bool getFloat(std::string_view key, float& out) const;
    [[nodiscard]] bool getDouble(std::string_view key, double& out) const;
    [[nodiscard]] bool getString(std::string_view key, std::string& out) const;
    [[nodiscard]] bool getVec2(std::string_view key, core::Vec2& out) const;
    [[nodiscard]] bool getRect(std::string_view key, core::Rect& out) const;

    /// \brief Pretty-printed JSON; invalid UTF-8 is replaced rather than thrown on.
    [[nodiscard]] std::string dump() const;

private:
    struct Impl;
    explicit SaveArchive(std::unique_ptr<Impl> impl);

    void ensureObject();
    void ensureArray();

    std::unique_ptr<Impl> m_impl;
};

}  // namespace fa::save
