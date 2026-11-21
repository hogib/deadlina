// SPDX-License-Identifier: GPL-3.0-or-later
//
// What an audio clip plays at the speed of its file, worked out from the
// document alone. Following the tempo, stretching and transposing are not
// projected yet: a clip that asks for them is played like one that does not,
// at original speed and pitch.
//
// The clip's warp map says which sample of the file belongs to which content
// position. At original speed only one thing is taken from it: the sample at
// a content position, by straight lines between markers and beyond the outer
// ones. From there the file runs at its own speed.
//   - The clip plays from the sample at its content start.
//   - Nothing outside the clip's region of the file is heard: positions are
//     held inside the region, and without a loop the clip is silent once the
//     region ends.
//   - With the loop on, the loop is the stretch of file between the samples
//     at the loop's start and end. Playing reaches the loop end and repeats
//     the loop from there. A content start at or behind the loop end, and a
//     loop that is empty in samples, mean no loop.
//
// One clip of the document becomes up to two engine clips, here called
// parts: what lies before the loop, and the loop itself.
#ifndef LINA_ENGINE_INTERNAL_AUDIO_PARTS_HPP
#define LINA_ENGINE_INTERNAL_AUDIO_PARTS_HPP

#include "lina/engine/internal/time_map.hpp"
#include "lina/model/clip.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace lina::engine::internal {

/// The parts of an audio clip that decide what it plays.
struct AudioClipContent {
  model::ClipExtent extent;
  model::LoopSettings loop;
  model::AudioContent audio;
  /// The sample rate of the file, as its media reference states it.
  std::uint32_t fileSampleRate{48'000};

  friend bool operator==(const AudioClipContent &,
                         const AudioClipContent &) = default;
};

/// One engine clip: `frameCount` frames of the arrangement from `startFrame`,
/// playing the file from `firstSample`. With a loop, playing wraps from the
/// loop's end to its start.
struct AudioPart {
  std::int64_t startFrame{0};
  std::int64_t frameCount{0};
  std::int64_t firstSample{0};
  std::optional<model::SampleRange> loop;

  friend bool operator==(const AudioPart &, const AudioPart &) = default;
};

/// The sample of the file that a warp map pins to a content position, to the
/// nearest sample. The map must hold at least two markers that increase in
/// ticks, which the document guarantees.
[[nodiscard]] std::int64_t sampleAt(const model::WarpMap &warp,
                                    model::TickPosition content);

/// The engine clips of one audio clip, in playing order; empty when nothing
/// of it can be heard.
[[nodiscard]] std::vector<AudioPart> audioParts(const AudioClipContent &clip,
                                                const TimeMap &time);

} // namespace lina::engine::internal

#endif // LINA_ENGINE_INTERNAL_AUDIO_PARTS_HPP
