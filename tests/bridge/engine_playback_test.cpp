// SPDX-License-Identifier: GPL-3.0-or-later
//
// The interface's view of the engine: plays only with an output, keeps
// time by the frames the engine ran, and drives its silent output from
// Qt's event loop by a clock that the test owns. The only real time used is
// Qt's timer that wakes the pacing; what it computes depends on the test's
// clock alone.
#include <catch2/catch_test_macros.hpp>

#include "lina/bridge/engine_playback.hpp"
#include "lina/bridge/event_loop_bridge.hpp"
#include "lina/engine/engine.hpp"
#include "lina/engine/message_thread.hpp"
#include "lina/engine/testing/test_environment.hpp"
#include "lina/model/command.hpp"
#include "lina/model/document.hpp"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QTimer>

#include <array>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <stdexcept>

using lina::bridge::EnginePlayback;
using lina::bridge::frameOfTick;
using lina::bridge::tickOfFrame;
namespace model = lina::model;
using namespace std::chrono_literals;

namespace {

constexpr std::int64_t quarter = model::ticksPerQuarter;

model::Tempo tempo(std::int32_t hundredths) {
  return model::Tempo::fromHundredths(hundredths).value_or(model::Tempo{});
}

bool spinUntil(const std::function<bool()> &done) {
  const QDeadlineTimer deadline(20s);
  QTimer wake;
  wake.start(20ms);
  while (!done() && !deadline.hasExpired()) {
    QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents);
  }
  return done();
}

struct Environment {
  explicit Environment(std::string_view name)
      : scratch(lina::engine::testing::enterScratchFolder(name)) {}

  lina::engine::testing::AssertionCounter assertions;
  std::filesystem::path scratch;
  int argc = 1;
  std::array<char, 12> name{"bridge-test"};
  std::array<char *, 2> argv{name.data(), nullptr};
  QCoreApplication app{argc, argv.data()};
  lina::engine::MessageThread messageThread;
  lina::bridge::EventLoopBridge bridge;
};

} // namespace

TEST_CASE("ticks and frames convert as the engine converts them",
          "[bridge][playback]") {
  // One quarter note at 120 beats per minute is half a second.
  CHECK(frameOfTick({quarter}, tempo(12000), 48000) == 24000);
  CHECK(tickOfFrame(24000, tempo(12000), 48000) ==
        model::TickPosition{quarter});
  CHECK(frameOfTick({}, tempo(12000), 48000) == 0);
  // 1 tick at 120 bpm and 48 kHz is 1.5873 frames: the nearest is 2.
  CHECK(frameOfTick({1}, tempo(12000), 48000) == 2);
  // Exactly between two frames goes to the later one: 63 ticks at 30 bpm
  // and 44.1 kHz are 367.5 frames.
  CHECK(frameOfTick({63}, tempo(3000), 44100) == 368);
  // The largest position a document stores does not overflow.
  CHECK(frameOfTick({model::maxTicks}, tempo(2000), 192000) > 0);
  CHECK(tickOfFrame(frameOfTick({123456789}, tempo(9650), 44100), tempo(9650),
                    44100) == model::TickPosition{123456789});
}

TEST_CASE("an engine playback keeps time by the frames the engine ran",
          "[bridge][playback]") {
  const Environment environment("bridge-engine-playback");
  {
    lina::engine::Engine engine({.storageFolder = environment.scratch / "e"});
    EnginePlayback playback(engine);
    model::Document document;
    const model::Project &project = document.project();
    playback.setProject(project, environment.scratch);
    CHECK(engine.hasProject());

    // No output: nothing can play.
    CHECK(!playback.hasOutput());
    CHECK_THROWS_AS(playback.play(project, {}), std::logic_error);
    CHECK(playback.state(project) == lina::bridge::PlaybackState{});
    playback.closeOutput();

    std::chrono::nanoseconds now{0};
    CHECK_THROWS_AS(
        playback.openSilentOutput({.sampleRate = 44100.5, .blockSize = 512},
                                  [&now] { return now; }),
        std::invalid_argument);
    for (const double rate : {0.0, -48000.0, 1.0e8}) {
      CHECK_THROWS_AS(
          playback.openSilentOutput({.sampleRate = rate, .blockSize = 512},
                                    [&now] { return now; }),
          std::invalid_argument);
    }
    CHECK_THROWS_AS(playback.openSilentOutput(
                        {.sampleRate = 48000.0, .blockSize = 512}, nullptr),
                    std::invalid_argument);
    playback.openSilentOutput({.sampleRate = 48000.0, .blockSize = 512},
                              [&now] { return now; });
    CHECK(playback.hasOutput());
    CHECK(engine.hasManualDevice());

    // Ran by hand.
    playback.play(project, {quarter});
    CHECK(playback.state(project) ==
          lina::bridge::PlaybackState{.playing = true, .position = {quarter}});
    playback.advance(24000);
    playback.advance(0);
    CHECK(playback.state(project).position == model::TickPosition{2 * quarter});

    // Paced by the clock: 100 ms are 4800 frames, a fifth of a quarter note.
    now += 100ms;
    REQUIRE(spinUntil([&] {
      return playback.state(project).position ==
             model::TickPosition{(2 * quarter) + (quarter / 5)};
    }));

    // Far behind (the machine stalled): a quarter of a second is played and
    // the rest skipped, as a device drops out.
    now += 10s;
    REQUIRE(spinUntil([&] {
      return playback.state(project).position ==
             model::TickPosition{(2 * quarter) + (quarter / 5) + (quarter / 2)};
    }));
    // The clock stands still: so does the playhead.
    const auto stalled = playback.state(project).position;
    bool ticked = false;
    QTimer::singleShot(EnginePlayback::paceInterval * 5,
                       [&ticked] { ticked = true; });
    REQUIRE(spinUntil([&] { return ticked; }));
    CHECK(playback.state(project).position == stalled);

    // Edits reach the engine while it plays.
    const auto changes =
        document.perform("Add", model::AddTrack{
                                    .kind = model::TrackKind::Midi,
                                    .name = "Keys",
                                    .index = 0,
                                });
    REQUIRE(changes.has_value());
    playback.applyChanges(document.project(), *changes);
    CHECK(engine.hasProject());

    playback.stop();
    playback.stop();
    CHECK(!playback.state(project).playing);
    playback.advance(1000);
    CHECK(!engine.isPlaying());

    // A new output stops what plays and starts from where it is told.
    playback.play(project, {});
    playback.openSilentOutput({.sampleRate = 44100.0, .blockSize = 256},
                              [&now] { return now; });
    CHECK(!playback.state(project).playing);
    playback.play(project, {quarter});
    playback.advance(22050);
    CHECK(playback.state(project).position == model::TickPosition{2 * quarter});

    playback.closeOutput();
    CHECK(!playback.hasOutput());
    CHECK(!engine.hasManualDevice());

    // Destroyed with an output open: it is closed first.
    playback.openSilentOutput({.sampleRate = 48000.0, .blockSize = 512},
                              [&now] { return now; });
    playback.play(project, {});
  }
  CHECK(environment.assertions.count() == 0);
}
