// SPDX-License-Identifier: GPL-3.0-or-later
//
// The projection's oracle (docs/design/13-architecture.md, "The projection"):
// an edit kept in step through any sequence of commands must equal an edit
// built from scratch from the resulting document. Checked here for generated
// sequences with fixed seeds, in two senses of "equal":
//   - structural: the two edits read the same, line by line (describeEdit);
//   - audible: the two edits render the same samples, bit for bit.
// A third kind of sequence makes its edits while the engine plays, and
// compares what is heard afterwards with the scratch edit played from the
// start.
//
// A sequence mixes single commands, transactions of several commands whose
// change sets reach the engine one by one or merged into one, transactions
// that are rolled back, undo and redo. At its end everything is undone.
//
// LINA_PROJECTION_SOAK=<n> in the environment runs n times as many
// sequences; the default run is sized for the everyday suite.

#include <catch2/catch_test_macros.hpp>

#include "lina/engine/internal/tracktion_access.hpp"
#include "lina/engine/message_thread.hpp"
#include "lina/engine/testing/edit_building.hpp"
#include "lina/engine/testing/projection_support.hpp"
#include "lina/engine/testing/test_environment.hpp"
#include "lina/engine/testing/test_plugins.hpp"
#include "lina/model/document.hpp"
#include "reference_signal.hpp"
#include "support/builders.hpp"
#include "support/generator.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace lina::engine;
using namespace lina::engine::testing;
using namespace lina::model;
using lina::model::testing::CommandGenerator;
using lina::model::testing::GeneratorOptions;

