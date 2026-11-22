// SPDX-License-Identifier: GPL-3.0-or-later
//
// The large project of docs/design/11-non-functional-requirements.md
// ("Reference points"): 256 tracks and 10,000 arrangement clips, among them
// MIDI clips of 10,000 notes. For the benches that measure edits while it
// plays; not used by any gating test.
//
// The layout, at 120 beats per minute:
//   - `midiTracks` MIDI tracks with the test instrument, each with
//     `clipsPerTrack` clips of `clipBeats` beats, back to back from beat 0,
//     of `notesPerClip` sixteenth notes each.
//   - The first `bigClips` MIDI tracks have one more clip after those, of
//     `bigClipNotes` notes a 64th note apart, 160 beats long.
//   - `audioTracks` audio tracks with clips of 2 beats every 16 beats, all
//     playing one 2-second file; as many as make the total `clipCount`.
//   - Two return tracks.
#ifndef LINA_TESTS_ENGINE_LARGE_PROJECT_HPP
#define LINA_TESTS_ENGINE_LARGE_PROJECT_HPP

#include <catch2/catch_test_macros.hpp>

#include "lina/engine/testing/projection_support.hpp"
#include "lina/engine/testing/test_plugins.hpp"
#include "lina/model/document.hpp"
#include "support/builders.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <numbers>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace lina::engine::testing {

struct LargeProjectShape {
  int midiTracks = 192;
  int audioTracks = 64;
  int clipsPerTrack = 39;
  /// Length of each of those clips, in beats.
  int clipBeats = 16;
  int notesPerClip = 64;
  int bigClips = 4;
  int bigClipNotes = 10'000;
  int clipCount = 10'000;
};

/// Near doc 11's reference project in size: 32 tracks, 500 clips, half of
/// the tracks MIDI and half audio, no clip of 10,000 notes.
inline LargeProjectShape referenceProjectShape() {
  LargeProjectShape shape;
  shape.midiTracks = 16;
  shape.audioTracks = 16;
  shape.clipsPerTrack = 15;
  shape.bigClips = 0;
  shape.clipCount = 500;
  return shape;
}

struct LargeProject {
  std::vector<model::TrackId> midiTracks;
  std::vector<model::TrackId> audioTracks;
  /// By track, then by position.
  std::vector<std::vector<model::ClipId>> midiClips;
  std::vector<model::ClipId> bigClips;
  std::vector<model::ClipId> audioClips;
  model::MediaId media;
  std::int64_t noteCount = 0;
};

/// Applies one command as its own transaction; it must succeed.
inline model::ChangeSet performOrFail(model::Document &document,
                                      const model::Command &command) {
  const auto changes = document.perform("bench", command);
  INFO("fault " << (changes.has_value()
                        ? -1
                        : static_cast<int>(changes.error().fault)));
  REQUIRE(changes.has_value());
  return changes.value_or(model::ChangeSet{});
}

template <model::ObjectKind Kind>
model::Id<Kind> performAndGet(model::Document &document,
                              const model::Command &command) {
  const auto id = performOrFail(document, command).template firstAdded<Kind>();
  REQUIRE(id.has_value());
  return id.value_or(model::Id<Kind>{});
}

inline std::vector<model::Note> sixteenths(int count) {
  std::vector<model::Note> notes;
  notes.reserve(static_cast<std::size_t>(count));
  for (int n = 0; n < count; ++n) {
    notes.push_back(model::testing::makeNote(
        static_cast<std::uint8_t>(36 + (n % 48)),
        model::TickPosition{n * (model::ticksPerQuarter / 4)},
        {model::ticksPerQuarter / 4}, 100));
  }
  return notes;
}

inline std::vector<model::Note> sixtyFourths(int count) {
  std::vector<model::Note> notes;
  notes.reserve(static_cast<std::size_t>(count));
  for (int n = 0; n < count; ++n) {
    notes.push_back(model::testing::makeNote(
        static_cast<std::uint8_t>(36 + (n % 48)),
        model::TickPosition{std::int64_t{n} * (model::ticksPerQuarter / 16)},
        {model::ticksPerQuarter / 16}, 90));
  }
  return notes;
}

