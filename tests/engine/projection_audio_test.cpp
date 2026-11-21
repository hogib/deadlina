// SPDX-License-Identifier: GPL-3.0-or-later
//
// Audio clips of a document played through the projection: the samples of
// the file, at the frames the document puts them on.

#include <catch2/catch_test_macros.hpp>

#include "lina/engine/internal/tracktion_access.hpp"
#include "projection_fixture.hpp"
#include "reference_signal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <numbers>
#include <random>
#include <span>
#include <string>
#include <tuple>
#include <vector>

using namespace lina::engine;
using namespace lina::engine::testing;
using namespace lina::model;
using lina::model::testing::beat;
using lina::model::testing::beats;
using lina::model::testing::makeAudioClip;
using lina::model::testing::makeMedia;

namespace {

/// Seeded noise between -1 and 1: no two stretches of it are alike, so a
/// sample that is one frame off shows.
AudioBuffer noise(std::size_t channels, std::size_t frames, double sampleRate,
                  unsigned seed) {
  std::mt19937 random(seed);
  AudioBuffer audio;
  audio.sampleRate = sampleRate;
  audio.channels.assign(channels, std::vector<float>(frames));
  for (auto &channel : audio.channels) {
    for (float &sample : channel) {
      sample = (static_cast<float>(random() % 2001) / 1000.0F) - 1.0F;
    }
  }
  return audio;
}

/// A fixture with one audio track and a 48 kHz file of two seconds.
struct AudioFixture : ProjectionFixture {
  explicit AudioFixture(std::string_view name, std::size_t channels = 2)
      : ProjectionFixture(name), file(noise(channels, 96'000, 48'000.0, 3)) {
    writeWav(scratch / "media" / "noise.wav", file);
    media = add<ObjectKind::Media>(AddMedia{makeMedia("media/noise.wav")});
    track = addTrack(TrackKind::Audio);
  }

  AudioBuffer file;
  MediaId media;
  TrackId track;
};

/// True when `count` frames of `played` from `at` are the file's from
/// `from`, bit for bit.
bool playsFile(std::span<const float> played, std::int64_t at,
               std::span<const float> file, std::int64_t from,
               std::int64_t count) {
  return std::ranges::equal(played.subspan(static_cast<std::size_t>(at),
                                           static_cast<std::size_t>(count)),
                            file.subspan(static_cast<std::size_t>(from),
                                         static_cast<std::size_t>(count)));
}

bool silent(std::span<const float> played, std::int64_t from, std::int64_t to) {
  return firstSoundingFrame(
             played.subspan(static_cast<std::size_t>(from),
                            static_cast<std::size_t>(to - from))) == -1;
}

} // namespace