namespace {

/// A low rate keeps the renders short; the projection works the same at any.
constexpr double sampleRate = 8000.0;
constexpr int blockSize = 512;
constexpr std::int64_t renderFrames = std::int64_t{12} * 8000;
/// The block sizes that renders and live sequences take turns with.
constexpr std::array<int, 4> blockSizes{512, 64, 1'000, 37};

struct Tally {
  std::size_t changeSets{0};
  std::size_t undone{0};
  std::size_t redone{0};
  std::size_t rolledBack{0};
  std::size_t transactions{0};
  std::size_t compared{0};
  std::size_t rendered{0};
  std::size_t audibleRenders{0};
  /// Renders of documents that are known to have something to hear.
  std::size_t mustBeAudible{0};
  /// Live sequences whose end could be compared, and those with sound in
  /// what was compared.
  std::size_t liveCompared{0};
  std::size_t liveAudible{0};
  std::size_t mostTracks{0};
  std::size_t mostClips{0};
};

std::size_t arrangementClipCount(const Project &project) {
  std::size_t count = 0;
  for (const Track &track : project.tracks) {
    count += track.arrangement.size();
  }
  return count;
}

/// The lines around the first one in which two descriptions differ.
std::string firstDifference(const std::string &incremental,
                            const std::string &fromScratch) {
  constexpr std::size_t contextLines = 12;
  std::istringstream left(incremental);
  std::istringstream right(fromScratch);
  std::vector<std::string> context;
  std::string a;
  std::string b;
  for (int number = 1;; ++number) {
    const bool moreLeft = static_cast<bool>(std::getline(left, a));
    const bool moreRight = static_cast<bool>(std::getline(right, b));
    if (!moreLeft && !moreRight) {
      return "no line";
    }
    if (!moreLeft || !moreRight || a != b) {
      std::string report = "line " + std::to_string(number) + "\n";
      for (const std::string &line : context) {
        report += "    " + line + "\n";
      }
      report += "  kept in step: " + (moreLeft ? a : "(end)") + "\n";
      report += "  from scratch: " + (moreRight ? b : "(end)") + "\n";
      return report;
    }
    context.push_back(a);
    if (context.size() > contextLines) {
      context.erase(context.begin());
    }
  }
}

/// Noise between -1 and 1, the same for the same seed.
AudioBuffer noise(std::size_t channels, std::size_t frames, double rate,
                  unsigned seed) {
  std::mt19937 random(seed);
  AudioBuffer audio;
  audio.sampleRate = rate;
  audio.channels.assign(channels, std::vector<float>(frames));
  for (auto &channel : audio.channels) {
    for (float &sample : channel) {
      sample = (static_cast<float>(random() % 2001) / 1000.0F) - 1.0F;
    }
  }
  return audio;
}

/// The identity a command gave to what it created.
template <ObjectKind Kind> Id<Kind> created(const ChangeSet &changes) {
  const auto id = changes.firstAdded<Kind>();
  REQUIRE(id.has_value());
  return id.value_or(Id<Kind>{});
}

/// What the sequences of one test case share: the headless environment and
/// the audio files that generated media references point at.
struct Environment {
  /// The media files are at `mediaRate`: the rate of the renders, so that
  /// the engine plays them sample for sample, unless a case is about
  /// files at another rate.
  explicit Environment(std::string_view name, double mediaRate = sampleRate)
      : scratch(enterScratchFolder(name)) {
    // Seeded noise in stereo, and a shorter mono file.
    writeWav(scratch / "media" / "stereo.wav", noise(2, 96000, mediaRate, 7));
    writeWav(scratch / "media" / "mono.wav", noise(1, 30000, mediaRate, 8));
  }

  /// With `withMemory` false, no generated effect delays: what is heard
  /// then depends on nothing that sounded before.
  [[nodiscard]] GeneratorOptions options(bool withMemory = true) const {
    return {
        .instruments = {toneDevice()},
        .effects =
            {
                delayGainDevice(withMemory ? 800 : 0, 0.5F),
                lina::model::testing::makeEffect("unknown"),
                delayGainDevice(0, -1.0F),
            },
        .mediaPaths =
            {
                "media/stereo.wav",
                "media/mono.wav",
                (scratch / "media" / "stereo.wav").string(),
                "media/missing.wav",
            },
        .parameterIds = {withMemory ? "delay" : "gain", "gain", "unknown"},
    };
  }

  AssertionCounter assertions;
  std::filesystem::path scratch;
  MessageThread messageThread;
};

/// A small project that can be heard: an audio track with a clip of the
/// stereo file, a MIDI track with the test instrument and a clip of notes,
/// and a return track with the test effect that both send to.
Project audibleProject(int returnDelayFrames = 400) {
  using namespace lina::model::testing;
  Document document;
  const auto added = [&](const Command &command) {
    const auto changes = document.perform("base", command);
    REQUIRE(changes.has_value());
    return *changes;
  };
  const MediaId media = created<ObjectKind::Media>(
      added(AddMedia{makeMedia("media/stereo.wav")}));
  const auto track = [&](TrackKind kind) {
    return created<ObjectKind::Track>(
        added(AddTrack{.kind = kind, .name = "base", .index = 0}));
  };
  const TrackId effects = track(TrackKind::Return);
  added(AddDevice{
      .track = effects,
      .device = delayGainDevice(returnDelayFrames, 0.5F),
      .index = 0,
  });
  const TrackId audio = track(TrackKind::Audio);
  added(AddClip{
      .container = audio,
      .clip = makeAudioClip(media, beat(0), beats(4)),
  });
  const TrackId midi = track(TrackKind::Midi);
  added(AddDevice{.track = midi, .device = toneDevice(), .index = 0});
  added(AddClip{
      .container = midi,
      .clip = makeMidiClip(beat(0), beats(64),
                           {
                               makeNote(57, beat(0), beats(2), 90),
                               makeNote(64, beat(1), beats(1), 70),
                               makeNote(60, beat(4), beats(6), 110),
                               makeNote(55, beat(12), beats(8), 100),
                               makeNote(67, beat(22), beats(10), 100),
                               makeNote(52, beat(34), beats(12), 100),
                               makeNote(64, beat(48), beats(14), 100),
                           }),
  });
  for (const TrackId source : {audio, midi}) {
    added(SetTrackProperty{
        .track = source,
        .property = SendValue{{.target = effects, .levelDb = -6.0}},
    });
  }
  return document.project();
}

/// True when the project is certain to be heard in a render from the start:
/// a MIDI track whose only device is the test instrument, that reaches the
/// master at a level above silence, plays a note within the rendered
/// stretch, and the master has no devices and is not silent. Worked out from
/// the document with the definitions of the design docs, not with the
/// engine's code. Many audible projects fail this test; none that passes it
/// is silent.
bool hasPlainNote(const Project &project) {
  const auto soloed = [](const Track &track) { return track.mixer.solo; };
  const bool anySolo = std::ranges::any_of(project.tracks, soloed) ||
                       std::ranges::any_of(project.returns, soloed);
  if (!project.master.devices.empty() ||
      project.master.mixer.volumeDb <= silenceDb) {
    return false;
  }
  // A tick lasts 60 / (bpm * 15120) seconds.
  const double framesPerTick =
      sampleRate * 60.0 /
      (project.tempo.beatsPerMinute() * static_cast<double>(ticksPerQuarter));
  const auto heard = [&](const Clip &clip) {
    const auto *midi = std::get_if<MidiContent>(&clip.content);
    if (midi == nullptr || clip.loop.enabled) {
      return false;
    }
    return std::ranges::any_of(midi->notes, [&](const lina::model::Note &note) {
      const std::int64_t intoClip =
          note.start.ticks - clip.extent.contentStart.ticks;
      // A margin of ten frames at each end keeps rounding out of it.
      const double on =
          static_cast<double>(clip.extent.position.ticks + intoClip) *
          framesPerTick;
      const double length =
          static_cast<double>(std::min(note.length.ticks,
                                       clip.extent.length.ticks - intoClip)) *
          framesPerTick;
      return intoClip >= 0 && intoClip < clip.extent.length.ticks &&
             on < static_cast<double>(renderFrames) - 10.0 && length > 10.0;
    });
  };
  return std::ranges::any_of(project.tracks, [&](const Track &track) {
    const bool plainInstrument =
        track.kind == TrackKind::Midi && track.devices.size() == 1 &&
        track.devices.front().plugin == toneDevice().plugin &&
        !track.devices.front().bypassed;
    const bool reachesMaster = !track.mixer.mute &&
                               track.mixer.volumeDb > silenceDb &&
                               (track.mixer.solo || !anySolo);
    return plainInstrument && reachesMaster &&
           std::ranges::any_of(track.arrangement, heard);
  });
}

/// A document, the engine that follows its change sets, and the engine that
/// is rebuilt from the document for every comparison. Each sequence gets
/// engines of its own: the engine underneath lets go of deleted plugins only
/// from a timer, which never fires here, and slows down as they pile up.
struct Oracle {
  Oracle(const Environment &environment, std::uint64_t seed, Tally &counts,
         Project start)
      : mediaFolder(environment.scratch),
        folder(mediaFolder / ("seed-" + std::to_string(seed))),
        document(Document::fromProject(std::move(start)).value()),
        tally(&counts) {
    for (Engine *engine : {&incrementalEngine, &scratchEngine}) {
      registerTestPlugins(*engine);
      engine->openManualDevice(
          {.sampleRate = sampleRate, .blockSize = blockSize});
    }
    incrementalEngine.setProject(document.project(), mediaFolder);
  }

  void follow(const ChangeSet &changes) {
    incrementalEngine.applyChanges(document.project(), changes);
    ++tally->changeSets;
  }

  void compare(const std::string &what) {
    scratchEngine.setProject(document.project(), mediaFolder);
    const std::string incremental = describeEdit(incrementalEngine);
    const std::string fromScratch = describeEdit(scratchEngine);
    ++tally->compared;
    tally->mostTracks =
        std::max(tally->mostTracks, document.project().tracks.size() +
                                        document.project().returns.size());
    tally->mostClips =
        std::max(tally->mostClips, arrangementClipCount(document.project()));
    if (incremental != fromScratch) {
      FAIL("after " << what << ", the edit kept in step differs from the "
                    << "edit built from scratch at "
                    << firstDifference(incremental, fromScratch));
    }
  }

  /// Renders both edits from the start; they must agree bit for bit.
  void compareAudio(const std::string &what) {
    // Each render with another block size than the one before.
    const RenderRequest request{
        .format =
            {
                .sampleRate = sampleRate,
                .blockSize = blockSizes.at(tally->rendered % blockSizes.size()),
            },
        .startFrame = 0,
        .frameCount = renderFrames,
    };
    const auto incremental = incrementalEngine.renderOffline(request);
    const auto fromScratch = scratchEngine.renderOffline(request);
    ++tally->rendered;
    const bool audible = firstSoundingFrame(incremental.channels.at(0)) != -1 ||
                         firstSoundingFrame(incremental.channels.at(1)) != -1;
    tally->audibleRenders += audible ? 1 : 0;
    if (hasPlainNote(document.project())) {
      ++tally->mustBeAudible;
      if (!audible) {
        FAIL("after " << what << ", the document has a note that must be "
                      << "heard, and the render is silent");
      }
    }
    // Tolerance: none.
    if (!identical(incremental, fromScratch)) {
      FAIL("after " << what << ", the two edits render different audio; "
                    << "largest difference on the left "
                    << maxDifference(incremental.channels.at(0),
                                     fromScratch.channels.at(0)));
    }
  }

  /// The next command of a sequence. Where the tempo must stay as it is, a
  /// command that would change it is replaced by one the engine ignores.
  Command next(CommandGenerator &generator) const {
    Command command = generator.next(document.project());
    const auto *property = std::get_if<SetProjectProperty>(&command);
    if (keepTempo && property != nullptr &&
        std::holds_alternative<TempoValue>(property->property)) {
      return SetProjectProperty{StartMarkerValue{TickPosition{0}}};
    }
    return command;
  }

  std::filesystem::path mediaFolder;
  std::filesystem::path folder;
  bool keepTempo{false};
  Engine incrementalEngine{{.storageFolder = folder / "incremental"}};
  Engine scratchEngine{{.storageFolder = folder / "scratch"}};
  Document document;
  Tally *tally;
};

/// Makes sure that the document has something that must be heard from
/// `position` on, whatever the sequence has done to it: the solos go, the
/// master is brought back to plain, and a new MIDI track with the test
/// instrument plays a note. These are edits like any other and reach the
/// engine as change sets.
void addWitness(Oracle &oracle, TickPosition position, TickDuration length) {
  using namespace lina::model::testing;
  Document &document = oracle.document;
  const auto perform = [&](std::span<const Command> commands) {
    const auto changes = document.perform("witness", commands);
    REQUIRE(changes.has_value());
    oracle.follow(*changes);
    return *changes;
  };
  const std::array<Command, 1> add{
      AddTrack{.kind = TrackKind::Midi, .name = "witness", .index = 0},
  };
  const TrackId track = created<ObjectKind::Track>(perform(add));
  std::vector<Command> rest{
      AddDevice{.track = track, .device = toneDevice(), .index = 0},
      AddClip{
          .container = track,
          .clip = makeMidiClip(position, length,
                               {makeNote(81, beat(0), length, 100)}),
      },
      SetTrackProperty{
          .track = document.project().master.id,
          .property = VolumeValue{0.0},
      },
  };
  for (const Device &device : document.project().master.devices) {
    rest.emplace_back(RemoveDevice{device.id});
  }
  for (const auto *list :
       {&document.project().tracks, &document.project().returns}) {
    for (const Track &other : *list) {
      if (other.mixer.solo) {
        rest.emplace_back(
            SetTrackProperty{.track = other.id, .property = SoloValue{false}});
      }
    }
  }
  perform(rest);
}

/// One transaction of several commands. Its change sets reach the engine one
/// by one, as during a drag, or merged into one, as after Document::perform.
void runTransaction(Oracle &oracle, CommandGenerator &generator, bool merged,
                    bool rollBack) {
  Document &document = oracle.document;
  REQUIRE(document.begin("gesture").has_value());
  ChangeSet all;
  const std::uint64_t count = 1 + generator.below(8);
  for (std::uint64_t i = 0; i < count; ++i) {
    const auto changes = document.apply(oracle.next(generator));
    if (!changes.has_value()) {
      continue;
    }
    if (merged) {
      all.merge(*changes);
    } else {
      oracle.follow(*changes);
    }
  }
  if (rollBack) {
    const auto undone = document.rollback();
    REQUIRE(undone.has_value());
    if (merged) {
      all.merge(*undone);
    } else {
      oracle.follow(*undone);
    }
    ++oracle.tally->rolledBack;
  } else {
    REQUIRE(document.commit().has_value());
    ++oracle.tally->transactions;
  }
  if (merged) {
    oracle.follow(all);
  }
}

/// One step of a sequence; returns what it was, for the failure message.
std::string runStep(Oracle &oracle, CommandGenerator &generator) {
  Document &document = oracle.document;
  const std::uint64_t choice = generator.below(20);
  if (choice < 9) {
    const auto changes = document.perform("edit", oracle.next(generator));
    if (changes.has_value()) {
      oracle.follow(*changes);
    }
    return "one command";
  }
  if (choice < 15) {
    const bool merged = generator.below(2) == 0;
    const bool rollBack = choice >= 13;
    runTransaction(oracle, generator, merged, rollBack);
    return std::string(rollBack ? "rollback" : "transaction") +
           (merged ? ", merged" : ", step by step");
  }
  const bool undo = choice < 18;
  if (undo ? document.canUndo() : document.canRedo()) {
    const auto changes = undo ? document.undo() : document.redo();
    REQUIRE(changes.has_value());
    oracle.follow(*changes);
    ++(undo ? oracle.tally->undone : oracle.tally->redone);
  }
  return undo ? "undo" : "redo";
}

/// Walks one generated sequence, comparing after every step, and at the end
/// undoes everything: the edit must come back to the empty one through the
/// change sets a user's undo produces.
void runSequence(const Environment &environment, Tally &tally,
                 std::uint64_t seed, int steps, int renderEvery,
                 Project start = makeEmptyProject()) {
  Oracle oracle(environment, seed, tally, std::move(start));
  CommandGenerator generator{seed, environment.options()};
  for (int step = 0; step < steps; ++step) {
    const std::string what = "seed " + std::to_string(seed) + " step " +
                             std::to_string(step) + " (" +
                             runStep(oracle, generator) + ")";
    oracle.compare(what);
    if (renderEvery > 0 && step % renderEvery == renderEvery - 1) {
      // Every second render is of the document as the sequence left it;
      // the others are made certain to have something to hear.
      if (step % (2 * renderEvery) == renderEvery - 1) {
        addWitness(oracle, TickPosition{0}, lina::model::testing::beats(2));
        oracle.compare(what + ", witness added");
      }
      oracle.compareAudio(what);
    }
  }
  while (oracle.document.canUndo()) {
    const auto changes = oracle.document.undo();
    REQUIRE(changes.has_value());
    oracle.follow(*changes);
    ++tally.undone;
  }
  oracle.compare("seed " + std::to_string(seed) + ", undoing everything");
}

/// The frame from which a stream that was edited while it played, up to
/// `lastEdit`, must be the document's own: every note of the final document
/// that is sounding at the last edit has ended (it may have been started
/// late, or not at all, when an edit put it under the playhead), and the
/// ramps and block-long fades of the last edit are through.
std::int64_t settledFrame(Engine &engine, std::int64_t lastEdit, int block) {
  const auto &projection = internal::TracktionAccess::projection(engine);
  constexpr std::int64_t rampFrames = 40;
  std::int64_t settled = lastEdit + block + rampFrames + 1;
  for (const auto &[id, entry] : projection.clips()) {
    if (!std::holds_alternative<internal::MidiClipContent>(entry.content)) {
      continue;
    }
    std::array<std::int64_t, 128> startedOn{};
    for (const internal::NoteEvent &event :
         projection.eventsOf(entry)->events) {
      if (event.isNoteOn()) {
        startedOn.at(event.pitch) = event.frame;
      } else if (startedOn.at(event.pitch) < lastEdit + block &&
                 event.frame >= lastEdit) {
        settled = std::max(settled, event.frame + 1);
      }
    }
  }
  return settled;
}

/// A sequence whose edits are made while the engine plays. After every step
/// the edit must read as one built from scratch, as in the other sequences.
/// When the last edit is made, the stream goes on for a while; from the
/// frame where nothing of the editing can still be heard, it must be the
/// scratch edit's stream, bit for bit. In particular no note sounds past
/// its end.
void runLiveSequence(const Environment &environment, Tally &tally,
                     std::uint64_t seed, int steps) {
  Oracle oracle(environment, seed, tally, audibleProject(0));
  oracle.keepTempo = true;
  const int block = blockSizes.at(seed % blockSizes.size());
  CommandGenerator generator{seed, environment.options(false)};
  Engine &live = oracle.incrementalEngine;
  live.openManualDevice({.sampleRate = sampleRate, .blockSize = block});
  live.play();
  AudioBuffer stream =
      live.process(1 + static_cast<std::int64_t>(generator.below(3'000)));
  for (int step = 0; step < steps; ++step) {
    const std::string what = "seed " + std::to_string(seed) + " live step " +
                             std::to_string(step) + " (" +
                             runStep(oracle, generator) + ")";
    oracle.compare(what);
    if (step + 1 < steps) {
      const auto more =
          live.process(1 + static_cast<std::int64_t>(generator.below(1'500)));
      for (std::size_t channel = 0; channel < 2; ++channel) {
        auto &samples = stream.channels.at(channel);
        samples.insert(samples.end(), more.channels.at(channel).begin(),
                       more.channels.at(channel).end());
      }
    }
  }
  // The last edit puts a note of eight seconds a second ahead of the
  // playhead, so that there is something to hear when everything older has
  // ended. The tempo is
  // 120 beats per minute, which nothing in a live sequence changes: a frame
  // at 8 kHz is 3.78 ticks.
  const std::int64_t witnessFrame =
      static_cast<std::int64_t>(stream.frameCount()) + 8'000;
  addWitness(oracle, TickPosition{(witnessFrame * 378) / 100},
             lina::model::testing::beats(16));
  oracle.compare("seed " + std::to_string(seed) + ", witness added");
  const auto lastEdit = static_cast<std::int64_t>(stream.frameCount());
  const std::int64_t total = lastEdit + std::int64_t{80'000};
  const auto tail = live.process(total - lastEdit);
  const auto fromScratch = oracle.scratchEngine.renderOffline({
      .format = {.sampleRate = sampleRate, .blockSize = block},
      .startFrame = 0,
      .frameCount = total,
  });
  const std::int64_t settled =
      settledFrame(oracle.scratchEngine, lastEdit, block);
  if (settled >= total) {
    return;
  }
  ++tally.liveCompared;
  const auto from = static_cast<std::size_t>(settled - lastEdit);
  bool audible = false;
  for (std::size_t channel = 0; channel < 2; ++channel) {
    const auto heard = std::span(tail.channels.at(channel)).subspan(from);
    const auto wanted = std::span(fromScratch.channels.at(channel))
                            .subspan(static_cast<std::size_t>(settled));
    audible = audible || firstSoundingFrame(wanted) != -1;
    // Tolerance: none.
    if (!std::ranges::equal(heard, wanted)) {
      FAIL("seed " << seed << ": after edits while playing up to frame "
                   << lastEdit << ", the stream from frame " << settled
                   << " is not the document's; largest difference "
                   << maxDifference(heard, wanted));
    }
  }
  tally.liveAudible += audible ? 1 : 0;
}

int soakFactor() {
  // NOLINTNEXTLINE(concurrency-mt-unsafe): read once, before any thread.
  const char *value = std::getenv("LINA_PROJECTION_SOAK");
  if (value == nullptr) {
    return 1;
  }
  // Anything that is not a number counts as one.
  constexpr long most = 1'000;
  return static_cast<int>(
      std::clamp(std::strtol(value, nullptr, 10), 1L, most));
}

void checkTally(const Tally &tally, int steps) {
  INFO("change sets " << tally.changeSets << ", undone " << tally.undone
                      << ", redone " << tally.redone << ", rolled back "
                      << tally.rolledBack << ", transactions "
                      << tally.transactions << ", most tracks "
                      << tally.mostTracks << ", most clips " << tally.mostClips
                      << ", renders " << tally.rendered << ", audible "
                      << tally.audibleRenders);
  // The sequences must reach every kind of step and build something worth
  // comparing, or the oracle proves little.
  CHECK(tally.changeSets > static_cast<std::size_t>(steps) / 2);
  CHECK(tally.undone > 0);
  CHECK(tally.redone > 0);
  CHECK(tally.rolledBack > 0);
  CHECK(tally.transactions > 0);
  CHECK(tally.mostTracks >= 4);
  CHECK(tally.mostClips >= 4);
}

} // namespace

TEST_CASE("an edit kept in step with generated commands reads the same as "
          "one built from scratch",
          "[projection][oracle]") {
  const Environment environment("projection-oracle-structure");
  Tally tally;
  constexpr int steps = 200;
  const int seeds = 6 * soakFactor();
  for (int seed = 1; seed <= seeds; ++seed) {
    runSequence(environment, tally, static_cast<std::uint64_t>(seed), steps, 0);
  }
  checkTally(tally, steps * seeds);
  CHECK(tally.compared == (static_cast<std::size_t>(steps) + 1) *
                              static_cast<std::size_t>(seeds));
  CHECK(environment.assertions.count() == 0);
}

TEST_CASE("an edit kept in step with generated commands renders the same "
          "audio as one built from scratch",
          "[projection][oracle]") {
  const Environment environment("projection-oracle-audio");
  Tally tally;
  constexpr int steps = 120;
  constexpr int renderEvery = 20;
  const int seeds = 3 * soakFactor();
  for (int seed = 101; seed < 101 + seeds; ++seed) {
    runSequence(environment, tally, static_cast<std::uint64_t>(seed), steps,
                renderEvery, audibleProject());
  }
  checkTally(tally, steps * seeds);
  CHECK(tally.rendered == static_cast<std::size_t>(steps / renderEvery) *
                              static_cast<std::size_t>(seeds));
  // A good part of the renders must have something to hear, and some of
  // those are of documents for which the test itself knows that.
  INFO("audible " << tally.audibleRenders << " of " << tally.rendered
                  << ", known to be audible " << tally.mustBeAudible);
  CHECK(tally.audibleRenders * 2 >= tally.rendered);
  CHECK(tally.mustBeAudible * 2 >= tally.rendered);
  CHECK(environment.assertions.count() == 0);
}

TEST_CASE("an edit kept in step renders the same audio as one built from "
          "scratch when the media files are at another rate than the device",
          "[projection][oracle]") {
  // The engine converts the files while it plays. That path is not exact,
  // but it is the same for the two edits.
  const Environment environment("projection-oracle-rates", 48'000.0);
  Tally tally;
  constexpr int steps = 40;
  constexpr int renderEvery = 10;
  for (const std::uint64_t seed : {201U, 202U}) {
    runSequence(environment, tally, seed, steps, renderEvery, audibleProject());
  }
  INFO("audible " << tally.audibleRenders << " of " << tally.rendered);
  CHECK(tally.rendered == 8);
  CHECK(tally.mustBeAudible >= 4);
  CHECK(tally.audibleRenders > 0);
  CHECK(environment.assertions.count() == 0);
}

TEST_CASE("after generated edits made while playing, the stream is the "
          "document's",
          "[projection][oracle]") {
  const Environment environment("projection-oracle-live");
  Tally tally;
  constexpr int steps = 40;
  const int seeds = 12 * soakFactor();
  for (int seed = 301; seed < 301 + seeds; ++seed) {
    runLiveSequence(environment, tally, static_cast<std::uint64_t>(seed),
                    steps);
  }
  INFO("compared " << tally.liveCompared << " of " << seeds
                   << " sequences, audible " << tally.liveAudible);
  CHECK(tally.changeSets > static_cast<std::size_t>(steps * seeds) / 2);
  // Every sequence ends with a note a second after its last edit, which
  // lies in what is compared unless an older note is still sounding then.
  CHECK(tally.liveCompared * 2 > static_cast<std::size_t>(seeds));
  CHECK(tally.liveAudible == tally.liveCompared);
  CHECK(environment.assertions.count() == 0);
}
