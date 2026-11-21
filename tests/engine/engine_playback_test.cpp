// SPDX-License-Identifier: GPL-3.0-or-later
//
// The engine driven block by block through its manual device: no audio
// device of the machine, no clock, no sleeping. Each test case asks for a
// number of frames and checks the samples it gets back.

#include <catch2/catch_test_macros.hpp>

#include "lina/engine/internal/tracktion_access.hpp"
#include "lina/engine/testing/engine_fixture.hpp"
#include "reference_signal.hpp"
#include "two_note_song.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <vector>

using namespace lina::engine;
using namespace lina::engine::testing;

namespace {

/// Plays the two-note song from the start on a fresh engine and returns the
/// audio.
AudioBuffer playTwoNoteSongOnce(const std::filesystem::path &storage,
                                int blockSize) {
  Engine engine({.storageFolder = storage});
  registerTestPlugins(engine);
  addTwoNoteSong(engine);
  engine.openManualDevice(
      {.sampleRate = songSampleRate, .blockSize = blockSize});
  engine.play();
  return engine.process(songFrameCount);
}

bool isSilent(const AudioBuffer &audio) {
  return std::ranges::all_of(audio.channels, [](const auto &channel) {
    return firstSoundingFrame(channel) == -1;
  });
}

} // namespace

