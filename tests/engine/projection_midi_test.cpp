// SPDX-License-Identifier: GPL-3.0-or-later
//
// MIDI clips of a document played through the projection: where notes start
// and end, to the sample.

#include <catch2/catch_test_macros.hpp>

#include "lina/engine/internal/tracktion_access.hpp"
#include "projection_fixture.hpp"
#include "reference_signal.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

using namespace lina::engine;
using namespace lina::engine::testing;
using namespace lina::model;
using lina::model::testing::beat;
using lina::model::testing::beats;
using lina::model::testing::makeMidiClip;
using lina::model::testing::makeNote;

namespace {

__extension__ using Wide = __int128;

/// The frame nearest to a tick, the later one on a tie, from the definition:
/// a tick lasts 25 / (63 * hundredths of a beat per minute) seconds.
std::int64_t nearestFrame(std::int64_t ticks, std::int32_t hundredths,
                          std::int64_t rate) {
  const Wide unit = Wide{63} * hundredths;
  return static_cast<std::int64_t>(((Wide{ticks} * 25 * rate * 2) + unit) /
                                   (2 * unit));
}

bool silent(std::span<const float> channel, std::int64_t from,
            std::int64_t to) {
  return firstSoundingFrame(
             channel.subspan(static_cast<std::size_t>(from),
                             static_cast<std::size_t>(to - from))) == -1;
}

} // namespace

