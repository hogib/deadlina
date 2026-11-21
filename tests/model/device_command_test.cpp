// SPDX-License-Identifier: GPL-3.0-or-later
//
// Commands on device chains.
#include "support/builders.hpp"
#include "support/checks.hpp"

#include "lina/model/document.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lina::model::testing {

namespace {

DeviceId add(Document &document, TrackId track, Device device,
             std::size_t index) {
  const ChangeSet changes = applied(
      document,
      AddDevice{.track = track, .device = std::move(device), .index = index});
  REQUIRE(changes.changes().size() == 1);
  const DeviceId id =
      changes.firstAdded<ObjectKind::Device>().value_or(DeviceId{});
  REQUIRE(changes.find(id)->parent == ObjectRef{track});
  return id;
}

std::vector<DeviceId> chain(const Project &project, TrackId track) {
  std::vector<DeviceId> ids;
  for (const Device &device : findTrack(project, track)->devices) {
    ids.push_back(device.id);
  }
  return ids;
}

SetDeviceProperty set(DeviceId device, DeviceProperty property) {
  return {.device = device, .property = std::move(property)};
}

SetDeviceProperty setParameter(DeviceId device, std::string id,
                               std::optional<double> value) {
  return set(device,
             ParameterSettingValue{{.id = std::move(id), .value = value}});
}

} // namespace

TEST_CASE("devices are added to the chain of any track",
          "[model][command][device]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const Project &project = fixture.project();

  const DeviceId delay = add(document, fixture.audio, makeEffect("delay"), 0);
  const DeviceId filter = add(document, fixture.audio, makeEffect("filter"), 0);
  const DeviceId reverb = add(document, fixture.audio, makeEffect("reverb"), 2);
  REQUIRE(chain(project, fixture.audio) ==
          std::vector<DeviceId>{filter, delay, reverb});
  const DeviceLocation location = findDevice(project, delay);
  REQUIRE(location.track->id == fixture.audio);
  REQUIRE(location.device->plugin.name == "delay");
  REQUIRE(location.device->role == DeviceRole::AudioEffect);

  const DeviceId synth =
      add(document, fixture.midi, makeInstrument("synth"), 0);
  const DeviceId chorus = add(document, fixture.midi, makeEffect("chorus"), 1);
  REQUIRE(chain(project, fixture.midi) == std::vector<DeviceId>{synth, chorus});

  const DeviceId onReturn =
      add(document, fixture.returnTrack, makeEffect("plate"), 0);
  const DeviceId onMaster =
      add(document, project.master.id, makeEffect("limiter"), 0);
  REQUIRE(findDevice(project, onReturn).track->id == fixture.returnTrack);
  REQUIRE(findDevice(project, onMaster).track->id == project.master.id);
  REQUIRE(findDevice(project, DeviceId{project.nextId}).device == nullptr);
}

TEST_CASE("adding a device is refused when it breaks a rule",
          "[model][command][device]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const auto adding = [](TrackId track, Device device, std::size_t index) {
    return AddDevice{
        .track = track,
        .device = std::move(device),
        .index = index,
    };
  };

  REQUIRE(refused(document, adding(TrackId{fixture.project().nextId},
                                   makeEffect("x"), 0)) ==
          Fault::UnknownObject);
  REQUIRE(refused(document, adding(fixture.audio, makeEffect("x"), 1)) ==
          Fault::IndexOutOfRange);
  Device withId = makeEffect("x");
  withId.id = DeviceId{fixture.project().nextId};
  REQUIRE(refused(document, adding(fixture.audio, withId, 0)) ==
          Fault::IdInvalid);

  // Instruments belong first in a MIDI track's chain and nowhere else.
  REQUIRE(refused(document, adding(fixture.audio, makeInstrument("x"), 0)) ==
          Fault::DeviceChainOrder);
  REQUIRE(refused(document, adding(fixture.returnTrack, makeInstrument("x"),
                                   0)) == Fault::DeviceChainOrder);
  REQUIRE(refused(document, adding(fixture.project().master.id,
                                   makeInstrument("x"), 0)) ==
          Fault::DeviceChainOrder);
  const DeviceId synth =
      add(document, fixture.midi, makeInstrument("synth"), 0);
  REQUIRE(refused(document, adding(fixture.midi, makeInstrument("x"), 1)) ==
          Fault::DeviceChainOrder);
  REQUIRE(refused(document, adding(fixture.midi, makeInstrument("x"), 0)) ==
          Fault::DeviceChainOrder);
  // An effect in front of the instrument would push it out of first place.
  REQUIRE(refused(document, adding(fixture.midi, makeEffect("x"), 0)) ==
          Fault::DeviceChainOrder);
  REQUIRE(chain(fixture.project(), fixture.midi) ==
          std::vector<DeviceId>{synth});

  Device unsorted = makeEffect("x");
  unsorted.parameters = {{.id = "b", .value = 0.5}, {.id = "a", .value = 0.5}};
  REQUIRE(refused(document, adding(fixture.audio, unsorted, 0)) ==
          Fault::OrderViolation);
  Device outOfRange = makeEffect("x");
  outOfRange.parameters = {{.id = "a", .value = 1.5}};
  REQUIRE(refused(document, adding(fixture.audio, outOfRange, 0)) ==
          Fault::ValueOutOfRange);
}

