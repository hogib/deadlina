// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the sequencer sends in a block (note_player.hpp), block by block,
// without an engine: chasing, ending what it started and nothing else, and
// edits that start nothing under the playhead.

#include <catch2/catch_test_macros.hpp>

#include "lina/engine/internal/note_player.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace lina::engine::internal;
using lina::model::ClipId;

namespace {

namespace te = tracktion;

constexpr double rate = 48'000.0;

NoteEvent on(std::int64_t frame, std::uint8_t pitch, std::uint8_t velocity) {
  return {.frame = frame, .pitch = pitch, .velocity = velocity};
}

NoteEvent off(std::int64_t frame, std::uint8_t pitch) {
  return {.frame = frame, .pitch = pitch, .velocity = 0};
}

SequencedClip clipOf(std::uint64_t id, std::int64_t start, std::int64_t end,
                     std::vector<NoteEvent> events) {
  return {
      .clip = ClipId{id},
      .playback = std::make_shared<const ClipPlayback>(ClipEvents{
          .events = std::move(events),
          .startFrame = start,
          .endFrame = end,
          .truncated = false,
      }),
  };
}

/// Two clips that meet at 1000. The first: pitch 60 from 100 to 1000 (cut
/// at the clip's end), pitch 62 from 200 to 300. The second: pitch 60 again
/// from 1000 to 1500.
TrackSequence twoClips(std::uint64_t number) {
  return {
      number,
      rate,
      {
          clipOf(1, 0, 1'000,
                 {
                     on(100, 60, 100),
                     on(200, 62, 90),
                     off(300, 62),
                     off(1'000, 60),
                 }),
          clipOf(2, 1'000, 2'000,
                 {
                     on(1'000, 60, 50),
                     off(1'500, 60),
                 }),
      },
  };
}

SequencerBlock playingBlock(std::int64_t start, std::int64_t length,
                            bool starting = false) {
  return {
      .start = start,
      .length = length,
      .playing = true,
      .starting = starting,
  };
}

/// The messages of a block as text, one per message: "on 60 v100 @0",
/// "off 60 @12", with the frame in the block the message lies in.
std::string text(const te::MidiMessageArray &messages) {
  std::string all;
  for (const auto &message : messages) {
    const auto frame =
        static_cast<long>(std::floor(message.getTimeStamp() * rate));
    if (message.isNoteOn()) {
      all += "on " + std::to_string(message.getNoteNumber()) + " v" +
             std::to_string(message.getVelocity()) + " @" +
             std::to_string(frame) + ", ";
    } else {
      all += "off " + std::to_string(message.getNoteNumber()) + " @" +
             std::to_string(frame) + ", ";
    }
  }
  return all;
}

/// Plays one block and returns what it sent; checks that bound() was kept.
/// The buffer has room for bound() messages, as play() requires: under
/// RealtimeSanitizer a buffer that grew would fail the test.
std::string play(NotePlayer &player, const TrackSequence *sequence,
                 const SequencerBlock &block) {
  te::MidiMessageArray out;
  const std::size_t bound = player.bound(sequence, block);
  out.reserve(static_cast<int>(bound));
  player.play(sequence, block, out, rate);
  CHECK(static_cast<std::size_t>(out.size()) <= bound);
  return text(out);
}

} // namespace

TEST_CASE("playback that begins inside a note starts it on the first frame",
          "[note-player]") {
  const TrackSequence sequence = twoClips(1);
  NotePlayer player;
  // From 250: pitch 60 and pitch 62 are sounding, both chased on frame 0.
  CHECK(play(player, &sequence, playingBlock(250, 100, true)) ==
        "on 60 v100 @0, on 62 v90 @0, off 62 @50, ");
  CHECK(player.isSounding(60));
  CHECK(!player.isSounding(62));
  CHECK(player.soundingCount() == 1);
  // Steady blocks send only their events: none until 1000, where the first
  // clip's note ends before the second clip's begins.
  CHECK(play(player, &sequence, playingBlock(350, 650)).empty());
  CHECK(play(player, &sequence, playingBlock(1'000, 10)) ==
        "off 60 @0, on 60 v50 @0, ");
  CHECK(player.soundingCount() == 1);
}

TEST_CASE("a note that starts on the first frame played is started once, "
          "and one that ends there not at all",
          "[note-player]") {
  const TrackSequence sequence = twoClips(1);
  {
    NotePlayer player;
    CHECK(play(player, &sequence, playingBlock(100, 10, true)) ==
          "on 60 v100 @0, ");
  }
  {
    // At 1000 the first clip's note ends and the second clip's begins.
    NotePlayer player;
    CHECK(play(player, &sequence, playingBlock(1'000, 10, true)) ==
          "on 60 v50 @0, ");
  }
  {
    NotePlayer player;
    CHECK(play(player, &sequence, playingBlock(300, 10, true)) ==
          "on 60 v100 @0, ");
  }
}

TEST_CASE("events are sent on their frames, an off before an on of the "
          "same frame",
          "[note-player]") {
  const TrackSequence sequence = twoClips(1);
  NotePlayer player;
  CHECK(play(player, &sequence, playingBlock(0, 512, true)) ==
        "on 60 v100 @100, on 62 v90 @200, off 62 @300, ");
  CHECK(play(player, &sequence, playingBlock(512, 512)) ==
        "off 60 @488, on 60 v50 @488, ");
  // The note-off lies an eighth of a frame into its frame, the note-on
  // three eighths.
  te::MidiMessageArray out;
  NotePlayer other;
  out.reserve(
      static_cast<int>(other.bound(&sequence, playingBlock(990, 20, true))));
  other.play(&sequence, playingBlock(990, 20, true), out, rate);
  std::vector<double> times;
  for (const auto &message : out) {
    times.push_back(message.getTimeStamp());
  }
  CHECK(times == std::vector{
                     noteOnOffsetFrames / rate,
                     (10.0 + noteOffOffsetFrames) / rate,
                     (10.0 + noteOnOffsetFrames) / rate,
                 });
}

TEST_CASE("stopping ends what was started, and the next block does not "
          "chase",
          "[note-player]") {
  const TrackSequence sequence = twoClips(1);
  NotePlayer player;
  CHECK(play(player, &sequence, playingBlock(150, 100, true)) ==
        "on 60 v100 @0, on 62 v90 @50, ");
  const SequencerBlock stopped{.start = 250, .length = 64, .playing = false};
  CHECK(player.bound(&sequence, stopped) == 2);
  CHECK(play(player, &sequence, stopped) == "off 60 @0, off 62 @0, ");
  CHECK(player.soundingCount() == 0);
  CHECK(player.bound(&sequence, stopped) == 0);
  CHECK(play(player, &sequence, stopped).empty());
  // Playing again without being told that playback begins: nothing is
  // chased; the note of 1000 is played from its start.
  CHECK(play(player, &sequence, playingBlock(500, 600)) == "on 60 v50 @500, ");
}

TEST_CASE("a new sequence ends what it no longer holds and starts nothing "
          "under the playhead",
          "[note-player]") {
  const TrackSequence before = twoClips(1);
  NotePlayer player;
  CHECK(play(player, &before, playingBlock(150, 100, true)) ==
        "on 60 v100 @0, on 62 v90 @50, ");
  // An edit: pitch 62 now ends at 260 instead of 300, pitch 60 is gone, and
  // pitch 67 is drawn from 240 to 400, under the playhead.
  const TrackSequence after(2, rate,
                            {
                                clipOf(1, 0, 1'000,
                                       {
                                           on(200, 62, 90),
                                           on(240, 67, 120),
                                           off(260, 62),
                                           off(400, 67),
                                       }),
                            });
  CHECK(play(player, &after, playingBlock(250, 100)) ==
        "off 60 @0, off 62 @10, ");
  CHECK(player.soundingCount() == 0);
  // The drawn note is not started; its end is not sent either.
  CHECK(play(player, &after, playingBlock(350, 100)).empty());
}

TEST_CASE("a note that a new sequence still holds sounds on to its new end",
          "[note-player]") {
  const TrackSequence before = twoClips(1);
  NotePlayer player;
  CHECK(play(player, &before, playingBlock(150, 100, true)) ==
        "on 60 v100 @0, on 62 v90 @50, ");
  // Pitch 62 lengthened to 700; nothing else changed.
  const TrackSequence after(2, rate,
                            {
                                clipOf(1, 0, 1'000,
                                       {
                                           on(100, 60, 100),
                                           on(200, 62, 90),
                                           off(700, 62),
                                           off(1'000, 60),
                                       }),
                            });
  CHECK(play(player, &after, playingBlock(250, 100)).empty());
  CHECK(player.soundingCount() == 2);
  CHECK(play(player, &after, playingBlock(350, 400)) == "off 62 @350, ");
  // Playing on under the same sequence checks nothing.
  CHECK(player.bound(&after, playingBlock(750, 100)) == 0);
}

TEST_CASE("a jump of the playhead without a new sequence ends what the "
          "sequence does not hold at the new place",
          "[note-player]") {
  const TrackSequence sequence = twoClips(1);
  NotePlayer player;
  CHECK(play(player, &sequence, playingBlock(150, 100, true)) ==
        "on 60 v100 @0, on 62 v90 @50, ");
  // From 250 to 600 (a change of tempo moves the playhead like this):
  // pitch 60 is held there, pitch 62 has ended, and nothing starts.
  CHECK(play(player, &sequence, playingBlock(600, 100)) == "off 62 @0, ");
  CHECK(player.isSounding(60));
}

TEST_CASE("a note-off is sent only for a pitch the player started",
          "[note-player]") {
  const TrackSequence sequence = twoClips(1);
  NotePlayer player;
  // Begun after both notes of the first clip started, without chasing (a
  // block that just follows on): their ends are not sent.
  CHECK(play(player, &sequence, playingBlock(250, 1'251)) ==
        "on 60 v50 @750, off 60 @1250, ");
  CHECK(player.soundingCount() == 0);
  // When the instrument has been told to end everything, the player forgets
  // what it started and sends no end for it.
  CHECK(play(player, &sequence, playingBlock(0, 150, true)) ==
        "on 60 v100 @100, ");
  player.forgetAll();
  CHECK(play(player, &sequence, playingBlock(150, 900)) ==
        "on 62 v90 @50, off 62 @150, on 60 v50 @850, ");
}

TEST_CASE("without a sequence nothing is played and everything started is "
          "ended",
          "[note-player]") {
  const TrackSequence sequence = twoClips(3);
  NotePlayer player;
  CHECK(play(player, &sequence, playingBlock(150, 100, true)) ==
        "on 60 v100 @0, on 62 v90 @50, ");
  CHECK(player.bound(nullptr, playingBlock(250, 100)) == 2);
  CHECK(play(player, nullptr, playingBlock(250, 100)) ==
        "off 60 @0, off 62 @0, ");
  CHECK(play(player, nullptr, playingBlock(350, 100, true)).empty());
  CHECK(player.bound(nullptr, playingBlock(450, 100, true)) == 0);
}

TEST_CASE("a skipped block forgets what was started and moves on",
          "[note-player]") {
  const TrackSequence sequence = twoClips(1);
  NotePlayer player;
  CHECK(play(player, &sequence, playingBlock(150, 100, true)) ==
        "on 60 v100 @0, on 62 v90 @50, ");
  player.skip(&sequence, playingBlock(250, 100));
  CHECK(player.soundingCount() == 0);
  // The next block follows on: nothing is checked or chased.
  CHECK(player.bound(&sequence, playingBlock(350, 100)) == 0);
  player.skip(&sequence, {.start = 450, .length = 10, .playing = false});
  // After a stopped block the next one does not follow on.
  CHECK(player.bound(&sequence, playingBlock(460, 10)) == 0);
  CHECK(play(player, &sequence, playingBlock(460, 10)).empty());
  player.reset();
  CHECK(player.soundingCount() == 0);
}

TEST_CASE("the bound covers the worst a block can need", "[note-player]") {
  const TrackSequence sequence = twoClips(1);
  NotePlayer player;
  // Starting: a note-on for every pitch at most, and the events.
  CHECK(player.bound(&sequence, playingBlock(0, 1'001, true)) ==
        midiPitchCount + 5);
  CHECK(play(player, &sequence, playingBlock(0, 1'001, true)) ==
        "on 60 v100 @100, on 62 v90 @200, off 62 @300, off 60 @1000, on 60 "
        "v50 @1000, ");
  // Following on: the events only.
  CHECK(player.bound(&sequence, playingBlock(1'001, 1'000)) == 1);
  // Not following on: an end for every note started, and the events.
  CHECK(player.bound(&sequence, playingBlock(1'200, 400)) == 2);
}
