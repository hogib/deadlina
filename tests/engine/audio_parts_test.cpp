// SPDX-License-Identifier: GPL-3.0-or-later
//
// What an audio clip plays at the speed of its file (audio_parts.hpp),
// without an engine.

#include <catch2/catch_test_macros.hpp>

#include "lina/engine/internal/audio_parts.hpp"
#include "support/builders.hpp"

#include <cstdint>
#include <optional>
#include <vector>

using namespace lina::engine::internal;
using namespace lina::model;
using lina::model::testing::beat;
using lina::model::testing::beats;

namespace {

/// 120 beats per minute at 48 kHz: 24000 frames to the quarter note.
TimeMap plainTime() { return {Tempo{}, 48'000.0}; }
constexpr std::int64_t quarter = 24'000;

/// A clip of a 48 kHz file whose samples 0 to 96000 are pinned to four
/// quarter notes: 24000 samples to the quarter note, as on the arrangement.
AudioClipContent clipOf(TickPosition position, TickDuration length) {
  AudioClipContent clip;
  clip.extent = {.position = position, .length = length, .contentStart = {}};
  clip.audio.region = {.start = 0, .end = 96'000};
  clip.audio.warp.markers = {
      {.sample = 0, .tick = {}},
      {.sample = 96'000, .tick = beat(4)},
  };
  clip.fileSampleRate = 48'000;
  return clip;
}

using Parts = std::vector<AudioPart>;

} // namespace

TEST_CASE("the warp map gives the sample at a content position",
          "[audio-parts]") {
  WarpMap warp;
  warp.markers = {
      {.sample = 1'000, .tick = beat(1)},
      {.sample = 49'000, .tick = beat(3)},
      {.sample = 50'000, .tick = beat(4)},
  };
  CHECK(sampleAt(warp, beat(1)) == 1'000);
  CHECK(sampleAt(warp, beat(2)) == 25'000);
  CHECK(sampleAt(warp, beat(3)) == 49'000);
  CHECK(sampleAt(warp, TickPosition{beat(3).ticks + (ticksPerQuarter / 2)}) ==
        49'500);
  CHECK(sampleAt(warp, beat(4)) == 50'000);
  // Outside the markers the outer segments carry on.
  CHECK(sampleAt(warp, beat(0)) == -23'000);
  CHECK(sampleAt(warp, beat(6)) == 52'000);
  // To the nearest sample: a tick is 100/63 of a sample in the first segment,
  // so 5 ticks are 7.94 samples and 6 ticks are 9.52.
  CHECK(sampleAt(warp, TickPosition{beat(1).ticks + 5}) == 1'008);
  CHECK(sampleAt(warp, TickPosition{beat(1).ticks + 6}) == 1'010);
  CHECK(sampleAt(warp, TickPosition{beat(1).ticks - 5}) == 992);
}

TEST_CASE("the sample at a position stays in range at the limits",
          "[audio-parts]") {
  constexpr std::int64_t twoToThe62 = 4'611'686'018'427'387'904;
  WarpMap steep;
  steep.markers = {
      {.sample = 0, .tick = {}},
      {.sample = twoToThe62, .tick = TickPosition{1}},
  };
  CHECK(sampleAt(steep, TickPosition{1}) == twoToThe62);
  // Far beyond what 64 bits hold: held at the largest value.
  CHECK(sampleAt(steep, TickPosition{maxTicks}) ==
        std::numeric_limits<std::int64_t>::max());
}

TEST_CASE("an audio clip plays its file from the content start",
          "[audio-parts]") {
  auto clip = clipOf(beat(2), beats(2));
  CHECK(audioParts(clip, plainTime()) == Parts{{
                                             .startFrame = 2 * quarter,
                                             .frameCount = 2 * quarter,
                                             .firstSample = 0,
                                             .loop = std::nullopt,
                                         }});
  clip.extent.contentStart = beat(1);
  CHECK(audioParts(clip, plainTime()) == Parts{{
                                             .startFrame = 2 * quarter,
                                             .frameCount = 2 * quarter,
                                             .firstSample = 24'000,
                                             .loop = std::nullopt,
                                         }});
}

TEST_CASE("an audio clip is silent once its region ends", "[audio-parts]") {
  auto clip = clipOf(beat(0), beats(8));
  clip.audio.region = {.start = 12'000, .end = 60'000};
  SECTION("the region limits both ends") {
    // The content start maps to sample 0, before the region.
    CHECK(audioParts(clip, plainTime()) == Parts{{
                                               .startFrame = 0,
                                               .frameCount = 48'000,
                                               .firstSample = 12'000,
                                               .loop = std::nullopt,
                                           }});
  }
  SECTION("a content start behind the region plays nothing") {
    clip.extent.contentStart = beat(3);
    CHECK(audioParts(clip, plainTime()).empty());
  }
  SECTION("a file at another rate lasts the same time") {
    clip.fileSampleRate = 44'100;
    // 48000 samples of a 44.1 kHz file are 52244.9 frames at 48 kHz.
    const Parts parts = audioParts(clip, plainTime());
    REQUIRE(parts.size() == 1);
    CHECK(parts.front().frameCount == 52'245);
  }
}

TEST_CASE("a looped audio clip repeats the stretch of file the loop names",
          "[audio-parts]") {
  auto clip = clipOf(beat(1), beats(8));
  clip.loop = {.enabled = true, .start = beat(1), .length = beats(2)};
  const SampleRange loop{.start = 24'000, .end = 72'000};

  SECTION("from before the loop: the lead-in, then the loop") {
    CHECK(audioParts(clip, plainTime()) == Parts{
                                               {
                                                   .startFrame = quarter,
                                                   .frameCount = quarter,
                                                   .firstSample = 0,
                                                   .loop = std::nullopt,
                                               },
                                               {
                                                   .startFrame = 2 * quarter,
                                                   .frameCount = 7 * quarter,
                                                   .firstSample = 24'000,
                                                   .loop = loop,
                                               },
                                           });
  }
  SECTION("a clip that ends inside the lead-in has no loop part") {
    clip.extent.length = TickDuration{ticksPerQuarter / 2};
    CHECK(audioParts(clip, plainTime()) == Parts{{
                                               .startFrame = quarter,
                                               .frameCount = quarter / 2,
                                               .firstSample = 0,
                                               .loop = std::nullopt,
                                           }});
  }
  SECTION("from inside the loop") {
    clip.extent.contentStart = beat(2);
    CHECK(audioParts(clip, plainTime()) == Parts{{
                                               .startFrame = quarter,
                                               .frameCount = 8 * quarter,
                                               .firstSample = 48'000,
                                               .loop = loop,
                                           }});
  }
  SECTION("from the loop end on there is no loop") {
    clip.extent.contentStart = beat(3);
    CHECK(audioParts(clip, plainTime()) == Parts{{
                                               .startFrame = quarter,
                                               .frameCount = quarter,
                                               .firstSample = 72'000,
                                               .loop = std::nullopt,
                                           }});
  }
  SECTION("a loop outside the region is no loop") {
    clip.loop.start = beat(5);
    CHECK(audioParts(clip, plainTime()) == Parts{{
                                               .startFrame = quarter,
                                               .frameCount = 4 * quarter,
                                               .firstSample = 0,
                                               .loop = std::nullopt,
                                           }});
  }
  SECTION("the loop is held inside the region") {
    clip.audio.region = {.start = 30'000, .end = 60'000};
    CHECK(audioParts(clip, plainTime()) ==
          Parts{{
              .startFrame = quarter,
              .frameCount = 8 * quarter,
              .firstSample = 30'000,
              .loop = SampleRange{.start = 30'000, .end = 60'000},
          }});
  }
}

TEST_CASE("an audio clip behind the engine's horizon has no parts",
          "[audio-parts]") {
  CHECK(audioParts(clipOf(beat(342'000), beats(4)), plainTime()).empty());
  // One that crosses it is cut there.
  const Parts parts = audioParts(clipOf(beat(341'999), beats(4)), plainTime());
  REQUIRE(parts.size() == 1);
  CHECK(parts.front().startFrame == plainTime().horizonFrame() - quarter);
  CHECK(parts.front().frameCount == quarter);
}
