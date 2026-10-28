#include "fa/world/Calendar.hpp"

#include <cmath>
#include <utility>

namespace fa::world {
namespace {

[[nodiscard]] float luminance(core::Color color) noexcept {
    return 0.2126f * color.r + 0.7152f * color.g + 0.0722f * color.b;
}

}  // namespace

std::string_view toString(Season season) noexcept {
    switch (season) {
        case Season::Spring:
            return "spring";
        case Season::Summer:
            return "summer";
        case Season::Fall:
            return "fall";
        case Season::Winter:
            return "winter";
        default:
            return "?";
    }
}

std::string_view toString(Weather weather) noexcept {
    switch (weather) {
        case Weather::Sunny:
            return "sunny";
        case Weather::Rainy:
            return "rainy";
        case Weather::Stormy:
            return "stormy";
        case Weather::Snowy:
            return "snowy";
        default:
            return "?";
    }
}

WorldCalendar::WorldCalendar(std::uint64_t weatherSeed) {
    reset(weatherSeed);
}

void WorldCalendar::reset(std::uint64_t weatherSeed) {
    m_clock = WorldClock{};
    m_weather.seed(weatherSeed);
    m_events.clear();
    m_stats = CalendarStats{};
    m_lastHour = static_cast<int>(kStartHour);
    m_today = rollWeather(m_clock.date.season);
    m_tomorrow = rollWeather(m_clock.date.season);
}

void WorldCalendar::advance(float realDeltaSeconds) {
    if (m_clock.paused || !(realDeltaSeconds > 0.0f) || m_clock.realSecondsPerGameMinute <= 0.0f) {
        return;
    }

    const float gameMinutes = realDeltaSeconds / m_clock.realSecondsPerGameMinute;
    m_clock.timeOfDay += gameMinutes / 60.0f;

    /// A huge real delta still has to terminate; the frame loop clamps deltas far
    /// below this in practice.
    std::size_t guard = 0;
    while (m_clock.timeOfDay >= 24.0f && guard < 64u) {
        m_clock.timeOfDay -= 24.0f;
        advanceDay();
        ++guard;
    }

    const int currentHour = hour();
    if (currentHour != m_lastHour) {
        m_lastHour = currentHour;
        emit(CalendarEventKind::HourChanged);
        ++m_stats.hoursElapsed;
    }
}

void WorldCalendar::skipToNextDay() {
    m_clock.timeOfDay = kStartHour;
    advanceDay();
    m_lastHour = static_cast<int>(kStartHour);
    emit(CalendarEventKind::HourChanged);
    ++m_stats.hoursElapsed;
}

int WorldCalendar::hour() const noexcept {
    int value = static_cast<int>(m_clock.timeOfDay);
    value %= kHoursPerDay;
    return value < 0 ? value + kHoursPerDay : value;
}

int WorldCalendar::minute() const noexcept {
    const float fraction = m_clock.timeOfDay - std::floor(m_clock.timeOfDay);
    /// A tiny epsilon keeps an exact minute boundary from flooring to the
    /// previous minute after `advance()` accumulates float error (e.g. 8:01).
    int value = static_cast<int>(fraction * 60.0f + 1.0e-3f);
    return value < 0 ? 0 : (value > 59 ? 59 : value);
}

bool WorldCalendar::isNight() const noexcept {
    const int current = hour();
    return current < 6 || current >= 18;
}

bool WorldCalendar::isRaining() const noexcept {
    return m_today == Weather::Rainy || m_today == Weather::Stormy;
}

bool WorldCalendar::isStormy() const noexcept {
    return m_today == Weather::Stormy;
}

bool WorldCalendar::isSnowing() const noexcept {
    return m_today == Weather::Snowy;
}

bool WorldCalendar::autoWatersCrops() const noexcept {
    return isRaining();
}

float WorldCalendar::cropGrowthMultiplier() const noexcept {
    switch (m_today) {
        case Weather::Rainy:
            return 1.1f;
        case Weather::Stormy:
            return 0.9f;
        case Weather::Snowy:
            return 0.5f;
        case Weather::Sunny:
        default:
            return 1.0f;
    }
}

core::Color WorldCalendar::ambientColor(bool indoor) const noexcept {
    if (indoor) {
        return core::Color{0.85f, 0.78f, 0.65f, 1.0f};
    }

    const float time = static_cast<float>(hour()) + static_cast<float>(minute()) / 60.0f;
    float factor = 0.18f;
    if (time >= 8.0f && time < 18.0f) {
        factor = 1.0f;
    } else if (time >= 6.0f && time < 8.0f) {
        factor = 0.18f + (time - 6.0f) / 2.0f * 0.82f;
    } else if (time >= 18.0f && time < 20.0f) {
        factor = 0.18f + (20.0f - time) / 2.0f * 0.82f;
    }

    float red = factor;
    float green = factor;
    float blue = factor;
    if (time >= 6.0f && time < 8.0f) {
        red = core::clamp01(red + 0.12f);
        green = core::clamp01(green + 0.04f);
    } else if (time >= 18.0f && time < 20.0f) {
        red = core::clamp01(red + 0.15f);
        green = core::clamp01(green + 0.05f);
    }

    switch (m_today) {
        case Weather::Rainy:
            red *= 0.70f;
            green *= 0.75f;
            blue *= 0.85f;
            break;
        case Weather::Stormy:
            red *= 0.55f;
            green *= 0.60f;
            blue *= 0.75f;
            break;
        case Weather::Snowy:
            red *= 0.85f;
            green *= 0.90f;
            break;
        case Weather::Sunny:
        default:
            break;
    }

    return core::Color{core::clamp01(red), core::clamp01(green), core::clamp01(blue), 1.0f};
}

float WorldCalendar::ambientIntensity(bool indoor) const noexcept {
    return indoor ? 0.82f : luminance(ambientColor(false));
}

AmbientLight WorldCalendar::ambientLight(bool indoor) const noexcept {
    AmbientLight light;
    light.color = ambientColor(indoor);
    light.intensity = ambientIntensity(indoor);
    return light;
}

CalendarStats WorldCalendar::stats() const noexcept {
    CalendarStats result = m_stats;
    result.events = m_events.size();
    return result;
}

void WorldCalendar::restore(const GameDate& date, float timeOfDay, Weather today, Weather tomorrow) {
    m_clock.date = date;
    m_clock.date.year = date.year > 0 ? date.year : 1;
    m_clock.date.day = date.day < 1 ? 1 : (date.day > kDaysPerSeason ? kDaysPerSeason : date.day);
    m_clock.timeOfDay = core::clampf(timeOfDay, 0.0f, 23.999f);
    m_today = today;
    m_tomorrow = tomorrow;
    m_lastHour = hour();
    m_events.clear();
}

void WorldCalendar::setRealSecondsPerGameMinute(float seconds) noexcept {
    m_clock.realSecondsPerGameMinute = core::clampf(seconds, 0.001f, 60.0f);
}

void WorldCalendar::restoreWeather(std::uint64_t state, std::uint64_t increment) noexcept {
    m_weather.restore(state, increment);
}

void WorldCalendar::advanceDay() {
    const GameDate previous = m_clock.date;

    m_clock.date.day += 1;
    if (m_clock.date.day > kDaysPerSeason) {
        m_clock.date.day = 1;
        const int next = (static_cast<int>(m_clock.date.season) + 1) % static_cast<int>(Season::Count);
        m_clock.date.season = static_cast<Season>(next);
        if (next == 0) {
            ++m_clock.date.year;
        }
        ++m_stats.seasonChanges;
    }

    m_today = m_tomorrow;
    m_tomorrow = rollWeather(m_clock.date.season);
    emit(CalendarEventKind::WeatherChanged);
    emit(CalendarEventKind::DayChanged);
    ++m_stats.daysElapsed;
    ++m_stats.weatherChanges;
    if (previous.season != m_clock.date.season || previous.year != m_clock.date.year) {
        emit(CalendarEventKind::SeasonChanged);
    }
}

void WorldCalendar::emit(CalendarEventKind kind) {
    CalendarEvent event;
    event.kind = kind;
    event.hour = hour();
    event.date = m_clock.date;
    event.weather = m_today;
    m_events.push_back(event);
}

Weather WorldCalendar::rollWeather(Season season) noexcept {
    const float roll = m_weather.nextFloat01();
    float cumulative = 0.0f;
    const auto pick = [&cumulative, roll](Weather weather, float weight) {
        cumulative += weight;
        return roll < cumulative ? weather : Weather::Count;
    };

    Weather result = Weather::Sunny;
    switch (season) {
        case Season::Spring:
            result = pick(Weather::Sunny, 0.50f);
            if (result == Weather::Count) {
                result = pick(Weather::Rainy, 0.35f);
            }
            if (result == Weather::Count) {
                result = pick(Weather::Stormy, 0.10f);
            }
            if (result == Weather::Count) {
                result = pick(Weather::Snowy, 0.05f);
            }
            break;
        case Season::Summer:
            result = pick(Weather::Sunny, 0.65f);
            if (result == Weather::Count) {
                result = pick(Weather::Rainy, 0.15f);
            }
            if (result == Weather::Count) {
                result = pick(Weather::Stormy, 0.20f);
            }
            if (result == Weather::Count) {
                result = pick(Weather::Snowy, 0.0f);
            }
            break;
        case Season::Fall:
            result = pick(Weather::Sunny, 0.45f);
            if (result == Weather::Count) {
                result = pick(Weather::Rainy, 0.35f);
            }
            if (result == Weather::Count) {
                result = pick(Weather::Stormy, 0.15f);
            }
            if (result == Weather::Count) {
                result = pick(Weather::Snowy, 0.05f);
            }
            break;
        case Season::Winter:
            result = pick(Weather::Sunny, 0.35f);
            if (result == Weather::Count) {
                result = pick(Weather::Rainy, 0.0f);
            }
            if (result == Weather::Count) {
                result = pick(Weather::Stormy, 0.10f);
            }
            if (result == Weather::Count) {
                result = pick(Weather::Snowy, 0.55f);
            }
            break;
        default:
            break;
    }
    return result == Weather::Count ? Weather::Sunny : result;
}

}  // namespace fa::world
