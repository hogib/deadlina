// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/engine/internal/audio_parts.hpp"

#include "lina/engine/internal/wide_math.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lina::engine::internal {

namespace {

std::int64_t narrowed(Wide value) {
  constexpr Wide low = std::numeric_limits<std::int64_t>::min();
  constexpr Wide high = std::numeric_limits<std::int64_t>::max();
  return static_cast<std::int64_t>(std::clamp(value, low, high));
}

/// How many frames of the arrangement `samples` samples of a file last, at
/// original speed: all of them when the rates agree, otherwise rounded up so
/// that the last sample is not cut.
std::int64_t framesFor(std::int64_t samples, std::uint32_t fileRate,
                       double sampleRate) {
  if (static_cast<double>(fileRate) == sampleRate) {
    return samples;
  }
  return static_cast<std::int64_t>(
      std::ceil(static_cast<double>(samples) * sampleRate /
                static_cast<double>(fileRate)));
}

} // namespace

std::int64_t sampleAt(const model::WarpMap &warp, model::TickPosition content) {
  const auto &markers = warp.markers;
  // The segment that holds the position, or the nearest one when it lies
  // outside all of them.
  auto after = std::ranges::upper_bound(
      markers, content.ticks, {},
      [](const model::WarpMarker &marker) { return marker.tick.ticks; });
  after = std::clamp(after, markers.begin() + 1, markers.end() - 1);
  const model::WarpMarker &from = *(after - 1);
  const model::WarpMarker &to = *after;
  const Wide offset =
      Wide{content.ticks - from.tick.ticks} * Wide{to.sample - from.sample};
  return narrowed(
      Wide{from.sample} +
      roundedQuotient(offset, Wide{to.tick.ticks - from.tick.ticks}));
}

std::vector<AudioPart> audioParts(const AudioClipContent &clip,
                                  const TimeMap &time) {
  const std::int64_t clipStart = clip.extent.position.ticks;
  const std::int64_t startFrame = time.frame(clipStart);
  const std::int64_t endFrame = std::min(
      time.frame(clipStart + clip.extent.length.ticks), time.horizonFrame());
  const model::SampleRange region = clip.audio.region;
  const auto inRegion = [region](model::TickPosition content,
                                 const model::WarpMap &warp) {
    return std::clamp(sampleAt(warp, content), region.start, region.end);
  };
  const std::int64_t first =
      inRegion(clip.extent.contentStart, clip.audio.warp);
  if (startFrame >= endFrame || first >= region.end) {
    return {};
  }
  const std::int64_t window = endFrame - startFrame;
  const auto frames = [&clip, &time](std::int64_t samples) {
    return framesFor(samples, clip.fileSampleRate, time.sampleRate());
  };

  const std::int64_t loopStart = inRegion(clip.loop.start, clip.audio.warp);
  const std::int64_t loopEnd =
      inRegion(clip.loop.start + clip.loop.length, clip.audio.warp);
  if (!clip.loop.enabled || loopEnd <= loopStart || first >= loopEnd) {
    return {
        {
            .startFrame = startFrame,
            .frameCount = std::min(window, frames(region.end - first)),
            .firstSample = first,
            .loop = std::nullopt,
        },
    };
  }

  const model::SampleRange loop{.start = loopStart, .end = loopEnd};
  if (first >= loopStart) {
    return {
        {
            .startFrame = startFrame,
            .frameCount = window,
            .firstSample = first,
            .loop = loop,
        },
    };
  }
  const std::int64_t lead = std::min(window, frames(loopStart - first));
  std::vector<AudioPart> parts{
      {
          .startFrame = startFrame,
          .frameCount = lead,
          .firstSample = first,
          .loop = std::nullopt,
      },
  };
  if (lead < window) {
    parts.push_back({
        .startFrame = startFrame + lead,
        .frameCount = window - lead,
        .firstSample = loopStart,
        .loop = loop,
    });
  }
  return parts;
}

} // namespace lina::engine::internal
