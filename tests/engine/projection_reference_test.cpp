// SPDX-License-Identifier: GPL-3.0-or-later
//
// Audio tier (docs/design/14-build-test-and-ci.md): a small project is built
// through commands, rendered through the projection and compared with a
// checked-in reference file.
//
// The reference is tests/audio-refs/projection-mix.wav: one second of stereo
// at 8 kHz in 32-bit floating point. To write it anew after a change that is
// meant to alter the audio, run this test with LINA_UPDATE_AUDIO_REFS=1 in
// the environment:
//
//     LINA_UPDATE_AUDIO_REFS=1
//     build/dev/tests/engine/lina_projection_reference_test
//
// That run rewrites the file and fails on purpose, so that it cannot be
// taken for a comparison; look at what changed in the file before
// committing it.

#include <catch2/catch_test_macros.hpp>

#include "projection_fixture.hpp"
#include "reference_signal.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <random>
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

constexpr double sampleRate = 8'000.0;
constexpr std::int64_t frameCount = 8'000;

/// Largest difference allowed against the reference file. The mix sums six
/// sources at most, each of which may differ by formulaTolerance when a
/// compiler or a maths library evaluates a cosine or a power differently.
constexpr float referenceTolerance = 6.0F * formulaTolerance;

/// A second of quiet seeded noise at the rate of the render, so that the
/// engine plays it sample for sample.
AudioBuffer noise(unsigned seed) {
  std::mt19937 random(seed);
  AudioBuffer audio;
  audio.sampleRate = sampleRate;
  audio.channels.assign(2, std::vector<float>(8'000));
  for (auto &channel : audio.channels) {
    for (float &sample : channel) {
      sample = (static_cast<float>(random() % 2001) / 8000.0F) - 0.125F;
    }
  }
  return audio;
}

/// The project: every part of the mixer that is projected has a hand in the
/// result.
void buildMix(ProjectionFixture &fixture) {
  writeWav(fixture.scratch / "media" / "noise.wav", noise(2026));
  // 100 beats per minute at 8 kHz: 4800 frames to the quarter note.
  fixture.setTempo(10'000);
  const TrackId master = fixture.project().master.id;
  fixture.set(master, VolumeValue{-1.0});

  const TrackId effects = fixture.addTrack(TrackKind::Return);
  fixture.addDevice(effects, delayGainDevice(400, 0.6F));
  fixture.set(effects, PanValue{0.7});
  fixture.set(effects, VolumeValue{-2.0});

  const TrackId lead = fixture.addToneTrack();
  fixture.set(lead, VolumeValue{-9.0});
  fixture.set(lead, PanValue{-0.4});
  fixture.set(lead, SendValue{{.target = effects, .levelDb = -9.0}});
  Clip melody = makeMidiClip(
      TickPosition{ticksPerQuarter / 4}, beats(1),
      {
          makeNote(57, TickPosition{0}, TickDuration{5'000}, 110),
          makeNote(64, TickPosition{3'001}, TickDuration{4'000}, 90),
          makeNote(69, TickPosition{6'007}, TickDuration{2'500}, 70),
          makeNote(57, TickPosition{9'000}, TickDuration{9'000}, 100),
      });
  melody.loop = {
      .enabled = true,
      .start = TickPosition{0},
      .length = TickDuration{11'340},
  };
  fixture.addClip(lead, melody);

  const TrackId bass = fixture.addToneTrack();
  fixture.addDevice(bass, delayGainDevice(0, -0.5F));
  fixture.set(bass, VolumeValue{3.0});
  fixture.addClip(
      bass, makeMidiClip(beat(0), beats(2),
                         {makeNote(33, beat(0), TickDuration{20'000}, 127)}));

  const TrackId audio = fixture.addTrack(TrackKind::Audio);
  MediaReference file = makeMedia("media/noise.wav");
  file.sampleRate = 8'000;
  const MediaId media = fixture.add<ObjectKind::Media>(AddMedia{file});
  Clip texture =
      makeAudioClip(media, TickPosition{ticksPerQuarter / 2}, beats(1));
  auto &content = std::get<AudioContent>(texture.content);
  content.region = {.start = 100, .end = 8'000};
  content.warp.markers = {
      {.sample = 0, .tick = {}},
      {.sample = 4'800, .tick = beat(1)},
  };
  content.gainDb = -4.0;
  fixture.addClip(audio, texture);
  fixture.set(audio, PanValue{0.25});
  fixture.set(audio, SendValue{{.target = effects, .levelDb = -3.0}});

  // A muted track adds nothing.
  const TrackId muted = fixture.addToneTrack();
  fixture.addClip(muted, makeMidiClip(beat(0), beats(2),
                                      {makeNote(81, beat(0), beats(2), 127)}));
  fixture.set(muted, MuteValue{true});
}

} // namespace

TEST_CASE("a small mix renders to its reference file",
          "[projection][audio][reference]") {
  ProjectionFixture fixture("projection-reference");
  buildMix(fixture);
  const auto rendered = fixture.engine.renderOffline({
      .format = {.sampleRate = sampleRate, .blockSize = 256},
      .startFrame = 0,
      .frameCount = frameCount,
  });
  const auto reference =
      std::filesystem::path(LINA_AUDIO_REFS_DIR) / "projection-mix.wav";

  // NOLINTNEXTLINE(concurrency-mt-unsafe): nothing writes the environment.
  if (std::getenv("LINA_UPDATE_AUDIO_REFS") != nullptr) {
    writeWav(reference, rendered);
    FAIL("wrote " << reference.string()
                  << "; run again without LINA_UPDATE_AUDIO_REFS to compare");
  }

  const AudioBuffer expected = readWav(reference);
  REQUIRE(expected.sampleRate == sampleRate);
  REQUIRE(expected.channels.size() == 2);
  REQUIRE(expected.frameCount() == static_cast<std::size_t>(frameCount));
  // Tolerance: referenceTolerance, 6e-6 of full scale.
  CHECK(maxDifference(rendered.channels.at(0), expected.channels.at(0)) <=
        referenceTolerance);
  CHECK(maxDifference(rendered.channels.at(1), expected.channels.at(1)) <=
        referenceTolerance);
  // The reference is of this mix and not of silence. Its first frame holds
  // the bass note alone, at full velocity through a gain of -0.5, a volume
  // of +3 dB and the master's -1 dB; its two sides differ later on.
  CHECK(std::abs(expected.channels.at(0).at(0) -
                 static_cast<float>(-0.5 * std::pow(10.0, 2.0 / 20.0))) <=
        formulaTolerance);
  CHECK(expected.channels.at(1).at(0) == expected.channels.at(0).at(0));
  CHECK(maxDifference(expected.channels.at(0), expected.channels.at(1)) > 0.1F);
  CHECK(fixture.assertions.count() == 0);
}
