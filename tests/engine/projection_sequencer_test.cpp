// SPDX-License-Identifier: GPL-3.0-or-later
//
// LINA's own sequencer (sequencer.hpp) as the projection drives it: what the
// instrument of a MIDI track is asked to play while the document is edited
// under the playhead, at every block size, and what it is never asked to do.
// Expected frames are worked out here from the definitions
// (docs/design/04-transport-and-musical-time.md,
// docs/design/08-midi-clips-and-note-editing.md, "Playback"), not with the
// engine's code.

#include <catch2/catch_test_macros.hpp>

#include "lina/engine/internal/tracktion_access.hpp"
#include "projection_fixture.hpp"
#include "reference_signal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

using namespace lina::engine;
using namespace lina::engine::testing;
using namespace lina::model;
using lina::model::testing::beat;
using lina::model::testing::beats;
using lina::model::testing::makeAudioClip;
using lina::model::testing::makeMedia;
using lina::model::testing::makeMidiClip;
using lina::model::testing::makeNote;

namespace {

namespace te = tracktion;

constexpr double sampleRate = 48'000.0;
/// 120 beats per minute at 48 kHz.
constexpr std::int64_t quarter = 24'000;

__extension__ using Wide = __int128;

/// The frame nearest to a tick at 120 beats per minute and 48 kHz, the later
/// one on a tie: a tick lasts 25 / (63 * 12000) seconds, 100/63 of a frame.
std::int64_t frameOf(std::int64_t ticks) {
  return static_cast<std::int64_t>(((Wide{ticks} * 200) + 63) / 126);
}

/// The frame nearest to a tick at 48 kHz and a tempo of `hundredths` of a
/// beat per minute, the later one on a tie: a tick lasts 25 / (63 *
/// hundredths) seconds.
std::int64_t frameAt(std::int64_t ticks, std::int32_t hundredths) {
  const Wide unit = Wide{63} * hundredths;
  return static_cast<std::int64_t>(((Wide{ticks} * 25 * 48'000 * 2) + unit) /
                                   (2 * unit));
}

/// The first tick whose frame is `frame` or later.
std::int64_t firstTickFrom(std::int64_t frame) {
  std::int64_t tick = (frame * 63) / 100;
  while (frameOf(tick) < frame) {
    ++tick;
  }
  while (frameOf(tick - 1) >= frame) {
    --tick;
  }
  return tick;
}

void append(AudioBuffer &audio, const AudioBuffer &more) {
  for (std::size_t channel = 0; channel < audio.channels.size(); ++channel) {
    auto &samples = audio.channels.at(channel);
    const auto &added = more.channels.at(channel);
    samples.insert(samples.end(), added.begin(), added.end());
  }
}

NoteId noteOfPitch(const Project &project, ClipId clip, int pitch) {
  const Clip *found = findClip(project, clip).clip;
  REQUIRE(found != nullptr);
  const auto &notes = std::get<MidiContent>(found->content).notes;
  const auto note = std::ranges::find(notes, pitch, &lina::model::Note::pitch);
  REQUIRE(note != notes.end());
  return note->id;
}

/// Undoes the last edit, in the document and in the engine.
void undo(ProjectionFixture &fixture) {
  const auto undone = fixture.document.undo();
  REQUIRE(undone.has_value());
  fixture.engine.applyChanges(fixture.project(), *undone);
}

/// Plays from the start in blocks of `blockSize`, makes `edits` after `at`
/// frames, plays on to `total` and undoes the edits.
AudioBuffer playAcross(ProjectionFixture &fixture, int blockSize,
                       std::int64_t at, std::int64_t total,
                       std::span<const Command> edits) {
  fixture.engine.openManualDevice(
      {.sampleRate = sampleRate, .blockSize = blockSize});
  fixture.engine.play();
  AudioBuffer audio = fixture.engine.process(at);
  for (const Command &edit : edits) {
    fixture.edit(edit);
  }
  append(audio, fixture.engine.process(total - at));
  for (std::size_t count = 0; count < edits.size(); ++count) {
    undo(fixture);
  }
  return audio;
}

} // namespace

TEST_CASE("an edit takes effect on the frame playback has reached, on and "
          "around block boundaries, whatever the block size",
          "[projection][sequencer]") {
  ProjectionFixture fixture("projection-sequencer-boundaries");
  const TrackId track = fixture.addToneTrack();
  // A quiet low note under everything, and a note on frame 47800 (tick
  // 30114) for two beats.
  constexpr std::int64_t heldOn = 47'800;
  REQUIRE(frameOf(30'114) == heldOn);
  const ClipId clip = fixture.addClip(
      track, makeMidiClip(beat(0), beats(16),
                          {
                              makeNote(40, beat(0), beats(16), 20),
                              makeNote(69, TickPosition{30'114}, beats(2), 127),
                          }));
  const NoteId held = noteOfPitch(fixture.project(), clip, 69);

  for (const int blockSize : {512, 441, 64, 7}) {
    const std::int64_t boundary =
        ((48'000 / blockSize) + 1) * std::int64_t{blockSize};
    for (const std::int64_t at : {boundary - 1, boundary, boundary + 1}) {
      CAPTURE(blockSize, at);
      const std::int64_t total = at + 3'000;
      const SoundingNote low{
          .pitch = 40,
          .velocity = 20,
          .onFrame = 0,
          .offFrame = total,
      };
      {
        // Removed: the held note ends on the frame the edit came at.
        const std::array<Command, 1> edits{
            RemoveNotes{.clip = clip, .notes = {held}},
        };
        const auto audio = playAcross(fixture, blockSize, at, total, edits);
        const std::array notes{
            low,
            SoundingNote{
                .pitch = 69,
                .velocity = 127,
                .onFrame = heldOn,
                .offFrame = at,
            },
        };
        // Tolerance: formulaTolerance, 1e-6 of full scale.
        CHECK(maxDifference(audio.channels.at(0),
                            referenceSignal(total, sampleRate, notes)) <=
              formulaTolerance);
      }
      {
        // Drawn from the first tick on or after the edit's frame: starts on
        // its frame. Drawn from the tick before: it covers the playhead and
        // is not started (rule 9).
        const std::int64_t tick = firstTickFrom(at);
        const std::array<Command, 1> edits{
            AddNotes{
                .clip = clip,
                .notes =
                    {
                        makeNote(76, TickPosition{tick}, beats(1), 90),
                        makeNote(79, TickPosition{tick - 1}, beats(1), 90),
                    },
            },
        };
        const auto audio = playAcross(fixture, blockSize, at, total, edits);
        const std::array notes{
            low,
            SoundingNote{
                .pitch = 69,
                .velocity = 127,
                .onFrame = heldOn,
                .offFrame = total,
            },
            SoundingNote{
                .pitch = 76,
                .velocity = 90,
                .onFrame = frameOf(tick),
                .offFrame = total,
            },
        };
        CHECK(maxDifference(audio.channels.at(0),
                            referenceSignal(total, sampleRate, notes)) <=
              formulaTolerance);
      }
    }
  }
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("an edit while playing starts no note under the playhead; the "
          "note sounds from its next start",
          "[projection][sequencer]") {
  // Docs/design/08-midi-clips-and-note-editing.md, "Playback", rule 9.
  ProjectionFixture fixture("projection-sequencer-rule-nine");
  const TrackId track = fixture.addToneTrack();
  const TrackId other = fixture.addToneTrack();
  // On the first track a clip that loops one beat, with a short note on
  // each pass.
  Clip playing = makeMidiClip(
      beat(0), beats(8), {makeNote(48, beat(0), {ticksPerQuarter / 4}, 60)});
  playing.loop = {.enabled = true, .start = beat(0), .length = beats(1)};
  const ClipId drawnInto = fixture.addClip(track, playing);
  // On the other, the same kind of clip far ahead, to be moved under the
  // playhead.
  Clip far = makeMidiClip(beat(40), beats(4),
                          {makeNote(64, beat(0), {ticksPerQuarter / 2}, 100)});
  far.loop = {.enabled = true, .start = beat(0), .length = beats(1)};
  const ClipId moved = fixture.addClip(other, far);

  // A quarter and 1234 frames into beat 2: the passes that began on beat 2
  // cover the playhead.
  constexpr std::int64_t at = (2 * quarter) + 1'234;
  constexpr std::int64_t total = 7 * quarter;
  const std::array<Command, 2> edits{
      MoveClip{.clip = moved, .container = other, .position = beat(2)},
      AddNotes{
          .clip = drawnInto,
          .notes = {makeNote(67, beat(0), {ticksPerQuarter / 2}, 90)},
      },
  };
  std::vector<SoundingNote> notes;
  for (std::int64_t pass = 0; pass < 7; ++pass) {
    notes.push_back({
        .pitch = 48,
        .velocity = 60,
        .onFrame = pass * quarter,
        .offFrame = (pass * quarter) + (quarter / 4),
    });
    // From the next pass on, the moved clip's note and the drawn one.
    if (pass >= 3) {
      notes.push_back({
          .pitch = 67,
          .velocity = 90,
          .onFrame = pass * quarter,
          .offFrame = (pass * quarter) + (quarter / 2),
      });
    }
    if (pass >= 3 && pass < 6) {
      notes.push_back({
          .pitch = 64,
          .velocity = 100,
          .onFrame = pass * quarter,
          .offFrame = (pass * quarter) + (quarter / 2),
      });
    }
  }
  const auto expected = referenceSignal(total, sampleRate, notes);
  for (const int blockSize : {512, 441, 64, 7}) {
    CAPTURE(blockSize);
    const auto audio = playAcross(fixture, blockSize, at, total, edits);
    // Tolerance: formulaTolerance, 1e-6 of full scale.
    CHECK(maxDifference(audio.channels.at(0), expected) <= formulaTolerance);
  }
  // Played from the start, the edited document has both notes on beat 2.
  fixture.edit(edits.at(0));
  fixture.edit(edits.at(1));
  const auto fresh = fixture.playFromStart(sampleRate, total);
  CHECK(fresh.channels.at(0).at(2 * quarter) != expected.at(2 * quarter));
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("after a change of tempo while playing, MIDI and audio go on from "
          "the same block",
          "[projection][sequencer]") {
  ProjectionFixture fixture("projection-sequencer-tempo");
  // A file at full level from its first frame.
  AudioBuffer file;
  file.sampleRate = sampleRate;
  file.channels.assign(2, std::vector<float>(48'000, 0.5F));
  writeWav(fixture.scratch / "media" / "steady.wav", file);
  const auto media =
      fixture.add<ObjectKind::Media>(AddMedia{makeMedia("media/steady.wav")});
  // MIDI on the left, audio on the right.
  const TrackId midi = fixture.addToneTrack();
  fixture.set(midi, PanValue{-1.0});
  const TrackId audio = fixture.addTrack(TrackKind::Audio);
  fixture.set(audio, PanValue{1.0});
  // Both start 63 ticks after beat 2: frame 48100 at 120 beats per minute,
  // 38480 at 150.
  constexpr TickPosition start{(2 * ticksPerQuarter) + 63};
  fixture.addClip(midi, makeMidiClip(start, beats(1),
                                     {makeNote(69, beat(0), beats(1), 127)}));
  fixture.addClip(audio, makeAudioClip(media, start, beats(1)));

  for (const int blockSize : {512, 441, 64, 7}) {
    CAPTURE(blockSize);
    fixture.engine.openManualDevice(
        {.sampleRate = sampleRate, .blockSize = blockSize});
    fixture.engine.play();
    AudioBuffer heard = fixture.engine.process(2 * quarter);
    // The change keeps the musical position: beat 2, frame 38400 at 150
    // beats per minute. Both clips begin 80 frames on, in the same block of
    // any size above 80, MIDI as early as audio for the smaller ones.
    fixture.setTempo(15'000);
    append(heard, fixture.engine.process(4'800));
    CHECK(firstSoundingFrame(heard.channels.at(0)) == (2 * quarter) + 80);
    CHECK(firstSoundingFrame(heard.channels.at(1)) == (2 * quarter) + 80);
    undo(fixture);
  }
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a render from the middle of a note is what playback from there "
          "gives, bit for bit",
          "[projection][sequencer]") {
  ProjectionFixture fixture("projection-sequencer-render");
  const TrackId track = fixture.addToneTrack();
  fixture.addDevice(track, delayGainDevice(777, 0.5F));
  fixture.addClip(track, makeMidiClip(beat(0), beats(8),
                                      {
                                          makeNote(57, beat(0), beats(5), 90),
                                          makeNote(61, beat(1), beats(1), 70),
                                          makeNote(64, beat(4), beats(2), 80),
                                      }));
  for (const int blockSize : {512, 441, 64, 7}) {
    CAPTURE(blockSize);
    const RenderRequest request{
        .format = {.sampleRate = sampleRate, .blockSize = blockSize},
        .startFrame = 30'001,
        .frameCount = 4 * quarter,
    };
    const auto rendered = fixture.engine.renderOffline(request);
    fixture.engine.openManualDevice(request.format);
    fixture.engine.play(request.startFrame);
    const auto played = fixture.engine.process(request.frameCount);
    // Tolerance: none.
    CHECK(identical(rendered, played));
    // The notes held across the start sound from its first frame, which
    // the delay brings out 777 frames later.
    CHECK(firstSoundingFrame(rendered.channels.at(0)) == 777);
  }
  CHECK(fixture.assertions.count() == 0);
}

namespace {

/// A MIDI track with the test instrument and the MIDI probe behind it. The
/// instrument hands on the MIDI it is given, so the probe sees what the
/// instrument was asked to play.
struct ProbedTrack {
  TrackId track;
  DeviceId probe;
};

ProbedTrack addProbedTrack(ProjectionFixture &fixture) {
  const TrackId track = fixture.addToneTrack();
  return {.track = track, .probe = fixture.addDevice(track, midiProbeDevice())};
}

MidiProbe &probeOf(Engine &engine, DeviceId device) {
  return dynamic_cast<MidiProbe &>(
      *internal::TracktionAccess::projection(engine)
           .devices()
           .at(device)
           .plugin);
}

} // namespace

TEST_CASE("stopping ends every note the sequencer started, and a new rate "
          "starts afresh",
          "[projection][sequencer]") {
  ProjectionFixture fixture("projection-sequencer-stop");
  const ProbedTrack probed = addProbedTrack(fixture);
  fixture.addClip(probed.track,
                  makeMidiClip(beat(0), beats(8),
                               {
                                   makeNote(60, beat(0), beats(6), 100),
                                   makeNote(64, beat(1), beats(4), 80),
                               }));
  MidiProbe &probe = probeOf(fixture.engine, probed.probe);
  // The notes the instrument was asked to start or end; the engine also
  // tells every plugin to end all notes when playback starts and stops.
  const auto notesOf = [](const std::vector<MidiProbe::Note> &records) {
    std::vector<MidiProbe::Note> notes;
    std::ranges::copy_if(records, std::back_inserter(notes),
                         [](const auto &note) { return !note.allNotesOff; });
    return notes;
  };
  fixture.engine.openManualDevice({.sampleRate = sampleRate, .blockSize = 64});
  fixture.engine.play(quarter + 100);
  std::ignore = fixture.engine.process(1'000);
  // Both notes chased on the first frame.
  const auto started = notesOf(probe.take());
  REQUIRE(started.size() == 2);
  for (const auto &note : started) {
    CHECK(note.frame == quarter + 100);
    CHECK(note.velocity > 0);
  }
  // Stopping ends both, note by note or by the engine telling every plugin
  // to end all notes, as it does when the transport stops; then the
  // sequencer sends no end of its own.
  fixture.engine.stop();
  const auto stopping = fixture.engine.process(64);
  const auto records = probe.take();
  const auto ended = notesOf(records);
  CHECK((std::ranges::any_of(records,
                             [](const auto &note) { return note.allNotesOff; })
             ? ended.empty()
             : ended.size() == 2));
  for (const auto &note : ended) {
    CHECK(note.velocity == 0);
    CHECK(!note.playing);
  }
  CHECK(firstSoundingFrame(stopping.channels.at(0)) == -1);
  std::ignore = fixture.engine.process(640);
  CHECK(notesOf(probe.take()).empty());

  // Played on at another rate: nothing of the first run is ended, both
  // notes are chased on the first frame at the new rate.
  constexpr double newRate = 44'100.0;
  fixture.engine.openManualDevice({.sampleRate = newRate, .blockSize = 441});
  const std::int64_t from = 33'000;
  fixture.engine.play(from);
  const auto audio = fixture.engine.process(10'000);
  const auto again = notesOf(probe.take());
  REQUIRE(again.size() == 2);
  for (const auto &note : again) {
    CHECK(note.frame == from);
    CHECK(note.velocity > 0);
  }
  const std::array notes{
      SoundingNote{
          .pitch = 60,
          .velocity = 100,
          .onFrame = 0,
          .offFrame = 10'000,
      },
      SoundingNote{
          .pitch = 64,
          .velocity = 80,
          .onFrame = 0,
          .offFrame = 10'000,
      },
  };
  // Tolerance: formulaTolerance, 1e-6 of full scale, for each note.
  CHECK(maxDifference(audio.channels.at(0),
                      referenceSignal(10'000, newRate, notes)) <=
        2.0F * formulaTolerance);
  CHECK(!probe.overflowed());
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("the sequencer plugin, driven block by block",
          "[projection][sequencer]") {
  ProjectionFixture fixture("projection-sequencer-by-hand");
  const TrackId track = fixture.addToneTrack();
  fixture.addClip(track, makeMidiClip(beat(0), beats(4),
                                      {
                                          makeNote(60, beat(0), beats(2), 100),
                                          makeNote(62, beat(2), beats(1), 90),
                                      }));
  const auto &projection =
      internal::TracktionAccess::projection(fixture.engine);
  auto &sequencer = *projection.tracks().at(track).sequencer;
  CHECK(sequencer.getName() == "LINA sequencer");
  CHECK(sequencer.getPluginType() == internal::SequencerPlugin::xmlTypeName);
  CHECK(sequencer.getSelectableDescription() == "LINA sequencer");
  CHECK(sequencer.takesMidiInput());
  // Out of any graph, only the newest publication is kept and no buffer is
  // made.
  CHECK(sequencer.sequencesKept() == 1);
  CHECK(sequencer.buffersReady() == 0);
  CHECK(sequencer.room() >= internal::SequencerPlugin::leastRoom);
  CHECK(sequencer.room() % internal::SequencerPlugin::roomStep == 0);

  // Playing puts the plugin in a graph at 48 kHz.
  fixture.engine.openManualDevice({.sampleRate = sampleRate, .blockSize = 512});
  fixture.engine.play();
  std::ignore = fixture.engine.process(512);
  fixture.engine.stop();
  std::ignore = fixture.engine.process(512);
  CHECK(sequencer.buffersReady() > 0);

  const auto block = [&](te::MidiMessageArray *messages, std::int64_t frame,
                         bool playing = true) {
    const te::PluginRenderContext context(
        nullptr, juce::AudioChannelSet::stereo(), 0, 512, messages, 0.0,
        {te::TimePosition::fromSamples(frame, sampleRate),
         te::TimePosition::fromSamples(frame + 512, sampleRate)},
        playing, false, false, false);
    sequencer.applyToBuffer(context);
  };
  // What a block holds, as text: "on 62", "off 62", "other 7".
  const auto text = [](const te::MidiMessageArray &messages) {
    std::string all;
    for (const auto &message : messages) {
      if (message.isController()) {
        all += "other " + std::to_string(message.getControllerNumber());
      } else {
        all += (message.isNoteOn() ? "on " : "off ") +
               std::to_string(message.getNoteNumber());
      }
      all += ',';
    }
    return all;
  };
  const auto republish = [&](double rate) {
    std::vector<internal::SequencedClip> clips = sequencer.published()->clips();
    sequencer.publish(std::move(clips), rate);
  };

  // A block without a MIDI buffer is left alone.
  block(nullptr, 1'000);

  // Publications do not pile up while the plugin is in a graph and no block
  // runs: only the newest is kept, and with it one set of buffers.
  for (int count = 0; count < 200; ++count) {
    republish(sampleRate);
    CHECK(sequencer.sequencesKept() == 1);
  }
  te::MidiMessageArray messages;
  block(&messages, 2'000);
  republish(sampleRate);
  CHECK(sequencer.sequencesKept() == 1);
  // That block followed nothing: the note under the playhead was not
  // started.
  CHECK(text(messages).empty());

  // A note that starts in a block is played; a sequence made for another
  // rate ends what was started and plays nothing.
  messages.clear();
  block(&messages, 48'000 - 100);
  CHECK(text(messages) == "on 62,");
  republish(44'100.0);
  messages.clear();
  block(&messages, 48'412);
  CHECK(text(messages) == "off 62,");
  messages.clear();
  block(&messages, 48'924);
  CHECK(text(messages).empty());

  // What came in passes on, in order of time with what the sequencer adds.
  republish(sampleRate);
  sequencer.requestStart();
  messages.clear();
  messages.addMidiMessage(juce::MidiMessage::controllerEvent(1, 7, 1),
                          100.0 / sampleRate, te::MPESourceID{});
  block(&messages, 1'000);
  CHECK(text(messages) == "on 60,other 7,");

  // More coming in than a buffer has room for: the block is not sent, the
  // instrument is told to end every note, and the sequencer forgets them.
  republish(sampleRate);
  messages.clear();
  for (std::size_t index = 0; index < sequencer.room(); ++index) {
    messages.addMidiMessage(juce::MidiMessage::controllerEvent(1, 7, 1), 0.0,
                            te::MPESourceID{});
  }
  const int incoming = messages.size();
  block(&messages, 1'512);
  CHECK(sequencer.blocksDropped() == 1);
  CHECK(messages.size() == incoming);
  CHECK(messages.isAllNotesOff);
  // So the note's end is not sent, and nothing is dropped any more.
  messages.clear();
  block(&messages, 47'900);
  CHECK(text(messages) == "on 62,");
  CHECK(sequencer.blocksDropped() == 1);
  // A block that does not play ends what was started.
  messages.clear();
  block(&messages, 48'412, false);
  CHECK(text(messages) == "off 62,");
  messages.clear();
  block(&messages, 48'412, false);
  CHECK(text(messages).empty());

  // A sequence denser than the buffers' room comes with buffers of more
  // room: 250 notes in 500 frames all go out in one block.
  internal::ClipEvents dense{
      .events = {},
      .startFrame = 96'000,
      .endFrame = 97'000,
      .truncated = false,
  };
  for (std::int64_t note = 0; note < 250; ++note) {
    const auto pitch = static_cast<std::uint8_t>(note % 100);
    dense.events.push_back(
        {.frame = 96'000 + (2 * note), .pitch = pitch, .velocity = 100});
    dense.events.push_back(
        {.frame = 96'001 + (2 * note), .pitch = pitch, .velocity = 0});
  }
  const std::size_t before = sequencer.room();
  std::vector<internal::SequencedClip> denseClips{
      {
          .clip = ClipId{999},
          .playback = std::make_shared<const internal::ClipPlayback>(dense),
      },
  };
  sequencer.publish(std::move(denseClips), sampleRate);
  CHECK(sequencer.room() > before);
  messages.clear();
  block(&messages, 96'000 - 1);
  CHECK(messages.size() == 500);
  CHECK(sequencer.blocksDropped() == 1);

  // A sequencer that was never given anything does nothing.
  auto &bare = addTrack(fixture.engine);
  const auto plugin =
      bare.pluginList.insertPlugin(internal::SequencerPlugin::create(), 0);
  auto *unused = dynamic_cast<internal::SequencerPlugin *>(plugin.get());
  REQUIRE(unused != nullptr);
  CHECK(unused->published() == nullptr);
  CHECK(unused->room() == 0);
  CHECK(unused->buffersReady() == 0);
  CHECK(unused->buffersAccountedFor());
  messages.clear();
  const te::PluginRenderContext context(
      nullptr, juce::AudioChannelSet::stereo(), 0, 512, &messages, 0.0,
      {te::TimePosition::fromSamples(0, sampleRate),
       te::TimePosition::fromSamples(512, sampleRate)},
      true, false, false, false);
  unused->applyToBuffer(context);
  CHECK(messages.isEmpty());
  // Taken into graphs and out again, it stays without buffers.
  fixture.engine.syncGraph();
  fixture.engine.syncGraph();
  CHECK(unused->graphsPrepared() == 2);
  CHECK(unused->buffersReady() == 0);
  // Prepared for another block size while a graph holds it: still nothing
  // to publish again.
  unused->baseClassInitialise({
      .startTime = {},
      .sampleRate = sampleRate,
      .blockSizeSamples = 2'048,
  });
  CHECK(unused->graphsPrepared() == 3);
  CHECK(unused->published() == nullptr);
  unused->baseClassDeinitialise();

  // A block that needs more room than there is, with nothing sounding: not
  // sent, and the instrument is not told to end anything.
  sequencer.requestStart();
  messages.clear();
  for (std::size_t index = 0; index < sequencer.room(); ++index) {
    messages.addMidiMessage(juce::MidiMessage::controllerEvent(1, 7, 1), 0.0,
                            te::MPESourceID{});
  }
  block(&messages, 0);
  CHECK(sequencer.blocksDropped() == 2);
  CHECK(!messages.isAllNotesOff);
  fixture.engine.closeManualDevice();
  CHECK(unused->buffersReady() == 0);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a device change while an older graph still holds the sequencer "
          "leaves what the audio thread uses alone",
          "[projection][sequencer]") {
  // Tracktion Engine v3.2.0 prepares a plugin with initialise() rather than
  // initialiseWithoutStopping() when the rate or block size differs from
  // the last call, even while another graph holds it
  // (Plugin::baseClassInitialise). The audio thread may then be running the
  // older graph.
  ProjectionFixture fixture("projection-sequencer-device-change");
  const TrackId track = fixture.addToneTrack();
  fixture.addClip(track, makeMidiClip(beat(0), beats(4),
                                      {makeNote(60, beat(0), beats(2), 100)}));
  auto &sequencer = *internal::TracktionAccess::projection(fixture.engine)
                         .tracks()
                         .at(track)
                         .sequencer;
  fixture.engine.openManualDevice({.sampleRate = sampleRate, .blockSize = 512});
  fixture.engine.play();
  std::ignore = fixture.engine.process(512);
  const auto block = [&](te::MidiMessageArray &messages, std::int64_t frame,
                         bool playing) {
    const te::PluginRenderContext context(
        nullptr, juce::AudioChannelSet::stereo(), 0, 512, &messages, 0.0,
        {te::TimePosition::fromSamples(frame, sampleRate),
         te::TimePosition::fromSamples(frame + 512, sampleRate)},
        playing, false, false, false);
    sequencer.applyToBuffer(context);
  };
  const auto before = sequencer.published();
  const std::uint64_t graphs = sequencer.graphsPrepared();

  // Another block size, same rate: the note the first block started is not
  // forgotten, and the newest sequence goes out again with new buffers.
  sequencer.baseClassInitialise({
      .startTime = {},
      .sampleRate = sampleRate,
      .blockSizeSamples = 1'024,
  });
  CHECK(sequencer.graphsPrepared() == graphs + 1);
  CHECK(sequencer.published() != before);
  CHECK(sequencer.published()->clips().size() == before->clips().size());
  CHECK(sequencer.sequencesKept() == 1);
  CHECK(sequencer.buffersReady() == internal::MidiOutlet::freshKept);
  te::MidiMessageArray messages;
  block(messages, 512, false);
  REQUIRE(messages.size() == 1);
  CHECK(messages.begin()->isNoteOff());

  // Another rate: the sequence published for 48 kHz is not played at the
  // new rate, and what was started is ended.
  te::MidiMessageArray started;
  sequencer.requestStart();
  block(started, 1'024, true);
  CHECK(started.size() == 1);
  sequencer.baseClassInitialise({
      .startTime = {},
      .sampleRate = 44'100.0,
      .blockSizeSamples = 1'024,
  });
  te::MidiMessageArray ended;
  block(ended, 1'536, true);
  REQUIRE(ended.size() == 1);
  CHECK(ended.begin()->isNoteOff());
  sequencer.baseClassDeinitialise();
  sequencer.baseClassDeinitialise();
  fixture.engine.closeManualDevice();
  CHECK(sequencer.blocksDropped() == 0);
  CHECK(fixture.assertions.count() == 0);
}

namespace {

/// What publication `number` plays: a note of velocity `number` every
/// `spacing` frames over the first 32768 frames.
std::vector<internal::SequencedClip> numberedSequence(int number,
                                                      std::int64_t spacing) {
  internal::ClipEvents events{
      .events = {},
      .startFrame = 0,
      .endFrame = 32'768,
      .truncated = false,
  };
  for (std::int64_t frame = 0; frame < 32'768; frame += spacing) {
    const auto pitch = static_cast<std::uint8_t>(30 + ((frame / spacing) % 60));
    events.events.push_back({
        .frame = frame,
        .pitch = pitch,
        .velocity = static_cast<std::uint8_t>(number),
    });
    events.events.push_back({
        .frame = frame + std::max<std::int64_t>(1, spacing / 2),
        .pitch = pitch,
        .velocity = 0,
    });
  }
  return {
      {
          .clip = ClipId{1},
          .playback = std::make_shared<const internal::ClipPlayback>(events),
      },
  };
}

} // namespace

TEST_CASE("the main thread publishes while the audio thread plays, with no "
          "lock between them",
          "[projection][sequencer][threads]") {
  // Aimed at ThreadSanitizer and AddressSanitizer: the two sides meet only
  // through the plugin's own atomics, and the join at the end.
  EngineFixture fixture("projection-sequencer-threads");
  auto &track = addTrack(fixture.engine);
  const auto plugin =
      track.pluginList.insertPlugin(internal::SequencerPlugin::create(), 0);
  auto *sequencer = dynamic_cast<internal::SequencerPlugin *>(plugin.get());
  REQUIRE(sequencer != nullptr);
  // What a playback graph does to the plugin, done by hand: no graph runs.
  sequencer->baseClassInitialise({
      .startTime = {},
      .sampleRate = sampleRate,
      .blockSizeSamples = 512,
  });
  sequencer->publish(numberedSequence(1, 64), sampleRate);

  constexpr int blocks = 20'000;
  constexpr int publications = 120;
  // The audio side: a stand-in for the engine's plugin node, which swaps its
  // two arrays after every block. It writes down the velocity of every
  // note-on, which is the number of the publication that played it.
  std::vector<int> heard;
  heard.reserve(static_cast<std::size_t>(blocks) * 16);
  std::thread audio([&] {
    te::MidiMessageArray array;
    te::MidiMessageArray output;
    for (int index = 0; index < blocks; ++index) {
      const std::int64_t frame = (std::int64_t{index} * 512) % 32'768;
      output.clear();
      array.clear();
      const te::PluginRenderContext context(
          nullptr, juce::AudioChannelSet::stereo(), 0, 512, &array, 0.0,
          {te::TimePosition::fromSamples(frame, sampleRate),
           te::TimePosition::fromSamples(frame + 512, sampleRate)},
          true, false, false, false);
      sequencer->applyToBuffer(context);
      output.swapWith(array);
      for (const auto &message : output) {
        if (message.isNoteOn()) {
          heard.push_back(message.getVelocity());
        }
      }
    }
  });
  // The main side: publications, now and then one dense enough to need
  // buffers with more room, and now and then a graph prepared, which tops
  // up the buffers. A fixed seed keeps what the main side does the same on
  // every run (CLAUDE.md, "Testing").
  // NOLINTNEXTLINE(bugprone-random-generator-seed,cert-msc32-c,cert-msc51-cpp)
  std::mt19937 random(2026);
  std::size_t mostKept = 0;
  int graphs = 0;
  for (int number = 2; number <= publications; ++number) {
    const bool dense = random() % 8 == 0;
    sequencer->publish(numberedSequence(number, dense ? 2 : 64), sampleRate);
    mostKept = std::max(mostKept, sequencer->sequencesKept());
    if (random() % 4 == 0) {
      sequencer->baseClassInitialise({
          .startTime = {},
          .sampleRate = sampleRate,
          .blockSizeSamples = 512,
      });
      ++graphs;
    }
  }
  audio.join();

  // The numbers the audio side saw never went backwards.
  CHECK(std::ranges::is_sorted(heard));
  CHECK(!heard.empty());
  CHECK(sequencer->blocksDropped() == 0);
  CHECK(mostKept <= 2);
  CHECK(sequencer->buffersAccountedFor());
  for (int count = 0; count <= graphs; ++count) {
    sequencer->baseClassDeinitialise();
  }
  CHECK(sequencer->sequencesKept() == 1);
  CHECK(fixture.assertions.count() == 0);
}

namespace {

/// One note of the document as it sounds: [on, off) in frames.
struct Interval {
  int pitch = 0;
  std::int64_t on = 0;
  std::int64_t off = 0;
};

/// What a MIDI clip of the document plays at 48 kHz and a tempo of
/// `hundredths` of a beat per minute, from the rules of
/// docs/design/08-midi-clips-and-note-editing.md, "Playback" (rules 2 and 6)
/// and docs/design/04-transport-and-musical- time.md: content from the content
/// start, the loop range repeated when the loop is on, a note starting in the
/// stretch played and cut at its end and at the clip's end, every edge on the
/// nearest frame, a note at least one frame long and ended where the next one
/// of its pitch starts.
std::vector<Interval> soundingOf(const Clip &clip, std::int32_t hundredths) {
  const auto &notes = std::get<MidiContent>(clip.content).notes;
  const std::int64_t position = clip.extent.position.ticks;
  const std::int64_t length = clip.extent.length.ticks;
  const std::int64_t contentStart = clip.extent.contentStart.ticks;
  struct Ticks {
    int pitch;
    std::int64_t on;
    std::int64_t off;
  };
  std::vector<Ticks> played;
  const auto stretch = [&](std::int64_t from, std::int64_t to,
                           std::int64_t offset) {
    const std::int64_t end = position + std::min(length, offset + (to - from));
    for (const lina::model::Note &note : notes) {
      const std::int64_t on = position + offset + (note.start.ticks - from);
      if (note.start.ticks >= from && note.start.ticks < to && on < end) {
        played.push_back({
            .pitch = note.pitch,
            .on = on,
            .off = std::min(on + note.length.ticks, end),
        });
      }
    }
  };
  const std::int64_t loopStart = clip.loop.start.ticks;
  const std::int64_t loopEnd = loopStart + clip.loop.length.ticks;
  if (!clip.loop.enabled || contentStart >= loopEnd) {
    stretch(contentStart, contentStart + length, 0);
  } else {
    stretch(contentStart, loopEnd, 0);
    for (std::int64_t offset = loopEnd - contentStart; offset < length;
         offset += loopEnd - loopStart) {
      stretch(loopStart, loopEnd, offset);
    }
  }
  std::vector<Interval> sounding;
  std::map<int, std::size_t> lastOfPitch;
  for (const Ticks &note : played) {
    const std::int64_t on = frameAt(note.on, hundredths);
    const std::int64_t off = std::max(frameAt(note.off, hundredths), on + 1);
    if (const auto last = lastOfPitch.find(note.pitch);
        last != lastOfPitch.end()) {
      auto &before = sounding.at(last->second);
      before.off = std::min(before.off, on);
    }
    lastOfPitch[note.pitch] = sounding.size();
    sounding.push_back({.pitch = note.pitch, .on = on, .off = off});
  }
  std::erase_if(sounding,
                [](const Interval &note) { return note.off <= note.on; });
  return sounding;
}

/// Everything each MIDI track of the project plays.
std::map<TrackId, std::vector<Interval>> soundingOf(const Project &project) {
  std::map<TrackId, std::vector<Interval>> all;
  for (const Track &track : project.tracks) {
    auto &intervals = all[track.id];
    for (const Clip &clip : track.arrangement) {
      const auto clipNotes = soundingOf(clip, project.tempo.hundredths());
      intervals.insert(intervals.end(), clipNotes.begin(), clipNotes.end());
    }
  }
  return all;
}

/// True when a note of `pitch` began before `frame` and ends on it or
/// later: it is still sounding when a block begins on `frame`.
bool holds(const std::vector<Interval> &intervals, int pitch,
           std::int64_t frame) {
  return std::ranges::any_of(intervals, [&](const Interval &note) {
    return note.pitch == pitch && note.on < frame && frame <= note.off;
  });
}

/// Edits the document while it plays and watches, through the probe on
/// every track, each note the instrument is asked to play.
class StuckNoteSweep {
public:
  StuckNoteSweep(std::uint64_t seed, int blockSize)
      : fixture_("projection-sequencer-sweep-" + std::to_string(seed)),
        random_(static_cast<std::mt19937::result_type>(seed)),
        blockSize_(blockSize) {
    for (int count = 0; count < 4; ++count) {
      addProbedTrack(fixture_);
    }
    for (int count = 0; count < 40; ++count) {
      perform(addClip());
    }
  }

  void run(int edits) {
    fixture_.engine.openManualDevice(
        {.sampleRate = sampleRate, .blockSize = blockSize_});
    const auto start = static_cast<std::int64_t>(below(2 * quarter));
    fixture_.engine.play(start);
    playStart_ = start;
    position_ = start;
    playOn();
    for (int edit = 0; edit < edits; ++edit) {
      INFO("seed edit " << edit);
      const Tempo tempo = fixture_.project().tempo;
      perform(nextEdit());
      if (fixture_.project().tempo != tempo) {
        followTempo(tempo);
      }
      mustEndNow();
      playOn();
    }
    // Stopping ends everything, note by note or by telling the instrument
    // to end all notes, which the engine does too.
    fixture_.engine.stop();
    std::ignore = fixture_.engine.process(blockSize_);
    for (const ProbedTrack &track : probedTracks()) {
      for (const auto &note : probeOf(fixture_.engine, track.probe).take()) {
        if (note.allNotesOff) {
          std::erase_if(sounding_, [&](const auto &entry) {
            return entry.first.first == track.track;
          });
          continue;
        }
        CHECK(note.velocity == 0);
        CHECK(sounding_.erase({track.track, note.pitch}) == 1);
      }
      CHECK(!probeOf(fixture_.engine, track.probe).overflowed());
    }
    CHECK(sounding_.empty());
    CHECK(fixture_.assertions.count() == 0);
  }

  [[nodiscard]] std::size_t notesHeard() const { return notesHeard_; }

private:
  using Key = std::pair<TrackId, int>;

  std::uint64_t below(std::uint64_t bound) { return random_() % bound; }

  void perform(const Command &command) {
    const auto changes = fixture_.document.perform("sweep", command);
    if (changes.has_value()) {
      fixture_.engine.applyChanges(fixture_.project(), *changes);
    }
  }

  /// Plays some frames and checks what the instruments were asked to do.
  void playOn() {
    const auto frames = 1 + static_cast<std::int64_t>(below(4'000));
    std::ignore = fixture_.engine.process(frames);
    const auto document = soundingOf(fixture_.project());
    // After a change of tempo one frame has been played already.
    const std::int64_t end =
        position_ + frames + (std::exchange(tempoMoved_, false) ? 1 : 0);
    for (const ProbedTrack &track : probedTracks()) {
      const auto &intervals = document.at(track.track);
      for (const auto &note : probeOf(fixture_.engine, track.probe).take()) {
        check(track.track, intervals, note);
      }
      // Whatever still sounds is a note of the document that has not ended.
      for (const auto &[key, on] : sounding_) {
        if (key.first == track.track) {
          INFO("pitch " << key.second << " sounding from " << on << " at "
                        << end);
          CHECK(holds(intervals, key.second, end));
        }
      }
    }
    CHECK(mustEnd_.empty());
    mustEnd_.clear();
    position_ = end;
  }

  void check(TrackId track, const std::vector<Interval> &intervals,
             const MidiProbe::Note &note) {
    INFO("pitch " << note.pitch << " frame " << note.frame << " velocity "
                  << note.velocity);
    CHECK(note.playing);
    if (note.allNotesOff) {
      // The engine tells every plugin to end all notes in the first block
      // after playback starts, when nothing has sounded yet; never else.
      CHECK(note.frame == playStart_);
      CHECK(std::ranges::none_of(sounding_, [&](const auto &entry) {
        return entry.first.first == track;
      }));
      return;
    }
    const Key key{track, note.pitch};
    if (const auto must = mustEnd_.find(key); must != mustEnd_.end()) {
      // An edit took its end away: it ends where playback was.
      CHECK(note.velocity == 0);
      CHECK(note.frame == must->second);
      mustEnd_.erase(must);
    }
    if (note.velocity == 0) {
      // Only what was started is ended.
      CHECK(sounding_.erase(key) == 1);
      return;
    }
    // A note starts on its own first frame, or on the first frame played
    // when playback began inside it; never twice.
    const bool starts = std::ranges::any_of(intervals, [&](const Interval &i) {
      return i.pitch == note.pitch && i.on == note.frame;
    });
    const bool chased =
        note.frame == playStart_ && holds(intervals, note.pitch, note.frame);
    CHECK((starts || chased));
    CHECK(sounding_.emplace(key, note.frame).second);
    ++notesHeard_;
  }

  /// After an edit: every sounding note that the document no longer holds
  /// at the playhead has to end right there. A track that is gone took its
  /// instrument and what it played with it.
  void mustEndNow() {
    const auto document = soundingOf(fixture_.project());
    std::erase_if(sounding_, [&](const auto &entry) {
      return !document.contains(entry.first.first);
    });
    for (const auto &[key, on] : sounding_) {
      if (!holds(document.at(key.first), key.second, position_)) {
        mustEnd_.emplace(key, position_);
      }
    }
  }

  /// After a change of tempo from `before`: the engine keeps the musical
  /// position, so the playhead moves to the frame of the same beat. One
  /// frame is played to see where; what it played is checked with the next
  /// stretch.
  void followTempo(Tempo before) {
    std::ignore = fixture_.engine.process(1);
    const std::int64_t frame =
        probeOf(fixture_.engine, probedTracks().front().probe).lastBlockStart();
    const double beatKept = static_cast<double>(position_) *
                            before.hundredths() /
                            fixture_.project().tempo.hundredths();
    INFO("playhead from " << position_ << " to " << frame
                          << ", the same beat at " << beatKept);
    CHECK(std::abs(static_cast<double>(frame) - beatKept) <= 1.0);
    position_ = frame;
    tempoMoved_ = true;
  }

  /// The MIDI tracks of the document and their probes.
  [[nodiscard]] std::vector<ProbedTrack> probedTracks() const {
    std::vector<ProbedTrack> tracks;
    for (const Track &track : fixture_.project().tracks) {
      const auto probe = std::ranges::find(
          track.devices, std::string(MidiProbe::xmlTypeName),
          [](const Device &device) { return device.plugin.identifier; });
      if (probe != track.devices.end()) {
        tracks.push_back({.track = track.id, .probe = probe->id});
      }
    }
    return tracks;
  }

  [[nodiscard]] TrackId randomTrack() {
    const auto tracks = probedTracks();
    return tracks.at(below(tracks.size())).track;
  }

  /// Edits that make the engine build its playback graph anew; each is done
  /// here, and what is returned changes nothing.
  Command graphEdit(std::uint64_t choice) {
    const auto &document = fixture_.document;
    switch (choice) {
    case 0: {
      constexpr std::array<std::int32_t, 4> tempos{
          12'000,
          9'000,
          15'000,
          13'333,
      };
      perform(SetProjectProperty{
          TempoValue{
              Tempo::fromHundredths(tempos.at(below(tempos.size())))
                  .value_or(Tempo{}),
          },
      });
      break;
    }
    case 1: {
      const TrackId track = randomTrack();
      perform(AddDevice{
          .track = track,
          .device = delayGainDevice(0, 1.0F),
          .index = findTrack(document.project(), track)->devices.size(),
      });
      break;
    }
    case 2:
      for (const Track &track : document.project().tracks) {
        const auto effect = std::ranges::find(
            track.devices, std::string(DelayGainEffect::xmlTypeName),
            [](const Device &device) { return device.plugin.identifier; });
        if (effect != track.devices.end()) {
          perform(RemoveDevice{effect->id});
          break;
        }
      }
      break;
    case 3:
      addProbedTrack(fixture_);
      break;
    case 4:
      // A MIDI track goes, sounding notes and all; one is always left.
      if (probedTracks().size() > 1) {
        perform(RemoveTrack{randomTrack()});
      }
      break;
    default:
      fixture_.engine.syncGraph();
      break;
    }
    return SetProjectProperty{StartMarkerValue{TickPosition{0}}};
  }

  /// A note that starts inside the first `length` ticks of a clip's
  /// content. A narrow range of pitches makes notes of one pitch meet
  /// often; starts and lengths lie off the grid.
  lina::model::Note randomNote(std::int64_t length) {
    constexpr std::int64_t eighth = ticksPerQuarter / 2;
    const auto steps =
        static_cast<std::uint64_t>(std::max<std::int64_t>(1, length / eighth));
    return makeNote(
        static_cast<std::uint8_t>(55 + below(8)),
        TickPosition{
            static_cast<std::int64_t>((below(steps) * eighth) +
                                      (below(3) * 977)),
        },
        TickDuration{static_cast<std::int64_t>(1 + below(ticksPerQuarter))},
        static_cast<std::uint8_t>(1 + below(127)));
  }

  TickPosition randomPosition() {
    return TickPosition{
        static_cast<std::int64_t>((below(40) * (ticksPerQuarter / 4)) +
                                  (below(2) * 331)),
    };
  }

  Command addClip() {
    const auto length =
        static_cast<std::int64_t>((1 + below(4)) * (ticksPerQuarter / 2));
    std::vector<lina::model::Note> notes;
    for (std::uint64_t count = 4 + below(9); count > 0; --count) {
      notes.push_back(randomNote(length));
    }
    // A clip is refused when two notes of one pitch overlap in it: each
    // note ends where the next of its pitch starts.
    std::ranges::sort(notes, {},
                      [](const auto &note) { return note.start.ticks; });
    std::map<int, std::size_t> lastOfPitch;
    for (std::size_t index = 0; index < notes.size(); ++index) {
      const auto &note = notes.at(index);
      if (const auto last = lastOfPitch.find(note.pitch);
          last != lastOfPitch.end()) {
        auto &before = notes.at(last->second);
        before.length.ticks = std::min(before.length.ticks,
                                       note.start.ticks - before.start.ticks);
      }
      lastOfPitch[note.pitch] = index;
    }
    std::erase_if(notes,
                  [](const auto &note) { return note.length.ticks <= 0; });
    return AddClip{
        .container = randomTrack(),
        .clip = makeMidiClip(randomPosition(), TickDuration{length},
                             std::move(notes)),
    };
  }

  /// A clip of the arrangement, or nothing.
  const Clip *randomClip() {
    std::vector<const Clip *> clips;
    for (const Track &track : fixture_.project().tracks) {
      for (const Clip &clip : track.arrangement) {
        clips.push_back(&clip);
      }
    }
    return clips.empty() ? nullptr : clips.at(below(clips.size()));
  }

  Command nextEdit() {
    const Clip *clip = randomClip();
    const std::uint64_t choice = below(18);
    if (choice >= 12) {
      return graphEdit(choice - 12);
    }
    if (clip == nullptr || choice == 0 || choice == 11) {
      return addClip();
    }
    const auto &notes = std::get<MidiContent>(clip->content).notes;
    switch (choice) {
    case 1:
      return AddNotes{
          .clip = clip->id,
          .notes = {randomNote(clip->extent.length.ticks)},
      };
    case 2:
    case 3:
      if (!notes.empty()) {
        lina::model::Note note = randomNote(clip->extent.length.ticks);
        note.id = notes.at(below(notes.size())).id;
        return SetNotes{.clip = clip->id, .notes = {note}};
      }
      return AddNotes{
          .clip = clip->id,
          .notes = {randomNote(clip->extent.length.ticks)},
      };
    case 4:
      if (!notes.empty()) {
        return RemoveNotes{
            .clip = clip->id,
            .notes = {notes.at(below(notes.size())).id},
        };
      }
      return RemoveClip{clip->id};
    case 5:
      return MoveClip{
          .clip = clip->id,
          .container = randomTrack(),
          .position = randomPosition(),
      };
    case 6:
      return RemoveClip{clip->id};
    case 7: {
      const LoopSettings loop{
          .enabled = below(3) != 0,
          .start =
              TickPosition{
                  static_cast<std::int64_t>(below(4) * (ticksPerQuarter / 4)),
              },
          .length =
              TickDuration{
                  static_cast<std::int64_t>((1 + below(6)) *
                                            (ticksPerQuarter / 4)),
              },
      };
      return SetClipProperty{.clip = clip->id, .property = LoopValue{loop}};
    }
    case 8: {
      ClipExtent extent = clip->extent;
      extent.contentStart = TickPosition{
          static_cast<std::int64_t>(below(4) * (ticksPerQuarter / 4)),
      };
      extent.length = TickDuration{
          static_cast<std::int64_t>((1 + below(12)) * (ticksPerQuarter / 2)),
      };
      return SetClipProperty{.clip = clip->id, .property = ExtentValue{extent}};
    }
    case 9:
      return undoOrNothing(true);
    default:
      return undoOrNothing(false);
    }
  }

  /// Undoes or redoes here and returns a command that changes nothing, so
  /// that every edit is checked the same way.
  Command undoOrNothing(bool undoing) {
    auto &document = fixture_.document;
    if (undoing ? document.canUndo() : document.canRedo()) {
      const auto changes = undoing ? document.undo() : document.redo();
      REQUIRE(changes.has_value());
      fixture_.engine.applyChanges(fixture_.project(), *changes);
    }
    return SetProjectProperty{StartMarkerValue{TickPosition{0}}};
  }

  ProjectionFixture fixture_;
  std::mt19937 random_;
  int blockSize_;
  std::int64_t playStart_ = 0;
  std::int64_t position_ = 0;
  bool tempoMoved_ = false;
  std::map<Key, std::int64_t> sounding_;
  std::map<Key, std::int64_t> mustEnd_;
  std::size_t notesHeard_ = 0;
};

} // namespace

TEST_CASE("while generated edits are made during playback, no note sounds "
          "past its end and nothing sounds after stop",
          "[projection][sequencer]") {
  std::size_t heard = 0;
  constexpr std::array<int, 4> blockSizes{512, 441, 64, 7};
  for (std::uint64_t seed = 1; seed <= 8; ++seed) {
    CAPTURE(seed);
    StuckNoteSweep sweep(seed, blockSizes.at(seed % blockSizes.size()));
    sweep.run(100);
    heard += sweep.notesHeard();
  }
  // The sweeps must have played something worth checking.
  CHECK(heard > 300);
}
