// SPDX-License-Identifier: GPL-3.0-or-later
//
// Editing the document while the engine plays it: after the change set has
// been applied and the graph synchronised, the stream goes on as if the
// changed document had been playing all along.

#include <catch2/catch_test_macros.hpp>

#include "lina/engine/internal/engine_plugins.hpp"
#include "lina/engine/internal/tracktion_access.hpp"
#include "projection_fixture.hpp"
#include "reference_signal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
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

constexpr double sampleRate = 48'000.0;
/// 120 beats per minute at 48 kHz.
constexpr std::int64_t quarter = 24'000;

/// Appends `more` to `audio`.
void append(AudioBuffer &audio, const AudioBuffer &more) {
  for (std::size_t channel = 0; channel < audio.channels.size(); ++channel) {
    auto &samples = audio.channels.at(channel);
    const auto &added = more.channels.at(channel);
    samples.insert(samples.end(), added.begin(), added.end());
  }
}

/// True when the two streams are the same from `frame` on, bit for bit.
bool sameFrom(const AudioBuffer &a, const AudioBuffer &b, std::int64_t frame) {
  if (a.channels.size() != b.channels.size() ||
      a.frameCount() != b.frameCount()) {
    return false;
  }
  for (std::size_t channel = 0; channel < a.channels.size(); ++channel) {
    const auto from = static_cast<std::size_t>(frame);
    if (!std::ranges::equal(std::span(a.channels.at(channel)).subspan(from),
                            std::span(b.channels.at(channel)).subspan(from))) {
      return false;
    }
  }
  return true;
}

