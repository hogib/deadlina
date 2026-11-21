// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ticks to engine time (time_map.hpp), checked against what the conversion
// is defined to be, in whole numbers and at the ends of the range a document
// can store.

#include <catch2/catch_test_macros.hpp>

#include "lina/engine/internal/time_map.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

using namespace lina::engine::internal;
using lina::model::maxTicks;
using lina::model::Tempo;
using lina::model::ticksPerQuarter;

namespace {

__extension__ using Wide = __int128;

Tempo tempoOf(std::int32_t hundredths) {
  const auto tempo = Tempo::fromHundredths(hundredths);
  REQUIRE(tempo.has_value());
  return tempo.value_or(Tempo{});
}

/// True when `frame` is the frame nearest to `ticks`, the later one on a tie:
///     frame - 1/2 <= ticks * 25 * rate / (63 * hundredths) < frame + 1/2,
/// decided without dividing.
bool isNearestFrame(std::int64_t frame, std::int64_t ticks,
                    std::int32_t hundredths, std::int64_t rate) {
  const Wide position = Wide{ticks} * 25 * rate * 2;
  const Wide unit = Wide{63} * hundredths;
  const Wide doubledFrame = Wide{frame} * 2 * unit;
  return doubledFrame - unit <= position && position < doubledFrame + unit;
}

constexpr std::array<std::int32_t, 8> tempos{
    2'000, 30'000, 12'000, 13'333, 9'999, 7'777, 20'001, 29'999,
};
constexpr std::array<std::int64_t, 7> rates{
    8'000, 44'100, 48'000, 88'200, 96'000, 176'400, 192'000,
};
/// Small values, values around a quarter note, long times and the largest
/// tick counts a document can hold.
constexpr std::array<std::int64_t, 16> tickCounts{
    0,
    1,
    2,
    7,
    ticksPerQuarter - 1,
    ticksPerQuarter,
    ticksPerQuarter + 1,
    1'000'003,
    // Ten hours at 120 beats per minute, and a prime near it.
    72'000 * ticksPerQuarter,
    1'088'640'007,
    // 2^40 + 12345 and 2^52 + 1.
    1'099'511'640'121,
    4'503'599'627'370'497,
    maxTicks - 15'121,
    maxTicks - 2,
    maxTicks - 1,
    maxTicks,
};

} // namespace