TEST_CASE("devices are removed and undo puts them back in place",
          "[model][command][device]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const DeviceId first = add(document, fixture.audio, makeEffect("first"), 0);
  const DeviceId second = add(document, fixture.audio, makeEffect("second"), 1);
  const DeviceId third = add(document, fixture.audio, makeEffect("third"), 2);

  const ChangeSet changes = applied(document, RemoveDevice{second});
  REQUIRE(changes.changes().size() == 1);
  REQUIRE(says(changes, second, ChangeKind::Removed));
  REQUIRE(changes.find(second)->parent == ObjectRef{fixture.audio});
  REQUIRE(chain(fixture.project(), fixture.audio) ==
          std::vector<DeviceId>{first, third});
  REQUIRE(document.undo().has_value());
  REQUIRE(chain(fixture.project(), fixture.audio) ==
          std::vector<DeviceId>{first, second, third});

  REQUIRE(refused(document, RemoveDevice{DeviceId{fixture.project().nextId}}) ==
          Fault::UnknownObject);
}

TEST_CASE("device properties and parameter values are set, reported and "
          "undone",
          "[model][command][device]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const DeviceId id = add(document, fixture.audio, makeEffect("filter"), 0);
  const auto device = [&] -> const Device & {
    return *findDevice(fixture.project(), id).device;
  };

  ChangeSet changes = applied(document, set(id, BypassedValue{true}));
  REQUIRE(says(changes, id, ChangeKind::Changed, Property::Bypassed));
  REQUIRE(changes.find(id)->parent == ObjectRef{fixture.audio});
  REQUIRE(device().bypassed);
  REQUIRE(applied(document, set(id, BypassedValue{true})).empty());

  const std::string state = std::string(64, 'e') + ".bin";
  changes = applied(document, set(id, StateValue{state}));
  REQUIRE(says(changes, id, ChangeKind::Changed, Property::State));
  REQUIRE(device().stateRef == state);

  // New values land in identifier order; an existing one is replaced;
  // forgetting removes.
  changes = applied(document, setParameter(id, "resonance", 0.25));
  REQUIRE(says(changes, id, ChangeKind::Changed, Property::Parameters));
  applied(document, setParameter(id, "cutoff", 0.5));
  applied(document, setParameter(id, "drive", 1.0));
  REQUIRE(device().parameters ==
          std::vector<ParameterValue>{{.id = "cutoff", .value = 0.5},
                                      {.id = "drive", .value = 1.0},
                                      {.id = "resonance", .value = 0.25}});
  applied(document, setParameter(id, "drive", 0.0));
  REQUIRE(device().parameters.at(1).value == 0.0);
  REQUIRE(applied(document, setParameter(id, "drive", 0.0)).empty());
  applied(document, setParameter(id, "drive", std::nullopt));
  REQUIRE(device().parameters.size() == 2);
  REQUIRE(applied(document, setParameter(id, "drive", std::nullopt)).empty());
  // Forgetting a value that sorts after every stored one.
  REQUIRE(applied(document, setParameter(id, "zoom", std::nullopt)).empty());
}

