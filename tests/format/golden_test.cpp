// SPDX-License-Identifier: GPL-3.0-or-later
//
// Golden files (docs/design/14-build-test-and-ci.md): the sample projects of
// format version 1 load, and saving them again changes no byte. They are
// never edited once the version is released.
#include "support/documents.hpp"
#include "support/reader_invariant.hpp"
#include "support/scratch.hpp"

#include "lina/format/document_io.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <variant>
#include <vector>

// The fuzz target's entry point, compiled into this executable as well.
// NOLINTNEXTLINE(readability-identifier-naming): the name libFuzzer looks for
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,
                                      std::size_t size);

namespace lina::format::testing {

namespace {

std::vector<std::filesystem::path> goldenFiles() {
  std::vector<std::filesystem::path> files;
  for (const auto &entry :
       std::filesystem::directory_iterator{goldenFile("v1")}) {
    files.push_back(entry.path());
  }
  std::ranges::sort(files);
  return files;
}

model::Project loadGolden(std::string_view name) {
  const auto loaded = readDocument(readText(goldenFile(name)));
  if (!loaded.has_value()) {
    FAIL(describe(loaded.error()));
  }
  return loaded->document.project();
}

// What a project holds, counted.
struct Census {
  std::set<model::TrackKind> trackKinds;
  std::set<std::size_t> inputs;
  std::set<std::size_t> targets;
  std::set<model::ControllerKind> controllers;
  std::set<model::Monitoring> monitoring;
  std::size_t slotMidi{0};
  std::size_t slotAudio{0};
  std::size_t arrangedMidi{0};
  std::size_t arrangedAudio{0};
  std::size_t notes{0};
  std::size_t warpMarkers{0};
  std::size_t devices{0};
  std::size_t parameters{0};
  std::size_t states{0};
  std::size_t instruments{0};
  std::size_t bypassed{0};
  std::size_t sends{0};
  std::size_t automationPoints{0};
  std::size_t launchOverrides{0};
  std::size_t loopedClips{0};
  std::size_t emptySlots{0};
};

void countClip(Census &census, const model::Clip &clip, bool inSlot) {
  census.launchOverrides += clip.launchQuantisation.has_value() ? 1 : 0;
  census.loopedClips += clip.loop.enabled ? 1 : 0;
  if (const auto *midi = std::get_if<model::MidiContent>(&clip.content)) {
    (inSlot ? census.slotMidi : census.arrangedMidi) += 1;
    census.notes += midi->notes.size();
    for (const model::ControllerEnvelope &envelope : midi->controllers) {
      census.controllers.insert(envelope.kind);
    }
    return;
  }
  (inSlot ? census.slotAudio : census.arrangedAudio) += 1;
  census.warpMarkers +=
      std::get<model::AudioContent>(clip.content).warp.markers.size();
}

void countDevices(Census &census, const model::Track &track) {
  for (const model::Device &device : track.devices) {
    ++census.devices;
    census.parameters += device.parameters.size();
    census.states += device.stateRef.empty() ? 0 : 1;
    census.instruments += device.role == model::DeviceRole::Instrument ? 1 : 0;
    census.bypassed += device.bypassed ? 1 : 0;
  }
}

void countTrack(Census &census, const model::Track &track) {
  census.trackKinds.insert(track.kind);
  census.inputs.insert(track.mixer.input.index());
  census.monitoring.insert(track.mixer.monitoring);
  census.sends += track.mixer.sends.size();
  countDevices(census, track);
  for (const model::Slot &slot : track.slots) {
    if (slot.clip.has_value()) {
      countClip(census, *slot.clip, true);
    } else {
      ++census.emptySlots;
    }
  }
  for (const model::Clip &clip : track.arrangement) {
    countClip(census, clip, false);
  }
  for (const model::Envelope &envelope : track.automation) {
    census.targets.insert(envelope.target.index());
    census.automationPoints += envelope.points.size();
  }
}

Census takeCensus(const model::Project &project) {
  Census census;
  for (const model::Track &track : project.tracks) {
    countTrack(census, track);
  }
  for (const model::Track &track : project.returns) {
    countTrack(census, track);
  }
  countTrack(census, project.master);
  return census;
}

} // namespace

TEST_CASE("format/golden: every version 1 file loads and saves unchanged") {
  const auto files = goldenFiles();
  REQUIRE(files.size() >= 2);
  for (const std::filesystem::path &file : files) {
    CAPTURE(file.string());
    const std::string bytes = readText(file);
    REQUIRE(!bytes.empty());
    const auto loaded = readDocument(bytes);
    if (!loaded.has_value()) {
      FAIL(describe(loaded.error()));
    }
    REQUIRE(loaded->formatRead == 1);
    REQUIRE(loaded->writtenBy == testWriter);
    const auto saved = writeDocument(loaded->document, loaded->writtenBy);
    REQUIRE(saved.has_value());
    REQUIRE(saved->bytes() == bytes);
    REQUIRE(readOutcome(bytes) == ReadOutcome::Accepted);
  }
}

TEST_CASE("format/golden: the files hold the documents built in code") {
  REQUIRE(loadGolden("v1/minimal.lina") == minimalDocument().project());
  REQUIRE(loadGolden("v1/everything.lina") == everythingDocument().project());
}

TEST_CASE("format/golden: the small file reads as its text says") {
  const model::Project project = loadGolden("v1/minimal.lina");
  REQUIRE(project.nextId == 8);
  REQUIRE(project.tempo.hundredths() == 12'000);
  REQUIRE(project.timeSignature.numerator() == 4);
  REQUIRE(project.timeSignature.denominator() == 4);
  REQUIRE(!project.loop.enabled);
  REQUIRE(project.launchQuantisation == model::LaunchQuantisation::Bar1);
  REQUIRE(project.media.empty());
  REQUIRE(project.scenes.size() == 1);
  REQUIRE(project.scenes.front().name == "Intro");
  REQUIRE(project.returns.empty());
  REQUIRE(project.master.name == "Master");
  REQUIRE(project.tracks.size() == 1);
  const model::Track &keys = project.tracks.front();
  REQUIRE(keys.kind == model::TrackKind::Midi);
  REQUIRE(keys.name == "Keys");
  REQUIRE(std::holds_alternative<model::AllMidiInputs>(keys.mixer.input));
  REQUIRE(keys.slots.size() == 1);
  REQUIRE(keys.slots.front().scene == project.scenes.front().id);
  REQUIRE(keys.slots.front().clip.has_value());
  const model::Clip &clip = keys.slots.front().clip.value_or(model::Clip{});
  REQUIRE(clip.name == "two notes");
  REQUIRE(clip.extent.length.ticks == 4 * model::ticksPerQuarter);
  const auto &notes = std::get<model::MidiContent>(clip.content).notes;
  REQUIRE(notes.size() == 2);
  REQUIRE(notes.front().pitch == 60);
  REQUIRE(notes.front().velocity == 100);
  REQUIRE(notes.back().pitch == 64);
  REQUIRE(notes.back().start.ticks == model::ticksPerQuarter);
  REQUIRE(notes.back().length.ticks == 2 * model::ticksPerQuarter);
  REQUIRE(notes.back().velocity == 90);
}

TEST_CASE("format/golden: the large file exercises every feature") {
  const model::Project project = loadGolden("v1/everything.lina");
  REQUIRE(project.tempo.hundredths() == 9'347);
  REQUIRE(project.timeSignature.numerator() == 7);
  REQUIRE(project.timeSignature.denominator() == 8);
  REQUIRE(project.loop.enabled);
  REQUIRE(project.startMarker.ticks > 0);
  REQUIRE(project.launchQuantisation == model::LaunchQuantisation::Half);
  REQUIRE(project.media.size() == 2);
  REQUIRE(project.media.front().path.starts_with("media/"));
  REQUIRE(project.media.back().path.starts_with('/'));
  REQUIRE(project.scenes.size() == 3);
  REQUIRE(project.returns.size() == 2);

  const Census census = takeCensus(project);
  REQUIRE(census.trackKinds.size() == 4);
  REQUIRE(census.inputs.size() == std::variant_size_v<model::TrackInput>);
  REQUIRE(census.targets.size() ==
          std::variant_size_v<model::AutomationTarget>);
  REQUIRE(census.controllers.size() == 3);
  REQUIRE(census.monitoring.size() == 3);
  REQUIRE(census.slotMidi >= 1);
  REQUIRE(census.slotAudio >= 1);
  REQUIRE(census.arrangedMidi >= 2);
  REQUIRE(census.arrangedAudio >= 2);
  REQUIRE(census.emptySlots >= 1);
  REQUIRE(census.notes >= 8);
  REQUIRE(census.warpMarkers >= 7);
  REQUIRE(census.devices >= 5);
  REQUIRE(census.parameters >= 3);
  REQUIRE(census.states == 1);
  REQUIRE(census.instruments == 1);
  REQUIRE(census.bypassed == 1);
  REQUIRE(census.sends == 8);
  REQUIRE(census.automationPoints >= 10);
  REQUIRE(census.launchOverrides == 2);
  REQUIRE(census.loopedClips >= 1);
}

// The two tests below are the anchors that do not go through the writer: the
// expected lines and the expected values were typed by hand from the
// description of the document (support/documents.cpp), field by field. A
// member that the encoder and the decoder swapped in the same way would pass
// every round trip and fails here.
TEST_CASE("format/golden: the large file holds these lines, as written") {
  const std::string text = readText(goldenFile("v1/everything.lina"));
  // NOLINTBEGIN(bugprone-suspicious-missing-comma): long lines, in two pieces
  const std::vector<std::string_view> lines{
      "{",
      "  \"format\": 1,",
      R"(  "written_by": "LINA tests 1",)",
      "  \"next_id\": 50,",
      "  \"tempo\": 93.47,",
      "  \"time_signature\": [7, 8],",
      R"(  "loop": { "enabled": true, "start": 211680, "length": 105840 },)",
      "  \"start_marker\": 45360,",
      R"(  "launch_quantisation": "1/2",)",
      // Media, inside the folder and outside it.
      "      \"id\": 2,",
      R"(      "path": "media/kick-3f9a1c2e.wav",)",
      "      \"size\": 352844,",
      "      \"hash\": "
      "\"3f9a1c2e5d0b4a6f8e7c9d1b2a3f4e5d6c7b8a9f0e1d2c3b4a5f6e7d"
      "8c9b0a1f\",",
      "      \"sample_rate\": 44100",
      R"(      "path": "/home/someone/samples/snare.flac",)",
      "      \"size\": 5000000000,",
      "      \"sample_rate\": 96000",
      // Scenes, one of them with everything that needs escaping.
      R"(    { "id": 4, "name": "Intro" },)",
      "    { \"id\": 5, \"name\": \"Verse \\\"two\\\" \\\\ caf\xC3\xA9 "
      "\xE2\x9C\x93\\tend\\n\" },",
      R"(    { "id": 6, "name": "" })",
      // A track header and its mixer strip.
      "      \"id\": 9,",
      R"(      "kind": "audio",)",
      R"(      "name": "Drums",)",
      R"(      "colour": "#ff8000",)",
      "        \"volume_db\": -6.5,",
      "        \"pan\": -0.25,",
      "        \"armed\": true,",
      R"(        "monitoring": "on",)",
      R"(        "input": { "type": "audio", "channel": 2, "stereo": true },)",
      R"(          { "target": 7, "level_db": -12.0 },)",
      R"(          { "target": 8, "level_db": -0.1 })",
      // The other kinds of input.
      "        \"input\": { \"type\": \"midi_device\", \"device\": "
      "\"Keystation "
      "61\" },",
      R"(        "input": { "type": "all_midi" },)",
      R"(        "input": { "type": "none" },)",
      R"(        "monitoring": "off",)",
      R"(        "monitoring": "auto",)",
      // A slot and the audio clip in it.
      "          \"id\": 10,",
      "          \"scene\": 4,",
      "            \"id\": 30,",
      R"(            "name": "kick loop",)",
      R"(            "colour": "#00ff7f",)",
      "            \"position\": 0,",
      "            \"length\": 90720,",
      "            \"content_start\": 15120,",
      "            \"loop\": { \"enabled\": true, \"start\": 30240, "
      "\"length\": "
      "60480 },",
      R"(            "launch_quantisation": "none",)",
      R"(              "type": "audio",)",
      "              \"media\": 2,",
      R"(              "region": { "start": 441, "end": 352800 },)",
      "              \"gain_db\": -3.25,",
      "              \"follow_tempo\": true,",
      R"(              "stretch_mode": "repitch",)",
      "              \"transpose_cents\": -1200,",
      R"(                "origin": "detected",)",
      R"(                  { "sample": 176400, "tick": 60480 },)",
      R"(                  { "sample": 352800, "tick": 120960 })",
      // An empty slot.
      "          \"id\": 11,",
      "          \"scene\": 5,",
      "          \"clip\": null",
      // An audio clip on the arrangement.
      "          \"id\": 32,",
      R"(          "name": "snare hit",)",
      "          \"position\": 120960,",
      "          \"length\": 30240,",
      "          \"launch_quantisation\": null,",
      "            \"media\": 3,",
      "            \"gain_db\": 24.0,",
      "            \"follow_tempo\": false,",
      R"(              "origin": "manual",)",
      // Devices.
      "          \"id\": 25,",
      R"(          "role": "instrument",)",
      R"(            "format": "LV2",)",
      R"(            "identifier": "urn:lina:synth",)",
      R"(            "name": "Test Synth",)",
      R"(            "vendor": "LINA tests",)",
      R"(            "version": "1.2.3")",
      "          \"state\": "
      "\"b5ed020be883230d0772b9d60a4410672a2becbbd626afb1ac"
      "6a9fd5ea827a35.bin\",",
      R"(            { "id": "cutoff", "value": 0.1 },)",
      R"(            { "id": "resonance", "value": 0.3333333333333333 })",
      "          \"id\": 26,",
      R"(          "role": "audio_effect",)",
      "          \"state\": null,",
      "          \"bypassed\": true,",
      R"(            { "id": "threshold", "value": 1e-06 })",
      // A MIDI clip in a slot: notes and controller data.
      "            \"id\": 33,",
      R"(            "name": "chords",)",
      R"(            "launch_quantisation": "8_bars",)",
      R"(              "type": "midi",)",
      "                { \"id\": 34, \"start\": 0, \"length\": 30240, "
      "\"pitch\": 60, "
      "\"velocity\": 100 },",
      "                { \"id\": 37, \"start\": 60480, \"length\": 1, "
      "\"pitch\": 0, "
      "\"velocity\": 64 },",
      "                { \"id\": 38, \"start\": 60480, \"length\": 60480, "
      "\"pitch\": 127, \"velocity\": 64 }",
      R"(                  "kind": "pitch_bend",)",
      R"(                    { "time": 0, "value": -8192 },)",
      R"(                    { "time": 30240, "value": 8191 })",
      R"(                  "kind": "channel_pressure",)",
      R"(                  "kind": "control_change",)",
      "                  \"controller\": 64,",
      R"(                    { "time": 105840, "value": 127 })",
      // MIDI clips on the arrangement, the second ending on the last tick.
      R"(          "name": "bass line",)",
      "              { \"id\": 42, \"start\": 30240, \"length\": 30240, "
      "\"pitch\": "
      "43, \"velocity\": 100 }",
      R"(          "name": "far away",)",
      "          \"position\": 9007199254680512,",
      // Automation, one envelope for each kind of target.
      "          \"id\": 44,",
      R"(          "target": { "type": "mixer", "parameter": "volume" },)",
      R"(            { "time": 60480, "value": -6.0 },)",
      R"(            { "time": 9007199254740992, "value": 6.0 })",
      R"(          "target": { "type": "mixer", "parameter": "pan" },)",
      R"(          "target": { "type": "send", "target": 7 },)",
      R"(            { "time": 30240, "value": -24.5 })",
      "          \"target\": { \"type\": \"device_parameter\", \"device\": 25, "
      "\"parameter\": \"cutoff\" },",
      R"(            { "time": 241920, "value": 0.12345678901234566 })",
      // Return tracks and the master.
      R"(      "kind": "return",)",
      R"(      "name": "Reverb",)",
      R"(      "colour": "#000001",)",
      "  \"master\": {",
      "    \"id\": 1,",
      R"(    "kind": "master",)",
      "      \"volume_db\": -1.5,",
      R"(          { "time": 483840, "value": -96.0 })",
      "}",
  };
  // NOLINTEND(bugprone-suspicious-missing-comma)
  // Each line is in the file, whole: from the start of a line to its end.
  REQUIRE(text.starts_with("{\n  \"format\": 1,\n"));
  for (const std::string_view line : lines) {
    CAPTURE(line);
    REQUIRE(("\n" + text).find("\n" + std::string{line} + "\n") !=
            std::string::npos);
  }
  REQUIRE(text.ends_with("\n}\n"));
}

TEST_CASE("format/golden: the large file loads to these values") {
  using namespace lina::model; // NOLINT(google-build-using-namespace)
  const Project project = loadGolden("v1/everything.lina");
  constexpr std::int64_t beat = 15'120;

  REQUIRE(project.nextId == 50);
  REQUIRE(project.tempo.hundredths() == 9'347);
  REQUIRE(project.timeSignature.numerator() == 7);
  REQUIRE(project.timeSignature.denominator() == 8);
  REQUIRE(project.loop.enabled);
  REQUIRE(project.loop.start.ticks == 14 * beat);
  REQUIRE(project.loop.length.ticks == 7 * beat);
  REQUIRE(project.startMarker.ticks == 3 * beat);
  REQUIRE(project.launchQuantisation == LaunchQuantisation::Half);

  REQUIRE(project.media.size() == 2);
  const MediaReference &kick = project.media.front();
  REQUIRE(kick.id.value == 2);
  REQUIRE(kick.path == "media/kick-3f9a1c2e.wav");
  REQUIRE(kick.sizeBytes == 352'844);
  REQUIRE(kick.contentHash ==
          "3f9a1c2e5d0b4a6f8e7c9d1b2a3f4e5d6c7b8a9f0e1d2c3b4a5f6e7d8c9b0a1f");
  REQUIRE(kick.sampleRate == 44'100);
  const MediaReference &snare = project.media.back();
  REQUIRE(snare.id.value == 3);
  REQUIRE(snare.path == "/home/someone/samples/snare.flac");
  REQUIRE(snare.sizeBytes == 5'000'000'000U);
  REQUIRE(snare.sampleRate == 96'000);

  REQUIRE(project.scenes.size() == 3);
  REQUIRE(project.scenes.at(0) == Scene{.id = {4}, .name = "Intro"});
  REQUIRE(project.scenes.at(1).id.value == 5);
  REQUIRE(project.scenes.at(1).name ==
          "Verse \"two\" \\ caf\xC3\xA9 \xE2\x9C\x93\tend\n");
  REQUIRE(project.scenes.at(2) == Scene{.id = {6}, .name = ""});

  // Tracks in display order, each with its kind and name.
  REQUIRE(project.tracks.size() == 4);
  const Track &drums = project.tracks.at(0);
  const Track &keys = project.tracks.at(1);
  const Track &bass = project.tracks.at(2);
  const Track &vox = project.tracks.at(3);
  REQUIRE(drums.id.value == 9);
  REQUIRE(drums.kind == TrackKind::Audio);
  REQUIRE(drums.name == "Drums");
  REQUIRE(drums.colour.rgb == 0xFF8000);
  REQUIRE(keys.id.value == 13);
  REQUIRE(keys.kind == TrackKind::Midi);
  REQUIRE(keys.name == "Keys");
  REQUIRE(keys.colour.rgb == 0x808080);
  REQUIRE(bass.id.value == 17);
  REQUIRE(bass.kind == TrackKind::Midi);
  REQUIRE(bass.name == "Bass");
  REQUIRE(vox.id.value == 21);
  REQUIRE(vox.kind == TrackKind::Audio);
  REQUIRE(vox.name == "Vox");

  // Mixer strips: every member has a value that no other member of the same
  // type shares on that track.
  REQUIRE(drums.mixer.volumeDb == -6.5);
  REQUIRE(drums.mixer.pan == -0.25);
  REQUIRE(!drums.mixer.mute);
  REQUIRE(!drums.mixer.solo);
  REQUIRE(drums.mixer.armed);
  REQUIRE(drums.mixer.monitoring == Monitoring::On);
  REQUIRE(drums.mixer.input ==
          TrackInput{AudioInput{.channel = 2, .stereo = true}});
  REQUIRE(drums.mixer.sends ==
          std::vector<Send>{{.target = {7}, .levelDb = -12.0},
                            {.target = {8}, .levelDb = -0.1}});
  REQUIRE(keys.mixer.solo);
  REQUIRE(!keys.mixer.mute);
  REQUIRE(!keys.mixer.armed);
  REQUIRE(keys.mixer.monitoring == Monitoring::Auto);
  REQUIRE(keys.mixer.input == TrackInput{MidiDeviceInput{"Keystation 61"}});
  REQUIRE(bass.mixer.mute);
  REQUIRE(!bass.mixer.solo);
  REQUIRE(bass.mixer.monitoring == Monitoring::Off);
  REQUIRE(bass.mixer.volumeDb == -96.0);
  REQUIRE(bass.mixer.input == TrackInput{AllMidiInputs{}});
  REQUIRE(vox.mixer.input == TrackInput{NoInput{}});
  REQUIRE(vox.mixer.pan == 1.0);
  REQUIRE(vox.mixer.volumeDb == 6.0);

  // Slots: identity and scene, in scene order.
  REQUIRE(drums.slots.size() == 3);
  REQUIRE(drums.slots.at(0).id.value == 10);
  REQUIRE(drums.slots.at(0).scene.value == 4);
  REQUIRE(drums.slots.at(1).id.value == 11);
  REQUIRE(drums.slots.at(1).scene.value == 5);
  REQUIRE(!drums.slots.at(1).clip.has_value());
  REQUIRE(keys.slots.at(1).id.value == 15);
  REQUIRE(keys.slots.at(1).scene.value == 5);

  // The audio clip in a slot.
  const Clip kickLoop = drums.slots.at(0).clip.value_or(Clip{});
  REQUIRE(kickLoop.id.value == 30);
  REQUIRE(kickLoop.name == "kick loop");
  REQUIRE(kickLoop.colour.rgb == 0x00FF7F);
  REQUIRE(kickLoop.extent.position.ticks == 0);
  REQUIRE(kickLoop.extent.length.ticks == 6 * beat);
  REQUIRE(kickLoop.extent.contentStart.ticks == 1 * beat);
  REQUIRE(kickLoop.loop.enabled);
  REQUIRE(kickLoop.loop.start.ticks == 2 * beat);
  REQUIRE(kickLoop.loop.length.ticks == 4 * beat);
  REQUIRE(kickLoop.launchQuantisation == LaunchQuantisation::None);
  const auto &kickAudio = std::get<AudioContent>(kickLoop.content);
  REQUIRE(kickAudio.media.value == 2);
  REQUIRE(kickAudio.region.start == 441);
  REQUIRE(kickAudio.region.end == 352'800);
  REQUIRE(kickAudio.gainDb == -3.25);
  REQUIRE(kickAudio.followTempo);
  REQUIRE(kickAudio.stretchMode == StretchMode::Repitch);
  REQUIRE(kickAudio.transposeCents == -1'200);
  REQUIRE(kickAudio.warp.origin == SourceTempoOrigin::Detected);
  REQUIRE(kickAudio.warp.markers ==
          std::vector<WarpMarker>{{.sample = 0, .tick = {0}},
                                  {.sample = 176'400, .tick = {4 * beat}},
                                  {.sample = 352'800, .tick = {8 * beat}}});

  // Audio clips on the arrangement.
  REQUIRE(drums.arrangement.size() == 2);
  REQUIRE(drums.arrangement.at(0).id.value == 31);
  REQUIRE(drums.arrangement.at(0).extent.position.ticks == 0);
  REQUIRE(drums.arrangement.at(0).extent.length.ticks == 4 * beat);
  REQUIRE(!drums.arrangement.at(0).launchQuantisation.has_value());
  const Clip &snareHit = drums.arrangement.at(1);
  REQUIRE(snareHit.id.value == 32);
  REQUIRE(snareHit.name == "snare hit");
  REQUIRE(snareHit.extent.position.ticks == 8 * beat);
  REQUIRE(snareHit.extent.length.ticks == 2 * beat);
  REQUIRE(snareHit.extent.contentStart.ticks == 0);
  REQUIRE(!snareHit.loop.enabled);
  const auto &snareAudio = std::get<AudioContent>(snareHit.content);
  REQUIRE(snareAudio.media.value == 3);
  REQUIRE(snareAudio.gainDb == 24.0);
  REQUIRE(!snareAudio.followTempo);
  REQUIRE(snareAudio.stretchMode == StretchMode::Stretch);
  REQUIRE(snareAudio.transposeCents == 0);
  REQUIRE(snareAudio.warp.origin == SourceTempoOrigin::Manual);

  // Devices.
  REQUIRE(keys.devices.size() == 2);
  const Device &synth = keys.devices.at(0);
  REQUIRE(synth.id.value == 25);
  REQUIRE(synth.role == DeviceRole::Instrument);
  REQUIRE(synth.plugin == PluginDescriptor{.format = "LV2",
                                           .identifier = "urn:lina:synth",
                                           .name = "Test Synth",
                                           .vendor = "LINA tests",
                                           .version = "1.2.3"});
  REQUIRE(synth.stateRef ==
          "b5ed020be883230d0772b9d60a4410672a2becbbd626afb1ac6a9fd5ea827a35"
          ".bin");
  REQUIRE(!synth.bypassed);
  REQUIRE(synth.parameters ==
          std::vector<ParameterValue>{{.id = "cutoff", .value = 0.1},
                                      {.id = "resonance", .value = 1.0 / 3.0}});
  const Device &chorus = keys.devices.at(1);
  REQUIRE(chorus.id.value == 26);
  REQUIRE(chorus.role == DeviceRole::AudioEffect);
  REQUIRE(chorus.plugin.identifier == "urn:lina:chorus");
  REQUIRE(chorus.stateRef.empty());
  REQUIRE(chorus.bypassed);
  REQUIRE(chorus.parameters.empty());
  REQUIRE(drums.devices.at(0).id.value == 27);
  REQUIRE(drums.devices.at(0).parameters ==
          std::vector<ParameterValue>{{.id = "threshold", .value = 0.000'001}});

  // The MIDI clip in a slot.
  const Clip chords = keys.slots.at(1).clip.value_or(Clip{});
  REQUIRE(chords.id.value == 33);
  REQUIRE(chords.name == "chords");
  REQUIRE(chords.extent.length.ticks == 8 * beat);
  REQUIRE(chords.launchQuantisation == LaunchQuantisation::Bars8);
  const auto &midi = std::get<MidiContent>(chords.content);
  REQUIRE(midi.notes == std::vector<Note>{
                            {.id = {34},
                             .pitch = 60,
                             .start = {0},
                             .length = {2 * beat},
                             .velocity = 100},
                            {.id = {35},
                             .pitch = 64,
                             .start = {0},
                             .length = {2 * beat},
                             .velocity = 1},
                            {.id = {36},
                             .pitch = 67,
                             .start = {0},
                             .length = {2 * beat},
                             .velocity = 127},
                            {.id = {37},
                             .pitch = 0,
                             .start = {4 * beat},
                             .length = {1},
                             .velocity = 64},
                            {.id = {38},
                             .pitch = 127,
                             .start = {4 * beat},
                             .length = {4 * beat},
                             .velocity = 64},
                        });
  REQUIRE(midi.controllers.size() == 4);
  REQUIRE(midi.controllers.at(0) ==
          ControllerEnvelope{.kind = ControllerKind::PitchBend,
                             .controller = 0,
                             .points = {{.time = {0}, .value = -8'192},
                                        {.time = {beat}, .value = 0},
                                        {.time = {2 * beat}, .value = 8'191}}});
  REQUIRE(midi.controllers.at(1).kind == ControllerKind::ChannelPressure);
  REQUIRE(midi.controllers.at(1).points ==
          std::vector<ControllerPoint>{{.time = {0}, .value = 127}});
  REQUIRE(midi.controllers.at(2).kind == ControllerKind::ControlChange);
  REQUIRE(midi.controllers.at(2).controller == 1);
  REQUIRE(midi.controllers.at(3) ==
          ControllerEnvelope{.kind = ControllerKind::ControlChange,
                             .controller = 64,
                             .points = {{.time = {7 * beat}, .value = 127}}});

  // MIDI clips on the arrangement.
  REQUIRE(bass.arrangement.size() == 2);
  REQUIRE(bass.arrangement.at(0).id.value == 39);
  REQUIRE(bass.arrangement.at(0).name == "bass line");
  REQUIRE(bass.arrangement.at(0).extent.position.ticks == 4 * beat);
  REQUIRE(std::get<MidiContent>(bass.arrangement.at(0).content).notes.back() ==
          Note{.id = {42},
               .pitch = 43,
               .start = {2 * beat},
               .length = {2 * beat},
               .velocity = 100});
  REQUIRE(bass.arrangement.at(1).id.value == 43);
  REQUIRE(bass.arrangement.at(1).extent.position.ticks ==
          maxTicks - ticksPerWholeNote);
  REQUIRE(bass.arrangement.at(1).extent.length.ticks == ticksPerWholeNote);

  // Automation.
  REQUIRE(drums.automation.size() == 3);
  REQUIRE(drums.automation.at(0) ==
          Envelope{.id = {44},
                   .target = MixerTarget{MixerParameter::Volume},
                   .points = {{.time = {0}, .value = -96.0},
                              {.time = {4 * beat}, .value = -6.0},
                              {.time = {4 * beat}, .value = 0.0},
                              {.time = {maxTicks}, .value = 6.0}}});
  REQUIRE(drums.automation.at(1) ==
          Envelope{.id = {45},
                   .target = MixerTarget{MixerParameter::Pan},
                   .points = {{.time = {0}, .value = -1.0},
                              {.time = {8 * beat}, .value = 1.0}}});
  REQUIRE(drums.automation.at(2) ==
          Envelope{.id = {46},
                   .target = SendTarget{{7}},
                   .points = {{.time = {2 * beat}, .value = -24.5}}});
  REQUIRE(keys.automation.at(0) ==
          Envelope{.id = {47},
                   .target = DeviceParameterTarget{.device = {25},
                                                   .parameter = "cutoff"},
                   .points = {{.time = {0}, .value = 0.0},
                              {.time = {16 * beat},
                               .value = 0.123'456'789'012'345'67}}});

  // Return tracks and the master.
  REQUIRE(project.returns.size() == 2);
  const Track &reverb = project.returns.at(0);
  REQUIRE(reverb.id.value == 7);
  REQUIRE(reverb.kind == TrackKind::Return);
  REQUIRE(reverb.name == "Reverb");
  REQUIRE(reverb.colour.rgb == 0x000001);
  REQUIRE(reverb.mixer.volumeDb == -3.0);
  REQUIRE(reverb.devices.at(0).id.value == 28);
  REQUIRE(reverb.devices.at(0).plugin.name == "Hall");
  REQUIRE(reverb.automation.at(0) ==
          Envelope{.id = {48},
                   .target = MixerTarget{MixerParameter::Pan},
                   .points = {}});
  REQUIRE(project.returns.at(1).id.value == 8);
  REQUIRE(project.returns.at(1).name == "Delay");
  REQUIRE(project.returns.at(1).mixer.mute);
  REQUIRE(project.master.id.value == 1);
  REQUIRE(project.master.kind == TrackKind::Master);
  REQUIRE(project.master.name == "Master");
  REQUIRE(project.master.mixer.volumeDb == -1.5);
  REQUIRE(project.master.devices.at(0).id.value == 29);
  REQUIRE(project.master.devices.at(0).plugin.name == "Limiter");
  REQUIRE(project.master.automation.at(0) ==
          Envelope{.id = {49},
                   .target = MixerTarget{MixerParameter::Volume},
                   .points = {{.time = {32 * beat}, .value = -96.0}}});
}

TEST_CASE("format/golden: the fuzz entry point takes its seed corpus") {
  std::vector<std::filesystem::path> seeds = goldenFiles();
  std::size_t inCorpus = 0;
  for (const auto &entry :
       std::filesystem::directory_iterator{LINA_FUZZ_CORPUS_DIR}) {
    seeds.push_back(entry.path());
    ++inCorpus;
  }
  REQUIRE(inCorpus >= 5);
  for (const std::filesystem::path &file : seeds) {
    CAPTURE(file.string());
    const std::string bytes = readText(file);
    const std::vector<std::uint8_t> data{bytes.begin(), bytes.end()};
    REQUIRE(LLVMFuzzerTestOneInput(data.data(), data.size()) == 0);
  }
  const std::vector<std::uint8_t> nothing;
  REQUIRE(LLVMFuzzerTestOneInput(nothing.data(), 0) == 0);
  // The golden files are among the seeds, unchanged.
  for (const char *name : {"minimal.lina", "everything.lina"}) {
    REQUIRE(readText(std::filesystem::path{LINA_FUZZ_CORPUS_DIR} / name) ==
            readText(goldenFile(std::string{"v1/"} + name)));
  }
}

} // namespace lina::format::testing