TEST_CASE("a quarter note is one engine beat", "[time-map]") {
  CHECK(toBeats(0) == 0.0);
  CHECK(toBeats(ticksPerQuarter) == 1.0);
  CHECK(toBeats(ticksPerQuarter / 16) == 0.0625);
  CHECK(toBeats(3 * ticksPerQuarter / 2) == 1.5);
  // 2^53 ticks: the division of two exact numbers rounds once.
  const long double exact =
      static_cast<long double>(maxTicks) / static_cast<long double>(15'120);
  CHECK(toBeats(maxTicks) == static_cast<double>(exact));
  CHECK(
      toBeats(maxTicks - 1) ==
      static_cast<double>(static_cast<long double>(maxTicks - 1) / 15'120.0L));
}

TEST_CASE("seconds of a tick count are right to the last place", "[time-map]") {
  const TimeMap plain(tempoOf(12'000), 48'000.0);
  CHECK(plain.seconds(0) == 0.0);
  CHECK(plain.seconds(ticksPerQuarter) == 0.5);
  CHECK(plain.seconds(ticksPerQuarter * 7'200) == 3'600.0);
  CHECK(TimeMap(tempoOf(6'000), 48'000.0).seconds(ticksPerQuarter) == 1.0);
  CHECK(TimeMap(tempoOf(30'000), 48'000.0).seconds(ticksPerQuarter) == 0.2);

  for (const std::int32_t hundredths : tempos) {
    const TimeMap time(tempoOf(hundredths), 44'100.0);
    for (const std::int64_t ticks : tickCounts) {
      CAPTURE(hundredths, ticks);
      // The reference has eleven more bits than a double.
      const long double exact = static_cast<long double>(ticks) * 25.0L /
                                (63.0L * static_cast<long double>(hundredths));
      const double seconds = time.seconds(ticks);
      const auto nearest = static_cast<double>(exact);
      // Tolerance: one unit in the last place.
      CHECK(seconds >= std::nextafter(nearest, 0.0));
      CHECK(seconds <=
            std::nextafter(nearest, std::numeric_limits<double>::infinity()));
    }
  }
}

TEST_CASE("the frame of a tick is the nearest sample, exactly", "[time-map]") {
  // 120 beats per minute at 48 kHz: 24000 frames to the quarter note.
  const TimeMap plain(tempoOf(12'000), 48'000.0);
  CHECK(plain.frame(0) == 0);
  CHECK(plain.frame(ticksPerQuarter) == 24'000);
  CHECK(plain.frame(ticksPerQuarter / 2) == 12'000);
  // 133.33 beats per minute at 44.1 kHz: a quarter note is 19845.496 frames.
  const TimeMap awkward(tempoOf(13'333), 44'100.0);
  CHECK(awkward.frame(ticksPerQuarter) == 19'845);
  CHECK(awkward.frame(2 * ticksPerQuarter) == 39'691);

  for (const std::int32_t hundredths : tempos) {
    for (const std::int64_t rate : rates) {
      const TimeMap time(tempoOf(hundredths), static_cast<double>(rate));
      for (const std::int64_t ticks : tickCounts) {
        CAPTURE(hundredths, rate, ticks);
        CHECK(isNearestFrame(time.frame(ticks), ticks, hundredths, rate));
      }
    }
  }
}

TEST_CASE("a tick exactly between two frames goes to the later one",
          "[time-map]") {
  // 20 beats per minute at 11025 Hz: a tick is 35/16 of a frame, so 8 ticks
  // are 17.5 frames and 24 ticks are 52.5.
  const TimeMap time(tempoOf(2'000), 11'025.0);
  CHECK(time.frame(8) == 18);
  CHECK(time.frame(24) == 53);
  CHECK(time.frame(16) == 35);
}

TEST_CASE("a sample rate that is not a whole number still gives the nearest "
          "frame",
          "[time-map]") {
  const TimeMap time(tempoOf(12'000), 44'100.5);
  // One quarter note is 22050.25 frames, two are 44100.5: a tie.
  CHECK(time.frame(ticksPerQuarter) == 22'050);
  CHECK(time.frame(2 * ticksPerQuarter) == 44'101);
  CHECK(time.frame(4 * ticksPerQuarter) == 88'201);
  CHECK(time.frameSeconds(88'201) == 88'201.0 / 44'100.5);
}

TEST_CASE("the engine's horizon is half an hour short of 48 hours",
          "[time-map]") {
  CHECK(horizonSeconds == 171'000.0);
  CHECK(TimeMap(tempoOf(12'000), 48'000.0).horizonFrame() == 8'208'000'000);
  CHECK(TimeMap(tempoOf(2'000), 44'100.0).horizonFrame() == 7'541'100'000);
}

TEST_CASE("a time map needs a positive sample rate", "[time-map]") {
  CHECK_THROWS_AS(TimeMap(Tempo{}, 0.0), std::invalid_argument);
  CHECK_THROWS_AS(TimeMap(Tempo{}, -48'000.0), std::invalid_argument);
  CHECK_THROWS_AS(TimeMap(Tempo{}, std::numeric_limits<double>::quiet_NaN()),
                  std::invalid_argument);
  const TimeMap time(tempoOf(9'000), 96'000.0);
  CHECK(time.tempo() == tempoOf(9'000));
  CHECK(time.sampleRate() == 96'000.0);
  CHECK(time == TimeMap(tempoOf(9'000), 96'000.0));
  CHECK(!(time == TimeMap(tempoOf(9'001), 96'000.0)));
}