TEST_CASE("a MIDI clip of a document plays its notes from the exact frame to "
          "the exact frame",
          "[projection][midi]") {
  ProjectionFixture fixture("projection-midi-exact");
  const TrackId track = fixture.addToneTrack();
  // 120 beats per minute at 48 kHz: a quarter note is 24000 frames.
  fixture.addClip(
      track, makeMidiClip(beat(1), beats(4),
                          {
                              makeNote(69, beat(0), {ticksPerQuarter / 2}, 127),
                              makeNote(72, beat(1), {ticksPerQuarter / 4}, 64),
                          }));
  const std::int64_t frameCount = 72'000;
  const auto audio = fixture.playFromStart(48'000.0, frameCount);

  const std::array notes{
      SoundingNote{
          .pitch = 69,
          .velocity = 127,
          .onFrame = 24'000,
          .offFrame = 36'000,
      },
      SoundingNote{
          .pitch = 72,
          .velocity = 64,
          .onFrame = 48'000,
          .offFrame = 54'000,
      },
  };
  const auto expected = referenceSignal(frameCount, 48'000.0, notes);
  // Tolerance: formulaTolerance, 1e-6 of full scale.
  CHECK(maxDifference(audio.channels.at(0), expected) <= formulaTolerance);
  CHECK(maxDifference(audio.channels.at(1), expected) <= formulaTolerance);
  const auto &left = audio.channels.at(0);
  CHECK(firstSoundingFrame(left) == 24'000);
  CHECK(left.at(24'000) == 1.0F);
  // The last frame of each note sounds and the frame of its end is silent:
  // the engine's own early note-off (releaseFrame) does not apply.
  CHECK(left.at(35'999) != 0.0F);
  CHECK(silent(left, 36'000, 48'000));
  CHECK(left.at(48'000) == 64.0F / 127.0F);
  CHECK(left.at(53'999) != 0.0F);
  CHECK(silent(left, 54'000, frameCount));
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("notes land on the nearest frame at awkward tempos and rates, far "
          "into the arrangement, whatever the block size",
          "[projection][midi]") {
  struct Case {
    std::int32_t hundredths;
    std::int64_t rate;
    /// Where the clip starts, in quarter notes.
    std::int64_t startBeat;
  };
  // 133.33 beats per minute at 44.1 kHz ten hours in; 99.99 at 48 kHz a
  // day in; the fastest tempo at the highest rate 44 hours in; the slowest
  // tempo 41 hours in; and a plain case near the start.
  const std::array cases{
      Case{.hundredths = 13'333, .rate = 44'100, .startBeat = 80'000},
      Case{.hundredths = 9'999, .rate = 48'000, .startBeat = 150'001},
      Case{.hundredths = 30'000, .rate = 192'000, .startBeat = 800'003},
      Case{.hundredths = 2'000, .rate = 44'100, .startBeat = 50'000},
      Case{.hundredths = 12'001, .rate = 96'000, .startBeat = 7},
  };

  ProjectionFixture fixture("projection-midi-awkward");
  const TrackId track = fixture.addToneTrack();
  for (const Case &testCase : cases) {
    CAPTURE(testCase.hundredths, testCase.rate, testCase.startBeat);
    fixture.setTempo(testCase.hundredths);
    // Notes on ticks that fall between frames: starts and lengths that share
    // no factor with anything.
    const std::array<lina::model::Note, 5> notes{
        makeNote(60, TickPosition{1}, TickDuration{1'511}, 100),
        makeNote(64, TickPosition{2'003}, TickDuration{2'999}, 90),
        makeNote(67, TickPosition{7'561}, TickDuration{3'001}, 80),
        makeNote(60, TickPosition{15'119}, TickDuration{1'009}, 70),
        makeNote(72, TickPosition{20'011}, TickDuration{4'447}, 127),
    };
    const std::int64_t clipStart = testCase.startBeat * ticksPerQuarter;
    const ClipId clip =
        fixture.addClip(track, makeMidiClip(TickPosition{clipStart}, beats(4),
                                            {notes.begin(), notes.end()}));

    // Playback starts a little before the clip, on a frame that no block size
    // below divides.
    const auto frameOf = [&](std::int64_t ticks) {
      return nearestFrame(clipStart + ticks, testCase.hundredths,
                          testCase.rate);
    };
    // Two of the frames in numbers worked out apart from this file, with
    // exact fractions: the first case's clip start and first note-on, and
    // the third case's first note, on and off.
    if (testCase.hundredths == 13'333) {
      REQUIRE(frameOf(0) == 1'587'639'691);
      REQUIRE(frameOf(1) == 1'587'639'692);
      REQUIRE(frameOf(20'011 + 4'447) == 1'587'671'793);
    }
    if (testCase.hundredths == 30'000) {
      REQUIRE(frameOf(1) == 30'720'115'203);
      REQUIRE(frameOf(1'512) == 30'720'119'040);
    }
    const std::int64_t startFrame = frameOf(0) - 1'003;
    const std::int64_t frameCount = frameOf(2 * ticksPerQuarter) - startFrame;
    std::vector<SoundingNote> sounding;
    sounding.reserve(notes.size());
    for (const lina::model::Note &note : notes) {
      sounding.push_back({
          .pitch = note.pitch,
          .velocity = note.velocity,
          .onFrame = frameOf(note.start.ticks) - startFrame,
          .offFrame =
              frameOf(note.start.ticks + note.length.ticks) - startFrame,
      });
    }
    const auto expected = referenceSignal(
        frameCount, static_cast<double>(testCase.rate), sounding);

    std::vector<AudioBuffer> runs;
    for (const int blockSize : {512, 7, 441}) {
      CAPTURE(blockSize);
      fixture.engine.openManualDevice({
          .sampleRate = static_cast<double>(testCase.rate),
          .blockSize = blockSize,
      });
      fixture.engine.play(startFrame);
      runs.push_back(fixture.engine.process(frameCount));
      const auto &left = runs.back().channels.at(0);
      // Tolerance: formulaTolerance, 1e-6 of full scale.
      CHECK(maxDifference(left, expected) <= formulaTolerance);
      // The edges themselves: each note is at full level on its first frame
      // and silent from its last. The first two notes do not overlap others.
      CHECK(firstSoundingFrame(left) == sounding.at(0).onFrame);
      CHECK(left.at(static_cast<std::size_t>(sounding.at(0).onFrame)) ==
            100.0F / 127.0F);
      CHECK(silent(left, sounding.at(0).offFrame, sounding.at(1).onFrame));
      CHECK(left.at(static_cast<std::size_t>(sounding.at(1).onFrame)) ==
            90.0F / 127.0F);
      CHECK(silent(left, sounding.at(4).offFrame, frameCount));
      // Tolerance: none. Every block size gives the same samples.
      CHECK(identical(runs.back(), runs.front()));
    }
    fixture.edit(RemoveClip{clip});
  }
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a note that follows another of its pitch without a gap starts "
          "anew on the frame the first one ends",
          "[projection][midi]") {
  ProjectionFixture fixture("projection-midi-retrigger");
  const TrackId track = fixture.addToneTrack();
  // 133.33 beats per minute at 44.1 kHz: the join falls between frames.
  fixture.setTempo(13'333);
  fixture.addClip(track, makeMidiClip(beat(0), beats(4),
                                      {
                                          makeNote(69, beat(0), beats(1), 127),
                                          makeNote(69, beat(1), beats(1), 64),
                                      }));
  // A second clip follows the first without a gap and starts with the pitch
  // the first one ends with.
  fixture.addClip(track, makeMidiClip(beat(4), beats(2),
                                      {makeNote(69, beat(0), beats(1), 32)}));
  const std::int64_t join = nearestFrame(ticksPerQuarter, 13'333, 44'100);
  const std::int64_t second = nearestFrame(2 * ticksPerQuarter, 13'333, 44'100);
  const std::int64_t clipJoin =
      nearestFrame(4 * ticksPerQuarter, 13'333, 44'100);
  const std::int64_t end = nearestFrame(5 * ticksPerQuarter, 13'333, 44'100);
  REQUIRE(join == 19'845);

  for (const int blockSize : {512, 7}) {
    CAPTURE(blockSize);
    const auto audio = fixture.playFromStart(44'100.0, end + 1'000, blockSize);
    const std::array notes{
        SoundingNote{
            .pitch = 69,
            .velocity = 127,
            .onFrame = 0,
            .offFrame = join,
        },
        SoundingNote{
            .pitch = 69,
            .velocity = 64,
            .onFrame = join,
            .offFrame = second,
        },
        SoundingNote{
            .pitch = 69,
            .velocity = 32,
            .onFrame = clipJoin,
            .offFrame = end,
        },
    };
    const auto expected = referenceSignal(end + 1'000, 44'100.0, notes);
    const auto &left = audio.channels.at(0);
    // Tolerance: formulaTolerance, 1e-6 of full scale.
    CHECK(maxDifference(left, expected) <= formulaTolerance);
    // The first note sounds up to the join, and the second starts there at
    // its own level and with a fresh phase: one voice, not two and not none.
    CHECK(left.at(static_cast<std::size_t>(join) - 1) != 0.0F);
    CHECK(left.at(static_cast<std::size_t>(join)) == 64.0F / 127.0F);
    CHECK(silent(left, second, clipJoin));
    CHECK(left.at(static_cast<std::size_t>(clipJoin)) == 32.0F / 127.0F);
    CHECK(silent(left, end, end + 1'000));
  }
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a note is cut at the end of its clip, also when the next clip "
          "takes its pitch up there",
          "[projection][midi]") {
  ProjectionFixture fixture("projection-midi-clip-end");
  const TrackId track = fixture.addToneTrack();
  fixture.addClip(track, makeMidiClip(beat(0), beats(1),
                                      {makeNote(60, beat(0), beats(8), 127)}));
  fixture.addClip(track, makeMidiClip(beat(1), beats(1),
                                      {makeNote(60, beat(0), beats(8), 64)}));
  const auto audio = fixture.playFromStart(48'000.0, 72'000);
  const std::array notes{
      SoundingNote{
          .pitch = 60,
          .velocity = 127,
          .onFrame = 0,
          .offFrame = 24'000,
      },
      SoundingNote{
          .pitch = 60,
          .velocity = 64,
          .onFrame = 24'000,
          .offFrame = 48'000,
      },
  };
  const auto expected = referenceSignal(72'000, 48'000.0, notes);
  const auto &left = audio.channels.at(0);
  // Tolerance: formulaTolerance, 1e-6 of full scale.
  CHECK(maxDifference(left, expected) <= formulaTolerance);
  CHECK(left.at(23'999) != 0.0F);
  CHECK(left.at(24'000) == 64.0F / 127.0F);
  CHECK(left.at(47'999) != 0.0F);
  CHECK(silent(left, 48'000, 72'000));
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a looped MIDI clip repeats its loop from its content start",
          "[projection][midi]") {
  ProjectionFixture fixture("projection-midi-loop");
  const TrackId track = fixture.addToneTrack();
  Clip clip = makeMidiClip(beat(1), beats(5),
                           {
                               makeNote(60, beat(0), beats(1), 100),
                               makeNote(64, beat(1), beats(1), 90),
                               makeNote(67, beat(2), beats(4), 80),
                           });
  clip.extent.contentStart = beat(1);
  clip.loop = {.enabled = true, .start = beat(1), .length = beats(2)};
  fixture.addClip(track, clip);
  const std::int64_t quarter = 24'000;
  const auto audio = fixture.playFromStart(48'000.0, 7 * quarter);
  // Content beat 1 plays first; the loop is beats 1 to 3, and the long note
  // is cut at its end. The clip ends in the middle of the third pass.
  const std::array notes{
      SoundingNote{
          .pitch = 64,
          .velocity = 90,
          .onFrame = quarter,
          .offFrame = 2 * quarter,
      },
      SoundingNote{
          .pitch = 67,
          .velocity = 80,
          .onFrame = 2 * quarter,
          .offFrame = 3 * quarter,
      },
      SoundingNote{
          .pitch = 64,
          .velocity = 90,
          .onFrame = 3 * quarter,
          .offFrame = 4 * quarter,
      },
      SoundingNote{
          .pitch = 67,
          .velocity = 80,
          .onFrame = 4 * quarter,
          .offFrame = 5 * quarter,
      },
      SoundingNote{
          .pitch = 64,
          .velocity = 90,
          .onFrame = 5 * quarter,
          .offFrame = 6 * quarter,
      },
  };
  const auto expected = referenceSignal(7 * quarter, 48'000.0, notes);
  // Tolerance: formulaTolerance, 1e-6 of full scale.
  CHECK(maxDifference(audio.channels.at(0), expected) <= formulaTolerance);
  CHECK(firstSoundingFrame(audio.channels.at(0)) == quarter);
  CHECK(silent(audio.channels.at(0), 6 * quarter, 7 * quarter));
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("playback that starts inside a note plays the rest of it",
          "[projection][midi]") {
  ProjectionFixture fixture("projection-midi-chase");
  const TrackId track = fixture.addToneTrack();
  fixture.addClip(track, makeMidiClip(beat(0), beats(4),
                                      {
                                          makeNote(60, beat(0), beats(2), 127),
                                          makeNote(64, beat(3), beats(1), 127),
                                      }));
  fixture.engine.openManualDevice({.sampleRate = 48'000.0, .blockSize = 512});
  fixture.engine.play(30'000);
  const auto audio = fixture.engine.process(72'000);
  const auto &left = audio.channels.at(0);
  // The engine starts a note that is already sounding 0.0001 s into the
  // first block, which is frame 5 at 48 kHz; from there it runs to its end
  // on beat 2, frame 48000 of the arrangement.
  CHECK(firstSoundingFrame(left) == 5);
  CHECK(left.at(5) == 1.0F);
  CHECK(left.at(48'000 - 30'000 - 1) != 0.0F);
  CHECK(silent(left, 48'000 - 30'000, 72'000 - 30'000));
  CHECK(left.at(72'000 - 30'000) == 1.0F);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a note that starts on the frame playback starts on is played "
          "once, on that frame",
          "[projection][midi]") {
  ProjectionFixture fixture("projection-midi-start-on-note");
  const TrackId track = fixture.addToneTrack();
  fixture.addClip(track, makeMidiClip(beat(0), beats(4),
                                      {
                                          makeNote(60, beat(0), beats(1), 127),
                                          makeNote(64, beat(2), beats(1), 127),
                                      }));
  fixture.engine.openManualDevice({.sampleRate = 48'000.0, .blockSize = 512});
  for (const std::int64_t start : {std::int64_t{0}, std::int64_t{48'000}}) {
    CAPTURE(start);
    fixture.engine.play(start);
    const auto audio = fixture.engine.process(24'000);
    const std::array note{
        SoundingNote{
            .pitch = start == 0 ? 60 : 64,
            .velocity = 127,
            .onFrame = 0,
            .offFrame = 24'000,
        },
    };
    // One voice at full level from the first frame: not started a second
    // time by the engine's catching up of sounding notes.
    // Tolerance: formulaTolerance, 1e-6 of full scale.
    CHECK(maxDifference(audio.channels.at(0),
                        referenceSignal(24'000, 48'000.0, note)) <=
          formulaTolerance);
    CHECK(audio.channels.at(0).at(0) == 1.0F);
  }
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a MIDI clip behind the engine's horizon is not played, and comes "
          "back when the tempo brings it before the horizon",
          "[projection][midi]") {
  ProjectionFixture fixture("projection-midi-horizon");
  const TrackId track = fixture.addToneTrack();
  // 400000 quarter notes: 55.6 hours at 120 beats per minute, 22.2 hours at
  // 300.
  const ClipId clip =
      fixture.addClip(track, makeMidiClip(beat(400'000), beats(4),
                                          {makeNote(60, beat(0), beats(1))}));
  const auto &entry =
      internal::TracktionAccess::projection(fixture.engine).clips().at(clip);
  CHECK(entry.parts.empty());
  const auto audio = fixture.playFromStart(48'000.0, 4'800);
  CHECK(firstSoundingFrame(audio.channels.at(0)) == -1);

  fixture.setTempo(30'000);
  REQUIRE(entry.parts.size() == 1);
  const std::int64_t start = nearestFrame(beat(400'000).ticks, 30'000, 48'000);
  fixture.engine.play(start - 100);
  const auto late = fixture.engine.process(4'800);
  CHECK(firstSoundingFrame(late.channels.at(0)) == 100);

  fixture.setTempo(12'000);
  CHECK(entry.parts.empty());
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a MIDI clip that the projection did not make is played the "
          "engine's own way",
          "[projection][midi]") {
  // The projection answers only for its own clips; an edit built by hand
  // keeps the engine's early note-off (reference_signal.hpp).
  ProjectionFixture fixture("projection-midi-foreign");
  auto &track = addTrack(fixture.engine);
  addToneInstrument(track);
  const std::array notes{
      lina::engine::testing::Note{
          .pitch = 69,
          .startBeat = 0.0,
          .lengthBeats = 0.5,
          .velocity = 127,
      },
  };
  auto &clip = addMidiClip(track, 0.0, 4.0, notes);
  const auto audio = fixture.playFromStart(48'000.0, 24'000);
  const std::int64_t off = releaseFrame(12'000, 48'000.0);
  CHECK(off == 11'995);
  CHECK(audio.channels.at(0).at(static_cast<std::size_t>(off) - 1) != 0.0F);
  CHECK(silent(audio.channels.at(0), off, 24'000));
  // Asked for beats, the projection's clips are the engine's business too:
  // the projection keeps no notes in the engine's own note list.
  CHECK(clip.getSequence()
            .exportToPlaybackMidiSequence(
                clip, tracktion::MidiList::TimeBase::beats, false)
            .getNumEvents() == 2);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("where one clip ends and the next begins on the same pitch, the "
          "new note sounds from its exact frame, at every position",
          "[projection][midi]") {
  // The note-off of the clip that ends and the note-on of the clip that
  // begins fall on one frame and come from two nodes of the engine. Pairs of
  // clips at many positions, so that the join falls anywhere within a frame
  // and anywhere within a block.
  struct Case {
    std::int32_t hundredths;
    std::int64_t rate;
    int pairs;
    int blockSize;
  };
  const std::array cases{
      Case{
          .hundredths = 12'000,
          .rate = 48'000,
          .pairs = 120,
          .blockSize = 512,
      },
      Case{
          .hundredths = 13'333,
          .rate = 48'000,
          .pairs = 120,
          .blockSize = 512,
      },
      Case{
          .hundredths = 13'333,
          .rate = 44'100,
          .pairs = 120,
          .blockSize = 441,
      },
      Case{.hundredths = 13'333, .rate = 44'100, .pairs = 40, .blockSize = 7},
      Case{.hundredths = 9'999, .rate = 96'000, .pairs = 120, .blockSize = 512},
      Case{.hundredths = 9'999, .rate = 44'100, .pairs = 40, .blockSize = 7},
      Case{.hundredths = 2'000, .rate = 44'100, .pairs = 40, .blockSize = 441},
      Case{.hundredths = 2'000, .rate = 48'000, .pairs = 40, .blockSize = 512},
      Case{
          .hundredths = 30'000,
          .rate = 96'000,
          .pairs = 120,
          .blockSize = 441,
      },
      Case{
          .hundredths = 30'000,
          .rate = 44'100,
          .pairs = 120,
          .blockSize = 512,
      },
      Case{.hundredths = 12'000, .rate = 96'000, .pairs = 40, .blockSize = 7},
  };
  // A clip is 601 ticks long, a number that shares no factor with a frame
  // at any of these tempos and rates; a pair takes 1803 ticks. The first
  // pair starts 977 ticks in.
  constexpr std::int64_t clipTicks = 601;
  constexpr std::int64_t pairTicks = 3 * clipTicks;
  constexpr std::int64_t firstTick = 977;

  ProjectionFixture fixture("projection-midi-joins");
  const TrackId track = fixture.addToneTrack();
  for (int pair = 0; pair < 120; ++pair) {
    const std::int64_t start = firstTick + (pair * pairTicks);
    // The first clip's note runs past the clip's end and is cut there; the
    // second clip's note is shorter than its clip.
    fixture.addClip(track,
                    makeMidiClip(TickPosition{start}, TickDuration{clipTicks},
                                 {
                                     makeNote(60, TickPosition{0},
                                              TickDuration{5 * clipTicks}, 127),
                                 }));
    fixture.addClip(
        track,
        makeMidiClip(TickPosition{start + clipTicks}, TickDuration{clipTicks},
                     {makeNote(60, TickPosition{0}, TickDuration{401}, 64)}));
  }

  for (const Case &testCase : cases) {
    CAPTURE(testCase.hundredths, testCase.rate, testCase.blockSize);
    fixture.setTempo(testCase.hundredths);
    const auto frameOf = [&](std::int64_t ticks) {
      return nearestFrame(ticks, testCase.hundredths, testCase.rate);
    };
    const std::int64_t frameCount =
        frameOf(firstTick + (testCase.pairs * pairTicks));
    std::vector<SoundingNote> sounding;
    sounding.reserve(2 * static_cast<std::size_t>(testCase.pairs));
    for (int pair = 0; pair < testCase.pairs; ++pair) {
      const std::int64_t start = firstTick + (pair * pairTicks);
      sounding.push_back({
          .pitch = 60,
          .velocity = 127,
          .onFrame = frameOf(start),
          .offFrame = frameOf(start + clipTicks),
      });
      sounding.push_back({
          .pitch = 60,
          .velocity = 64,
          .onFrame = frameOf(start + clipTicks),
          .offFrame = frameOf(start + clipTicks + 401),
      });
    }
    const auto expected = referenceSignal(
        frameCount, static_cast<double>(testCase.rate), sounding);
    const auto audio = fixture.playFromStart(static_cast<double>(testCase.rate),
                                             frameCount, testCase.blockSize);
    const auto &left = audio.channels.at(0);
    // Tolerance: formulaTolerance, 1e-6 of full scale.
    CHECK(maxDifference(left, expected) <= formulaTolerance);
    // Every second note is at its own level on the frame of the join: a
    // fresh voice, not silence and not the first note running on.
    int silentJoins = 0;
    for (int pair = 0; pair < testCase.pairs; ++pair) {
      const auto join = static_cast<std::size_t>(
          frameOf(firstTick + (pair * pairTicks) + clipTicks));
      silentJoins += left.at(join) == 64.0F / 127.0F ? 0 : 1;
    }
    CHECK(silentJoins == 0);
  }
  CHECK(fixture.assertions.count() == 0);
}