/// Builds the project into `document`, command by command. The audio file
/// is written below `mediaFolder` as media/large.wav.
inline LargeProject buildLargeProject(model::Document &document,
                                      const std::filesystem::path &mediaFolder,
                                      const LargeProjectShape &shape) {
  using model::testing::beat;
  using model::testing::beats;
  LargeProject result;

  AudioBuffer file;
  file.sampleRate = 48'000.0;
  file.channels.assign(2, std::vector<float>(96'000));
  for (auto &channel : file.channels) {
    for (std::size_t frame = 0; frame < channel.size(); ++frame) {
      channel.at(frame) = static_cast<float>(
          0.1 * std::sin(2.0 * std::numbers::pi * 220.0 *
                         static_cast<double>(frame) / 48'000.0));
    }
  }
  writeWav(mediaFolder / "media" / "large.wav", file);
  result.media = performAndGet<model::ObjectKind::Media>(
      document, model::AddMedia{model::testing::makeMedia("media/large.wav")});

  const std::vector<model::Note> clipNotes = sixteenths(shape.notesPerClip);
  int clipsMade = 0;
  for (int t = 0; t < shape.midiTracks; ++t) {
    const auto track = performAndGet<model::ObjectKind::Track>(
        document, model::AddTrack{
                      .kind = model::TrackKind::Midi,
                      .name = "midi",
                      .index = document.project().tracks.size(),
                  });
    result.midiTracks.push_back(track);
    performOrFail(document, model::AddDevice{
                                .track = track,
                                .device = toneDevice(),
                                .index = 0,
                            });
    auto &clips = result.midiClips.emplace_back();
    for (int c = 0; c < shape.clipsPerTrack; ++c) {
      clips.push_back(performAndGet<model::ObjectKind::Clip>(
          document, model::AddClip{
                        .container = track,
                        .clip = model::testing::makeMidiClip(
                            beat(std::int64_t{shape.clipBeats} * c),
                            beats(shape.clipBeats), clipNotes),
                    }));
      result.noteCount += shape.notesPerClip;
      ++clipsMade;
    }
    if (t < shape.bigClips) {
      result.bigClips.push_back(performAndGet<model::ObjectKind::Clip>(
          document,
          model::AddClip{
              .container = track,
              .clip = model::testing::makeMidiClip(
                  beat(std::int64_t{shape.clipBeats} * shape.clipsPerTrack),
                  beats(160), sixtyFourths(shape.bigClipNotes)),
          }));
      result.noteCount += shape.bigClipNotes;
      ++clipsMade;
    }
  }
  for (int t = 0; t < shape.audioTracks; ++t) {
    result.audioTracks.push_back(performAndGet<model::ObjectKind::Track>(
        document, model::AddTrack{
                      .kind = model::TrackKind::Audio,
                      .name = "audio",
                      .index = document.project().tracks.size(),
                  }));
  }
  for (int c = 0; clipsMade < shape.clipCount && shape.audioTracks > 0; ++c) {
    const auto track =
        result.audioTracks.at(static_cast<std::size_t>(c % shape.audioTracks));
    const std::int64_t slot = c / shape.audioTracks;
    result.audioClips.push_back(performAndGet<model::ObjectKind::Clip>(
        document,
        model::AddClip{
            .container = track,
            .clip = model::testing::makeAudioClip(
                result.media, beat(std::int64_t{16} * slot), beats(2)),
        }));
    ++clipsMade;
  }
  for (int r = 0; r < 2; ++r) {
    performOrFail(document, model::AddTrack{
                                .kind = model::TrackKind::Return,
                                .name = "return",
                                .index = document.project().returns.size(),
                            });
  }
  return result;
}

/// One edit the benches time. An even run makes it, an odd run takes it
/// back, so that the project keeps its size.
struct BenchEdit {
  std::string name;
  std::function<model::Command(int run)> command;
};

/// Adding a note of `pitch` at `start` to `clip`, and removing it again.
/// Nothing else in the clip may have that pitch.
inline BenchEdit noteAddedAndRemoved(std::string name,
                                     const model::Document &document,
                                     model::ClipId clip, std::uint8_t pitch,
                                     model::TickPosition start) {
  return {
      .name = std::move(name),
      .command = [&document, clip, pitch, start](int run) -> model::Command {
        if (run % 2 == 0) {
          return model::AddNotes{
              .clip = clip,
              .notes = {model::testing::makeNote(pitch, start, {3780}, 90)},
          };
        }
        const model::Clip *found =
            model::findClip(document.project(), clip).clip;
        const auto &notes = std::get<model::MidiContent>(found->content).notes;
        const auto added = std::ranges::find(notes, pitch, &model::Note::pitch);
        REQUIRE(added != notes.end());
        return model::RemoveNotes{.clip = clip, .notes = {added->id}};
      },
  };
}

/// A clip moved to `away` on its own track, and back.
inline BenchEdit clipMovedAndBack(std::string name,
                                  const model::Document &document,
                                  model::ClipId clip, model::TrackId track,
                                  model::TickPosition away) {
  const model::TickPosition home =
      model::findClip(document.project(), clip).clip->extent.position;
  return {
      .name = std::move(name),
      .command = [clip, track, away, home](int run) -> model::Command {
        return model::MoveClip{
            .clip = clip,
            .container = track,
            .position = run % 2 == 0 ? away : home,
        };
      },
  };
}

/// The edits both benches time, in this order: notes, clips, mixer, time
/// signature, tempo.
inline std::vector<BenchEdit> benchEdits(const model::Document &document,
                                         const LargeProject &large,
                                         const LargeProjectShape &shape) {
  using model::testing::beat;
  const model::ClipId ordinary = large.midiClips.at(10).at(
      static_cast<std::size_t>(shape.clipsPerTrack / 4));
  std::vector<BenchEdit> edits;
  edits.push_back(noteAddedAndRemoved("note added/removed, ordinary clip",
                                      document, ordinary, 100, beat(15)));
  if (!large.bigClips.empty()) {
    edits.push_back(noteAddedAndRemoved("note added/removed, 10,000-note clip",
                                        document, large.bigClips.front(), 127,
                                        beat(159)));
  }
  edits.push_back({
      .name = "note moved, ordinary clip",
      .command = [&document, ordinary](int run) -> model::Command {
        const model::Clip *clip =
            model::findClip(document.project(), ordinary).clip;
        model::Note note =
            std::get<model::MidiContent>(clip->content).notes.at(10);
        note.start.ticks += run % 2 == 0 ? 1'890 : -1'890;
        return model::SetNotes{.clip = ordinary, .notes = {note}};
      },
  });
  if (shape.clipsPerTrack > 1) {
    edits.push_back(
        clipMovedAndBack("MIDI clip moved", document,
                         large.midiClips.at(12).at(
                             static_cast<std::size_t>(shape.clipsPerTrack / 2)),
                         large.midiTracks.at(12), beat(1'000)));
  }
  const std::size_t audio = large.audioClips.size() / 2;
  edits.push_back(clipMovedAndBack(
      "audio clip moved", document, large.audioClips.at(audio),
      large.audioTracks.at(audio % large.audioTracks.size()), beat(2'000)));
  edits.push_back({
      .name = "fader",
      .command = [track = large.midiTracks.at(3)](int run) -> model::Command {
        return model::SetTrackProperty{
            .track = track,
            .property = model::VolumeValue{run % 2 == 0 ? -6.0 : 0.0},
        };
      },
  });
  edits.push_back({
      .name = "mute",
      .command = [track = large.midiTracks.at(5)](int run) -> model::Command {
        return model::SetTrackProperty{
            .track = track,
            .property = model::MuteValue{run % 2 == 0},
        };
      },
  });
  edits.push_back({
      .name = "time signature",
      .command = [](int run) -> model::Command {
        return model::SetProjectProperty{
            model::TimeSignatureValue{
                model::TimeSignature::make(run % 2 == 0 ? 3 : 4, 4)
                    .value_or(model::TimeSignature{}),
            },
        };
      },
  });
  edits.push_back({
      .name = "tempo",
      .command = [](int run) -> model::Command {
        return model::SetProjectProperty{
            model::TempoValue{
                model::Tempo::fromHundredths(run % 2 == 0 ? 12'100 : 12'000)
                    .value_or(model::Tempo{}),
            },
        };
      },
  });
  return edits;
}

/// Milliseconds a call takes, by the steady clock.
template <class Call> double milliseconds(const Call &call) {
  const auto start = std::chrono::steady_clock::now();
  call();
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now() - start)
      .count();
}

/// Median and maximum of a set of timings.
struct Timing {
  double median = 0.0;
  double max = 0.0;
};

inline Timing summarise(std::vector<double> values) {
  if (values.empty()) {
    return {};
  }
  std::ranges::sort(values);
  return {.median = values.at(values.size() / 2), .max = values.back()};
}

} // namespace lina::engine::testing

#endif // LINA_TESTS_ENGINE_LARGE_PROJECT_HPP
