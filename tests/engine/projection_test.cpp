// SPDX-License-Identifier: GPL-3.0-or-later
//
// The projection as a whole: what the engine's edit looks like for a
// document, what it ignores, and what the engine must not decide by itself.

#include <catch2/catch_test_macros.hpp>

#include "lina/engine/internal/tracktion_access.hpp"
#include "lina/engine/testing/test_plugins.hpp"
#include "projection_fixture.hpp"
#include "reference_signal.hpp"
#include "two_note_song.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

using namespace lina::engine;
using namespace lina::engine::testing;
using namespace lina::model;
using lina::model::testing::beat;
using lina::model::testing::beats;
using lina::model::testing::makeAudioClip;
using lina::model::testing::makeEffect;
using lina::model::testing::makeMedia;
using lina::model::testing::makeMidiClip;
using lina::model::testing::makeNote;

namespace {

namespace te = tracktion;

/// The identities of the projection's tracks in the order the engine holds
/// them.
std::vector<TrackId> engineTrackOrder(Engine &engine) {
  const auto &tracks = internal::TracktionAccess::projection(engine).tracks();
  std::vector<TrackId> order;
  for (const auto *track :
       te::getAudioTracks(internal::TracktionAccess::edit(engine))) {
    for (const auto &[id, entry] : tracks) {
      if (entry.track.get() == track) {
        order.push_back(id);
      }
    }
  }
  return order;
}

bool contains(const std::string &text, const std::string &part) {
  return text.contains(part);
}

} // namespace