/// A track that keeps state across the edit: the test instrument, whose
/// phase runs on, into a delay of a quarter of a second at half level, whose
/// line holds what was played before.
struct LiveFixture : ProjectionFixture {
  explicit LiveFixture(std::string_view name) : ProjectionFixture(name) {
    track = addToneTrack();
    addDevice(track, delayGainDevice(12'000, 0.5F));
    // A long note that sounds across the edit, and a short one before it.
    clip = addClip(track, makeMidiClip(beat(0), beats(8),
                                       {
                                           makeNote(48, beat(0), beats(6), 80),
                                           makeNote(72, beat(1),
                                                    {ticksPerQuarter / 4}, 100),
                                       }));
  }

  /// Plays `before` frames, makes the edit and plays on to `total` frames.
  /// Returns the whole stream.
  AudioBuffer playAcross(std::int64_t before, std::int64_t total,
                         const std::function<void()> &makeEdit) {
    engine.openManualDevice({.sampleRate = sampleRate, .blockSize = 512});
    engine.play();
    AudioBuffer audio = engine.process(before);
    makeEdit();
    append(audio, engine.process(total - before));
    return audio;
  }

  TrackId track;
  ClipId clip;
};

constexpr std::int64_t editFrame = (2 * quarter) + 1'234;
constexpr std::int64_t totalFrames = 8 * quarter;

/// The last frame that is not silent, or -1.
std::int64_t lastSoundingFrame(std::span<const float> channel) {
  const auto last =
      std::ranges::find_if(channel.rbegin(), channel.rend(),
                           [](float sample) { return sample != 0.0F; });
  return (channel.rend() - last) - 1;
}

} // namespace

TEST_CASE("a note added ahead of the playhead is heard as if it had always "
          "been there",
          "[projection][live]") {
  LiveFixture fixture("projection-live-note");
  const auto unedited = fixture.playFromStart(sampleRate, totalFrames);
  const auto live = fixture.playAcross(editFrame, totalFrames, [&] {
    fixture.edit(AddNotes{
        .clip = fixture.clip,
        .notes = {makeNote(64, beat(3), beats(1), 127)},
    });
  });
  const auto fromStart = fixture.playFromStart(sampleRate, totalFrames);

  // Tolerance: none. The stream is the changed document played from the
  // start: the held note's phase and the delay line carried across the
  // edit, and the new note sounds from beat 3, a delay later.
  CHECK(identical(live, fromStart));
  CHECK(!sameFrom(live, unedited, 0));
  constexpr std::size_t newNoteHeard = (3 * quarter) + 12'000;
  CHECK(std::ranges::equal(
      std::span(live.channels.at(0)).first(newNoteHeard),
      std::span(unedited.channels.at(0)).first(newNoteHeard)));
  CHECK(live.channels.at(0).at(newNoteHeard) !=
        unedited.channels.at(0).at(newNoteHeard));
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a clip moved ahead of the playhead plays at its new place",
          "[projection][live]") {
  LiveFixture fixture("projection-live-move");
  const TrackId other = fixture.addToneTrack();
  const ClipId moving = fixture.addClip(
      other, makeMidiClip(beat(9), beats(1),
                          {makeNote(76, beat(0), {ticksPerQuarter / 2}, 100)}));
  const auto unedited = fixture.playFromStart(sampleRate, totalFrames);

  SECTION("on its own track") {
    const auto live = fixture.playAcross(editFrame, totalFrames, [&] {
      fixture.edit(
          MoveClip{.clip = moving, .container = other, .position = beat(4)});
    });
    const auto fromStart = fixture.playFromStart(sampleRate, totalFrames);
    // Tolerance: none.
    CHECK(identical(live, fromStart));
    CHECK(!identical(live, unedited));
  }
  SECTION("to another track, where its sounding note starts again") {
    const auto live = fixture.playAcross(editFrame, totalFrames, [&] {
      fixture.edit(MoveClip{
          .clip = fixture.clip,
          .container = other,
          .position = beat(0),
      });
    });
    // The held note ends on the track it left, whose delay plays out what
    // it holds for 12000 frames more. On the track it came to, the engine
    // starts it 0.0001 s into the next block, frame 5 at 48 kHz, and it
    // runs to its end on beat 6.
    const std::array notes{
        SoundingNote{
            .pitch = 48,
            .velocity = 80,
            .onFrame = editFrame + 5,
            .offFrame = 6 * quarter,
        },
    };
    const auto expected = referenceSignal(totalFrames, sampleRate, notes);
    const auto from = static_cast<std::size_t>(editFrame + 12'000);
    // Tolerance: formulaTolerance, 1e-6 of full scale.
    CHECK(maxDifference(std::span(live.channels.at(0)).subspan(from),
                        std::span(expected).subspan(from)) <= formulaTolerance);
    // Until then the delay's tail and the restarted note sound together.
    CHECK(live.channels.at(0).at(from - 1) != expected.at(from - 1));
  }
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a change of volume while playing is heard from the next block, "
          "after its ramp",
          "[projection][live]") {
  LiveFixture fixture("projection-live-volume");
  const auto unedited = fixture.playFromStart(sampleRate, totalFrames);
  const auto live = fixture.playAcross(editFrame, totalFrames, [&] {
    fixture.set(fixture.track, VolumeValue{-12.0});
  });
  const auto fromStart = fixture.playFromStart(sampleRate, totalFrames);
  const auto rampFrames = static_cast<std::int64_t>(
      internal::StripGainPlugin::rampSeconds * sampleRate);
  REQUIRE(rampFrames == 240);

  // Before the edit the stream is the unedited document's.
  CHECK(std::ranges::equal(
      std::span(live.channels.at(0)).first(static_cast<std::size_t>(editFrame)),
      std::span(unedited.channels.at(0))
          .first(static_cast<std::size_t>(editFrame))));
  // Tolerance: none. Once the ramp is through, it is the changed
  // document's.
  CHECK(sameFrom(live, fromStart, editFrame + rampFrames - 1));
  // Inside the ramp every sample lies between the two.
  for (std::int64_t frame = editFrame; frame < editFrame + rampFrames;
       ++frame) {
    const auto index = static_cast<std::size_t>(frame);
    const float loud = std::abs(unedited.channels.at(0).at(index));
    const float quiet = std::abs(fromStart.channels.at(0).at(index));
    const float heard = std::abs(live.channels.at(0).at(index));
    CHECK(heard <= loud);
    CHECK(heard >= quiet);
  }
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a track added while playing joins the stream",
          "[projection][live]") {
  LiveFixture fixture("projection-live-track");
  const auto unedited = fixture.playFromStart(sampleRate, totalFrames);
  const auto live = fixture.playAcross(editFrame, totalFrames, [&] {
    // One gesture: a track with an instrument, an effect and a clip.
    const TrackId added = fixture.addTrack(TrackKind::Midi);
    const std::array<Command, 3> rest{
        AddDevice{.track = added, .device = toneDevice(), .index = 0},
        AddDevice{
            .track = added,
            .device = delayGainDevice(0, -0.5F),
            .index = 1,
        },
        AddClip{
            .container = added,
            .clip = makeMidiClip(beat(3), beats(2),
                                 {makeNote(67, beat(0), beats(1), 90)}),
        },
    };
    fixture.editAll(rest);
  });
  const auto fromStart = fixture.playFromStart(sampleRate, totalFrames);
  // Tolerance: none.
  CHECK(identical(live, fromStart));
  CHECK(!identical(live, unedited));
  CHECK(live.channels.at(0).at(3 * quarter) !=
        unedited.channels.at(0).at(3 * quarter));
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a sounding note that an edit takes away ends at once",
          "[projection][live]") {
  LiveFixture fixture("projection-live-stale");
  const auto &notes =
      std::get<MidiContent>(
          fixture.project().tracks.at(0).arrangement.at(0).content)
          .notes;
  const NoteId held = notes.at(0).id;
  // The held note sounds from beat 0 to beat 6; through the delay its last
  // frame comes out 12000 frames after it ends.
  const std::int64_t delay = 12'000;

  SECTION("when the note is removed") {
    const auto live = fixture.playAcross(editFrame, totalFrames, [&] {
      fixture.edit(RemoveNotes{.clip = fixture.clip, .notes = {held}});
    });
    CHECK(lastSoundingFrame(live.channels.at(0)) == editFrame + delay - 1);
  }
  SECTION("when the note is cut short behind the playhead") {
    const auto live = fixture.playAcross(editFrame, totalFrames, [&] {
      fixture.edit(SetNotes{
          .clip = fixture.clip,
          .notes =
              {
                  {
                      .id = held,
                      .pitch = 48,
                      .start = beat(0),
                      .length = beats(1),
                      .velocity = 80,
                  },
              },
      });
    });
    CHECK(lastSoundingFrame(live.channels.at(0)) == editFrame + delay - 1);
  }
  SECTION("when its clip is removed") {
    const auto live = fixture.playAcross(editFrame, totalFrames, [&] {
      fixture.edit(RemoveClip{fixture.clip});
    });
    CHECK(lastSoundingFrame(live.channels.at(0)) == editFrame + delay - 1);
  }
  SECTION("when its track is removed, the sound stops with the track") {
    const auto live = fixture.playAcross(editFrame, totalFrames, [&] {
      fixture.edit(RemoveTrack{fixture.track});
    });
    CHECK(lastSoundingFrame(live.channels.at(0)) < editFrame);
  }
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a change of tempo while playing keeps the musical position, and "
          "sounding notes run to their end",
          "[projection][live]") {
  LiveFixture fixture("projection-live-tempo");
  const std::int64_t delay = 12'000;
  fixture.setTempo(6'000);
  // At 60 beats per minute a beat is 48000 frames. The edit comes 1.25
  // beats in, with the held note sounding; at 300 beats per minute that
  // position is frame 12000 and the note's end on beat 6 is frame 57600,
  // 45600 frames on.
  const std::int64_t tempoEdit = 60'000;
  const auto live = fixture.playAcross(tempoEdit, totalFrames,
                                       [&] { fixture.setTempo(30'000); });
  // The frame the edit came on lies behind the note's new end in frames;
  // the note is not taken for one that lost its end.
  CHECK(lastSoundingFrame(live.channels.at(0)) ==
        tempoEdit + 45'600 + delay - 1);
  // The whole stream, from the definitions: the held note sounds from frame
  // 0 with its phase running on across the change, until 45600 frames after
  // it; the short note sounded from beat 1 to beat 1.25 of the old tempo,
  // frames 48000 to 60000, and ends on the frame of the change. Everything
  // comes out of the delay 12000 frames later at half level.
  const std::array notes{
      SoundingNote{
          .pitch = 48,
          .velocity = 80,
          .onFrame = delay,
          .offFrame = tempoEdit + 45'600 + delay,
          .gain = 0.5F,
      },
      SoundingNote{
          .pitch = 72,
          .velocity = 100,
          .onFrame = 48'000 + delay,
          .offFrame = 60'000 + delay,
          .gain = 0.5F,
      },
  };
  // Tolerance: formulaTolerance, 1e-6 of full scale.
  CHECK(maxDifference(live.channels.at(0),
                      referenceSignal(totalFrames, sampleRate, notes)) <=
        formulaTolerance);
  CHECK(fixture.assertions.count() == 0);
}

namespace {

/// A track with the test instrument and nothing behind it, so that a note
/// is heard from the frame it starts to the frame it ends and not a frame
/// longer. One note starts on frame 47800 and lasts two beats; another
/// sounds from frame 0 and is never touched.
struct StaleFixture : ProjectionFixture {
  static constexpr std::int64_t noteOn = 47'800;
  /// 47800 frames are 30114 ticks at 120 beats per minute and 48 kHz.
  static constexpr std::int64_t noteOnTick = 30'114;

  explicit StaleFixture(std::string_view name) : ProjectionFixture(name) {
    track = addToneTrack();
    clip = addClip(
        track,
        makeMidiClip(beat(0), beats(16),
                     {
                         makeNote(40, beat(0), beats(16), 20),
                         makeNote(69, TickPosition{noteOnTick}, beats(2), 127),
                     }));
  }

  /// The note that starts on frame 47800.
  [[nodiscard]] NoteId late() const {
    return std::get<MidiContent>(
               project().tracks.at(0).arrangement.at(0).content)
        .notes.at(1)
        .id;
  }

  /// Plays `before` frames in blocks of `blockSize`, makes the edit and
  /// plays on.
  AudioBuffer playAcross(std::int64_t before, std::int64_t total, int blockSize,
                         const std::function<void()> &makeEdit) {
    engine.openManualDevice({.sampleRate = sampleRate, .blockSize = blockSize});
    engine.play();
    AudioBuffer audio = engine.process(before);
    makeEdit();
    append(audio, engine.process(total - before));
    return audio;
  }

  /// The stream with the late note sounding from 47800 to `end`, and with
  /// the low note left out when `withLow` is false.
  static std::vector<float> expected(std::int64_t total, std::int64_t end,
                                     bool withLow = true) {
    std::vector<SoundingNote> notes{
        {.pitch = 69, .velocity = 127, .onFrame = noteOn, .offFrame = end},
    };
    if (withLow) {
      notes.push_back(
          {.pitch = 40, .velocity = 20, .onFrame = 0, .offFrame = total});
    }
    return referenceSignal(total, sampleRate, notes);
  }

  TrackId track;
  ClipId clip;
};

} // namespace

TEST_CASE("a note that started in the block before an edit removes it ends "
          "with the next block",
          "[projection][live]") {
  StaleFixture fixture("projection-live-last-block");
  // The edit comes anywhere from one frame to more than a block after the
  // note-on. What was played before it stays; from the edit's frame on the
  // removed note is silent and the other note runs on untouched.
  for (const int blockSize : {512, 7, 441}) {
    for (std::int64_t after = 1; after <= 1'100;
         after += blockSize == 7 ? 211 : 37) {
      const std::int64_t editAt = StaleFixture::noteOn + after;
      CAPTURE(blockSize, editAt);
      const auto removed =
          fixture.playAcross(editAt, editAt + 2'000, blockSize, [&] {
            fixture.edit(
                RemoveNotes{.clip = fixture.clip, .notes = {fixture.late()}});
          });
      // Tolerance: formulaTolerance, 1e-6 of full scale.
      CHECK(maxDifference(removed.channels.at(0),
                          StaleFixture::expected(editAt + 2'000, editAt)) <=
            formulaTolerance);
      const auto undone = fixture.document.undo();
      REQUIRE(undone.has_value());
      fixture.engine.applyChanges(fixture.project(), *undone);
    }
  }
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a note whose end an edit moves into what was already played ends "
          "with the next block",
          "[projection][live]") {
  StaleFixture fixture("projection-live-end-moved-back");
  for (const int blockSize : {512, 7, 441}) {
    CAPTURE(blockSize);
    // After 48000 frames the note has sounded for 200. Its end is moved to
    // 100 frames (63 ticks) after its start.
    const auto live = fixture.playAcross(48'000, 60'000, blockSize, [&] {
      fixture.edit(SetNotes{
          .clip = fixture.clip,
          .notes =
              {
                  {
                      .id = fixture.late(),
                      .pitch = 69,
                      .start = TickPosition{StaleFixture::noteOnTick},
                      .length = TickDuration{63},
                      .velocity = 127,
                  },
              },
      });
    });
    CHECK(maxDifference(live.channels.at(0),
                        StaleFixture::expected(60'000, 48'000)) <=
          formulaTolerance);
    const auto undone = fixture.document.undo();
    REQUIRE(undone.has_value());
    fixture.engine.applyChanges(fixture.project(), *undone);
  }
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a note that an edit moves ahead ends at once and starts again at "
          "its new place",
          "[projection][live]") {
  StaleFixture fixture("projection-live-moved-ahead");
  // From frame 47800 to beat 3, frame 72000.
  const auto live = fixture.playAcross(48'000, 100'000, 512, [&] {
    fixture.edit(SetNotes{
        .clip = fixture.clip,
        .notes =
            {
                {
                    .id = fixture.late(),
                    .pitch = 69,
                    .start = beat(3),
                    .length = beats(1),
                    .velocity = 127,
                },
            },
    });
  });
  const std::array notes{
      SoundingNote{
          .pitch = 40,
          .velocity = 20,
          .onFrame = 0,
          .offFrame = 100'000,
      },
      SoundingNote{
          .pitch = 69,
          .velocity = 127,
          .onFrame = 47'800,
          .offFrame = 48'000,
      },
      SoundingNote{
          .pitch = 69,
          .velocity = 127,
          .onFrame = 72'000,
          .offFrame = 96'000,
      },
  };
  CHECK(maxDifference(live.channels.at(0),
                      referenceSignal(100'000, sampleRate, notes)) <=
        formulaTolerance);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a clip or a track that goes with a note that started in the last "
          "block leaves no sound behind",
          "[projection][live]") {
  for (const int blockSize : {512, 7, 441}) {
    CAPTURE(blockSize);
    StaleFixture fixture("projection-live-clip-gone");
    // A second track that plays on, so that silence is the removed track's
    // and not the engine's.
    const TrackId other = fixture.addToneTrack();
    fixture.addClip(other,
                    makeMidiClip(beat(0), beats(16),
                                 {makeNote(52, beat(0), beats(16), 30)}));
    const std::array otherNote{
        SoundingNote{
            .pitch = 52,
            .velocity = 30,
            .onFrame = 0,
            .offFrame = 60'000,
        },
    };
    const auto otherAlone = referenceSignal(60'000, sampleRate, otherNote);
    const auto before = [&](std::int64_t frame) {
      // Up to the edit all three notes sound.
      std::vector<SoundingNote> notes{
          {.pitch = 52, .velocity = 30, .onFrame = 0, .offFrame = 60'000},
          {.pitch = 40, .velocity = 20, .onFrame = 0, .offFrame = frame},
          {
              .pitch = 69,
              .velocity = 127,
              .onFrame = StaleFixture::noteOn,
              .offFrame = frame,
          },
      };
      return referenceSignal(60'000, sampleRate, notes);
    };

    const auto clipGone = fixture.playAcross(48'000, 60'000, blockSize, [&] {
      fixture.edit(RemoveClip{fixture.clip});
    });
    CHECK(maxDifference(clipGone.channels.at(0), before(48'000)) <=
          formulaTolerance);
    CHECK(std::ranges::equal(
        std::span(clipGone.channels.at(0)).subspan(48'000),
        std::span(otherAlone).subspan(48'000),
        [](float a, float b) { return std::abs(a - b) <= formulaTolerance; }));

    const auto undone = fixture.document.undo();
    REQUIRE(undone.has_value());
    fixture.engine.applyChanges(fixture.project(), *undone);
    const auto trackGone = fixture.playAcross(48'000, 60'000, blockSize, [&] {
      fixture.edit(RemoveTrack{fixture.track});
    });
    CHECK(maxDifference(trackGone.channels.at(0), before(48'000)) <=
          formulaTolerance);
    CHECK(fixture.assertions.count() == 0);
  }
}

TEST_CASE("describing the edit between an edit and the next block changes "
          "nothing",
          "[projection][live]") {
  StaleFixture fixture("projection-live-describe");
  const auto &projection =
      internal::TracktionAccess::projection(fixture.engine);
  fixture.engine.openManualDevice({.sampleRate = sampleRate, .blockSize = 512});
  fixture.engine.play();
  AudioBuffer audio = fixture.engine.process(48'000);
  const std::uint64_t requests = projection.sequenceRequests();
  const std::string before = describeEdit(fixture.engine);
  CHECK(describeEdit(fixture.engine) == before);
  CHECK(projection.sequenceRequests() == requests);
  fixture.edit(RemoveNotes{.clip = fixture.clip, .notes = {fixture.late()}});
  std::ignore = describeEdit(fixture.engine);
  std::ignore = fixture.engine.truncatedClips();
  append(audio, fixture.engine.process(12'000));
  CHECK(maxDifference(audio.channels.at(0),
                      StaleFixture::expected(60'000, 48'000)) <=
        formulaTolerance);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("applying a change set while playing leaves nothing for later",
          "[projection][live]") {
  StaleFixture fixture("projection-live-complete");
  const auto &projection =
      internal::TracktionAccess::projection(fixture.engine);
  auto &edit = internal::TracktionAccess::edit(fixture.engine);
  const TrackId other = fixture.addToneTrack();
  fixture.addClip(other, makeMidiClip(beat(0), beats(4),
                                      {makeNote(52, beat(0), beats(1), 30)}));
  fixture.engine.openManualDevice({.sampleRate = sampleRate, .blockSize = 512});
  fixture.engine.play();
  std::ignore = fixture.engine.process(4'096);

  // An edit of the notes: the graph is rebuilt once, which asks for each of
  // the two MIDI clips once, and the engine's own timer has nothing left to
  // do: running what it would run rebuilds nothing.
  std::uint64_t requests = projection.sequenceRequests();
  fixture.edit(AddNotes{
      .clip = fixture.clip,
      .notes = {makeNote(76, beat(8), beats(1), 90)},
  });
  CHECK(projection.sequenceRequests() == requests + 2);
  edit.dispatchPendingUpdatesSynchronously();
  CHECK(projection.sequenceRequests() == requests + 2);
  const auto &guard = *projection.tracks().at(fixture.track).guard;
  CHECK(guard.isWatching());
  std::ignore = fixture.engine.process(std::int64_t{512} *
                                       internal::NoteGuardPlugin::settleBlocks);
  CHECK(!guard.isWatching());

  // A structural edit, which the engine notices by itself: once as well.
  requests = projection.sequenceRequests();
  fixture.addDevice(other, delayGainDevice(3, 1.0F));
  CHECK(projection.sequenceRequests() == requests + 2);
  edit.dispatchPendingUpdatesSynchronously();
  CHECK(projection.sequenceRequests() == requests + 2);

  // A fader needs no new graph at all.
  requests = projection.sequenceRequests();
  fixture.set(other, VolumeValue{-3.0});
  fixture.set(other, NameValue{"renamed"});
  edit.dispatchPendingUpdatesSynchronously();
  CHECK(projection.sequenceRequests() == requests);

  // Neither does a change of the time signature alone.
  const auto sevenEight = TimeSignature::make(7, 8);
  REQUIRE(sevenEight.has_value());
  const auto *engineClip =
      projection.clips().at(fixture.clip).parts.front().get();
  fixture.edit(SetProjectProperty{
      TimeSignatureValue{sevenEight.value_or(TimeSignature{})},
  });
  CHECK(projection.clips().at(fixture.clip).parts.front().get() == engineClip);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a sounding note whose end is moved plays to its new end",
          "[projection][live]") {
  LiveFixture fixture("projection-live-length");
  const NoteId held =
      std::get<MidiContent>(
          fixture.project().tracks.at(0).arrangement.at(0).content)
          .notes.at(0)
          .id;
  const auto resize = [&](std::int64_t beatCount) {
    return [&fixture, held, beatCount] {
      fixture.edit(SetNotes{
          .clip = fixture.clip,
          .notes =
              {
                  {
                      .id = held,
                      .pitch = 48,
                      .start = beat(0),
                      .length = beats(beatCount),
                      .velocity = 80,
                  },
              },
      });
    };
  };
  const std::int64_t delay = 12'000;
  // Shortened to end ahead of the playhead, on beat 3.
  const auto shorter = fixture.playAcross(editFrame, totalFrames, resize(3));
  CHECK(lastSoundingFrame(shorter.channels.at(0)) == (3 * quarter) + delay - 1);
  // The stream is the changed document's from the start: nothing restarted.
  CHECK(identical(shorter, fixture.playFromStart(sampleRate, totalFrames)));
  // Lengthened to beat 7.
  const auto longer = fixture.playAcross(editFrame, totalFrames, resize(7));
  CHECK(lastSoundingFrame(longer.channels.at(0)) == (7 * quarter) + delay - 1);
  CHECK(identical(longer, fixture.playFromStart(sampleRate, totalFrames)));
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("the note guard ends what its schedule does not hold, block by "
          "block",
          "[projection][live]") {
  // The guard of a real track, driven by hand with blocks of its own.
  namespace te = tracktion;
  StaleFixture fixture("projection-live-guard");
  const auto &projection =
      internal::TracktionAccess::projection(fixture.engine);
  auto &guard = *projection.tracks().at(fixture.track).guard;
  CHECK(guard.getName() == "LINA note guard");
  CHECK(guard.getPluginType() == internal::NoteGuardPlugin::xmlTypeName);
  CHECK(guard.getSelectableDescription() == "LINA note guard");
  CHECK(guard.takesMidiInput());
  // Playing a block gives the guard its sample rate, 48 kHz.
  fixture.engine.openManualDevice({.sampleRate = sampleRate, .blockSize = 512});
  fixture.engine.play();
  std::ignore = fixture.engine.process(512);
  fixture.engine.stop();

  const auto block = [&](te::MidiMessageArray *messages, std::int64_t frame,
                         bool playing) {
    const te::PluginRenderContext context(
        nullptr, juce::AudioChannelSet::stereo(), 0, 512, messages, 0.0,
        {te::TimePosition::fromSamples(frame, sampleRate),
         te::TimePosition::fromSamples(frame + 512, sampleRate)},
        playing, false, false, false);
    guard.applyToBuffer(context);
  };
  const auto noteOn = [](int pitch) {
    return juce::MidiMessage::noteOn(1, pitch, juce::uint8{100});
  };
  // What a block holds, as text: "off 60", "on 62", "other".
  const auto text = [](const te::MidiMessageArray &messages) {
    std::string all;
    for (const auto &message : messages) {
      if (message.isNoteOn()) {
        all += "on ";
      } else if (message.isNoteOff()) {
        all += "off ";
      } else {
        all += "other ";
      }
      all += std::to_string(message.getNoteNumber()) + ",";
    }
    return all;
  };

  // A block without a MIDI buffer is left alone.
  block(nullptr, 1'000, true);
  // Without a watch everything passes: two notes start.
  te::MidiMessageArray messages;
  messages.addMidiMessage(noteOn(60), 0.001, {});
  messages.addMidiMessage(noteOn(61), 0.002, {});
  block(&messages, 1'000, true);
  CHECK(text(messages) == "on 60,on 61,");
  CHECK(!guard.isWatching());

  // A watch against a schedule that holds pitch 61 until frame 5000 and
  // nothing else.
  internal::ClipEvents clip;
  clip.events = {
      {.frame = 0, .pitch = 61, .velocity = 100},
      {.frame = 5'000, .pitch = 61, .velocity = 0},
  };
  const std::array<const internal::ClipEvents *, 1> clips{&clip};
  guard.watch(std::make_shared<const internal::NoteSchedule>(clips));
  CHECK(guard.isWatching());
  // While the transport does not play, nothing is ended.
  messages.clear();
  block(&messages, 2'000, false);
  CHECK(text(messages).empty());
  // The next playing block ends pitch 60 at its start, ahead of what the
  // block holds, and leaves pitch 61 to its own end. Pitch 40 is the note
  // the fixture's clip started in the block the engine played.
  messages.clear();
  messages.addMidiMessage(juce::MidiMessage::controllerEvent(1, 7, 100), 0.001,
                          {});
  messages.addMidiMessage(noteOn(62), 0.002, {});
  block(&messages, 2'000, true);
  CHECK(text(messages) == "off 40,off 60,other 7,on 62,");
  CHECK(messages.begin()->getTimeStamp() == 0.0);
  CHECK(!messages.isAllNotesOff);
  // The rebuilt graph has not been handed over: the watch goes on. Pitch 62
  // was started since and is not in the schedule either; the one buffer
  // with room is used up, so the instrument is told to end everything.
  messages.clear();
  block(&messages, 2'512, true);
  CHECK(text(messages).empty());
  CHECK(messages.isAllNotesOff);
  CHECK(guard.isWatching());
  // Nothing is sounding now: the next block ends nothing.
  messages.clear();
  block(&messages, 3'024, true);
  CHECK(text(messages).empty());
  CHECK(!messages.isAllNotesOff);

  // A new watch brings a new buffer with room. A block that holds more
  // than the buffer has room for gets the flag instead.
  messages.addMidiMessage(noteOn(70), 0.0, {});
  block(&messages, 3'536, true);
  guard.watch(std::make_shared<const internal::NoteSchedule>(clips));
  messages.clear();
  for (int index = 0; index < internal::NoteGuardPlugin::bufferRoom; ++index) {
    messages.addMidiMessage(juce::MidiMessage::controllerEvent(1, 7, 1), 0.001,
                            {});
  }
  block(&messages, 4'048, true);
  CHECK(messages.size() == internal::NoteGuardPlugin::bufferRoom);
  CHECK(messages.isAllNotesOff);

  // Once the graph is handed over the watch ends after a few blocks.
  guard.graphHandedOver();
  for (int count = 0; count < internal::NoteGuardPlugin::settleBlocks;
       ++count) {
    CHECK(guard.isWatching());
    messages.clear();
    block(&messages, 4'560 + (512 * count), true);
  }
  CHECK(!guard.isWatching());
  // Handing over without a watch changes nothing.
  guard.graphHandedOver();
  CHECK(!guard.isWatching());
  // A watch that is forgotten is over at once.
  guard.watch(std::make_shared<const internal::NoteSchedule>(clips));
  guard.forget();
  CHECK(!guard.isWatching());
  CHECK(fixture.assertions.count() == 0);
}
