// SPDX-License-Identifier: GPL-3.0-or-later
//
// The mixer of a document played through the projection: levels are measured
// in the rendered audio and compared with what the document says
// (docs/design/09-mixer-routing-and-devices.md).
//
// The measuring set-up: a track plays the test instrument at full velocity
// for 500 frames from frame 0, so the first frame of the stream carries the
// track's own gain. The track sends to a return track whose only device
// delays by 1000 frames, so frame 1000 carries the gain of the path through
// the send and the return track and nothing else.

#include <catch2/catch_test_macros.hpp>

#include "lina/engine/internal/engine_plugins.hpp"
#include "lina/engine/internal/mixing.hpp"
#include "lina/engine/internal/tracktion_access.hpp"
#include "projection_fixture.hpp"
#include "reference_signal.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>

using namespace lina::engine;
using namespace lina::engine::testing;
using namespace lina::model;
using lina::model::testing::beat;
using lina::model::testing::beats;
using lina::model::testing::makeMidiClip;
using lina::model::testing::makeNote;

namespace {

constexpr std::int64_t returnDelay = 1'000;
/// 500 frames at 120 beats per minute and 48 kHz.
constexpr TickDuration noteLength{315};

struct Side {
  float left{0.0F};
  float right{0.0F};

  friend bool operator==(const Side &, const Side &) = default;
};

/// What reaches the output directly and through the return track.
struct Levels {
  Side direct;
  Side returned;
};

constexpr Side unity{.left = 1.0F, .right = 1.0F};
constexpr Side nothing{};

struct MixerFixture : ProjectionFixture {
  explicit MixerFixture(std::string_view name) : ProjectionFixture(name) {
    track = addToneTrack();
    addClip(track, makeMidiClip(beat(0), beats(1),
                                {makeNote(69, beat(0), noteLength, 127)}));
    effects = addTrack(TrackKind::Return);
    addDevice(effects, delayGainDevice(returnDelay, 1.0F));
  }

  /// Plays the start of the arrangement and reads the two levels of the
  /// clip that starts on `clipFrame`.
  Levels measure(std::int64_t clipFrame = 0) {
    const auto audio = playFromStart(48'000.0, clipFrame + 3'000);
    const auto at = [&](std::int64_t frame) {
      return Side{
          .left = audio.channels.at(0).at(static_cast<std::size_t>(frame)),
          .right = audio.channels.at(1).at(static_cast<std::size_t>(frame)),
      };
    };
    return {.direct = at(clipFrame), .returned = at(clipFrame + returnDelay)};
  }

  void send(TrackId from, double levelDb) {
    set(from, SendValue{{.target = effects, .levelDb = levelDb}});
  }

  TrackId track;
  TrackId effects;
};

float gainOf(double db) {
  return static_cast<float>(std::pow(10.0, db / 20.0));
}

/// True when a measured level is the gain of `db` decibels to within one
/// part in a million.
bool isLevel(float measured, double db) {
  const double gain = std::pow(10.0, db / 20.0);
  return std::abs(measured - gain) <= gain * 1e-6;
}

} // namespace

