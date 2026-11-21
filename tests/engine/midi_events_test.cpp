// SPDX-License-Identifier: GPL-3.0-or-later
//
// What a MIDI clip plays (midi_events.hpp), from ticks to note events on
// sample frames, without an engine.

#include <catch2/catch_test_macros.hpp>

#include "lina/engine/internal/midi_events.hpp"
#include "support/builders.hpp"

#include <cstdint>
#include <vector>

using namespace lina::engine::internal;
using namespace lina::model;
using lina::model::testing::beat;
using lina::model::testing::beats;
using lina::model::testing::makeNote;

namespace {

/// 120 beats per minute at 48 kHz: 24000 frames to the quarter note, and a
/// tick is 100/63 of a frame.
TimeMap plainTime() { return {Tempo{}, 48'000.0}; }
constexpr std::int64_t quarter = 24'000;

Tempo tempoOf(std::int32_t hundredths) {
  const auto tempo = Tempo::fromHundredths(hundredths);
  REQUIRE(tempo.has_value());
  return tempo.value_or(Tempo{});
}

MidiClipContent clipOf(TickPosition position, TickDuration length,
                       std::vector<Note> notes) {
  MidiClipContent clip;
  clip.extent.position = position;
  clip.extent.length = length;
  clip.notes = std::move(notes);
  return clip;
}

NoteEvent on(std::int64_t frame, std::uint8_t pitch, std::uint8_t velocity) {
  return {.frame = frame, .pitch = pitch, .velocity = velocity};
}

NoteEvent off(std::int64_t frame, std::uint8_t pitch) {
  return {.frame = frame, .pitch = pitch, .velocity = 0};
}

using Events = std::vector<NoteEvent>;

} // namespace

TEST_CASE("notes become an on and an off on their frames", "[midi-events]") {
  const auto clip = clipOf(beat(1), beats(4),
                           {
                               makeNote(60, beat(0), beats(1), 100),
                               makeNote(64, beat(2), {ticksPerQuarter / 2}, 90),
                           });
  const ClipEvents events = noteEvents(clip, plainTime());
  CHECK(events.startFrame == quarter);
  CHECK(events.endFrame == 5 * quarter);
  CHECK(!events.truncated);
  CHECK(events.events == Events{
                             on(quarter, 60, 100),
                             off(2 * quarter, 60),
                             on(3 * quarter, 64, 90),
                             off((3 * quarter) + 12'000, 64),
                         });
  CHECK(events.events.front().isNoteOn());
  CHECK(!events.events.back().isNoteOn());
}

TEST_CASE("an edge between two frames goes to the nearest", "[midi-events]") {
  // 133.33 beats per minute at 44.1 kHz: a quarter note is 19845.496 frames.
  const TimeMap time(tempoOf(13'333), 44'100.0);
  const auto clip =
      clipOf(beat(3), beats(8), {makeNote(60, beat(1), beats(2), 100)});
  const ClipEvents events = noteEvents(clip, time);
  // Beats 4 and 6 from the start: 79381.98 and 119072.98.
  CHECK(events.events == Events{on(79'382, 60, 100), off(119'073, 60)});
  CHECK(events.startFrame == 59'536);
  CHECK(events.endFrame == 218'300);
}

TEST_CASE("a note is cut at the end of the clip, and one that starts there "
          "is not played",
          "[midi-events]") {
  const auto clip = clipOf(beat(0), beats(2),
                           {
                               makeNote(60, beat(1), beats(4), 100),
                               makeNote(62, beat(2), beats(1), 100),
                               makeNote(64, beat(3), beats(1), 100),
                           });
  CHECK(noteEvents(clip, plainTime()).events ==
        Events{on(quarter, 60, 100), off(2 * quarter, 60)});
}

TEST_CASE("playing starts at the content start; a note that began before it "
          "does not sound",
          "[midi-events]") {
  auto clip = clipOf(beat(4), beats(4),
                     {
                         makeNote(60, beat(0), beats(2), 100),
                         makeNote(62, beat(1), beats(1), 100),
                         makeNote(64, beat(3), beats(1), 100),
                     });
  clip.extent.contentStart = beat(1);
  // Content beat 1 plays at arrangement beat 4.
  CHECK(noteEvents(clip, plainTime()).events == Events{
                                                    on(4 * quarter, 62, 100),
                                                    off(5 * quarter, 62),
                                                    on(6 * quarter, 64, 100),
                                                    off(7 * quarter, 64),
                                                });
}

TEST_CASE("a looped clip repeats its loop range and cuts notes at the loop "
          "end",
          "[midi-events]") {
  auto clip = clipOf(beat(0), beats(7),
                     {
                         makeNote(60, beat(0), beats(1), 100),
                         makeNote(62, beat(1), beats(1), 90),
                         makeNote(64, beat(2), beats(4), 80),
                         makeNote(66, beat(3), beats(1), 70),
                     });
  clip.loop = {.enabled = true, .start = beat(1), .length = beats(2)};

  SECTION("from before the loop: the lead-in once, then the loop") {
    CHECK(noteEvents(clip, plainTime()).events ==
          Events{
              on(0, 60, 100),
              off(quarter, 60),
              on(quarter, 62, 90),
              off(2 * quarter, 62),
              on(2 * quarter, 64, 80),
              // The loop end at content beat 3 cuts the long note; the note
              // at beat 3 is never reached.
              off(3 * quarter, 64),
              on(3 * quarter, 62, 90),
              off(4 * quarter, 62),
              on(4 * quarter, 64, 80),
              off(5 * quarter, 64),
              on(5 * quarter, 62, 90),
              off(6 * quarter, 62),
              on(6 * quarter, 64, 80),
              // The clip ends in the middle of the third pass.
              off(7 * quarter, 64),
          });
  }
  SECTION("from inside the loop") {
    clip.extent.contentStart = beat(2);
    clip.extent.length = beats(3);
    CHECK(noteEvents(clip, plainTime()).events == Events{
                                                      on(0, 64, 80),
                                                      off(quarter, 64),
                                                      on(quarter, 62, 90),
                                                      off(2 * quarter, 62),
                                                      on(2 * quarter, 64, 80),
                                                      off(3 * quarter, 64),
                                                  });
  }
  SECTION("from the loop end on, the content runs straight on") {
    clip.extent.contentStart = beat(3);
    clip.extent.length = beats(3);
    CHECK(noteEvents(clip, plainTime()).events ==
          Events{on(0, 66, 70), off(quarter, 66)});
  }
  SECTION("with the loop switched off") {
    clip.loop.enabled = false;
    CHECK(noteEvents(clip, plainTime()).events == Events{
                                                      on(0, 60, 100),
                                                      off(quarter, 60),
                                                      on(quarter, 62, 90),
                                                      off(2 * quarter, 62),
                                                      on(2 * quarter, 64, 80),
                                                      on(3 * quarter, 66, 70),
                                                      off(4 * quarter, 66),
                                                      off(6 * quarter, 64),
                                                  });
  }
}

TEST_CASE("a loop range without notes is not walked", "[midi-events]") {
  // The longest clip a document can hold, looping one tick without a note
  // in it: 2^53 passes if they were counted.
  auto clip = clipOf(TickPosition{0}, TickDuration{maxTicks},
                     {makeNote(60, beat(0), beats(1), 100)});
  clip.loop = {.enabled = true, .start = beat(2), .length = TickDuration{1}};
  const ClipEvents events = noteEvents(clip, plainTime());
  CHECK(events.events == Events{on(0, 60, 100), off(quarter, 60)});
  CHECK(!events.truncated);
  CHECK(events.endFrame == plainTime().horizonFrame());

  // Nor is it when the only notes left lie behind the loop.
  clip.notes.push_back(makeNote(62, beat(3), beats(1), 100));
  CHECK(noteEvents(clip, plainTime()).events ==
        Events{on(0, 60, 100), off(quarter, 60)});
}

TEST_CASE("a looped clip that reaches the horizon before its loop end stops "
          "there",
          "[midi-events]") {
  // The clip starts a beat before the horizon; its loop would begin two
  // beats later.
  auto clip = clipOf(beat(341'999), beats(64),
                     {
                         makeNote(60, beat(0), beats(1), 100),
                         makeNote(62, beat(2), beats(1), 100),
                         makeNote(64, beat(5), beats(1), 100),
                     });
  clip.loop = {.enabled = true, .start = beat(4), .length = beats(4)};
  const std::int64_t horizon = plainTime().horizonFrame();
  CHECK(noteEvents(clip, plainTime()).events ==
        Events{on(horizon - quarter, 60, 100), off(horizon, 60)});
}

TEST_CASE("a note-off and a note-on of one pitch on one frame come out off "
          "first",
          "[midi-events]") {
  const auto clip = clipOf(beat(0), beats(4),
                           {
                               makeNote(60, beat(0), beats(1), 100),
                               makeNote(72, beat(1), beats(1), 50),
                               makeNote(60, beat(1), beats(1), 90),
                           });
  CHECK(noteEvents(clip, plainTime()).events == Events{
                                                    on(0, 60, 100),
                                                    off(quarter, 60),
                                                    on(quarter, 72, 50),
                                                    on(quarter, 60, 90),
                                                    off(2 * quarter, 72),
                                                    off(2 * quarter, 60),
                                                });
}

TEST_CASE("a note shorter than a frame lasts one frame", "[midi-events]") {
  // At 300 beats per minute and 8 kHz a tick is 20/189 of a frame, so a few
  // ticks stay inside one frame.
  const TimeMap fast(tempoOf(30'000), 8'000.0);
  SECTION("alone") {
    const auto clip =
        clipOf(beat(0), beats(4), {makeNote(60, TickPosition{945}, {3}, 100)});
    // 945 ticks are frame 100 exactly; 948 ticks are frame 100.3.
    CHECK(noteEvents(clip, fast).events ==
          Events{on(100, 60, 100), off(101, 60)});
  }
  SECTION("ended by the next note of its pitch where that one starts") {
    const auto clip = clipOf(beat(0), beats(4),
                             {
                                 makeNote(60, TickPosition{945}, {3}, 100),
                                 makeNote(60, TickPosition{954}, {945}, 90),
                             });
    // The second note starts on frame 100.95, which is frame 101.
    CHECK(noteEvents(clip, fast).events == Events{
                                               on(100, 60, 100),
                                               off(101, 60),
                                               on(101, 60, 90),
                                               off(201, 60),
                                           });
  }
  SECTION("left out when the next note of its pitch starts on its frame") {
    const auto clip = clipOf(beat(0), beats(4),
                             {
                                 makeNote(60, TickPosition{945}, {2}, 100),
                                 makeNote(61, TickPosition{945}, {2}, 80),
                                 makeNote(60, TickPosition{948}, {945}, 90),
                             });
    CHECK(noteEvents(clip, fast).events == Events{
                                               on(100, 61, 80),
                                               on(100, 60, 90),
                                               off(101, 61),
                                               off(200, 60),
                                           });
  }
}

TEST_CASE("a note whose start rounds to the clip's last frame is not played",
          "[midi-events]") {
  const TimeMap fast(tempoOf(30'000), 8'000.0);
  // The clip ends on tick 945, frame 100; the note starts two ticks before,
  // on frame 99.8.
  const auto clip = clipOf(TickPosition{0}, TickDuration{945},
                           {
                               makeNote(60, TickPosition{0}, {945}, 100),
                               makeNote(62, TickPosition{943}, {2}, 100),
                           });
  const ClipEvents events = noteEvents(clip, fast);
  CHECK(events.endFrame == 100);
  CHECK(events.events == Events{on(0, 60, 100), off(100, 60)});
}

TEST_CASE("nothing is played behind the engine's horizon", "[midi-events]") {
  const std::int64_t horizon = plainTime().horizonFrame();
  // 342000 quarter notes are 47.5 hours at 120 beats per minute.
  const TickPosition atHorizon = beat(342'000);
  SECTION("a clip that starts there is empty") {
    const ClipEvents events = noteEvents(
        clipOf(atHorizon, beats(4), {makeNote(60, beat(0), beats(1), 100)}),
        plainTime());
    CHECK(events.events.empty());
    CHECK(events.startFrame == horizon);
    CHECK(events.endFrame == horizon);
  }
  SECTION("a clip that crosses it is cut there") {
    const ClipEvents events =
        noteEvents(clipOf(beat(341'999), beats(4),
                          {
                              makeNote(60, beat(0), beats(2), 100),
                              makeNote(62, beat(1), beats(1), 100),
                          }),
                   plainTime());
    CHECK(events.endFrame == horizon);
    CHECK(events.events ==
          Events{on(horizon - quarter, 60, 100), off(horizon, 60)});
  }
  SECTION("the last storable position is far behind it") {
    const ClipEvents events =
        noteEvents(clipOf(TickPosition{maxTicks - 1}, TickDuration{1},
                          {makeNote(60, TickPosition{0}, {1}, 100)}),
                   plainTime());
    CHECK(events.events.empty());
  }
}

TEST_CASE("a clip stops adding notes at the limit and says so",
          "[midi-events]") {
  // A sixteenth note looped over ten thousand hours of clip.
  auto clip = clipOf(TickPosition{0}, TickDuration{maxTicks},
                     {makeNote(60, beat(0), {ticksPerQuarter / 8}, 100)});
  clip.loop = {
      .enabled = true,
      .start = beat(0),
      .length = {ticksPerQuarter / 4},
  };
  const ClipEvents events = noteEvents(clip, plainTime());
  CHECK(events.truncated);
  REQUIRE(events.events.size() == 2 * maxSoundingNotesPerClip);
  CHECK(events.events.at(0) == on(0, 60, 100));
  CHECK(events.events.at(1) == off(3'000, 60));
  CHECK(events.events.at(2) == on(6'000, 60, 100));
  CHECK(events.events.back() ==
        off(((static_cast<std::int64_t>(maxSoundingNotesPerClip) - 1) * 6'000) +
                3'000,
            60));
}
