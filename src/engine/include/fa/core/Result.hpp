#pragma once

#include <optional>
#include <utility>

#include <fa/core/Assert.hpp>
#include <fa/core/Error.hpp>

namespace fa::core {

/// Marker for "succeeded with no value". C++20 has no Result<void>, exceptions are
/// disabled, so an explicit empty type keeps every call site uniform:
///     Status status = doThing();
///     if (!status) { return status.error(); }
struct Unit {};

/// Value-or-Error without exceptions. `T` only needs to be movable; it is stored in
/// a std::optional so no default constructor is required.
///
/// Note: `value()` and `error()` assert on the wrong state instead of throwing -
/// check `isOk()` or the bool conversion first.
template <typename T>
class [[nodiscard]] Result {
public:
    using ValueType = T;

    Result() = delete;

    static Result ok(T value) { return Result(std::in_place, std::move(value)); }
    static Result err(Error error) { return Result(std::move(error)); }
    static Result err(int code, std::string message) { return Result(Error(code, std::move(message))); }

    bool isOk() const noexcept { return m_value.has_value(); }
    bool isError() const noexcept { return !m_value.has_value(); }
    explicit operator bool() const noexcept { return isOk(); }

    T& value() & {
        FA_ASSERT(isOk(), "Result::value() called on an error");
        return *m_value;
    }

    const T& value() const& {
        FA_ASSERT(isOk(), "Result::value() called on an error");
        return *m_value;
    }

    T take() && {
        FA_ASSERT(isOk(), "Result::take() called on an error");
        return std::move(*m_value);
    }

    const Error& error() const& {
        FA_ASSERT(isError(), "Result::error() called on a successful result");
        return m_error;
    }

    T valueOr(T fallback) const { return isOk() ? *m_value : std::move(fallback); }

    /// Discards the value, keeping only success/failure. `T` need not be copyable.
    Result<Unit> ignoreValue() const {
        if (isOk()) {
            return Result<Unit>::ok(Unit{});
        }
        return Result<Unit>::err(m_error);
    }

private:
    Result(std::in_place_t, T value) : m_value(std::in_place, std::move(value)) {}
    explicit Result(Error error) : m_error(std::move(error)) {}

    std::optional<T> m_value;
    Error m_error;
};

using Status = Result<Unit>;

inline Status okStatus() {
    return Status::ok(Unit{});
}

inline Status errorStatus(int code, std::string message) {
    return Status::err(code, std::move(message));
}

}  // namespace fa::core