TEST_CASE("with every control at its default a track plays at the level of "
          "its source and sends nothing",
          "[projection][mixer]") {
  MixerFixture fixture("projection-mixer-defaults");
  const Levels levels = fixture.measure();
  // Tolerance: none. Nothing in the path changes the signal.
  CHECK(levels.direct == unity);
  CHECK(levels.returned == nothing);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("the volume in decibels is the level that is heard",
          "[projection][mixer]") {
  MixerFixture fixture("projection-mixer-volume");
  for (const double db : {-6.0, 6.0, -12.5, 3.3, -40.0, -95.0}) {
    CAPTURE(db);
    fixture.set(fixture.track, VolumeValue{db});
    const Levels levels = fixture.measure();
    // Tolerance: none against the gain in single precision; one part in a
    // million against 10^(dB/20).
    CHECK(levels.direct == Side{.left = gainOf(db), .right = gainOf(db)});
    CHECK(isLevel(levels.direct.left, db));
  }
  // The top of the range is twice the amplitude, near enough.
  fixture.set(fixture.track, VolumeValue{maxVolumeDb});
  CHECK(std::abs(fixture.measure().direct.left - 1.9952623F) < 1e-6F);
  // The bottom of the range is silence.
  fixture.set(fixture.track, VolumeValue{silenceDb});
  CHECK(fixture.measure().direct == nothing);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("pan keeps the centre at unity and the power constant",
          "[projection][mixer]") {
  MixerFixture fixture("projection-mixer-pan");
  const auto root2 = static_cast<float>(std::numbers::sqrt2);
  fixture.set(fixture.track, PanValue{-1.0});
  CHECK(fixture.measure().direct == Side{.left = root2, .right = 0.0F});
  fixture.set(fixture.track, PanValue{1.0});
  CHECK(fixture.measure().direct == Side{.left = 0.0F, .right = root2});
  for (const double pan : {-0.75, -0.3, 0.1, 0.5, 0.9}) {
    CAPTURE(pan);
    fixture.set(fixture.track, PanValue{pan});
    const Side level = fixture.measure().direct;
    const double angle = (pan + 1.0) * std::numbers::pi / 4.0;
    // Tolerance: 1e-6.
    CHECK(std::abs(level.left - (std::numbers::sqrt2 * std::cos(angle))) <
          1e-6);
    CHECK(std::abs(level.right - (std::numbers::sqrt2 * std::sin(angle))) <
          1e-6);
    CHECK(std::abs((level.left * level.left) + (level.right * level.right) -
                   2.0F) < 1e-6F);
  }
  // Volume and pan multiply.
  fixture.set(fixture.track, PanValue{-1.0});
  fixture.set(fixture.track, VolumeValue{-6.0});
  CHECK(fixture.measure().direct ==
        Side{
            .left =
                static_cast<float>(std::pow(10.0, -0.3) * std::numbers::sqrt2),
            .right = 0.0F,
        });
  fixture.set(fixture.track, PanValue{0.0});
  fixture.set(fixture.track, VolumeValue{0.0});
  CHECK(fixture.measure().direct == unity);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a send feeds the return track at its level, after the track's "
          "volume and pan",
          "[projection][mixer]") {
  MixerFixture fixture("projection-mixer-send");
  for (const double db : {0.0, -6.0, -20.0, -60.0}) {
    CAPTURE(db);
    fixture.send(fixture.track, db);
    const Levels levels = fixture.measure();
    CHECK(levels.direct == unity);
    // Tolerance: one part in a million. The engine's send holds its level
    // as a fader position in single precision.
    CHECK(isLevel(levels.returned.left, db));
    CHECK(levels.returned.right == levels.returned.left);
  }
  // The send is taken behind volume and pan.
  fixture.send(fixture.track, -6.0);
  fixture.set(fixture.track, VolumeValue{-12.0});
  fixture.set(fixture.track, PanValue{-1.0});
  const Levels panned = fixture.measure();
  CHECK(isLevel(panned.returned.left / static_cast<float>(std::numbers::sqrt2),
                -18.0));
  CHECK(panned.returned.right == 0.0F);
  // A send at the bottom of its range is silent.
  fixture.send(fixture.track, silenceDb);
  CHECK(fixture.measure().returned == nothing);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a return track has volume, pan and mute of its own",
          "[projection][mixer]") {
  MixerFixture fixture("projection-mixer-return");
  fixture.send(fixture.track, 0.0);
  fixture.set(fixture.effects, VolumeValue{-6.0});
  Levels levels = fixture.measure();
  CHECK(levels.direct == unity);
  CHECK(isLevel(levels.returned.left, -6.0));
  CHECK(isLevel(levels.returned.right, -6.0));

  fixture.set(fixture.effects, PanValue{1.0});
  levels = fixture.measure();
  CHECK(levels.returned.left == 0.0F);
  CHECK(isLevel(levels.returned.right / static_cast<float>(std::numbers::sqrt2),
                -6.0));

  fixture.set(fixture.effects, MuteValue{true});
  levels = fixture.measure();
  CHECK(levels.direct == unity);
  CHECK(levels.returned == nothing);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a second return track gets a send of its own",
          "[projection][mixer]") {
  MixerFixture fixture("projection-mixer-two-returns");
  // The second return track delays by 2000 frames, so each can be told
  // apart.
  const TrackId second = fixture.addTrack(TrackKind::Return);
  fixture.addDevice(second, delayGainDevice(2 * returnDelay, 1.0F));
  fixture.send(fixture.track, -6.0);
  fixture.set(fixture.track, SendValue{{.target = second, .levelDb = -12.0}});
  const auto audio = fixture.playFromStart(48'000.0, 3'000);
  const auto &left = audio.channels.at(0);
  CHECK(left.at(0) == 1.0F);
  CHECK(isLevel(left.at(1'000), -6.0));
  CHECK(isLevel(left.at(2'000), -12.0));

  // Reordering the return tracks changes nothing that is heard.
  fixture.edit(MoveTrack{.track = second, .index = 0});
  const auto reordered = fixture.playFromStart(48'000.0, 3'000);
  CHECK(identical(reordered, audio));

  // Removing the first return track leaves the second and its send.
  fixture.edit(RemoveTrack{fixture.effects});
  const auto one = fixture.playFromStart(48'000.0, 3'000);
  CHECK(one.channels.at(0).at(0) == 1.0F);
  CHECK(one.channels.at(0).at(1'000) == 0.0F);
  CHECK(isLevel(one.channels.at(0).at(2'000), -12.0));
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("mute silences a track and its sends", "[projection][mixer]") {
  MixerFixture fixture("projection-mixer-mute");
  fixture.send(fixture.track, 0.0);
  fixture.set(fixture.track, MuteValue{true});
  Levels levels = fixture.measure();
  CHECK(levels.direct == nothing);
  CHECK(levels.returned == nothing);
  fixture.set(fixture.track, MuteValue{false});
  levels = fixture.measure();
  CHECK(levels.direct == unity);
  CHECK(levels.returned == unity);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("solo silences the other tracks and leaves the return tracks "
          "audible",
          "[projection][mixer]") {
  MixerFixture fixture("projection-mixer-solo");
  fixture.send(fixture.track, 0.0);
  // A second track whose clip starts on beat 1, frame 24000.
  const TrackId other = fixture.addToneTrack();
  fixture.addClip(other,
                  makeMidiClip(beat(1), beats(1),
                               {makeNote(60, beat(0), noteLength, 127)}));
  fixture.send(other, 0.0);
  const std::int64_t otherFrame = 24'000;

  SECTION("a soloed track is heard with its send effects; the others are "
          "silent, sends included") {
    fixture.set(fixture.track, SoloValue{true});
    Levels soloed = fixture.measure();
    CHECK(soloed.direct == unity);
    CHECK(soloed.returned == unity);
    Levels silenced = fixture.measure(otherFrame);
    CHECK(silenced.direct == nothing);
    CHECK(silenced.returned == nothing);

    // Solo on both: both are heard.
    fixture.set(other, SoloValue{true});
    silenced = fixture.measure(otherFrame);
    CHECK(silenced.direct == unity);
    CHECK(silenced.returned == unity);

    // A soloed track that is muted stays silent.
    fixture.set(fixture.track, MuteValue{true});
    soloed = fixture.measure();
    CHECK(soloed.direct == nothing);
    CHECK(soloed.returned == nothing);
  }
  SECTION("a soloed return track is heard alone, fed by every track's send") {
    const TrackId second = fixture.addTrack(TrackKind::Return);
    fixture.addDevice(second, delayGainDevice(2 * returnDelay, 1.0F));
    fixture.set(fixture.track, SendValue{{.target = second, .levelDb = 0.0}});
    fixture.set(fixture.effects, SoloValue{true});
    const auto audio = fixture.playFromStart(48'000.0, 30'000);
    const auto &left = audio.channels.at(0);
    // The tracks' direct signals are gone, the soloed return plays both
    // tracks, and the other return is silent.
    CHECK(left.at(0) == 0.0F);
    CHECK(left.at(1'000) == 1.0F);
    CHECK(left.at(2'000) == 0.0F);
    CHECK(left.at(24'000) == 0.0F);
    CHECK(left.at(25'000) == 1.0F);
  }
  SECTION("with a track and a return soloed, each kind follows its own") {
    fixture.set(fixture.track, SoloValue{true});
    fixture.set(fixture.effects, SoloValue{true});
    const Levels soloed = fixture.measure();
    CHECK(soloed.direct == unity);
    CHECK(soloed.returned == unity);
    const Levels silenced = fixture.measure(otherFrame);
    CHECK(silenced.direct == nothing);
    CHECK(silenced.returned == nothing);
  }
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("everything passes through the master's volume and devices",
          "[projection][mixer]") {
  MixerFixture fixture("projection-mixer-master");
  fixture.send(fixture.track, 0.0);
  const TrackId master = fixture.project().master.id;
  fixture.set(master, VolumeValue{-6.0});
  Levels levels = fixture.measure();
  CHECK(levels.direct == Side{.left = gainOf(-6.0), .right = gainOf(-6.0)});
  CHECK(isLevel(levels.returned.left, -6.0));

  // A device on the master: everything comes 300 frames later, inverted.
  fixture.set(master, VolumeValue{0.0});
  fixture.addDevice(master, delayGainDevice(300, -1.0F));
  const auto audio = fixture.playFromStart(48'000.0, 3'000);
  const auto &left = audio.channels.at(0);
  CHECK(firstSoundingFrame(left) == 300);
  CHECK(left.at(300) == -1.0F);
  CHECK(left.at(1'300) == -1.0F);

  fixture.set(master, VolumeValue{silenceDb});
  const auto silent = fixture.playFromStart(48'000.0, 3'000);
  CHECK(firstSoundingFrame(silent.channels.at(0)) == -1);
  CHECK(firstSoundingFrame(silent.channels.at(1)) == -1);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("devices of a track process its signal in chain order",
          "[projection][mixer]") {
  MixerFixture fixture("projection-mixer-devices");
  const auto &edit = internal::TracktionAccess::edit(fixture.engine);
  const auto &projection =
      internal::TracktionAccess::projection(fixture.engine);
  const DeviceId delay =
      fixture.addDevice(fixture.track, delayGainDevice(100, 0.5F));

  SECTION("an effect is heard, and a bypassed one is not") {
    Levels levels = fixture.measure();
    CHECK(levels.direct == nothing);
    const auto audio = fixture.playFromStart(48'000.0, 1'000);
    CHECK(firstSoundingFrame(audio.channels.at(0)) == 100);
    CHECK(audio.channels.at(0).at(100) == 0.5F);

    fixture.edit(
        SetDeviceProperty{.device = delay, .property = BypassedValue{true}});
    levels = fixture.measure();
    CHECK(levels.direct == unity);
    fixture.edit(
        SetDeviceProperty{.device = delay, .property = BypassedValue{false}});
    CHECK(fixture.measure().direct == nothing);
  }
  SECTION("a parameter the device stores is handed to the plugin") {
    // The gain parameter runs from -2 to 2: 0.25 is -1.
    fixture.edit(SetDeviceProperty{
        .device = delay,
        .property = ParameterSettingValue{{.id = "gain", .value = 0.25}},
    });
    auto audio = fixture.playFromStart(48'000.0, 1'000);
    CHECK(audio.channels.at(0).at(100) == -1.0F);
    // A parameter the plugin does not know is ignored.
    fixture.edit(SetDeviceProperty{
        .device = delay,
        .property = ParameterSettingValue{{.id = "unknown", .value = 0.9}},
    });
    // Forgetting a parameter puts it back to the plugin's default.
    fixture.edit(SetDeviceProperty{
        .device = delay,
        .property = ParameterSettingValue{{.id = "delay", .value = {}}},
    });
    audio = fixture.playFromStart(48'000.0, 1'000);
    CHECK(audio.channels.at(0).at(0) == -1.0F);
  }
  SECTION("a plugin the engine does not know passes its input through") {
    const DeviceId unknown = fixture.addDevice(
        fixture.track, lina::model::testing::makeEffect("no such plugin"));
    CHECK(projection.devices().at(unknown).binding == nullptr);
    CHECK(projection.devices().at(delay).binding != nullptr);
    const auto audio = fixture.playFromStart(48'000.0, 1'000);
    CHECK(firstSoundingFrame(audio.channels.at(0)) == 100);
    CHECK(audio.channels.at(0).at(100) == 0.5F);
  }
  SECTION("an instrument the engine does not know is silent") {
    const TrackId silentTrack = fixture.addTrack(TrackKind::Midi);
    fixture.addDevice(silentTrack,
                      lina::model::testing::makeInstrument("no such plugin"));
    fixture.addClip(silentTrack,
                    makeMidiClip(beat(0), beats(1),
                                 {makeNote(60, beat(0), noteLength, 127)}));
    fixture.set(fixture.track, MuteValue{true});
    const auto audio = fixture.playFromStart(48'000.0, 3'000);
    CHECK(firstSoundingFrame(audio.channels.at(0)) == -1);
  }
  SECTION("devices keep their order through moves, and their plugin when "
          "they change track") {
    const DeviceId second =
        fixture.addDevice(fixture.track, delayGainDevice(7, 2.0F));
    auto *track = projection.tracks().at(fixture.track).track.get();
    const auto chain = [&] {
      std::vector<tracktion::Plugin *> plugins;
      for (auto *plugin : track->pluginList.getPlugins()) {
        plugins.push_back(plugin);
      }
      return plugins;
    };
    auto *delayPlugin = projection.devices().at(delay).plugin.get();
    auto *secondPlugin = projection.devices().at(second).plugin.get();
    // The note guard, the instrument, the two effects, the strip, the send
    // and the output.
    REQUIRE(chain().size() == 7);
    CHECK(chain().at(0) == projection.tracks().at(fixture.track).guard.get());
    CHECK(chain().at(2) == delayPlugin);
    CHECK(chain().at(3) == secondPlugin);

    fixture.edit(
        MoveDevice{.device = second, .track = fixture.track, .index = 1});
    CHECK(chain().at(2) == secondPlugin);
    CHECK(chain().at(3) == delayPlugin);
    CHECK(projection.devices().at(second).plugin.get() == secondPlugin);

    // To the return track: the same plugin object, now in that chain.
    fixture.edit(
        MoveDevice{.device = second, .track = fixture.effects, .index = 0});
    CHECK(projection.devices().at(second).plugin.get() == secondPlugin);
    CHECK(chain().size() == 6);
    const auto *effects = projection.tracks().at(fixture.effects).track.get();
    CHECK(effects->pluginList.getPlugins().getObjectPointer(1) == secondPlugin);

    // Removing a device takes its plugin out of the edit.
    fixture.edit(RemoveDevice{delay});
    CHECK(chain().size() == 5);
    CHECK(!projection.devices().contains(delay));
    CHECK(tracktion::getAllPlugins(edit, false).contains(secondPlugin));
    CHECK(!tracktion::getAllPlugins(edit, false).contains(delayPlugin));
  }
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("the projection's own plugins say what they are, and the strip "
          "gain leaves a block without audio alone",
          "[projection][mixer]") {
  MixerFixture fixture("projection-mixer-plugins");
  const DeviceId unknown = fixture.addDevice(
      fixture.track, lina::model::testing::makeEffect("no such plugin"));
  const auto &projection =
      internal::TracktionAccess::projection(fixture.engine);
  auto &strip = *projection.tracks().at(fixture.track).strip;
  CHECK(strip.getName() == "LINA strip gain");
  CHECK(strip.getPluginType() == internal::StripGainPlugin::xmlTypeName);
  CHECK(strip.getSelectableDescription() == "LINA strip gain");
  auto &placeholder = *projection.devices().at(unknown).plugin;
  CHECK(placeholder.getName() == "LINA placeholder");
  CHECK(placeholder.getPluginType() ==
        internal::PlaceholderPlugin::xmlTypeName);
  CHECK(placeholder.getSelectableDescription() == "LINA placeholder");

  // The engine may call a plugin with no audio buffer at all.
  const tracktion::PluginRenderContext noAudio(
      nullptr, juce::AudioChannelSet::stereo(), 0, 64, nullptr, 0.0, {}, true,
      false, false, false);
  strip.applyToBuffer(noAudio);
  placeholder.applyToBuffer(noAudio);
  CHECK(fixture.measure().direct == unity);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a change of level is a short ramp, not a step",
          "[projection][mixer]") {
  ProjectionFixture fixture("projection-mixer-ramp");
  const TrackId track = fixture.addToneTrack();
  // A4 for two beats: at 48 kHz the tone is at a peak every 1200 frames...
  fixture.addClip(track, makeMidiClip(beat(0), beats(2),
                                      {makeNote(69, beat(0), beats(2), 127)}));
  fixture.engine.openManualDevice({.sampleRate = 48'000.0, .blockSize = 512});
  fixture.engine.play();
  const auto before = fixture.engine.process(4'800);
  fixture.set(track, VolumeValue{silenceDb});
  const auto after = fixture.engine.process(4'800);
  const auto tone = [](std::int64_t frame) {
    return toneSample({.pitch = 69, .velocity = 127}, frame, 48'000.0);
  };
  // Tolerance: formulaTolerance.
  CHECK(std::abs(before.channels.at(0).at(4'799) - tone(4'799)) <=
        formulaTolerance);
  // The gain falls in a straight line over 5 ms, 240 frames: the first
  // frame after the change is at 239/240, the middle one at a half, and
  // from the last on there is silence.
  const auto &left = after.channels.at(0);
  const auto gainAt = [&](std::size_t frame) {
    return left.at(frame) / tone(4'800 + static_cast<std::int64_t>(frame));
  };
  CHECK(std::abs(gainAt(0) - (239.0F / 240.0F)) < 1e-5F);
  CHECK(std::abs(gainAt(119) - 0.5F) < 1e-5F);
  CHECK(std::abs(gainAt(238) - (1.0F / 240.0F)) < 1e-5F);
  CHECK(firstSoundingFrame(std::span(left).subspan(239)) == -1);
  CHECK(fixture.assertions.count() == 0);
}
