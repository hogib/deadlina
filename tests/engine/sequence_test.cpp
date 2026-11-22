// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the sequencer plays (sequence.hpp): the notes of a clip and of a
// track in frames, and the questions the audio thread asks of them, without
// an engine.

#include <catch2/catch_test_macros.hpp>

#include "lina/engine/internal/sequence.hpp"
#include "support/builders.hpp"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

using namespace lina::engine::internal;
using namespace lina::model;
using lina::model::testing::beat;
using lina::model::testing::beats;
using lina::model::testing::makeNote;

namespace {

NoteEvent on(std::int64_t frame, std::uint8_t pitch, std::uint8_t velocity) {
  return {.frame = frame, .pitch = pitch, .velocity = velocity};
}

NoteEvent off(std::int64_t frame, std::uint8_t pitch) {
  return {.frame = frame, .pitch = pitch, .velocity = 0};
}

std::shared_ptr<const ClipPlayback>
playback(std::int64_t start, std::int64_t end, std::vector<NoteEvent> events) {
  return std::make_shared<const ClipPlayback>(ClipEvents{
      .events = std::move(events),
      .startFrame = start,
      .endFrame = end,
      .truncated = false,
  });
}

/// Pitch 60 from 100 to 200 and from 200 to 250, pitch 64 from 150 to 400,
/// in a clip from 0 to 400.
std::shared_ptr<const ClipPlayback> firstClip() {
  return playback(0, 400,
                  {
                      on(100, 60, 90),
                      on(150, 64, 70),
                      off(200, 60),
                      on(200, 60, 80),
                      off(250, 60),
                      off(400, 64),
                  });
}

} // namespace

TEST_CASE("a clip's playback holds its events and finds the note across a "
          "frame",
          "[sequence]") {
  const auto clip = firstClip();
  CHECK(clip->startFrame() == 0);
  CHECK(clip->endFrame() == 400);
  CHECK(clip->events().events.size() == 6);
  // Across a frame means: began before it, ends on it or later.
  CHECK(clip->across(60, 100) == nullptr);
  REQUIRE(clip->across(60, 101) != nullptr);
  CHECK(*clip->across(60, 101) ==
        PlayedNote{.on = 100, .off = 200, .pitch = 60, .velocity = 90});
  // On the frame where one note of a pitch ends and the next begins, the
  // one that ends is across it.
  REQUIRE(clip->across(60, 200) != nullptr);
  CHECK(clip->across(60, 200)->on == 100);
  REQUIRE(clip->across(60, 201) != nullptr);
  CHECK(clip->across(60, 201)->velocity == 80);
  CHECK(clip->across(60, 250) != nullptr);
  CHECK(clip->across(60, 251) == nullptr);
  CHECK(*clip->across(60, 201) != *clip->across(60, 101));
  CHECK(clip->across(64, 400) != nullptr);
  CHECK(clip->across(64, 401) == nullptr);
  CHECK(clip->across(61, 160) == nullptr);
  // A pitch is taken modulo 128.
  CHECK(clip->across(60 + 128, 101) == clip->across(60, 101));

  CHECK(clip->firstEventFrom(0) == 0);
  CHECK(clip->firstEventFrom(101) == 1);
  CHECK(clip->firstEventFrom(200) == 2);
  CHECK(clip->firstEventFrom(401) == 6);
  CHECK(clip->eventsIn(0, 100) == 0);
  CHECK(clip->eventsIn(100, 201) == 4);
  CHECK(clip->eventsIn(200, 400) == 3);
  CHECK(clip->eventsIn(200, 401) == 4);
}

TEST_CASE("a clip's playback is the clip's note events under the time map",
          "[sequence]") {
  // 120 beats per minute at 48 kHz: a quarter note is 24000 frames.
  MidiClipContent content;
  content.extent.position = beat(1);
  content.extent.length = beats(2);
  content.notes = {makeNote(62, beat(0), beats(4), 100)};
  const ClipPlayback clip(content, TimeMap(Tempo{}, 48'000.0));
  CHECK(clip.events() == noteEvents(content, TimeMap(Tempo{}, 48'000.0)));
  // The note is cut at the end of the clip.
  REQUIRE(clip.across(62, 72'000) != nullptr);
  CHECK(*clip.across(62, 72'000) == PlayedNote{
                                        .on = 24'000,
                                        .off = 72'000,
                                        .pitch = 62,
                                        .velocity = 100,
                                    });
}

TEST_CASE("a track's sequence keeps the clips that play, in order",
          "[sequence]") {
  // Given out of order, with a clip without notes in between.
  const TrackSequence sequence(
      7, 48'000.0,
      {
          {
              .clip = ClipId{3},
              .playback = playback(400, 600,
                                   {
                                       on(400, 60, 50),
                                       off(450, 60),
                                       on(500, 62, 60),
                                       off(600, 62),
                                   }),
          },
          {.clip = ClipId{9}, .playback = playback(600, 700, {})},
          {.clip = ClipId{1}, .playback = firstClip()},
          {.clip = ClipId{4}, .playback = nullptr},
      });
  CHECK(sequence.number() == 7);
  CHECK(sequence.sampleRate() == 48'000.0);
  REQUIRE(sequence.clips().size() == 2);
  CHECK(sequence.clips().at(0).clip == ClipId{1});
  CHECK(sequence.clips().at(1).clip == ClipId{3});

  // The first clip that ends on the frame or later.
  CHECK(sequence.firstEndingFrom(0) == 0);
  CHECK(sequence.firstEndingFrom(400) == 0);
  CHECK(sequence.firstEndingFrom(401) == 1);
  CHECK(sequence.firstEndingFrom(601) == 2);

  // Where the clips meet, the note cut at the end of the first is across
  // the frame, and nothing of the second.
  REQUIRE(sequence.across(64, 400) != nullptr);
  CHECK(sequence.across(64, 400)->off == 400);
  CHECK(sequence.across(60, 400) == nullptr);
  REQUIRE(sequence.across(60, 420) != nullptr);
  CHECK(sequence.across(60, 420)->velocity == 50);
  CHECK(sequence.across(62, 700) == nullptr);
  CHECK(sequence.across(62, 0) == nullptr);

  // Events counted over both clips: 400 holds an off and an on.
  CHECK(sequence.eventsIn(0, 1'000) == 10);
  CHECK(sequence.eventsIn(400, 401) == 2);
  CHECK(sequence.eventsIn(250, 450) == 3);
  CHECK(sequence.eventsIn(601, 1'000) == 0);

  // The densest stretch: 400 to 450 holds three events in 51 frames, 100 to
  // 200 holds four in 101.
  CHECK(sequence.densestStretch(1) == 2);
  CHECK(sequence.densestStretch(51) == 3);
  CHECK(sequence.densestStretch(101) == 4);
  CHECK(sequence.densestStretch(1'000) == 10);
}

TEST_CASE("an empty sequence answers every question with nothing",
          "[sequence]") {
  const TrackSequence sequence(1, 44'100.0, {});
  CHECK(sequence.clips().empty());
  CHECK(sequence.firstEndingFrom(0) == 0);
  CHECK(sequence.across(60, 10) == nullptr);
  CHECK(sequence.eventsIn(0, 1'000) == 0);
  CHECK(sequence.densestStretch(512) == 0);
}
