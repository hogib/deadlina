// SPDX-License-Identifier: GPL-3.0-or-later
//
// The schedule a note guard checks sounding notes against (note_guard.hpp),
// without an engine.

#include <catch2/catch_test_macros.hpp>

#include "lina/engine/internal/note_guard.hpp"

#include <array>
#include <cstdint>
#include <vector>

using namespace lina::engine::internal;

namespace {

NoteEvent on(std::int64_t frame, std::uint8_t pitch) {
  return {.frame = frame, .pitch = pitch, .velocity = 100};
}

NoteEvent off(std::int64_t frame, std::uint8_t pitch) {
  return {.frame = frame, .pitch = pitch, .velocity = 0};
}

} // namespace

TEST_CASE("a schedule holds a note from the frame after it starts to the "
          "frame its note-off is on",
          "[note-guard]") {
  ClipEvents clip;
  clip.startFrame = 0;
  clip.events = {on(100, 60), on(150, 64), off(200, 60), off(300, 64)};
  const std::array<const ClipEvents *, 1> clips{&clip};
  const NoteSchedule schedule(clips);
  // A block that starts on the note's own frame has not played its note-on
  // yet: the note is not sounding, and whoever plays the schedule starts
  // it.
  CHECK(!schedule.holds(60, 99));
  CHECK(!schedule.holds(60, 100));
  CHECK(schedule.holds(60, 101));
  // A block that starts on the frame of the note-off still gets it.
  CHECK(schedule.holds(60, 200));
  CHECK(!schedule.holds(60, 201));
  CHECK(schedule.holds(64, 200));
  CHECK(schedule.holds(64, 300));
  CHECK(!schedule.holds(64, 301));
  // A pitch without notes, the lowest and the highest.
  CHECK(!schedule.holds(61, 150));
  CHECK(!schedule.holds(0, 150));
  CHECK(!schedule.holds(127, 150));
}

TEST_CASE("a schedule takes the notes of every clip of a track, whatever "
          "order the clips come in",
          "[note-guard]") {
  ClipEvents late;
  late.startFrame = 1'000;
  late.events = {on(1'000, 60), off(1'100, 60), on(1'200, 60), off(1'300, 60)};
  ClipEvents early;
  early.startFrame = 0;
  early.events = {on(0, 127), on(10, 60), off(1'000, 60), off(1'000, 127)};
  const ClipEvents silent;
  const std::array<const ClipEvents *, 3> clips{&late, &silent, &early};
  const NoteSchedule schedule(clips);
  CHECK(schedule.holds(60, 11));
  // Where the clips meet on one pitch the first note is held up to the
  // join and the second from the frame after.
  CHECK(schedule.holds(60, 1'000));
  CHECK(schedule.holds(60, 1'001));
  CHECK(schedule.holds(60, 1'100));
  CHECK(!schedule.holds(60, 1'101));
  CHECK(!schedule.holds(60, 1'200));
  CHECK(schedule.holds(60, 1'300));
  CHECK(!schedule.holds(60, 5'000));
  CHECK(schedule.holds(127, 1));
  CHECK(schedule.holds(127, 1'000));
  CHECK(!schedule.holds(127, 1'001));
}

TEST_CASE("a schedule of nothing holds nothing", "[note-guard]") {
  const NoteSchedule schedule({});
  CHECK(!schedule.holds(60, 0));
  CHECK(!schedule.holds(127, 1'000'000));
}
