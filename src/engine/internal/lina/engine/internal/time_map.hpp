// SPDX-License-Identifier: GPL-3.0-or-later
//
// The one place where the document's musical time becomes engine time
// (docs/design/13-architecture.md, "The projection"). Ticks go in; beats,
// seconds and sample frames come out. Nothing here converts back.
//
// Frames are computed in whole numbers and are exact: the frame of a tick is
// the nearest sample, the later one on a tie. Seconds are within one unit in
// the last place of the true value. Every stored tick count is at most 2^53
// (lina/model/time.hpp), so no step overflows.
#ifndef LINA_ENGINE_INTERNAL_TIME_MAP_HPP
#define LINA_ENGINE_INTERNAL_TIME_MAP_HPP

#include "lina/model/time.hpp"

#include <cstdint>

namespace lina::engine::internal {

/// The engine does not play past this point of the timeline. Tracktion Engine
/// v3.2.0 ends an edit at 48 hours; half an hour below that leaves room for
/// what the engine adds after a clip's end.
inline constexpr double horizonSeconds = 47.5 * 60.0 * 60.0;

/// Quarter notes from the start of the arrangement, which is what the engine
/// counts as beats whatever the time signature. Correctly rounded.
[[nodiscard]] double toBeats(std::int64_t ticks);

/// Converts ticks under one tempo and one sample rate.
class TimeMap {
public:
  /// `sampleRate` must be positive.
  TimeMap(model::Tempo tempo, double sampleRate);

  [[nodiscard]] model::Tempo tempo() const { return tempo_; }
  [[nodiscard]] double sampleRate() const { return sampleRate_; }

  /// Seconds from the start of the arrangement, or the length in seconds of
  /// that many ticks.
  [[nodiscard]] double seconds(std::int64_t ticks) const;

  /// The sample frame nearest to a tick; a tick exactly between two frames
  /// goes to the later one. Exact for a whole-number sample rate. For any
  /// other rate it is the rounded product of seconds() and the rate.
  [[nodiscard]] std::int64_t frame(std::int64_t ticks) const;

  /// The first frame the engine does not play (see horizonSeconds).
  [[nodiscard]] std::int64_t horizonFrame() const;

  /// The time of a frame in seconds, as the engine computes it.
  [[nodiscard]] double frameSeconds(std::int64_t frame) const {
    return static_cast<double>(frame) / sampleRate_;
  }

  friend bool operator==(const TimeMap &, const TimeMap &) = default;

private:
  model::Tempo tempo_;
  double sampleRate_;
};

} // namespace lina::engine::internal

#endif // LINA_ENGINE_INTERNAL_TIME_MAP_HPP
