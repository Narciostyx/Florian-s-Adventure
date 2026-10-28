#include "fa/debug/DebugLog.hpp"

#include <utility>

namespace fa::debug {

std::string formatLogLine(const DebugLogLine& line) {
    std::string result;
    result.reserve(line.message.size() + 24);
    result.push_back('[');
    result += core::toString(line.level);
    result += "][";
    result += core::toString(line.category);
    result += "] ";
    result += line.message;
    return result;
}

DebugLogSink::DebugLogSink(std::size_t capacity) : m_capacity(capacity > 0 ? capacity : 1) {
    m_lines.reserve(m_capacity);
}

void DebugLogSink::write(core::LogLevel level, core::LogCategory category, std::string_view message) {
    lock();
    if (level < m_minimum || level == core::LogLevel::Off) {
        unlock();
        return;
    }
    if (m_lines.size() >= m_capacity) {
        m_lines.erase(m_lines.begin());
        ++m_dropped;
    }
    m_lines.push_back(DebugLogLine{level, category, std::string(message)});
    unlock();
}

std::vector<DebugLogLine> DebugLogSink::snapshot() const {
    lock();
    std::vector<DebugLogLine> copy = m_lines;
    unlock();
    return copy;
}

void DebugLogSink::clear() {
    lock();
    m_lines.clear();
    unlock();
}

void DebugLogSink::setMinimumLevel(core::LogLevel level) noexcept {
    lock();
    m_minimum = level;
    unlock();
}

core::LogLevel DebugLogSink::minimumLevel() const noexcept {
    lock();
    const core::LogLevel level = m_minimum;
    unlock();
    return level;
}

std::size_t DebugLogSink::dropped() const noexcept {
    lock();
    const std::size_t dropped = m_dropped;
    unlock();
    return dropped;
}

void DebugLogSink::lock() const noexcept {
    while (m_lock.test_and_set(std::memory_order_acquire)) {
    }
}

void DebugLogSink::unlock() const noexcept {
    m_lock.clear(std::memory_order_release);
}

}  // namespace fa::debug