TEST_CASE("an engine has no project until it is given one", "[projection]") {
  EngineFixture fixture("projection-no-project");
  Document document;
  CHECK(!fixture.engine.hasProject());
  const auto changes = document.perform(
      "edit", AddTrack{.kind = TrackKind::Audio, .name = "a", .index = 0});
  REQUIRE(changes.has_value());
  CHECK_THROWS_AS(fixture.engine.applyChanges(document.project(), *changes),
                  std::logic_error);

  fixture.engine.setProject(document.project(), fixture.scratch);
  CHECK(fixture.engine.hasProject());
  CHECK(internal::TracktionAccess::projection(fixture.engine).tracks().size() ==
        1);
  // An empty change set changes nothing.
  const std::string before = describeEdit(fixture.engine);
  fixture.engine.applyChanges(document.project(), ChangeSet{});
  CHECK(describeEdit(fixture.engine) == before);

  // Both calls belong to the message thread.
  std::thread([&] {
    CHECK_THROWS_AS(fixture.engine.setProject(document.project(), {}),
                    std::logic_error);
    CHECK_THROWS_AS(fixture.engine.applyChanges(document.project(), *changes),
                    std::logic_error);
  }).join();
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("the engine's tracks follow the document's, return tracks last",
          "[projection]") {
  ProjectionFixture fixture("projection-tracks");
  const TrackId first = fixture.addTrack(TrackKind::Audio);
  const TrackId effects = fixture.addTrack(TrackKind::Return);
  const TrackId second = fixture.addTrack(TrackKind::Midi);
  const TrackId third = fixture.add<ObjectKind::Track>(
      AddTrack{.kind = TrackKind::Audio, .name = "front", .index = 0});
  using Order = std::vector<TrackId>;
  CHECK(engineTrackOrder(fixture.engine) ==
        Order{third, first, second, effects});

  fixture.edit(MoveTrack{.track = third, .index = 2});
  CHECK(engineTrackOrder(fixture.engine) ==
        Order{first, second, third, effects});
  const TrackId moreEffects = fixture.add<ObjectKind::Track>(
      AddTrack{.kind = TrackKind::Return, .name = "front", .index = 0});
  CHECK(engineTrackOrder(fixture.engine) ==
        Order{first, second, third, moreEffects, effects});
  fixture.edit(MoveTrack{.track = moreEffects, .index = 1});
  CHECK(engineTrackOrder(fixture.engine) ==
        Order{first, second, third, effects, moreEffects});

  fixture.edit(RemoveTrack{second});
  CHECK(engineTrackOrder(fixture.engine) ==
        Order{first, third, effects, moreEffects});
  const auto &tracks =
      internal::TracktionAccess::projection(fixture.engine).tracks();
  CHECK(tracks.at(first).kind == TrackKind::Audio);
  CHECK(tracks.at(effects).kind == TrackKind::Return);
  CHECK(!tracks.contains(second));
  // Every audio and MIDI track has a send to every return track, in return
  // order.
  REQUIRE(tracks.at(third).sends.size() == 2);
  CHECK(tracks.at(third).sends.at(0).target == effects);
  CHECK(tracks.at(third).sends.at(1).target == moreEffects);
  CHECK(tracks.at(effects).sends.empty());

  // Undo brings the removed track back where it was.
  const auto undone = fixture.document.undo();
  REQUIRE(undone.has_value());
  fixture.engine.applyChanges(fixture.project(), *undone);
  CHECK(engineTrackOrder(fixture.engine) ==
        Order{first, second, third, effects, moreEffects});
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("setting a project sets everything the engine would otherwise "
          "decide for itself",
          "[projection]") {
  EngineFixture fixture("projection-defaults");
  auto &edit = internal::TracktionAccess::edit(fixture.engine);
  // An edit that someone has left in a state: a track of its own, a master
  // volume and pan, another pan law as the engine-wide default, the click
  // and the transport loop switched on, a different tempo.
  addTwoNoteSong(fixture.engine);
  edit.setMasterVolumeSliderPos(te::decibelsToVolumeFaderPosition(-3.0F));
  edit.setMasterPanPos(0.5F);
  const te::PanLaw lawBefore = te::getDefaultPanLaw();
  te::setDefaultPanLaw(te::PanLaw3dBCenter);
  edit.getMasterVolumePlugin()->setPanLaw(te::PanLawDefault);
  edit.masterFadeIn = te::TimeDuration::fromSeconds(0.5);
  edit.masterFadeOut = te::TimeDuration::fromSeconds(0.5);
  edit.clickTrackEnabled = true;
  edit.getTransport().looping = true;
  lina::engine::testing::setTempo(fixture.engine, 97.0);

  Document document;
  const auto perform = [&](const Command &command) {
    const auto changes = document.perform("edit", command);
    REQUIRE(changes.has_value());
    return *changes;
  };
  const auto added =
      perform(AddTrack{.kind = TrackKind::Midi, .name = "t", .index = 0})
          .firstAdded<ObjectKind::Track>();
  REQUIRE(added.has_value());
  const TrackId track = added.value_or(TrackId{});
  perform(AddDevice{.track = track, .device = toneDevice(), .index = 0});
  perform(AddClip{
      .container = track,
      .clip = makeMidiClip(beat(0), beats(1),
                           {makeNote(69, beat(0), beats(1), 127)}),
  });
  const auto sevenEight = TimeSignature::make(7, 8);
  REQUIRE(sevenEight.has_value());
  perform(SetProjectProperty{
      TimeSignatureValue{sevenEight.value_or(TimeSignature{})},
  });
  fixture.engine.setProject(document.project(), fixture.scratch);

  CHECK(te::getAudioTracks(edit).size() == 1);
  CHECK(edit.getMasterSliderPosParameter()->getCurrentValue() ==
        te::decibelsToVolumeFaderPosition(0.0F));
  CHECK(edit.getMasterPanParameter()->getCurrentValue() == 0.0F);
  CHECK(edit.getMasterVolumePlugin()->getPanLaw() == te::PanLawLinear);
  CHECK(edit.masterFadeIn.get() == te::TimeDuration{});
  CHECK(edit.masterFadeOut.get() == te::TimeDuration{});
  CHECK(!edit.clickTrackEnabled.get());
  CHECK(!edit.getTransport().looping.get());
  CHECK(edit.tempoSequence.getNumTempos() == 1);
  CHECK(edit.tempoSequence.getBpmAt({}) == 120.0);
  CHECK(edit.tempoSequence.getTimeSig(0)->numerator.get() == 7);
  CHECK(edit.tempoSequence.getTimeSig(0)->denominator.get() == 8);
  // The engine's own undo history is not used.
  CHECK(!edit.getUndoManager().canUndo());
  // A new track has none of the engine's default plugins: only the note
  // guard, the device and the projection's two gains.
  const auto &entry =
      internal::TracktionAccess::projection(fixture.engine).tracks().at(track);
  CHECK(entry.track->pluginList.size() == 4);
  CHECK(entry.track->getVolumePlugin() == nullptr);

  // What is heard proves it: full scale in, full scale out, on both sides,
  // and a beat is a quarter note in 7/8 too.
  fixture.engine.openManualDevice({.sampleRate = 48'000.0, .blockSize = 512});
  fixture.engine.play();
  const auto audio = fixture.engine.process(30'000);
  const std::array note{
      SoundingNote{
          .pitch = 69,
          .velocity = 127,
          .onFrame = 0,
          .offFrame = 24'000,
      },
  };
  const auto expected = referenceSignal(30'000, 48'000.0, note);
  // Tolerance: formulaTolerance, 1e-6 of full scale.
  CHECK(maxDifference(audio.channels.at(0), expected) <= formulaTolerance);
  CHECK(maxDifference(audio.channels.at(1), expected) <= formulaTolerance);
  CHECK(audio.channels.at(0).at(0) == 1.0F);
  CHECK(audio.channels.at(1).at(0) == 1.0F);

  te::setDefaultPanLaw(lawBefore);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("setting another project replaces the first", "[projection]") {
  ProjectionFixture fixture("projection-replace");
  const TrackId track = fixture.addToneTrack();
  fixture.addClip(track, makeMidiClip(beat(0), beats(1),
                                      {makeNote(60, beat(0), beats(1))}));
  fixture.addDevice(fixture.project().master.id, delayGainDevice(10, 0.5F));
  const std::string full = describeEdit(fixture.engine);

  const Document empty;
  fixture.engine.setProject(empty.project(), fixture.scratch);
  const auto &projection =
      internal::TracktionAccess::projection(fixture.engine);
  CHECK(projection.tracks().empty());
  CHECK(projection.clips().empty());
  CHECK(projection.devices().empty());
  const auto &edit = internal::TracktionAccess::edit(fixture.engine);
  CHECK(te::getAudioTracks(edit).isEmpty());
  CHECK(edit.getMasterPluginList().size() == 1);

  fixture.engine.setProject(fixture.project(), fixture.scratch);
  CHECK(describeEdit(fixture.engine) == full);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("what the projection does not play is ignored without a trace",
          "[projection]") {
  ProjectionFixture fixture("projection-ignored");
  const TrackId audio = fixture.addTrack(TrackKind::Audio);
  const TrackId midi = fixture.addToneTrack();
  const TrackId effects = fixture.addTrack(TrackKind::Return);
  const MediaId media =
      fixture.add<ObjectKind::Media>(AddMedia{makeMedia("media/none.wav")});
  const ClipId clip = fixture.addClip(
      midi, makeMidiClip(beat(0), beats(4), {makeNote(60, beat(0), beats(1))}));
  const std::string before = describeEdit(fixture.engine);

  // Scenes, slots and the clips in them.
  const SceneId scene =
      fixture.add<ObjectKind::Scene>(AddScene{.name = "A", .index = 0});
  fixture.edit(AddScene{.name = "B", .index = 1});
  fixture.edit(MoveScene{.scene = scene, .index = 1});
  fixture.edit(RenameScene{.scene = scene, .name = "C"});
  const Track *midiTrack = findTrack(fixture.project(), midi);
  REQUIRE(midiTrack != nullptr);
  const SlotId slot = midiTrack->slots.at(0).id;
  const ClipId slotClip = fixture.add<ObjectKind::Clip>(AddClip{
      .container = slot,
      .clip =
          makeMidiClip(beat(0), beats(4), {makeNote(72, beat(0), beats(1))}),
  });
  fixture.edit(
      AddNotes{.clip = slotClip, .notes = {makeNote(74, beat(1), beats(1))}});
  fixture.set(
      slotClip,
      LoopValue{{.enabled = true, .start = beat(0), .length = beats(2)}});
  const Track *audioTrack = findTrack(fixture.project(), audio);
  REQUIRE(audioTrack != nullptr);
  fixture.edit(AddClip{
      .container = audioTrack->slots.at(1).id,
      .clip = makeAudioClip(media, beat(0), beats(4)),
  });
  // Automation and controller data.
  Envelope volume;
  volume.target = MixerTarget{MixerParameter::Volume};
  volume.points = {{.time = beat(0), .value = -6.0}};
  const EnvelopeId envelope = fixture.add<ObjectKind::Envelope>(
      AddEnvelope{.track = midi, .envelope = volume});
  fixture.edit(SetEnvelopePoints{
      .envelope = envelope,
      .points =
          {
              {.time = beat(0), .value = -12.0},
              {.time = beat(4), .value = 0.0},
          },
  });
  fixture.edit(SetControllerData{
      .clip = clip,
      .data =
          {
              .kind = ControllerKind::ControlChange,
              .controller = 1,
              .points = {{.time = beat(0), .value = 64}},
          },
  });
  // Names, colours, the transport's settings, inputs.
  fixture.set(midi, NameValue{"lead"});
  fixture.set(midi, ColourValue{{0x112233}});
  fixture.set(clip, NameValue{"riff"});
  fixture.set(clip, ColourValue{{0x445566}});
  fixture.set(clip, LaunchOverrideValue{LaunchQuantisation::Quarter});
  fixture.set(midi, ArmedValue{true});
  fixture.set(midi, MonitoringValue{Monitoring::On});
  fixture.set(midi, InputValue{MidiDeviceInput{"keys"}});
  fixture.set(audio, InputValue{AudioInput{.channel = 1, .stereo = true}});
  fixture.edit(SetProjectProperty{
      LoopRangeValue{{.enabled = true, .start = beat(4), .length = beats(8)}},
  });
  fixture.edit(SetProjectProperty{StartMarkerValue{beat(2)}});
  fixture.edit(
      SetProjectProperty{DefaultLaunchValue{LaunchQuantisation::Bars2}});
  // A media reference that no clip of the arrangement uses.
  const MediaId unused =
      fixture.add<ObjectKind::Media>(AddMedia{makeMedia("media/unused.wav")});
  fixture.edit(RelinkMedia{.media = unused, .path = "media/moved.wav"});
  fixture.edit(RemoveMedia{unused});

  CHECK(describeEdit(fixture.engine) == before);
  CHECK(!internal::TracktionAccess::projection(fixture.engine)
             .clips()
             .contains(slotClip));

  // Taking the ignored things away again is ignored as well.
  fixture.edit(RemoveEnvelope{envelope});
  fixture.edit(RemoveClip{slotClip});
  fixture.edit(RemoveScene{scene});
  CHECK(describeEdit(fixture.engine) == before);
  CHECK(fixture.project().returns.at(0).id == effects);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a clip is in the engine while it is in the arrangement",
          "[projection]") {
  ProjectionFixture fixture("projection-clip-places");
  fixture.edit(AddScene{.name = "A", .index = 0});
  const TrackId track = fixture.addToneTrack();
  const TrackId other = fixture.addToneTrack();
  const ClipId clip =
      fixture.addClip(track, makeMidiClip(beat(2), beats(2),
                                          {makeNote(60, beat(0), beats(1))}));
  const auto &projection =
      internal::TracktionAccess::projection(fixture.engine);
  const auto engineClips = [&](TrackId id) {
    return projection.tracks().at(id).track->getClips().size();
  };
  REQUIRE(projection.clips().contains(clip));
  CHECK(projection.clips().at(clip).track == track);
  CHECK(engineClips(track) == 1);

  // Into a slot: gone from the engine.
  const Track *owner = findTrack(fixture.project(), track);
  REQUIRE(owner != nullptr);
  const SlotId slot = owner->slots.at(0).id;
  fixture.edit(MoveClip{.clip = clip, .container = slot, .position = beat(0)});
  CHECK(!projection.clips().contains(clip));
  CHECK(engineClips(track) == 0);

  // Back to an arrangement, on the other track.
  fixture.edit(MoveClip{.clip = clip, .container = other, .position = beat(1)});
  REQUIRE(projection.clips().contains(clip));
  CHECK(projection.clips().at(clip).track == other);
  CHECK(engineClips(track) == 0);
  CHECK(engineClips(other) == 1);
  const auto audio = fixture.playFromStart(48'000.0, 30'000);
  CHECK(firstSoundingFrame(audio.channels.at(0)) == 24'000);

  // One gesture: the clip moves off a track and the track is removed.
  const std::array<Command, 2> gesture{
      MoveClip{.clip = clip, .container = track, .position = beat(0)},
      RemoveTrack{other},
  };
  fixture.editAll(gesture);
  REQUIRE(projection.clips().contains(clip));
  CHECK(projection.clips().at(clip).track == track);
  CHECK(engineClips(track) == 1);
  const auto moved = fixture.playFromStart(48'000.0, 30'000);
  CHECK(firstSoundingFrame(moved.channels.at(0)) == 0);

  fixture.edit(RemoveClip{clip});
  CHECK(projection.clips().empty());
  CHECK(engineClips(track) == 0);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("clips move onto the sample grid of the device that plays them",
          "[projection]") {
  ProjectionFixture fixture("projection-sample-rate");
  fixture.setTempo(13'333);
  const TrackId track = fixture.addToneTrack();
  const ClipId clip =
      fixture.addClip(track, makeMidiClip(beat(3), beats(2),
                                          {makeNote(60, beat(0), beats(1))}));
  const auto &projection =
      internal::TracktionAccess::projection(fixture.engine);
  // Before a device is open the projection assumes 48 kHz.
  CHECK(projection.timeMap().sampleRate() == 48'000.0);
  // Beat 3 at 133.33 beats per minute: 1.35003 s.
  const auto first = fixture.playFromStart(44'100.0, 70'000);
  CHECK(projection.timeMap().sampleRate() == 44'100.0);
  CHECK(firstSoundingFrame(first.channels.at(0)) == 59'536);
  const auto second = fixture.playFromStart(96'000.0, 140'000);
  CHECK(projection.timeMap().sampleRate() == 96'000.0);
  CHECK(firstSoundingFrame(second.channels.at(0)) == 129'603);
  // The engine clip starts a little before its first event, on a frame.
  const auto start =
      projection.clips().at(clip).parts.front()->getPosition().getStart();
  CHECK(start.inSeconds() * 96'000.0 ==
        static_cast<double>(projection.clips().at(clip).originFrame));
  CHECK(projection.clips().at(clip).originFrame < 129'603);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("the document may hold more than the engine's own limits allow",
          "[projection]") {
  // Tracktion Engine stops at 16 plugins on a track and 4 on the master
  // unless told otherwise.
  ProjectionFixture fixture("projection-limits");
  const TrackId track = fixture.addToneTrack();
  const TrackId master = fixture.project().master.id;
  for (int index = 0; index < 20; ++index) {
    fixture.addDevice(track, delayGainDevice(1, 1.0F));
  }
  for (int index = 0; index < 6; ++index) {
    fixture.addDevice(master, delayGainDevice(1, 1.0F));
  }
  fixture.addClip(track, makeMidiClip(beat(0), beats(1),
                                      {makeNote(69, beat(0), beats(1), 127)}));
  const auto &projection =
      internal::TracktionAccess::projection(fixture.engine);
  CHECK(projection.tracks().at(track).track->pluginList.size() == 24);
  // Twenty-six delays of one frame each.
  const auto audio = fixture.playFromStart(48'000.0, 1'000);
  CHECK(firstSoundingFrame(audio.channels.at(0)) == 26);
  CHECK(audio.channels.at(0).at(26) == 1.0F);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("the description of an edit names what the projection does not "
          "account for",
          "[projection]") {
  // The oracle compares descriptions; it has to see a stray object.
  ProjectionFixture fixture("projection-description");
  const TrackId track = fixture.addToneTrack();
  fixture.addClip(track, makeMidiClip(beat(0), beats(1),
                                      {makeNote(60, beat(0), beats(1))}));
  fixture.addDevice(track, makeEffect("unknown"));
  const std::string clean = describeEdit(fixture.engine);
  CHECK(contains(clean, "placeholder"));
  CHECK(contains(clean, "undo history: none"));
  CHECK(!contains(clean, "not in the projection"));

  const auto &projection =
      internal::TracktionAccess::projection(fixture.engine);
  auto *engineTrack = projection.tracks().at(track).track.get();
  addDelayGainEffect(*engineTrack, 3, 1.0F);
  engineTrack->insertMIDIClip(
      "stray",
      {te::TimePosition::fromSeconds(5.0), te::TimePosition::fromSeconds(6.0)},
      nullptr);
  addTrack(fixture.engine);
  const std::string stray = describeEdit(fixture.engine);
  CHECK(contains(stray, "plugin not in the projection: linaTestDelayGain"));
  CHECK(contains(stray, "clip not in the projection"));
  CHECK(contains(stray, "track not in the projection"));
  CHECK(contains(stray, "undo history: kept"));

  // And an entry of the projection whose engine object has gone.
  fixture.engine.setProject(fixture.project(), fixture.scratch);
  CHECK(describeEdit(fixture.engine) == clean);
  projection.clips().begin()->second.parts.front()->removeFromParent();
  projection.devices().begin()->second.plugin->removeFromParent();
  const TrackId extra = fixture.addTrack(TrackKind::Audio);
  internal::TracktionAccess::edit(fixture.engine)
      .deleteTrack(projection.tracks().at(extra).track.get());
  const std::string missing = describeEdit(fixture.engine);
  CHECK(contains(missing, "of the projection is not in the edit"));
  CHECK(contains(missing, "engine clips, 0 in the edit"));
}

TEST_CASE("the engine is told what LINA decides in its place", "[projection]") {
  EngineFixture fixture("projection-behaviour");
  auto &behaviour =
      internal::TracktionAccess::engine(fixture.engine).getEngineBehaviour();
  // A beat is a quarter note whatever the time signature.
  CHECK(!behaviour.lengthOfOneBeatDependsOnTimeSignature());
  // Nothing moves by itself when the tempo changes.
  CHECK(!behaviour.areAudioClipsRemappedWhenTempoChanges());
  CHECK(!behaviour.areAutoTempoClipsRemappedWhenTempoChanges());
  CHECK(!behaviour.areMidiClipsRemappedWhenTempoChanges());
  CHECK(!behaviour.arePluginsRemappedWhenTempoChanges());
  // Audio clips play their file, without fades of the engine's choosing.
  CHECK(!behaviour.autoAddClipEdgeFades());
  CHECK(!behaviour.getClipDefaults().useProxyFile);
  CHECK(behaviour.getClipDefaults().resamplingQuality ==
        te::ResamplingQuality::lagrange);
  // The engine's limits on tracks, clips and plugins are out of the way.
  const te::EditLimits limits = behaviour.getEditLimits();
  CHECK(limits.maxNumTracks == 1'000'000);
  CHECK(limits.maxClipsInTrack == 1'000'000);
  CHECK(limits.maxPluginsOnTrack == 1'000'000);
  CHECK(limits.maxNumMasterPlugins == 1'000'000);
  CHECK(limits.maxPluginsOnClip == 0);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a change set that names what the projection never had does no "
          "harm",
          "[projection]") {
  ProjectionFixture fixture("projection-unknown-objects");
  const TrackId track = fixture.addToneTrack();
  const std::string before = describeEdit(fixture.engine);
  ChangeSet stray;
  stray.removed(DeviceId{9'001}, track);
  stray.removed(ClipId{9'002}, track);
  stray.changed(NoteId{9'003}, ClipId{9'002}, Property::NoteData);
  stray.changed(ClipId{9'004}, track, Property::Extent);
  stray.changed(DeviceId{9'005}, track, Property::Bypassed);
  stray.changed(MediaId{9'006}, projectRef, Property::Path);
  fixture.engine.applyChanges(fixture.project(), stray);
  CHECK(describeEdit(fixture.engine) == before);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a project that breaks the document's rules is refused",
          "[projection]") {
  // A Document cannot hold such a project; a plain Project can.
  EngineFixture fixture("projection-invalid-project");
  Project project = makeEmptyProject();
  Track track;
  track.id = TrackId{project.nextId++};
  track.kind = TrackKind::Audio;
  Clip clip = makeAudioClip(MediaId{77}, beat(0), beats(1));
  clip.id = ClipId{project.nextId++};
  track.arrangement.push_back(clip);
  project.tracks.push_back(track);
  REQUIRE(!validate(project).empty());
  CHECK_THROWS_AS(fixture.engine.setProject(project, fixture.scratch),
                  std::invalid_argument);
}

TEST_CASE("a device whose binding names a plugin the engine cannot make is "
          "an error",
          "[projection]") {
  class BrokenBinding final : public internal::DeviceBinding {
  public:
    [[nodiscard]] juce::ValueTree
    createState([[maybe_unused]] const Device &device) const override {
      return te::createValueTree(te::IDs::PLUGIN, te::IDs::type,
                                 "linaNoSuchPluginType");
    }
    void apply([[maybe_unused]] te::Plugin &plugin,
               [[maybe_unused]] const Device &device) const override {}
  };
  EngineFixture fixture("projection-broken-binding");
  internal::TracktionAccess::devices(fixture.engine)
      .add("lina-test", "broken", std::make_unique<BrokenBinding>());
  Document document;
  Device device = makeEffect("broken");
  device.plugin.format = "lina-test";
  device.plugin.identifier = "broken";
  REQUIRE(document
              .perform("edit", AddDevice{.track = document.project().master.id,
                                         .device = device,
                                         .index = 0})
              .has_value());
  CHECK_THROWS_AS(
      fixture.engine.setProject(document.project(), fixture.scratch),
      std::runtime_error);
}

TEST_CASE("a media file that is not audio plays nothing", "[projection]") {
  ProjectionFixture fixture("projection-not-audio");
  std::filesystem::create_directories(fixture.scratch / "media");
  {
    std::ofstream notes(fixture.scratch / "media" / "notes.txt");
    notes << "not audio\n";
  }
  const MediaId media =
      fixture.add<ObjectKind::Media>(AddMedia{makeMedia("media/notes.txt")});
  const TrackId track = fixture.addTrack(TrackKind::Audio);
  fixture.addClip(track, makeAudioClip(media, beat(0), beats(1)));
  using Reports = std::vector<MediaReport>;
  CHECK(fixture.engine.mediaProblems() ==
        Reports{{.media = media,
                 .problem = MediaProblem::Unreadable,
                 .fileSampleRate = 0.0}});
  const auto audio = fixture.playFromStart(48'000.0, 30'000);
  CHECK(firstSoundingFrame(audio.channels.at(0)) == -1);
  CHECK(fixture.assertions.count() == 0);
}

namespace {

/// A binding that makes the test effect and throws when told to.
class FaultyBinding final : public internal::DeviceBinding {
public:
  /// How many of the next calls throw.
  struct Faults {
    int create{0};
    int apply{0};
  };

  explicit FaultyBinding(Faults &faults) : faults_(&faults) {}

  [[nodiscard]] juce::ValueTree
  createState([[maybe_unused]] const Device &device) const override {
    if (faults_->create > 0) {
      --faults_->create;
      throw std::runtime_error("the binding could not make its plugin");
    }
    return DelayGainEffect::create(0, 1.0F);
  }
  void apply([[maybe_unused]] te::Plugin &plugin,
             [[maybe_unused]] const Device &device) const override {
    if (faults_->apply > 0) {
      --faults_->apply;
      throw std::runtime_error("the binding could not set its plugin");
    }
  }

private:
  Faults *faults_;
};

Device faultyDevice() {
  Device device = makeEffect("faulty");
  device.plugin.format = "lina-test";
  device.plugin.identifier = "faulty";
  return device;
}

/// A fixture whose engine knows the faulty binding.
struct FaultFixture : EngineFixture {
  explicit FaultFixture(std::string_view name) : EngineFixture(name) {
    internal::TracktionAccess::devices(engine).add(
        "lina-test", "faulty", std::make_unique<FaultyBinding>(faults));
    engine.setProject(document.project(), scratch);
  }

  /// Applies a command to the document and hands the change set on.
  void edit(const Command &command) {
    const auto changes = document.perform("edit", command);
    REQUIRE(changes.has_value());
    engine.applyChanges(document.project(), *changes);
  }

  /// True when the edit is what a build from scratch of the document
  /// gives.
  bool editMatchesDocument() {
    const std::string now = describeEdit(engine);
    engine.setProject(document.project(), scratch);
    return describeEdit(engine) == now;
  }

  FaultyBinding::Faults faults;
  Document document;
};

} // namespace

TEST_CASE("when something throws while a change set is applied, the edit is "
          "rebuilt from the project and the exception comes out",
          "[projection]") {
  FaultFixture fixture("projection-failure-rebuild");
  fixture.edit(AddTrack{.kind = TrackKind::Midi, .name = "a", .index = 0});
  const TrackId track = fixture.document.project().tracks.at(0).id;
  fixture.edit(AddDevice{.track = track, .device = toneDevice(), .index = 0});
  fixture.edit(AddClip{
      .container = track,
      .clip =
          makeMidiClip(beat(0), beats(1), {makeNote(60, beat(0), beats(1))}),
  });

  SECTION("making a plugin fails once") {
    fixture.faults.create = 1;
    CHECK_THROWS_AS(fixture.edit(AddDevice{
                        .track = track, .device = faultyDevice(), .index = 1}),
                    std::runtime_error);
  }
  SECTION("setting a plugin fails once") {
    fixture.edit(
        AddDevice{.track = track, .device = faultyDevice(), .index = 1});
    const DeviceId device =
        fixture.document.project().tracks.at(0).devices.at(1).id;
    fixture.faults.apply = 1;
    CHECK_THROWS_AS(fixture.edit(SetDeviceProperty{
                        .device = device,
                        .property = BypassedValue{true},
                    }),
                    std::runtime_error);
  }
  SECTION("in the middle of a gesture that also adds a track and a clip") {
    REQUIRE(fixture.document.begin("gesture").has_value());
    ChangeSet all;
    const auto apply = [&](const Command &command) {
      const auto changes = fixture.document.apply(command);
      REQUIRE(changes.has_value());
      all.merge(*changes);
      return *changes;
    };
    const auto added =
        apply(AddTrack{.kind = TrackKind::Midi, .name = "b", .index = 1})
            .firstAdded<ObjectKind::Track>();
    REQUIRE(added.has_value());
    const TrackId second = added.value_or(TrackId{});
    apply(AddDevice{.track = second, .device = toneDevice(), .index = 0});
    apply(AddDevice{.track = second, .device = faultyDevice(), .index = 1});
    apply(AddClip{
        .container = second,
        .clip =
            makeMidiClip(beat(2), beats(1), {makeNote(64, beat(0), beats(1))}),
    });
    apply(RemoveClip{
        fixture.document.project().tracks.at(0).arrangement.at(0).id,
    });
    REQUIRE(fixture.document.commit().has_value());
    fixture.faults.create = 1;
    CHECK_THROWS_AS(
        fixture.engine.applyChanges(fixture.document.project(), all),
        std::runtime_error);
  }
  // The engine still plays the project, and exactly the project.
  CHECK(fixture.engine.hasProject());
  CHECK(fixture.faults.create == 0);
  CHECK(fixture.faults.apply == 0);
  CHECK(fixture.editMatchesDocument());
  // And goes on taking change sets.
  fixture.edit(AddTrack{.kind = TrackKind::Audio, .name = "c", .index = 0});
  CHECK(fixture.editMatchesDocument());
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("when the rebuild after a failure fails too, the engine has no "
          "project and an empty edit",
          "[projection]") {
  FaultFixture fixture("projection-failure-not-built");
  fixture.edit(AddTrack{.kind = TrackKind::Audio, .name = "a", .index = 0});
  const TrackId track = fixture.document.project().tracks.at(0).id;
  fixture.faults.create = 1'000;
  CHECK_THROWS_AS(fixture.edit(AddDevice{
                      .track = track, .device = faultyDevice(), .index = 0}),
                  std::runtime_error);
  CHECK(!fixture.engine.hasProject());
  const auto &edit = internal::TracktionAccess::edit(fixture.engine);
  const auto &projection =
      internal::TracktionAccess::projection(fixture.engine);
  CHECK(te::getAudioTracks(edit).isEmpty());
  CHECK(edit.getMasterPluginList().size() == 0);
  CHECK(projection.tracks().empty());
  CHECK(projection.devices().empty());
  CHECK(projection.masterStrip() == nullptr);
  CHECK(fixture.engine.mediaProblems().empty());
  // Change sets are refused until a project is set again.
  CHECK_THROWS_AS(
      fixture.engine.applyChanges(fixture.document.project(), ChangeSet{}),
      std::logic_error);
  // Setting the same project fails the same way and leaves the same state.
  CHECK_THROWS_AS(
      fixture.engine.setProject(fixture.document.project(), fixture.scratch),
      std::runtime_error);
  CHECK(!fixture.engine.hasProject());
  CHECK(te::getAudioTracks(edit).isEmpty());
  // Once the binding works again, so does the engine.
  fixture.faults.create = 0;
  fixture.engine.setProject(fixture.document.project(), fixture.scratch);
  CHECK(fixture.engine.hasProject());
  CHECK(projection.devices().size() == 1);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a failure while playing leaves a stream that plays the project",
          "[projection]") {
  FaultFixture fixture("projection-failure-playing");
  fixture.edit(AddTrack{.kind = TrackKind::Midi, .name = "a", .index = 0});
  const TrackId track = fixture.document.project().tracks.at(0).id;
  fixture.edit(AddDevice{.track = track, .device = toneDevice(), .index = 0});
  fixture.edit(AddClip{
      .container = track,
      .clip = makeMidiClip(beat(0), beats(4),
                           {makeNote(69, beat(1), beats(1), 127)}),
  });
  fixture.engine.openManualDevice({.sampleRate = 48'000.0, .blockSize = 512});
  fixture.engine.play();
  std::ignore = fixture.engine.process(12'000);
  fixture.faults.create = 1;
  CHECK_THROWS_AS(fixture.edit(AddDevice{
                      .track = track, .device = faultyDevice(), .index = 1}),
                  std::runtime_error);
  // The rebuilt edit plays on: the note on beat 1 comes on its frame.
  const auto audio = fixture.engine.process(24'000);
  CHECK(firstSoundingFrame(audio.channels.at(0)) == 12'000);
  CHECK(audio.channels.at(0).at(12'000) == 1.0F);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("the media folder has to be an absolute path", "[projection]") {
  ProjectionFixture fixture("projection-media-folder");
  const TrackId track = fixture.addTrack(TrackKind::Audio);
  CHECK_THROWS_AS(fixture.engine.setProject(fixture.project(), "media"),
                  std::invalid_argument);
  CHECK_THROWS_AS(fixture.engine.setProject(fixture.project(), {}),
                  std::invalid_argument);
  // The project that was set before is still played.
  CHECK(fixture.engine.hasProject());
  CHECK(internal::TracktionAccess::projection(fixture.engine)
            .tracks()
            .contains(track));
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("the engine names the clips that hold more notes than it plays",
          "[projection]") {
  ProjectionFixture fixture("projection-truncated");
  const TrackId track = fixture.addToneTrack();
  fixture.addClip(track, makeMidiClip(beat(0), beats(4),
                                      {makeNote(60, beat(0), beats(1))}));
  CHECK(fixture.engine.truncatedClips().empty());
  // A sixteenth note looped over more than a day: 250,000 repetitions fit
  // into 62,500 beats.
  Clip endless =
      makeMidiClip(beat(8), beats(100'000),
                   {makeNote(62, beat(0), TickDuration{ticksPerQuarter / 8})});
  endless.loop = {
      .enabled = true,
      .start = beat(0),
      .length = TickDuration{ticksPerQuarter / 4},
  };
  const ClipId clip = fixture.addClip(track, endless);
  CHECK(fixture.engine.truncatedClips() == std::vector<ClipId>{clip});
  fixture.set(
      clip,
      LoopValue{{.enabled = false, .start = beat(0), .length = beats(4)}});
  CHECK(fixture.engine.truncatedClips().empty());
  CHECK(fixture.assertions.count() == 0);
}