TEST_CASE("device properties are refused when out of range or unknown",
          "[model][command][device]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const DeviceId id = add(document, fixture.audio, makeEffect("filter"), 0);

  REQUIRE(refused(document, setParameter(id, "cutoff", 1.01)) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, setParameter(id, "cutoff", -0.01)) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, setParameter(id, "", 0.5)) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, setParameter(id, "", std::nullopt)) ==
          Fault::ValueOutOfRange);
  const DeviceId unknown{fixture.project().nextId};
  REQUIRE(refused(document, set(unknown, BypassedValue{true})) ==
          Fault::UnknownObject);
}

TEST_CASE("devices move inside their chain", "[model][command][device]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const TrackId track = fixture.midi;
  const DeviceId synth = add(document, track, makeInstrument("synth"), 0);
  const DeviceId a = add(document, track, makeEffect("a"), 1);
  const DeviceId b = add(document, track, makeEffect("b"), 2);
  const DeviceId c = add(document, track, makeEffect("c"), 3);
  const auto move = [track](DeviceId device, std::size_t index) {
    return MoveDevice{.device = device, .track = track, .index = index};
  };

  const ChangeSet changes = applied(document, move(a, 3));
  REQUIRE(changes.changes().size() == 1);
  REQUIRE(says(changes, track, ChangeKind::Changed, Property::DeviceOrder));
  REQUIRE(chain(fixture.project(), track) ==
          std::vector<DeviceId>{synth, b, c, a});
  applied(document, move(a, 1));
  REQUIRE(chain(fixture.project(), track) ==
          std::vector<DeviceId>{synth, a, b, c});

  REQUIRE(applied(document, move(b, 2)).empty());
  REQUIRE(refused(document, move(b, 4)) == Fault::IndexOutOfRange);
  REQUIRE(refused(document, move(synth, 1)) == Fault::DeviceChainOrder);
  REQUIRE(refused(document, move(c, 0)) == Fault::DeviceChainOrder);
  REQUIRE(refused(document, move(DeviceId{fixture.project().nextId}, 0)) ==
          Fault::UnknownObject);
  REQUIRE(
      refused(document, MoveDevice{.device = a,
                                   .track = TrackId{fixture.project().nextId},
                                   .index = 0}) == Fault::UnknownObject);
}

TEST_CASE("a device moves to another track with its identity, its values and "
          "its automation",
          "[model][command][device]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const Project &project = fixture.project();
  const DeviceId filter = add(document, fixture.audio, makeEffect("filter"), 0);
  const DeviceId delay = add(document, fixture.audio, makeEffect("delay"), 1);
  const DeviceId synth =
      add(document, fixture.midi, makeInstrument("synth"), 0);
  applied(document, SetDeviceProperty{
                        .device = filter,
                        .property =
                            ParameterSettingValue{
                                {
                                    .id = "cutoff",
                                    .value = 0.25,
                                },
                            },
                    });
  const auto automate = [&](DeviceId device, const char *parameter) {
    return applied(document,
                   AddEnvelope{
                       .track = fixture.audio,
                       .envelope =
                           {
                               .id = EnvelopeId{0},
                               .target =
                                   DeviceParameterTarget{
                                       .device = device,
                                       .parameter = parameter,
                                   },
                               .points = {{.time = beat(0), .value = 0.5}},
                           },
                   })
        .firstAdded<ObjectKind::Envelope>()
        .value_or(EnvelopeId{});
  };
  const EnvelopeId cutoff = automate(filter, "cutoff");
  const EnvelopeId feedback = automate(delay, "feedback");
  const EnvelopeId resonance = automate(filter, "resonance");
  const Device before = *findDevice(project, filter).device;

  const ChangeSet changes =
      applied(document,
              MoveDevice{.device = filter, .track = fixture.midi, .index = 1});
  // The device, its two envelopes, and both tracks' chains.
  REQUIRE(changes.changes().size() == 5);
  REQUIRE(says(changes, filter, ChangeKind::Changed, Property::Container));
  REQUIRE(changes.find(filter)->previousParent == ObjectRef{fixture.audio});
  REQUIRE(changes.find(filter)->parent == ObjectRef{fixture.midi});
  REQUIRE(says(changes, cutoff, ChangeKind::Changed, Property::Container));
  REQUIRE(says(changes, resonance, ChangeKind::Changed, Property::Container));
  REQUIRE(changes.find(resonance)->parent == ObjectRef{fixture.midi});
  REQUIRE(changes.find(feedback) == nullptr);
  REQUIRE(
      says(changes, fixture.audio, ChangeKind::Changed, Property::DeviceOrder));
  REQUIRE(
      says(changes, fixture.midi, ChangeKind::Changed, Property::DeviceOrder));

  REQUIRE(chain(project, fixture.audio) == std::vector<DeviceId>{delay});
  REQUIRE(chain(project, fixture.midi) == std::vector<DeviceId>{synth, filter});
  REQUIRE(*findDevice(project, filter).device == before);
  REQUIRE(findEnvelope(project, cutoff).track->id == fixture.midi);
  REQUIRE(findEnvelope(project, resonance).track->id == fixture.midi);
  REQUIRE(findEnvelope(project, feedback).track->id == fixture.audio);

  // Onto a return track and the master too.
  applied(
      document,
      MoveDevice{.device = filter, .track = fixture.returnTrack, .index = 0});
  applied(document,
          MoveDevice{.device = filter, .track = project.master.id, .index = 0});
  REQUIRE(findEnvelope(project, cutoff).track->id == project.master.id);
}

