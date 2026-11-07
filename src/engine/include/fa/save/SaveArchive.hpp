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
    /// \brief An empty JSON object. @{
    static SaveArchive object();
    /// \brief An empty JSON array.
    static SaveArchive array();
    /// @}

    /// \brief Parses JSON text without throwing.
    [[nodiscard]] static core::Result<SaveArchive> parse(std::string_view text);

    /// \brief Default-constructed value is JSON `null`. Rule of five is explicit because
    /// `Impl` is incomplete in the header. @{
    SaveArchive();
    SaveArchive(const SaveArchive& other);
    SaveArchive& operator=(const SaveArchive& other);
    SaveArchive(SaveArchive&&) noexcept;
    SaveArchive& operator=(SaveArchive&&) noexcept;
    ~SaveArchive();
    /// @}

    /// \brief Shape queries; non-container nodes report size 0. @{
    [[nodiscard]] bool isNull() const noexcept;
    [[nodiscard]] bool isObject() const noexcept;
    [[nodiscard]] bool isArray() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    /// @}

    /// \brief Object access. `child` returns a null archive when the key is absent or
    /// this node is not an object; `keys` is empty for non-objects. @{
    [[nodiscard]] bool has(std::string_view key) const;
    [[nodiscard]] SaveArchive child(std::string_view key) const;
    [[nodiscard]] std::vector<std::string> keys() const;
    /// @}

    /// \brief Array element access; out-of-range or non-array yields a null archive.
    [[nodiscard]] SaveArchive at(std::size_t index) const;
    /// \brief Appends to an array node (converts `null` to `[]` first).
    void push(SaveArchive value);
    /// \brief Sets one object member (converts `null` to `{}` first).
    void setChild(std::string_view key, SaveArchive value);

    /// \brief Scalar setters; each converts a `null` node to an object first. @{
    void setBool(std::string_view key, bool value);
    void setInt(std::string_view key, int value);
    void setInt64(std::string_view key, std::int64_t value);
    void setFloat(std::string_view key, float value);
    void setDouble(std::string_view key, double value);
    void setString(std::string_view key, std::string_view value);
    /// \brief Two-number vector stored as `{ "x": ..., "y": ... }`.
    void setVec2(std::string_view key, core::Vec2 value);
    /// \brief Four-number rectangle stored as `{ "x", "y", "w", "h" }`.
    void setRect(std::string_view key, core::Rect value);
    /// @}

    /// \brief Type-checked getters: false when the key is absent, the node is not an
    /// object, or the stored type does not match (the out parameter keeps its value).
    /// Integers accept both JSON signed and unsigned spellings within `int` range;
    /// floats accept any number. @{
    [[nodiscard]] bool getBool(std::string_view key, bool& out) const;
    [[nodiscard]] bool getInt(std::string_view key, int& out) const;
    [[nodiscard]] bool getInt64(std::string_view key, std::int64_t& out) const;
    [[nodiscard]] bool getFloat(std::string_view key, float& out) const;
    [[nodiscard]] bool getDouble(std::string_view key, double& out) const;
    [[nodiscard]] bool getString(std::string_view key, std::string& out) const;
    /// \brief Reads both `x` and `y`; false when either is missing or not a number.
    [[nodiscard]] bool getVec2(std::string_view key, core::Vec2& out) const;
    /// \brief Reads `x`, `y`, `w` and `h`; all four must be numbers.
    [[nodiscard]] bool getRect(std::string_view key, core::Rect& out) const;
    /// @}

    /// \brief Pretty-printed JSON; invalid UTF-8 is replaced rather than thrown on.
    [[nodiscard]] std::string dump() const;

private:
    struct Impl;
    /// \brief Internal: wraps a parsed value; used by `parse` and the factories.
    explicit SaveArchive(std::unique_ptr<Impl> impl);

    /// \brief Convert-on-write guards for the setters. @{
    void ensureObject();
    void ensureArray();
    /// @}

    std::unique_ptr<Impl> m_impl;
};

}  // namespace fa::save
