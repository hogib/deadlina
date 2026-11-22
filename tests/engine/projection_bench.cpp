// SPDX-License-Identifier: GPL-3.0-or-later
//
// What edits cost while a large project plays. Not a test and not part of
// the gating suite (docs/design/14-build-test-and-ci.md, "Benches"): it reads
// the clock and prints milliseconds. Build the target
// `lina_projection_bench` in the release preset and run it by hand:
//
//     cmake --build --preset release --target lina_projection_bench
//     build/release/tests/engine/lina_projection_bench
//
// The project: 64 MIDI tracks with the test instrument, 40 clips of 64
// notes on each (2560 clips, 163,840 notes), two return tracks.

#include <catch2/catch_test_macros.hpp>

#include "lina/engine/internal/tracktion_access.hpp"
#include "projection_fixture.hpp"

#include <chrono>
#include <cstdint>
#include <print>
#include <tuple>
#include <vector>

using namespace lina::engine;
using namespace lina::engine::testing;
using namespace lina::model;
using lina::model::testing::beat;
using lina::model::testing::beats;
using lina::model::testing::makeMidiClip;
using lina::model::testing::makeNote;

namespace {

/// Milliseconds a call takes.
template <class Call> double milliseconds(const Call &call) {
  const auto start = std::chrono::steady_clock::now();
  call();
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now() - start)
      .count();
}

} // namespace

TEST_CASE("the cost of edits while a large project plays", "[bench]") {
  ProjectionFixture fixture("projection-bench");
  constexpr int trackCount = 64;
  constexpr int clipsPerTrack = 40;
  constexpr int notesPerClip = 64;
  std::vector<TrackId> tracks;
  std::vector<ClipId> clips;
  const double building = milliseconds([&] {
    for (int t = 0; t < trackCount; ++t) {
      tracks.push_back(fixture.addToneTrack());
      for (int c = 0; c < clipsPerTrack; ++c) {
        std::vector<lina::model::Note> notes;
        notes.reserve(notesPerClip);
        for (int n = 0; n < notesPerClip; ++n) {
          notes.push_back(makeNote(static_cast<std::uint8_t>(36 + (n % 48)),
                                   TickPosition{n * (ticksPerQuarter / 4)},
                                   {ticksPerQuarter / 4}, 100));
        }
        clips.push_back(fixture.addClip(
            tracks.back(),
            makeMidiClip(beat(std::int64_t{16} * c), beats(16), notes)));
      }
    }
    fixture.addTrack(TrackKind::Return);
    fixture.addTrack(TrackKind::Return);
  });
  std::println("building the project command by command: {:.1f} ms", building);

  fixture.engine.openManualDevice({.sampleRate = 48'000.0, .blockSize = 512});
  fixture.engine.play();
  std::ignore = fixture.engine.process(4'096);
  const auto &edit = internal::TracktionAccess::edit(fixture.engine);

  // One rebuild of the playback graph, which edits of notes, MIDI clips and
  // the mixer no longer cause.
  const double rebuild = milliseconds([&] { fixture.engine.syncGraph(); });
  std::println("one rebuild of the playback graph: {:.1f} ms", rebuild);

  // An edit in its two parts: the command on the document, and the change
  // set through the engine, graph included.
  const auto timed = [&](const char *what, const Command &command) {
    ChangeSet changes;
    const double document = milliseconds([&] {
      const auto performed = fixture.document.perform("bench", command);
      REQUIRE(performed.has_value());
      changes = *performed;
    });
    const double engine = milliseconds(
        [&] { fixture.engine.applyChanges(fixture.project(), changes); });
    std::println("{}: document {:.1f} ms, engine {:.1f} ms", what, document,
                 engine);
  };
  for (const std::uint8_t pitch : {100, 101}) {
    timed("one note added",
          AddNotes{
              .clip = clips.at(100),
              .notes = {makeNote(pitch, beat(15), {ticksPerQuarter / 4}, 90)},
          });
  }
  std::println("fader moved, mean of 100: {:.3f} ms",
               milliseconds([&] {
                 for (int i = 0; i < 100; ++i) {
                   fixture.set(tracks.at(3), VolumeValue{-0.05 * i});
                 }
               }) / 100.0);
  timed("time signature changed",
        SetProjectProperty{
            TimeSignatureValue{
                TimeSignature::make(3, 4).value_or(TimeSignature{}),
            },
        });
  timed("tempo changed (every clip moves)",
        SetProjectProperty{
            TempoValue{Tempo::fromHundredths(12'100).value_or(Tempo{})},
        });
  std::println("track muted: {:.3f} ms", milliseconds([&] {
                 fixture.set(tracks.at(5), MuteValue{true});
               }));
  std::println("the project set anew from scratch: {:.1f} ms",
               milliseconds([&] {
                 fixture.engine.setProject(fixture.project(), fixture.scratch);
               }));
  CHECK(tracktion::getAudioTracks(edit).size() == trackCount + 2);
}