TEST_CASE("moving a device to another track is refused when the chain rule or "
          "the index says no",
          "[model][command][device]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  applied(document,
          AddTrack{.kind = TrackKind::Midi, .name = "Keys", .index = 2});
  const TrackId keys = fixture.project().tracks.at(2).id;
  const DeviceId synth =
      add(document, fixture.midi, makeInstrument("synth"), 0);
  const DeviceId chorus = add(document, fixture.midi, makeEffect("chorus"), 1);
  const DeviceId piano = add(document, keys, makeInstrument("piano"), 0);
  const auto move = [](DeviceId device, TrackId track, std::size_t index) {
    return MoveDevice{.device = device, .track = track, .index = index};
  };

  // An instrument only goes first on a MIDI track that has none.
  REQUIRE(refused(document, move(synth, fixture.audio, 0)) ==
          Fault::DeviceChainOrder);
  REQUIRE(refused(document, move(synth, fixture.returnTrack, 0)) ==
          Fault::DeviceChainOrder);
  REQUIRE(refused(document, move(synth, keys, 0)) == Fault::DeviceChainOrder);
  REQUIRE(refused(document, move(synth, keys, 1)) == Fault::DeviceChainOrder);
  // An effect may not push an instrument out of first place.
  REQUIRE(refused(document, move(chorus, keys, 0)) == Fault::DeviceChainOrder);
  REQUIRE(refused(document, move(chorus, keys, 2)) == Fault::IndexOutOfRange);

  applied(document, move(chorus, keys, 1));
  applied(document, RemoveDevice{piano});
  applied(document, move(synth, keys, 0));
  REQUIRE(chain(fixture.project(), keys) ==
          std::vector<DeviceId>{synth, chorus});
  REQUIRE(chain(fixture.project(), fixture.midi).empty());
}

TEST_CASE("removing an automated device removes its envelopes, and undo "
          "brings both back",
          "[model][command][device]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const Project &project = fixture.project();
  const DeviceId filter = add(document, fixture.audio, makeEffect("filter"), 0);
  add(document, fixture.audio, makeEffect("delay"), 1);
  const auto envelopeOn = [&](AutomationTarget target) {
    return applied(document,
                   AddEnvelope{
                       .track = fixture.audio,
                       .envelope =
                           {
                               .id = EnvelopeId{0},
                               .target = std::move(target),
                               .points = {},
                           },
                   })
        .firstAdded<ObjectKind::Envelope>()
        .value_or(EnvelopeId{});
  };
  const EnvelopeId cutoff = envelopeOn(
      DeviceParameterTarget{.device = filter, .parameter = "cutoff"});
  const EnvelopeId volume = envelopeOn(MixerTarget{MixerParameter::Volume});
  const EnvelopeId resonance = envelopeOn(
      DeviceParameterTarget{.device = filter, .parameter = "resonance"});
  const Project before = project;

  const ChangeSet changes = applied(document, RemoveDevice{filter});
  REQUIRE(changes.changes().size() == 3);
  REQUIRE(says(changes, filter, ChangeKind::Removed));
  REQUIRE(says(changes, cutoff, ChangeKind::Removed));
  REQUIRE(says(changes, resonance, ChangeKind::Removed));
  REQUIRE(changes.find(cutoff)->parent == ObjectRef{fixture.audio});
  REQUIRE(project.tracks.at(0).automation.size() == 1);
  REQUIRE(findEnvelope(project, volume).envelope != nullptr);
  REQUIRE(findEnvelope(project, cutoff).envelope == nullptr);

  REQUIRE(document.undo().has_value());
  REQUIRE(sameContent(project, before));
  REQUIRE(project.tracks.at(0).automation.at(0).id == cutoff);
  REQUIRE(project.tracks.at(0).automation.at(2).id == resonance);
}

