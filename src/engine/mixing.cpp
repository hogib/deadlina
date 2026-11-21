// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/engine/internal/mixing.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace lina::engine::internal {

double gainFromDb(double db) {
  constexpr double decibelsPerDecade = 20.0;
  return db <= model::silenceDb ? 0.0 : std::pow(10.0, db / decibelsPerDecade);
}

namespace {

struct Sides {
  double left{1.0};
  double right{1.0};
};

Sides panSides(double pan) {
  // The ends and the centre are stated, not computed: the cosine of a
  // quarter turn is not exactly zero in floating point, and the centre must
  // be exactly unity.
  if (pan == 0.0) {
    return {.left = 1.0, .right = 1.0};
  }
  if (pan <= -1.0) {
    return {.left = std::numbers::sqrt2, .right = 0.0};
  }
  if (pan >= 1.0) {
    return {.left = 0.0, .right = std::numbers::sqrt2};
  }
  const double angle = (pan + 1.0) * std::numbers::pi / 4.0;
  return {
      .left = std::numbers::sqrt2 * std::cos(angle),
      .right = std::numbers::sqrt2 * std::sin(angle),
  };
}

} // namespace

StereoGain stripGains(const model::MixerStrip &strip, bool audible) {
  if (!audible) {
    return {.left = 0.0F, .right = 0.0F};
  }
  const double volume = gainFromDb(strip.volumeDb);
  const Sides sides = panSides(strip.pan);
  return {
      .left = static_cast<float>(volume * sides.left),
      .right = static_cast<float>(volume * sides.right),
  };
}

SoloState soloState(const model::Project &project) {
  const auto soloed = [](const model::Track &track) {
    return track.mixer.solo;
  };
  return {
      .onTracks = std::ranges::any_of(project.tracks, soloed),
      .onReturns = std::ranges::any_of(project.returns, soloed),
  };
}

bool trackFeedsSends(const model::MixerStrip &strip, SoloState solo) {
  return !strip.mute && (strip.solo || !solo.onTracks);
}

bool trackReachesMaster(const model::MixerStrip &strip, SoloState solo) {
  return !strip.mute && (strip.solo || !(solo.onTracks || solo.onReturns));
}

bool returnReachesMaster(const model::MixerStrip &strip, SoloState solo) {
  return !strip.mute && (strip.solo || !solo.onReturns);
}

} // namespace lina::engine::internal
