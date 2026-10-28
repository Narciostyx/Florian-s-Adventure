/// \file
/// \brief Tests for M22: calendar rollover, hour/day/season events, weather weights
/// and effects, ambient light curve and point-light sampling. Pure logic; no GL.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>

#include <fa/core/Math.hpp>
#include <fa/world/Calendar.hpp>
#include <fa/world/Lighting.hpp>

namespace {

using Catch::Approx;

using fa::world::AmbientLight;
using fa::world::CalendarEventKind;
using fa::world::GameDate;
using fa::world::LightSet;
using fa::world::PointLight;
using fa::world::Season;
using fa::world::Weather;
using fa::world::WorldCalendar;

bool hasEvent(const WorldCalendar& calendar, CalendarEventKind kind) {
    for (const fa::world::CalendarEvent& event : calendar.events()) {
        if (event.kind == kind) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("WorldCalendar starts at year 1 spring day 1 08:00", "[world]") {
    WorldCalendar calendar;
    CHECK(calendar.date().year == 1);
    CHECK(calendar.date().season == Season::Spring);
    CHECK(calendar.date().day == 1);
    CHECK(calendar.hour() == 8);
    CHECK(calendar.minute() == 0);
    CHECK_FALSE(calendar.isNight());
    CHECK(fa::world::toString(Season::Winter) == "winter");
    CHECK(fa::world::toString(Weather::Snowy) == "snowy");
}

TEST_CASE("WorldCalendar advances minutes and hours", "[world]") {
    WorldCalendar calendar;
    calendar.setRealSecondsPerGameMinute(0.7f);

    calendar.advance(0.7f);
    CHECK(calendar.hour() == 8);
    CHECK(calendar.minute() == 1);

    calendar.clearEvents();
    calendar.advance(0.7f * 60.0f);
    CHECK(calendar.hour() == 9);
    CHECK(calendar.minute() == 1);
    CHECK(hasEvent(calendar, CalendarEventKind::HourChanged));
    CHECK(calendar.stats().hoursElapsed >= 1);
}

TEST_CASE("WorldCalendar rolls days, seasons and years", "[world]") {
    WorldCalendar calendar;
    calendar.advance(0.7f * 60.0f * 19.0f);
    CHECK(calendar.date().day == 2);
    CHECK(calendar.date().season == Season::Spring);
    CHECK(hasEvent(calendar, CalendarEventKind::DayChanged));
    CHECK(hasEvent(calendar, CalendarEventKind::WeatherChanged));

    calendar.clearEvents();
    calendar.skipToNextDay();
    CHECK(calendar.date().day == 3);
    CHECK(calendar.hour() == 8);

    for (int index = 0; index < 26; ++index) {
        calendar.skipToNextDay();
    }
    CHECK(calendar.date().season == Season::Summer);
    CHECK(calendar.date().day == 1);
    CHECK(calendar.stats().seasonChanges >= 1);

    for (int index = 0; index < 28 * 3; ++index) {
        calendar.skipToNextDay();
    }
    CHECK(calendar.date().year == 2);
    CHECK(calendar.date().season == Season::Spring);
    CHECK(calendar.date().day == 1);
    CHECK(calendar.stats().seasonChanges >= 4);
}

TEST_CASE("WorldCalendar weather drives watering and growth", "[world]") {
    WorldCalendar calendar(1234);
    calendar.restore(GameDate{1, Season::Spring, 1}, 12.0f, Weather::Rainy, Weather::Sunny);
    CHECK(calendar.isRaining());
    CHECK_FALSE(calendar.isStormy());
    CHECK(calendar.autoWatersCrops());
    CHECK(calendar.cropGrowthMultiplier() == Approx(1.1f));

    calendar.restore(GameDate{1, Season::Spring, 1}, 12.0f, Weather::Stormy, Weather::Sunny);
    CHECK(calendar.isStormy());
    CHECK(calendar.autoWatersCrops());
    CHECK(calendar.cropGrowthMultiplier() == Approx(0.9f));

    calendar.restore(GameDate{1, Season::Winter, 1}, 12.0f, Weather::Snowy, Weather::Sunny);
    CHECK(calendar.isSnowing());
    CHECK_FALSE(calendar.autoWatersCrops());
    CHECK(calendar.cropGrowthMultiplier() == Approx(0.5f));

    calendar.restore(GameDate{1, Season::Summer, 1}, 12.0f, Weather::Sunny, Weather::Sunny);
    CHECK_FALSE(calendar.isRaining());
    CHECK_FALSE(calendar.autoWatersCrops());
    CHECK(calendar.cropGrowthMultiplier() == Approx(1.0f));
}

TEST_CASE("WorldCalendar ambient light follows the day and weather", "[world]") {
    WorldCalendar calendar;
    calendar.restore(GameDate{1, Season::Spring, 1}, 12.0f, Weather::Sunny, Weather::Sunny);
    const AmbientLight noon = calendar.ambientLight();
    CHECK(noon.intensity == Approx(1.0f).margin(0.01f));
    CHECK(noon.color.r == Approx(1.0f).margin(0.01f));

    calendar.restore(GameDate{1, Season::Spring, 1}, 23.0f, Weather::Sunny, Weather::Sunny);
    CHECK(calendar.isNight());
    CHECK(calendar.ambientIntensity() < 0.3f);

    calendar.restore(GameDate{1, Season::Spring, 1}, 12.0f, Weather::Stormy, Weather::Sunny);
    CHECK(calendar.ambientIntensity() < noon.intensity);

    const AmbientLight indoor = calendar.ambientLight(true);
    CHECK(indoor.intensity == Approx(0.82f).margin(0.01f));
}

TEST_CASE("WorldCalendar pause and weather state", "[world]") {
    WorldCalendar calendar;
    calendar.setPaused(true);
    calendar.advance(10.0f);
    CHECK(calendar.timeOfDay() == Approx(8.0f));

    calendar.setPaused(false);
    calendar.setRealSecondsPerGameMinute(1.0f);
    calendar.advance(60.0f);
    CHECK(calendar.hour() == 9);

    calendar.setRealSecondsPerGameMinute(0.0f);
    CHECK(calendar.realSecondsPerGameMinute() == Approx(0.001f));

    const std::uint64_t state = calendar.weatherState();
    const std::uint64_t increment = calendar.weatherIncrement();
    WorldCalendar restored;
    restored.restoreWeather(state, increment);
    CHECK(restored.weatherState() == state);
    CHECK(restored.weatherIncrement() == increment);
}

TEST_CASE("LightSet adds, samples and removes lights", "[world]") {
    LightSet lights;
    PointLight first;
    first.position = fa::core::Vec2{0.0f, 0.0f};
    first.radius = 48.0f;
    first.color = fa::core::Color{1.0f, 0.5f, 0.25f, 1.0f};
    first.intensity = 1.0f;
    const std::uint32_t firstHandle = lights.add(first);

    PointLight second = first;
    second.position = fa::core::Vec2{200.0f, 0.0f};
    const std::uint32_t secondHandle = lights.add(second);

    REQUIRE(firstHandle != 0);
    REQUIRE(secondHandle != 0);
    CHECK(firstHandle != secondHandle);
    REQUIRE(lights.lights().size() == 2);

    const fa::core::Color center = lights.sample(fa::core::Vec2{0.0f, 0.0f});
    CHECK(center.r == Approx(1.0f));
    CHECK(center.g == Approx(0.5f));
    CHECK(lights.sample(fa::core::Vec2{500.0f, 500.0f}).r == Approx(0.0f));

    CHECK(lights.remove(firstHandle));
    CHECK_FALSE(lights.remove(firstHandle));
    CHECK(lights.lights().size() == 1);
    lights.clear();
    CHECK(lights.lights().empty());
}