TEST_CASE("an audio clip plays the samples of its file on the frames of the "
          "document, without waiting for anything",
          "[projection][audio]") {
  AudioFixture fixture("projection-audio-exact");
  // One beat into the arrangement, for two beats: frames 24000 to 72000.
  fixture.addClip(fixture.track,
                  makeAudioClip(fixture.media, beat(1), beats(2)));
  // The file is mapped into memory by the time the change set has been
  // applied; the first block can play it.
  const auto &projection =
      internal::TracktionAccess::projection(fixture.engine);
  REQUIRE(projection.files().size() == 1);
  CHECK(std::filesystem::path(projection.files().begin()->first) ==
        fixture.scratch / "media" / "noise.wav");
  CHECK(projection.files().begin()->second.playable());
  CHECK(fixture.engine.mediaProblems().empty());

  const std::int64_t frameCount = 96'000;
  std::vector<AudioBuffer> runs;
  for (const int blockSize : {512, 7, 4'096}) {
    CAPTURE(blockSize);
    runs.push_back(fixture.playFromStart(48'000.0, frameCount, blockSize));
    for (std::size_t channel = 0; channel < 2; ++channel) {
      CAPTURE(channel);
      const auto &played = runs.back().channels.at(channel);
      const auto &file = fixture.file.channels.at(channel);
      CHECK(silent(played, 0, 24'000));
      // Tolerance: none. The file's samples come out bit for bit.
      CHECK(playsFile(played, 24'000, file, 0, 48'000));
      CHECK(silent(played, 72'000, frameCount));
    }
  }
  // A render is the same thing.
  const auto rendered = fixture.engine.renderOffline({
      .format = {.sampleRate = 48'000.0, .blockSize = 512},
      .startFrame = 0,
      .frameCount = frameCount,
  });
  CHECK(identical(rendered, runs.front()));
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a mono file plays equally on both sides", "[projection][audio]") {
  AudioFixture fixture("projection-audio-mono", 1);
  fixture.addClip(fixture.track,
                  makeAudioClip(fixture.media, beat(0), beats(1)));
  const auto audio = fixture.playFromStart(48'000.0, 30'000);
  const auto &file = fixture.file.channels.at(0);
  CHECK(playsFile(audio.channels.at(0), 0, file, 0, 24'000));
  CHECK(playsFile(audio.channels.at(1), 0, file, 0, 24'000));
  CHECK(silent(audio.channels.at(0), 24'000, 30'000));
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("an audio clip starts at its content start and is silent past its "
          "region",
          "[projection][audio]") {
  AudioFixture fixture("projection-audio-region");
  Clip clip = makeAudioClip(fixture.media, beat(1), beats(4));
  // Half a beat into the content: sample 12000 of the file. The region ends
  // at sample 60000, two beats later.
  clip.extent.contentStart = TickPosition{ticksPerQuarter / 2};
  std::get<AudioContent>(clip.content).region = {.start = 0, .end = 60'000};
  fixture.addClip(fixture.track, clip);
  const auto audio = fixture.playFromStart(48'000.0, 120'000);
  const auto &played = audio.channels.at(0);
  const auto &file = fixture.file.channels.at(0);
  CHECK(silent(played, 0, 24'000));
  CHECK(playsFile(played, 24'000, file, 12'000, 48'000));
  CHECK(silent(played, 72'000, 120'000));
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("an audio clip is placed on the nearest frame at an awkward tempo",
          "[projection][audio]") {
  AudioFixture fixture("projection-audio-awkward");
  // 133.33 beats per minute at 48 kHz: beat 3 is frame 64801.6 and beat 5
  // is frame 108002.7.
  fixture.setTempo(13'333);
  fixture.addClip(fixture.track,
                  makeAudioClip(fixture.media, beat(3), beats(2)));
  for (const int blockSize : {512, 7}) {
    CAPTURE(blockSize);
    const auto audio = fixture.playFromStart(48'000.0, 120'000, blockSize);
    const auto &played = audio.channels.at(1);
    CHECK(silent(played, 0, 64'802));
    // The file runs at its own speed, whatever the tempo.
    CHECK(playsFile(played, 64'802, fixture.file.channels.at(1), 0,
                    108'003 - 64'802));
    CHECK(silent(played, 108'003, 120'000));
  }
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("the gain of an audio clip is what is heard", "[projection][audio]") {
  AudioFixture fixture("projection-audio-gain");
  const ClipId clip = fixture.addClip(
      fixture.track, makeAudioClip(fixture.media, beat(0), beats(1)));
  const auto &file = fixture.file.channels.at(0);
  for (const double db : {-6.0, 12.0, 24.0, -40.5}) {
    CAPTURE(db);
    fixture.set(clip, GainValue{db});
    const auto audio = fixture.playFromStart(48'000.0, 24'000);
    const double gain = std::pow(10.0, db / 20.0);
    float worst = 0.0F;
    for (std::size_t frame = 0; frame < 24'000; ++frame) {
      worst =
          std::max(worst, std::abs(audio.channels.at(0).at(frame) -
                                   static_cast<float>(gain * file.at(frame))));
    }
    // Tolerance: 1e-6 of the gain. The engine computes the gain in single
    // precision.
    CHECK(worst <= static_cast<float>(gain * 1e-6));
  }
  // At the bottom of the range the clip is silent.
  fixture.set(clip, GainValue{silenceDb});
  const auto audio = fixture.playFromStart(48'000.0, 24'000);
  CHECK(silent(audio.channels.at(0), 0, 24'000));
  CHECK(silent(audio.channels.at(1), 0, 24'000));
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a looped audio clip repeats the stretch of file its loop names",
          "[projection][audio]") {
  AudioFixture fixture("projection-audio-loop");
  const auto &file = fixture.file.channels.at(0);

  SECTION("from inside the loop") {
    Clip clip = makeAudioClip(fixture.media, beat(1), beats(5));
    // The loop is content beats 1 to 3: samples 24000 to 72000. Playing
    // starts at content beat 2.
    clip.loop = {.enabled = true, .start = beat(1), .length = beats(2)};
    clip.extent.contentStart = beat(2);
    fixture.addClip(fixture.track, clip);
    const auto audio = fixture.playFromStart(48'000.0, 168'000);
    const auto &played = audio.channels.at(0);
    CHECK(silent(played, 0, 24'000));
    CHECK(playsFile(played, 24'000, file, 48'000, 24'000));
    CHECK(playsFile(played, 48'000, file, 24'000, 48'000));
    CHECK(playsFile(played, 96'000, file, 24'000, 48'000));
    CHECK(silent(played, 144'000, 168'000));
  }
  SECTION("from before the loop: the lead-in once, then the loop") {
    Clip clip = makeAudioClip(fixture.media, beat(0), beats(6));
    clip.loop = {.enabled = true, .start = beat(1), .length = beats(2)};
    fixture.addClip(fixture.track, clip);
    const auto &entry = internal::TracktionAccess::projection(fixture.engine)
                            .clips()
                            .begin()
                            ->second;
    CHECK(entry.parts.size() == 2);
    const auto audio = fixture.playFromStart(48'000.0, 168'000);
    const auto &played = audio.channels.at(0);
    CHECK(playsFile(played, 0, file, 0, 24'000));
    CHECK(playsFile(played, 24'000, file, 24'000, 48'000));
    CHECK(playsFile(played, 72'000, file, 24'000, 48'000));
    CHECK(playsFile(played, 120'000, file, 24'000, 24'000));
    CHECK(silent(played, 144'000, 168'000));
  }
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("an audio clip that asks to follow the tempo is played at the "
          "speed of its file all the same",
          "[projection][audio]") {
  // Following the tempo, stretching and transposing are not projected yet.
  // The defined fallback: such a clip plays exactly like one with the switch
  // off.
  AudioFixture fixture("projection-audio-follow");
  fixture.setTempo(9'000);
  Clip clip = makeAudioClip(fixture.media, beat(1), beats(2));
  auto &content = std::get<AudioContent>(clip.content);
  content.followTempo = false;
  const ClipId id = fixture.addClip(fixture.track, clip);
  const auto plain = fixture.playFromStart(48'000.0, 120'000);
  // 90 beats per minute: beat 1 is frame 32000.
  CHECK(playsFile(plain.channels.at(0), 32'000, fixture.file.channels.at(0), 0,
                  64'000));

  fixture.set(id, FollowTempoValue{true});
  fixture.set(id, StretchModeValue{StretchMode::Repitch});
  fixture.set(id, TransposeValue{700});
  const auto following = fixture.playFromStart(48'000.0, 120'000);
  // Tolerance: none.
  CHECK(identical(following, plain));
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("an audio clip whose file is missing is silent until its media is "
          "relinked",
          "[projection][audio]") {
  AudioFixture fixture("projection-audio-missing");
  const MediaId missing =
      fixture.add<ObjectKind::Media>(AddMedia{makeMedia("media/gone.wav")});
  fixture.addClip(fixture.track, makeAudioClip(missing, beat(0), beats(1)));
  const auto &projection =
      internal::TracktionAccess::projection(fixture.engine);
  REQUIRE(projection.files().size() == 1);
  CHECK(!projection.files().begin()->second.playable());
  using Reports = std::vector<MediaReport>;
  CHECK(fixture.engine.mediaProblems() ==
        Reports{{.media = missing,
                 .problem = MediaProblem::Missing,
                 .fileSampleRate = 0.0}});
  const auto before = fixture.playFromStart(48'000.0, 30'000);
  CHECK(silent(before.channels.at(0), 0, 30'000));

  // Relinked to the file that exists, by a path that is not relative.
  fixture.edit(RelinkMedia{
      .media = missing,
      .path = (fixture.scratch / "media" / "noise.wav").string(),
  });
  CHECK(projection.files().begin()->second.playable());
  CHECK(fixture.engine.mediaProblems().empty());
  const auto after = fixture.playFromStart(48'000.0, 30'000);
  CHECK(playsFile(after.channels.at(0), 0, fixture.file.channels.at(0), 0,
                  24'000));

  // And to another file with other samples.
  const AudioBuffer other = noise(2, 48'000, 48'000.0, 11);
  writeWav(fixture.scratch / "media" / "other.wav", other);
  fixture.edit(RelinkMedia{.media = missing, .path = "media/other.wav"});
  const auto relinked = fixture.playFromStart(48'000.0, 30'000);
  CHECK(playsFile(relinked.channels.at(0), 0, other.channels.at(0), 0, 24'000));
  REQUIRE(projection.files().size() == 1);
  CHECK(std::filesystem::path(projection.files().begin()->first).filename() ==
        "other.wav");

  // Relinking back to a file that is not there silences the clip again.
  fixture.edit(RelinkMedia{.media = missing, .path = "media/gone.wav"});
  CHECK(fixture.engine.mediaProblems().size() == 1);
  const auto gone = fixture.playFromStart(48'000.0, 30'000);
  CHECK(silent(gone.channels.at(0), 0, 30'000));
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a file at another sample rate is played at its own speed",
          "[projection][audio]") {
  // Converting sample rates is the engine's own doing for now; only what
  // the projection decides is checked: where the clip starts and how long
  // it lasts.
  AudioFixture fixture("projection-audio-rate");
  fixture.addClip(fixture.track,
                  makeAudioClip(fixture.media, beat(1), beats(8)));
  // On a 44.1 kHz device, beat 1 is frame 22050 and the two seconds of the
  // file end on frame 110250.
  const auto audio = fixture.playFromStart(44'100.0, 132'300);
  const auto &played = audio.channels.at(0);
  CHECK(silent(played, 0, 22'050));
  CHECK(firstSoundingFrame(played) - 22'050 < 8);
  CHECK(!silent(played, 110'000, 110'250));
  CHECK(silent(played, 110'258, 132'300));
  CHECK(fixture.assertions.count() == 0);
}

namespace {

/// Two seconds of a 1 kHz sine at half scale, in stereo.
AudioBuffer sine(int sampleRate) {
  AudioBuffer audio;
  audio.sampleRate = sampleRate;
  audio.channels.assign(
      2, std::vector<float>(static_cast<std::size_t>(2 * sampleRate)));
  for (auto &channel : audio.channels) {
    for (std::size_t frame = 0; frame < channel.size(); ++frame) {
      channel.at(frame) =
          0.5F *
          static_cast<float>(std::sin(2.0 * std::numbers::pi * 1000.0 *
                                      static_cast<double>(frame) / sampleRate));
    }
  }
  return audio;
}

/// What a stretch of a 1 kHz sine is besides a 1 kHz sine.
struct SineFit {
  double amplitude{0.0};
  /// The power of what is left after the best-fitting 1 kHz sine is taken
  /// away, in decibels relative to that sine.
  double residualDb{0.0};
  /// The fitted sine's phase, in frames.
  double delayFrames{0.0};
};

/// Least squares fit of a * sin + b * cos at 1 kHz over `count` frames from
/// `from`.
SineFit fitSine(std::span<const float> played, std::size_t from,
                std::size_t count, double sampleRate) {
  double ss = 0.0;
  double cc = 0.0;
  double sc = 0.0;
  double xs = 0.0;
  double xc = 0.0;
  const auto angle = [&](std::size_t frame) {
    return 2.0 * std::numbers::pi * 1000.0 * static_cast<double>(frame) /
           sampleRate;
  };
  for (std::size_t frame = from; frame < from + count; ++frame) {
    const double s = std::sin(angle(frame));
    const double c = std::cos(angle(frame));
    const double x = played.subspan(frame).front();
    ss += s * s;
    cc += c * c;
    sc += s * c;
    xs += x * s;
    xc += x * c;
  }
  const double determinant = (ss * cc) - (sc * sc);
  const double a = ((xs * cc) - (xc * sc)) / determinant;
  const double b = ((xc * ss) - (xs * sc)) / determinant;
  double residual = 0.0;
  for (std::size_t frame = from; frame < from + count; ++frame) {
    const double error =
        played.subspan(frame).front() -
        ((a * std::sin(angle(frame))) + (b * std::cos(angle(frame))));
    residual += error * error;
  }
  const double power = ((a * a) + (b * b)) / 2.0;
  return {
      .amplitude = std::sqrt((a * a) + (b * b)),
      .residualDb =
          10.0 *
          std::log10(((residual / static_cast<double>(count)) + 1e-30) / power),
      .delayFrames =
          -std::atan2(b, a) * sampleRate / (2.0 * std::numbers::pi * 1000.0),
  };
}

} // namespace

TEST_CASE("a file at another sample rate is converted without distortion",
          "[projection][audio]") {
  struct Case {
    int fileRate;
    double deviceRate;
  };
  for (const Case testCase : {
           Case{.fileRate = 44'100, .deviceRate = 48'000.0},
           Case{.fileRate = 48'000, .deviceRate = 44'100.0},
       }) {
    CAPTURE(testCase.fileRate, testCase.deviceRate);
    ProjectionFixture fixture("projection-audio-convert-" +
                              std::to_string(testCase.fileRate));
    writeWav(fixture.scratch / "media" / "sine.wav", sine(testCase.fileRate));
    MediaReference reference = makeMedia("media/sine.wav");
    reference.sampleRate = static_cast<std::uint32_t>(testCase.fileRate);
    const MediaId media = fixture.add<ObjectKind::Media>(AddMedia{reference});
    const TrackId track = fixture.addTrack(TrackKind::Audio);
    Clip clip = makeAudioClip(media, beat(1), beats(8));
    auto &content = std::get<AudioContent>(clip.content);
    content.region = {.start = 0, .end = std::int64_t{2} * testCase.fileRate};
    content.warp.markers = {
        {.sample = 0, .tick = {}},
        {.sample = std::int64_t{2} * testCase.fileRate, .tick = beat(4)},
    };
    fixture.addClip(track, clip);
    CHECK(fixture.engine.mediaProblems().empty());

    const auto deviceFrames = static_cast<std::int64_t>(testCase.deviceRate);
    std::vector<SineFit> fits;
    for (const int blockSize : {512, 64, 2'048}) {
      CAPTURE(blockSize);
      const auto audio = fixture.playFromStart(testCase.deviceRate,
                                               2 * deviceFrames, blockSize);
      // One second from well inside the clip, which starts half a second
      // in.
      const SineFit fit = fitSine(
          audio.channels.at(0), static_cast<std::size_t>(deviceFrames * 6 / 10),
          static_cast<std::size_t>(deviceFrames), testCase.deviceRate);
      fits.push_back(fit);
      CHECK(std::abs(fit.amplitude - 0.5) < 1e-4);
      // Tolerance: what is not the sine lies 120 dB below it (measured:
      // -146 to -149 dB; the engine's plain converter leaves -27 dB).
      CHECK(fit.residualDb < -120.0);
      // The sine in the file starts at phase zero on the clip's first
      // frame, half a second in. What comes out is that sine moved by a
      // fraction of a frame that depends on the block size (measured:
      // between -0.44 and +0.44 frames). Tolerance: three quarters of a
      // frame.
      const double expectedDelay =
          std::fmod(testCase.deviceRate / 2.0, testCase.deviceRate / 1000.0);
      const double period = testCase.deviceRate / 1000.0;
      double delay = std::fmod(fit.delayFrames - expectedDelay, period);
      delay = delay > period / 2.0 ? delay - period : delay;
      delay = delay < -period / 2.0 ? delay + period : delay;
      CHECK(std::abs(delay) < 0.75);
      // Nothing sounds before the clip's first frame.
      CHECK(silent(audio.channels.at(0), 0, deviceFrames / 2));
    }
  }
}

TEST_CASE("the sample rate found in a file counts, not the one its media "
          "reference states",
          "[projection][audio]") {
  AudioFixture fixture("projection-audio-stated-rate");
  // The file is at 48 kHz; this reference to it says 44.1 kHz.
  MediaReference wrong = makeMedia("media/noise.wav");
  wrong.sampleRate = 44'100;
  const MediaId media = fixture.add<ObjectKind::Media>(AddMedia{wrong});
  fixture.addClip(fixture.track, makeAudioClip(media, beat(1), beats(2)));
  using Reports = std::vector<MediaReport>;
  CHECK(fixture.engine.mediaProblems() ==
        Reports{{.media = media,
                 .problem = MediaProblem::SampleRateDiffers,
                 .fileSampleRate = 48'000.0}});
  // It plays as the 48 kHz file it is: sample for sample on a 48 kHz
  // device.
  const auto audio = fixture.playFromStart(48'000.0, 96'000);
  CHECK(playsFile(audio.channels.at(0), 24'000, fixture.file.channels.at(0), 0,
                  48'000));
  CHECK(silent(audio.channels.at(0), 72'000, 96'000));
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a file in a format the engine cannot map into memory is not "
          "played",
          "[projection][audio]") {
  AudioFixture fixture("projection-audio-flac");
  // A quarter of a second of a 1 kHz tone at 48 kHz, 16 bits, made with the
  // flac tool and checked in: the FLAC encoder that comes with the engine's
  // framework trips the undefined-behaviour sanitizer, so the test does not
  // encode.
  std::filesystem::copy_file(std::filesystem::path(LINA_AUDIO_REFS_DIR) /
                                 "tone.flac",
                             fixture.scratch / "media" / "noise.flac");
  const MediaId media =
      fixture.add<ObjectKind::Media>(AddMedia{makeMedia("media/noise.flac")});
  fixture.addClip(fixture.track, makeAudioClip(media, beat(0), beats(2)));
  using Reports = std::vector<MediaReport>;
  CHECK(fixture.engine.mediaProblems() ==
        Reports{{.media = media,
                 .problem = MediaProblem::UnsupportedFormat,
                 .fileSampleRate = 48'000.0}});
  for (const int blockSize : {512, 7}) {
    const auto audio = fixture.playFromStart(48'000.0, 30'000, blockSize);
    CHECK(silent(audio.channels.at(0), 0, 30'000));
    CHECK(silent(audio.channels.at(1), 0, 30'000));
  }
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("an audio file without a single frame is not played",
          "[projection][audio]") {
  AudioFixture fixture("projection-audio-empty");
  AudioBuffer nothing;
  nothing.sampleRate = 48'000.0;
  nothing.channels.assign(2, {});
  writeWav(fixture.scratch / "media" / "empty.wav", nothing);
  const MediaId media =
      fixture.add<ObjectKind::Media>(AddMedia{makeMedia("media/empty.wav")});
  fixture.addClip(fixture.track, makeAudioClip(media, beat(0), beats(1)));
  using Reports = std::vector<MediaReport>;
  CHECK(fixture.engine.mediaProblems() ==
        Reports{{.media = media,
                 .problem = MediaProblem::Unreadable,
                 .fileSampleRate = 0.0}});
  const auto audio = fixture.playFromStart(48'000.0, 30'000);
  CHECK(silent(audio.channels.at(0), 0, 30'000));
  CHECK(fixture.assertions.count() == 0);
}
