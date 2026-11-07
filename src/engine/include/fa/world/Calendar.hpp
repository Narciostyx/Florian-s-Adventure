#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <fa/core/Math.hpp>
#include <fa/core/Rng.hpp>
#include <fa/world/Lighting.hpp>

namespace fa::world {

inline constexpr int kDaysPerSeason = 28;
inline constexpr int kSeasonsPerYear = 4;
inline constexpr int kHoursPerDay = 24;
inline constexpr float kMidnightHour = 0.0f;
inline constexpr float kStartHour = 8.0f;
inline constexpr float kDefaultRealSecondsPerGameMinute = 0.7f;

/// \brief Calendar season. Numeric values are stable for save files.
enum class Season : std::uint8_t {
    Spring = 0,
    Summer,
    Fall,
    Winter,
    Count,
};

/// \brief Daily weather. Numeric values are stable for save files.
enum class Weather : std::uint8_t {
    Sunny = 0,
    Rainy,
    Stormy,
    Snowy,
    Count,
};

[[nodiscard]] std::string_view toString(Season season) noexcept;
[[nodiscard]] std::string_view toString(Weather weather) noexcept;

/// \brief Year/season/day, independent of the time of day.
struct GameDate {
    int year = 1;
    Season season = Season::Spring;
    int day = 1;
};

/// \brief Mutable clock state. Time is 0..24; the day starts at 08:00.
struct WorldClock {
    GameDate date;
    float timeOfDay = kStartHour;
    float realSecondsPerGameMinute = kDefaultRealSecondsPerGameMinute;
    bool paused = false;
};

/// \brief Which calendar transition a \ref CalendarEvent describes.
enum class CalendarEventKind : std::uint8_t {
    HourChanged = 0,
    DayChanged,
    SeasonChanged,
    WeatherChanged,
    Count,
};

/// \brief One calendar/weather transition.
struct CalendarEvent {
    CalendarEventKind kind = CalendarEventKind::HourChanged;
    int hour = 0;
    GameDate date;
    Weather weather = Weather::Sunny;
};

/// \brief Counters for the smoke summary and M27.
struct CalendarStats {
    std::uint64_t daysElapsed = 0;
    std::uint64_t hoursElapsed = 0;
    std::uint64_t seasonChanges = 0;
    std::uint64_t weatherChanges = 0;
    std::size_t events = 0;
};

/// \brief Calendar, weather and ambient light authority.
///
/// Pure logic: no file/GL/ECS access. The composition root calls `advance()` with
/// real seconds, consumes `events()`, and draws the ambient/point lights. Gameplay
/// modules subscribe to DayChanged/SeasonChanged/WeatherChanged through the
/// composition root.
class WorldCalendar {
public:
    /// \brief Fresh spring day-1 clock at the start hour; weather seeded from
    /// \p weatherSeed.
    explicit WorldCalendar(std::uint64_t weatherSeed = core::Rng::kDefaultSeed);

    /// \brief Back to the day-1 state and a reseeded weather stream.
    void reset(std::uint64_t weatherSeed = core::Rng::kDefaultSeed);

    /// \brief Advances by real seconds; wraps days and rolls weather per day.
    void advance(float realDeltaSeconds);
    /// \brief Moves to 08:00 the next day (sleep/skip), rolling weather.
    void skipToNextDay();

    /// \brief Clock accessors; `hour`/`minute` read from `timeOfDay`, `isNight`
    /// compares against the night window. @{
    void setPaused(bool paused) noexcept { m_clock.paused = paused; }
    [[nodiscard]] bool paused() const noexcept { return m_clock.paused; }
    [[nodiscard]] const WorldClock& clock() const noexcept { return m_clock; }
    [[nodiscard]] const GameDate& date() const noexcept { return m_clock.date; }
    [[nodiscard]] Season season() const noexcept { return m_clock.date.season; }
    [[nodiscard]] float timeOfDay() const noexcept { return m_clock.timeOfDay; }
    [[nodiscard]] int hour() const noexcept;
    [[nodiscard]] int minute() const noexcept;
    [[nodiscard]] bool isNight() const noexcept;
    /// @}

    /// \brief Weather queries; the `is*` helpers test `today()`. @{
    [[nodiscard]] Weather today() const noexcept { return m_today; }
    [[nodiscard]] Weather tomorrow() const noexcept { return m_tomorrow; }
    [[nodiscard]] bool isRaining() const noexcept;
    [[nodiscard]] bool isStormy() const noexcept;
    [[nodiscard]] bool isSnowing() const noexcept;
    /// \brief Rain and storms water farmland overnight.
    [[nodiscard]] bool autoWatersCrops() const noexcept;
    /// \brief Weather/season growth factor for crops (1.0 is neutral).
    [[nodiscard]] float cropGrowthMultiplier() const noexcept;
    /// @}

    /// \brief Outdoor day/night curve with weather tint; indoor ignores the curve.
    /// Colour/intensity/light accessors. @{
    [[nodiscard]] core::Color ambientColor(bool indoor = false) const noexcept;
    [[nodiscard]] float ambientIntensity(bool indoor = false) const noexcept;
    [[nodiscard]] AmbientLight ambientLight(bool indoor = false) const noexcept;
    /// @}

    /// \brief Events emitted since the last `clearEvents()`; and cumulative counters. @{
    [[nodiscard]] std::span<const CalendarEvent> events() const noexcept { return m_events; }
    void clearEvents() noexcept { m_events.clear(); }
    [[nodiscard]] CalendarStats stats() const noexcept;
    /// @}

    /// \brief Restores a save snapshot; clears pending events.
    void restore(const GameDate& date, float timeOfDay, Weather today, Weather tomorrow);

    /// \brief Simulation speed: real seconds per in-game minute (0 freezes the clock).
    void setRealSecondsPerGameMinute(float seconds) noexcept;
    [[nodiscard]] float realSecondsPerGameMinute() const noexcept { return m_clock.realSecondsPerGameMinute; }

    /// \brief Weather stream persistence: the two Rng words plus their restore. @{
    [[nodiscard]] std::uint64_t weatherState() const noexcept { return m_weather.state(); }
    [[nodiscard]] std::uint64_t weatherIncrement() const noexcept { return m_weather.increment(); }
    void restoreWeather(std::uint64_t state, std::uint64_t increment) noexcept;
    /// @}

private:
    /// \brief Rolls one day: date advance, weather re-draw, events, stats.
    void advanceDay();
    /// \brief Appends one event to the bounded ring.
    void emit(CalendarEventKind kind);
    /// \brief Draws a season-weighted weather from the seeded stream.
    [[nodiscard]] Weather rollWeather(Season season) noexcept;

    WorldClock m_clock;
    core::Rng m_weather;
    Weather m_today = Weather::Sunny;
    Weather m_tomorrow = Weather::Sunny;
    std::vector<CalendarEvent> m_events;
    CalendarStats m_stats;
    int m_lastHour = static_cast<int>(kStartHour);
};

}  // namespace fa::world