TEST_CASE("an engine keeps to its storage folder and opens no device",
          "[engine][playback]") {
  EngineFixture fixture("engine-storage");
  auto &engine = internal::TracktionAccess::engine(fixture.engine);
  const auto storage = fixture.scratch / "engine";

  CHECK(std::filesystem::is_directory(storage));
  CHECK(engine.getDeviceManager().deviceManager.getCurrentAudioDevice() ==
        nullptr);
  CHECK(engine.getDeviceManager()
            .deviceManager.getAvailableDeviceTypes()
            .isEmpty());
  CHECK(engine.getTemporaryFileManager().getTempDirectory().isAChildOf(
      juce::File(storage.string())));
  CHECK(engine.getPropertyStorage().getAppPrefsFolder() ==
        juce::File(storage.string()));
  CHECK(&internal::TracktionAccess::edit(fixture.engine).engine == &engine);
  CHECK_FALSE(fixture.engine.hasManualDevice());

  fixture.engine.openManualDevice({.sampleRate = 44100.0, .blockSize = 256});
  CHECK(fixture.engine.hasManualDevice());
  auto *device =
      engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
  REQUIRE(device != nullptr);
  CHECK(device->getTypeName() == "Hosted Device");
  CHECK(device->getCurrentSampleRate() == 44100.0);
  CHECK(device->getCurrentBufferSizeSamples() == 256);

  fixture.engine.closeManualDevice();
  CHECK_FALSE(fixture.engine.hasManualDevice());
  CHECK(engine.getDeviceManager().deviceManager.getCurrentAudioDevice() ==
        nullptr);
  // Closing again changes nothing.
  fixture.engine.closeManualDevice();
  CHECK_FALSE(fixture.engine.hasManualDevice());
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a MIDI clip through the test instrument plays the expected samples "
          "at the expected frames",
          "[engine][playback]") {
  EngineFixture fixture("playback-expected-samples");
  addTwoNoteSong(fixture.engine);
  fixture.engine.openManualDevice(
      {.sampleRate = songSampleRate, .blockSize = 512});
  fixture.engine.play();
  const auto audio = fixture.engine.process(songFrameCount);

  const auto notes = twoNoteSongNotes();
  const auto expected = referenceSignal(songFrameCount, songSampleRate, notes);
  CHECK(audio.sampleRate == songSampleRate);
  REQUIRE(audio.channels.size() == 2);
  REQUIRE(audio.frameCount() == static_cast<std::size_t>(songFrameCount));
  // Tolerance: formulaTolerance, 1e-6 of full scale.
  CHECK(maxDifference(audio.channels.at(0), expected) <= formulaTolerance);
  CHECK(maxDifference(audio.channels.at(1), expected) <= formulaTolerance);

  // The edges themselves, which a tolerance cannot blur: silence is exact
  // zero, and a note starts at full level on its frame.
  const auto &left = audio.channels.at(0);
  CHECK(firstSoundingFrame(left) == 24000);
  CHECK(left.at(24000) == 1.0F);
  CHECK(left.at(static_cast<std::size_t>(notes.at(0).offFrame) - 1) != 0.0F);
  CHECK(firstSoundingFrame(std::span(left).subspan(static_cast<std::size_t>(
            notes.at(0).offFrame))) == 48000 - notes.at(0).offFrame);
  CHECK(firstSoundingFrame(std::span(left).subspan(
            static_cast<std::size_t>(notes.at(1).offFrame))) == -1);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a note that falls between two frames starts on the nearest one",
          "[engine][playback]") {
  EngineFixture fixture("playback-between-frames");
  // 130 quarter notes per minute at 44.1 kHz: a beat is 20353.846 frames.
  const double sampleRate = 44100.0;
  const double framesPerBeat = sampleRate * 60.0 / 130.0;
  setTempo(fixture.engine, 130.0);
  auto &track = addTrack(fixture.engine);
  addToneInstrument(track);
  const std::array notes{
      Note{.pitch = 69, .startBeat = 1.0, .lengthBeats = 0.5, .velocity = 127},
      Note{
          .pitch = 60,
          .startBeat = 3.25,
          .lengthBeats = 0.4,
          .velocity = 100,
      },
  };
  addMidiClip(track, 0.0, 8.0, notes);

  // A note edge at `beat` quarter notes, in frames and not yet rounded.
  const auto frameAt = [&](double beat) { return beat * framesPerBeat; };
  // Note-offs come 0.0001 s early (see releaseFrame), here 4.41 frames.
  const double early = 0.0001 * sampleRate;
  const std::array sounding{
      SoundingNote{
          .pitch = 69,
          .velocity = 127,
          .onFrame = std::lround(frameAt(1.0)),
          .offFrame = std::lround(frameAt(1.5) - early),
      },
      SoundingNote{
          .pitch = 60,
          .velocity = 100,
          .onFrame = std::lround(frameAt(3.25)),
          .offFrame = std::lround(frameAt(3.65) - early),
      },
  };
  // 20353.846 and 30530.769 - 4.41; 66150.0 and 74291.538 - 4.41.
  REQUIRE(sounding.at(0).onFrame == 20354);
  REQUIRE(sounding.at(0).offFrame == 30526);
  REQUIRE(sounding.at(1).onFrame == 66150);
  REQUIRE(sounding.at(1).offFrame == 74287);
  const std::int64_t frameCount = 80000;
  const auto expected = referenceSignal(frameCount, sampleRate, sounding);

  // The same frames whatever the block size, 7 and 441 included, which
  // divide neither the positions nor each other.
  std::vector<AudioBuffer> runs;
  for (const int blockSize : {512, 441, 7}) {
    CAPTURE(blockSize);
    fixture.engine.openManualDevice(
        {.sampleRate = sampleRate, .blockSize = blockSize});
    fixture.engine.play();
    runs.push_back(fixture.engine.process(frameCount));
    const auto &audio = runs.back();
    // Against the formula. Tolerance: formulaTolerance, 1e-6 of full scale.
    CHECK(maxDifference(audio.channels.at(0), expected) <= formulaTolerance);
    CHECK(firstSoundingFrame(audio.channels.at(0)) == 20354);
    CHECK(audio.channels.at(0).at(20354) == 1.0F);
    // Against the first block size. Tolerance: none.
    CHECK(identical(audio, runs.front()));
  }
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("the output does not depend on the block size",
          "[engine][playback]") {
  EngineFixture fixture("playback-block-size");
  addTwoNoteSong(fixture.engine);

  std::vector<AudioBuffer> runs;
  for (const int blockSize : {512, 64, 441, 4096}) {
    fixture.engine.openManualDevice(
        {.sampleRate = songSampleRate, .blockSize = blockSize});
    fixture.engine.play();
    runs.push_back(fixture.engine.process(songFrameCount));
  }
  // The runs are of the song and not of silence.
  REQUIRE(firstSoundingFrame(runs.front().channels.at(0)) == 24000);
  REQUIRE(runs.front().channels.at(0).at(24000) == 1.0F);
  REQUIRE(runs.front().channels.at(0).at(48000) == 64.0F / 127.0F);
  // Tolerance: none. Every run is equal to the first bit for bit.
  for (const auto &run : runs) {
    CHECK(identical(run, runs.front()));
  }
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("two engines given the same edit produce identical samples",
          "[engine][playback]") {
  const AssertionCounter assertions;
  const auto scratch = enterScratchFolder("playback-repeatable");
  const MessageThread messageThread;

  const auto first = playTwoNoteSongOnce(scratch / "first", 512);
  const auto second = playTwoNoteSongOnce(scratch / "second", 512);

  REQUIRE(first.frameCount() == static_cast<std::size_t>(songFrameCount));
  CHECK(firstSoundingFrame(first.channels.at(0)) == 24000);
  CHECK(first.channels.at(0).at(24000) == 1.0F);
  // Tolerance: none. The two runs are equal bit for bit.
  CHECK(identical(first, second));
  CHECK(assertions.count() == 0);
}

TEST_CASE("worker threads do not change the samples", "[engine][playback]") {
  const AssertionCounter assertions;
  const auto scratch = enterScratchFolder("playback-threads");
  const MessageThread messageThread;

  // The engine gives the graph at most one worker thread per processor
  // beyond the first, whatever it is asked for. On a machine with a single
  // processor both runs below would be single-threaded and prove nothing.
  const int wanted = 3;
  const int available = juce::SystemStats::getNumCpus() - 1;
  if (available < 1) {
    SKIP("this machine has one processor: the engine starts no worker "
         "threads, so there is nothing to compare");
  }

  struct Run {
    AudioBuffer audio;
    /// How many threads the engine tells its playback graph to compute
    /// with, the calling one included.
    int graphThreads = 0;
  };
  const auto play = [&](const char *folder, int audioThreads) {
    Engine engine(
        {.storageFolder = scratch / folder, .audioThreads = audioThreads});
    registerTestPlugins(engine);
    // Three tracks, so that there is something to hand to other threads.
    addTwoNoteSong(engine);
    auto &second = addTwoNoteSong(engine);
    addDelayGainEffect(second, 37, 0.25F);
    auto &third = addTwoNoteSong(engine);
    addDelayGainEffect(third, 4800, -0.5F);
    engine.openManualDevice({.sampleRate = songSampleRate, .blockSize = 256});
    engine.play();
    Run run;
    // Asked of the engine, not counted in the process: threads of an engine
    // that has just gone can still be seen there for a moment.
    run.graphThreads = internal::TracktionAccess::engine(engine)
                           .getEngineBehaviour()
                           .getNumberOfCPUsToUseForAudio();
    run.audio = engine.process(songFrameCount);
    return run;
  };

  const auto single = play("single", 1);
  const auto pooled = play("pooled", wanted + 1);
  // The second run's graph was given worker threads and the first one's
  // none: the playback context takes this number less one as its pool.
  CHECK(single.graphThreads == 1);
  CHECK(pooled.graphThreads == wanted + 1);
  CHECK(firstSoundingFrame(single.audio.channels.at(0)) == 24000);
  CHECK(single.audio.channels.at(0).at(24000) == 1.0F);
  // Tolerance: none. Each track is computed alone and the mix is summed in
  // a fixed order, so the runs are equal bit for bit.
  CHECK(identical(single.audio, pooled.audio));
  CHECK(assertions.count() == 0);
}

TEST_CASE("the test effect delays and scales what the instrument plays",
          "[engine][playback]") {
  EngineFixture fixture("playback-effect");
  // Track 1: the song through a delay of 100 frames at half level.
  auto &delayed = addTwoNoteSong(fixture.engine);
  addDelayGainEffect(delayed, 100, 0.5F);
  // Track 2: one note of its own, inverted and not delayed.
  auto &inverted = addTrack(fixture.engine);
  addToneInstrument(inverted);
  addDelayGainEffect(inverted, 0, -1.0F);
  const std::array note{
      Note{.pitch = 57, .startBeat = 0.0, .lengthBeats = 0.5, .velocity = 90},
  };
  addMidiClip(inverted, 0.25, 1.0, note);

  fixture.engine.openManualDevice(
      {.sampleRate = songSampleRate, .blockSize = 512});
  fixture.engine.play();
  const auto audio = fixture.engine.process(songFrameCount);

  auto song = twoNoteSongNotes();
  std::vector<SoundingNote> sounding;
  for (auto &songNote : song) {
    songNote.onFrame += 100;
    songNote.offFrame += 100;
    songNote.gain = 0.5F;
    sounding.push_back(songNote);
  }
  sounding.push_back({
      .pitch = 57,
      .velocity = 90,
      .onFrame = 6000,
      .offFrame = releaseFrame(18000, songSampleRate),
      .gain = -1.0F,
  });
  const auto expected =
      referenceSignal(songFrameCount, songSampleRate, sounding);

  // Tolerance: formulaTolerance, 1e-6 of full scale.
  CHECK(maxDifference(audio.channels.at(0), expected) <= formulaTolerance);
  CHECK(maxDifference(audio.channels.at(1), expected) <= formulaTolerance);
  // The delayed track is silent for exactly 100 frames more.
  const auto &left = audio.channels.at(0);
  CHECK(firstSoundingFrame(left) == 6000);
  CHECK(firstSoundingFrame(std::span(left).subspan(18000)) == 24100 - 18000);
  CHECK(left.at(24100) == 0.5F);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("playback starts on the frame it is asked to start on",
          "[engine][playback]") {
  EngineFixture fixture("playback-start-frame");
  addTwoNoteSong(fixture.engine);
  fixture.engine.openManualDevice(
      {.sampleRate = songSampleRate, .blockSize = 512});

  // 20000 frames in, 4000 before the first note.
  fixture.engine.play(20000);
  CHECK(fixture.engine.isPlaying());
  const auto audio = fixture.engine.process(songFrameCount - 20000);

  const auto whole =
      referenceSignal(songFrameCount, songSampleRate, twoNoteSongNotes());
  const auto expected = std::span(whole).subspan(20000);
  // Tolerance: formulaTolerance, 1e-6 of full scale.
  CHECK(maxDifference(audio.channels.at(0), expected) <= formulaTolerance);
  CHECK(firstSoundingFrame(audio.channels.at(0)) == 4000);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a stream is continuous across process calls of any length",
          "[engine][playback]") {
  EngineFixture fixture("playback-continuous");
  addTwoNoteSong(fixture.engine);
  fixture.engine.openManualDevice(
      {.sampleRate = songSampleRate, .blockSize = 512});

  fixture.engine.play();
  const auto whole = fixture.engine.process(songFrameCount);

  fixture.engine.play();
  std::vector<float> pieces;
  // Lengths that are not multiples of the block size, and an empty call.
  for (const std::int64_t length : {
           std::int64_t{1},
           std::int64_t{0},
           std::int64_t{777},
           std::int64_t{24000},
           songFrameCount - 24778,
       }) {
    const auto piece = fixture.engine.process(length);
    REQUIRE(piece.channels.size() == 2);
    REQUIRE(piece.frameCount() == static_cast<std::size_t>(length));
    pieces.insert(pieces.end(), piece.channels.at(0).begin(),
                  piece.channels.at(0).end());
  }
  // The stream is the song, and the pieces are the stream. Tolerance: none.
  CHECK(firstSoundingFrame(whole.channels.at(0)) == 24000);
  CHECK(whole.channels.at(0).at(24000) == 1.0F);
  CHECK(maxDifference(pieces, whole.channels.at(0)) == 0.0F);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("stopping silences the stream and playing again repeats it",
          "[engine][playback]") {
  EngineFixture fixture("playback-stop");
  addTwoNoteSong(fixture.engine);
  fixture.engine.openManualDevice(
      {.sampleRate = songSampleRate, .blockSize = 512});
  CHECK_FALSE(fixture.engine.isPlaying());
  // Not playing yet: the device runs and is silent.
  CHECK(isSilent(fixture.engine.process(1000)));

  fixture.engine.play();
  CHECK(fixture.engine.isPlaying());
  const auto first = fixture.engine.process(songFrameCount);

  // Stop in the middle of the first note.
  fixture.engine.play();
  const auto head = fixture.engine.process(30000);
  CHECK(head.channels.at(0).at(29999) != 0.0F);
  fixture.engine.stop();
  CHECK_FALSE(fixture.engine.isPlaying());
  CHECK(isSilent(fixture.engine.process(2000)));

  fixture.engine.play();
  const auto second = fixture.engine.process(songFrameCount);
  CHECK(firstSoundingFrame(first.channels.at(0)) == 24000);
  // Tolerance: none.
  CHECK(identical(first, second));
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("an edit changed between two plays is heard as changed",
          "[engine][playback]") {
  EngineFixture fixture("playback-edit-change");
  auto &track = addTwoNoteSong(fixture.engine);
  fixture.engine.openManualDevice(
      {.sampleRate = songSampleRate, .blockSize = 512});
  fixture.engine.play();
  const auto before = fixture.engine.process(songFrameCount);
  CHECK(firstSoundingFrame(before.channels.at(0)) == 24000);

  // A second clip, earlier than the first, and an effect on the track.
  const std::array note{
      Note{
          .pitch = 64,
          .startBeat = 0.0,
          .lengthBeats = 0.25,
          .velocity = 127,
      },
  };
  addMidiClip(track, 0.5, 0.5, note);
  addDelayGainEffect(track, 0, 0.5F);
  fixture.engine.play();
  const auto after = fixture.engine.process(songFrameCount);

  CHECK(firstSoundingFrame(after.channels.at(0)) == 12000);
  CHECK(after.channels.at(0).at(12000) == 0.5F);
  CHECK(after.channels.at(0).at(24000) == 0.5F);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("the engine rejects calls it cannot carry out",
          "[engine][playback]") {
  const AssertionCounter assertions;
  const auto scratch = enterScratchFolder("playback-preconditions");
  const MessageThread messageThread;
  CHECK(messageThread.isCurrentThread());

  CHECK_THROWS_AS(Engine(EngineConfig{}), std::invalid_argument);

  // A thread count below one means one.
  Engine engine({.storageFolder = scratch / "engine", .audioThreads = 0});
  CHECK(internal::TracktionAccess::engine(engine)
            .getEngineBehaviour()
            .getNumberOfCPUsToUseForAudio() == 1);

  CHECK_THROWS_AS(engine.play(), std::logic_error);
  CHECK_THROWS_AS(engine.syncGraph(), std::logic_error);
  CHECK_THROWS_AS((void)engine.process(16), std::logic_error);
  CHECK_THROWS_AS(
      engine.openManualDevice({.sampleRate = 0.0, .blockSize = 512}),
      std::invalid_argument);
  CHECK_THROWS_AS(
      engine.openManualDevice({.sampleRate = 48000.0, .blockSize = 0}),
      std::invalid_argument);
  CHECK_FALSE(engine.hasManualDevice());

  engine.openManualDevice({.sampleRate = 48000.0, .blockSize = 512});
  CHECK_THROWS_AS(engine.play(-1), std::invalid_argument);
  CHECK_THROWS_AS((void)engine.process(-1), std::invalid_argument);
  const auto nothing = engine.process(0);
  CHECK(nothing.channels.size() == 2);
  CHECK(nothing.frameCount() == 0);
  // A buffer without channels has no frames either.
  CHECK(AudioBuffer{}.frameCount() == 0);
  CHECK(assertions.count() == 0);
}

TEST_CASE("an engine cannot be made without a message thread",
          "[engine][playback]") {
  const AssertionCounter assertions;
  const auto scratch = enterScratchFolder("playback-no-message-thread");

  CHECK_THROWS_AS(Engine({.storageFolder = scratch / "engine"}),
                  std::logic_error);
  // Refused before anything was built or written.
  CHECK_FALSE(std::filesystem::exists(scratch / "engine"));
  CHECK(assertions.count() == 0);
}

TEST_CASE("an engine is refused on any thread but the message thread",
          "[engine][playback]") {
  const AssertionCounter assertions;
  const auto scratch = enterScratchFolder("playback-wrong-thread");
  const MessageThread messageThread;
  Engine engine({.storageFolder = scratch / "engine"});
  engine.openManualDevice({.sampleRate = 48000.0, .blockSize = 512});

  // Counts the calls that were refused with std::logic_error.
  int refused = 0;
  const auto expectRefusal = [&refused](const auto &call) {
    try {
      call();
    } catch (const std::logic_error &) {
      ++refused;
    }
  };
  bool otherThreadIsMessageThread = true;
  std::thread other([&] {
    otherThreadIsMessageThread = messageThread.isCurrentThread();
    RenderRequest request;
    request.frameCount = 16;
    expectRefusal(
        [&] { const Engine second({.storageFolder = scratch / "b"}); });
    expectRefusal([&] { engine.openManualDevice({}); });
    expectRefusal([&] { engine.closeManualDevice(); });
    expectRefusal([&] { engine.play(); });
    expectRefusal([&] { engine.stop(); });
    expectRefusal([&] { engine.syncGraph(); });
    expectRefusal([&] { (void)engine.process(16); });
    expectRefusal([&] { (void)engine.renderOffline(request); });
    expectRefusal([&] {
      std::ignore = engine.renderToWavFile(request, scratch / "out.wav");
    });
  });
  other.join();

  CHECK_FALSE(otherThreadIsMessageThread);
  CHECK(refused == 9);
  CHECK_FALSE(std::filesystem::exists(scratch / "b"));
  CHECK_FALSE(std::filesystem::exists(scratch / "out.wav"));
  // The engine is as it was.
  CHECK(engine.hasManualDevice());
  CHECK(assertions.count() == 0);
}

TEST_CASE("the graph follows an edit made while playing, without a break",
          "[engine][playback]") {
  EngineFixture fixture("playback-sync-graph");
  auto &track = addTwoNoteSong(fixture.engine);
  // A delay of a tenth of a second at unity gain. Whatever is in its line
  // when the graph is rebuilt has to come out afterwards.
  addDelayGainEffect(track, 4800, 1.0F);
  const std::int64_t frameCount = 80000;
  const std::int64_t syncFrame = 30000;
  fixture.engine.openManualDevice(
      {.sampleRate = songSampleRate, .blockSize = 512});

  // Play into the first note: it is held, and the delay line is full of it.
  fixture.engine.play();
  const auto head = fixture.engine.process(syncFrame);
  REQUIRE(head.channels.at(0).at(static_cast<std::size_t>(syncFrame) - 1) !=
          0.0F);

  // A note ahead of the playhead, at beat 2.5, which is frame 60000.
  const std::array note{
      Note{
          .pitch = 64,
          .startBeat = 0.0,
          .lengthBeats = 0.25,
          .velocity = 127,
      },
  };
  addMidiClip(track, 2.5, 0.5, note);
  fixture.engine.syncGraph();
  CHECK(fixture.engine.isPlaying());
  const auto tail = fixture.engine.process(frameCount - syncFrame);

  std::vector<float> stream = head.channels.at(0);
  stream.insert(stream.end(), tail.channels.at(0).begin(),
                tail.channels.at(0).end());

  // Everything 4800 frames late: the two notes of the song, unbroken across
  // the rebuild, and the new note on its exact frame.
  auto song = twoNoteSongNotes();
  std::vector<SoundingNote> sounding;
  for (auto &songNote : song) {
    songNote.onFrame += 4800;
    songNote.offFrame += 4800;
    sounding.push_back(songNote);
  }
  sounding.push_back({
      .pitch = 64,
      .velocity = 127,
      .onFrame = 60000 + 4800,
      .offFrame = releaseFrame(66000, songSampleRate) + 4800,
  });
  const auto expected = referenceSignal(frameCount, songSampleRate, sounding);
  // Tolerance: formulaTolerance, 1e-6 of full scale.
  CHECK(maxDifference(stream, expected) <= formulaTolerance);
  CHECK(stream.at(64800) == 1.0F);
  CHECK(stream.at(64799) == 0.0F);

  // And the stream is the one that playing the changed edit from the start
  // gives. Tolerance: none.
  fixture.engine.play();
  CHECK(maxDifference(
            stream, fixture.engine.process(frameCount).channels.at(0)) == 0.0F);
  CHECK(fixture.assertions.count() == 0);
}