TEST_CASE("removing one device and moving another in one transaction undoes "
          "exactly",
          "[model][command][device]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const Project &project = fixture.project();
  const DeviceId a = add(document, fixture.audio, makeEffect("a"), 0);
  const DeviceId b = add(document, fixture.audio, makeEffect("b"), 1);
  const DeviceId c = add(document, fixture.audio, makeEffect("c"), 2);
  const Project before = project;

  // Away and back to another place: the track must say its order changed.
  const std::array<Command, 3> shuffle{
      Command{MoveDevice{.device = a, .track = fixture.midi, .index = 0}},
      Command{RemoveDevice{b}},
      Command{MoveDevice{.device = a, .track = fixture.audio, .index = 1}},
  };
  const auto changes = document.perform("Shuffle", shuffle);
  REQUIRE(changes.has_value());
  REQUIRE(chain(project, fixture.audio) == std::vector<DeviceId>{c, a});
  REQUIRE(covers(*changes, diff(before, project)));
  REQUIRE(
      changes->find(fixture.audio)->properties.contains(Property::DeviceOrder));
  REQUIRE(says(*changes, b, ChangeKind::Removed));
  const Project shuffled = project;

  const auto undone = document.undo();
  REQUIRE(undone.has_value());
  REQUIRE(sameContent(project, before));
  REQUIRE(chain(project, fixture.audio) == std::vector<DeviceId>{a, b, c});
  REQUIRE(covers(*undone, diff(shuffled, project)));
  REQUIRE(document.redo().has_value());
  REQUIRE(project == shuffled);
}

TEST_CASE("a refused device names the track it was meant for",
          "[model][command][device]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const ObjectRef track{fixture.audio};
  Device bad = makeEffect("x");
  bad.parameters = {{.id = "a", .value = 1.5}};
  REQUIRE(
      refusal(document,
              AddDevice{.track = fixture.audio, .device = bad, .index = 0}) ==
      Failure{.fault = Fault::ValueOutOfRange, .object = track});
  REQUIRE(refusal(document, AddDevice{.track = fixture.audio,
                                      .device = makeInstrument("x"),
                                      .index = 0}) ==
          Failure{.fault = Fault::DeviceChainOrder, .object = track});
  Device assigned = makeEffect("x");
  assigned.id = DeviceId{fixture.project().nextId + 4};
  REQUIRE(refusal(document, AddDevice{.track = fixture.audio,
                                      .device = assigned,
                                      .index = 0}) ==
          Failure{.fault = Fault::IdInvalid, .object = track});
  Device unknownRole = makeEffect("x");
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): on purpose
  unknownRole.role = static_cast<DeviceRole>(9);
  REQUIRE(refusal(document, AddDevice{.track = fixture.audio,
                                      .device = unknownRole,
                                      .index = 0}) ==
          Failure{.fault = Fault::ValueOutOfRange, .object = track});

  // Negative zero is stored as zero.
  Device zero = makeEffect("zero");
  zero.parameters = {{.id = "a", .value = -0.0}};
  const DeviceId id = add(document, fixture.audio, zero, 0);
  REQUIRE(!std::signbit(
      findDevice(fixture.project(), id).device->parameters.at(0).value));
  applied(document,
          SetDeviceProperty{
              .device = id,
              .property = ParameterSettingValue{{.id = "b", .value = -0.0}},
          });
  REQUIRE(!std::signbit(
      findDevice(fixture.project(), id).device->parameters.at(1).value));
  REQUIRE(
      applied(document,
              SetDeviceProperty{
                  .device = id,
                  .property = ParameterSettingValue{{.id = "b", .value = -0.0}},
              })
          .empty());
}

} // namespace lina::model::testing
