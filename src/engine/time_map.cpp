// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/engine/internal/time_map.hpp"

#include "lina/engine/internal/wide_math.hpp"

#include <cmath>
#include <stdexcept>

namespace lina::engine::internal {

namespace {

// A quarter note lasts 60 / bpm seconds and has 15,120 ticks, and the tempo is
// stored in hundredths of a beat per minute, so
//   seconds = ticks * 6000 / (15120 * hundredths) = ticks * 25 / (63 * h).
constexpr std::int64_t secondsNumerator = 25;
constexpr std::int64_t secondsDenominatorPerHundredth = 63;

std::int64_t secondsDenominator(model::Tempo tempo) {
  return secondsDenominatorPerHundredth * tempo.hundredths();
}

} // namespace

double toBeats(std::int64_t ticks) {
  // Both operands are whole numbers below 2^53 and therefore exact, so the
  // one division rounds correctly.
  return static_cast<double>(ticks) /
         static_cast<double>(model::ticksPerQuarter);
}

TimeMap::TimeMap(model::Tempo tempo, double sampleRate)
    : tempo_(tempo), sampleRate_(sampleRate) {
  if (!(sampleRate > 0.0)) {
    throw std::invalid_argument("TimeMap needs a positive sample rate");
  }
}

double TimeMap::seconds(std::int64_t ticks) const {
  const Wide numerator = Wide{ticks} * secondsNumerator;
  const Wide denominator = secondsDenominator(tempo_);
  // The whole seconds are exact; the remainder is below one second and its
  // division rounds once, as does the sum.
  const auto whole = static_cast<std::int64_t>(numerator / denominator);
  const auto rest = static_cast<std::int64_t>(numerator % denominator);
  return static_cast<double>(whole) +
         (static_cast<double>(rest) / static_cast<double>(denominator));
}

std::int64_t TimeMap::frame(std::int64_t ticks) const {
  const double wholeRate = std::floor(sampleRate_);
  if (wholeRate != sampleRate_) {
    return static_cast<std::int64_t>(
        std::floor((seconds(ticks) * sampleRate_) + 0.5));
  }
  const Wide numerator =
      Wide{ticks} * secondsNumerator * static_cast<std::int64_t>(wholeRate);
  return static_cast<std::int64_t>(
      roundedQuotient(numerator, secondsDenominator(tempo_)));
}

std::int64_t TimeMap::horizonFrame() const {
  return static_cast<std::int64_t>(std::floor(horizonSeconds * sampleRate_));
}

} // namespace lina::engine::internal
