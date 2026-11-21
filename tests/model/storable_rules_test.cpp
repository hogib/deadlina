// SPDX-License-Identifier: GPL-3.0-or-later
//
// Rules that keep every valid document writable: text is well-formed UTF-8
// without U+0000, content hashes and state references have their one shape,
// and the identity counter stops at `maxNextId` instead of wrapping round.
#include "support/builders.hpp"
#include "support/checks.hpp"

#include "lina/model/document.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lina::model::testing {

namespace {

std::string goodHashText() {
  std::string hash(64, 'a');
  return hash;
}
std::string goodStateName() { return goodHashText() + ".bin"; }

// Text no document may hold.
std::vector<std::string> badTexts() {
  return {
      std::string{"a\0b", 3}, // U+0000
      "caf\xE9",              // Latin-1, not UTF-8
      "\xC0\x80",             // overlong
      "\xED\xA0\x80",         // a surrogate
      "\xF4\x90\x80\x80",     // past U+10FFFF
      "\xE2\x9C",             // cut short
      "\x80",                 // a continuation byte alone
      "\xE2\x41\x80",         // continued by the wrong byte
  };
}

Document documentOf(Project project) {
  auto document = Document::fromProject(std::move(project));
  REQUIRE(document.has_value());
  return std::move(*document);
}

// The fixture's project with its counter moved up to `nextId`.
Project fixtureProjectAt(std::uint64_t nextId) {
  Project project = makeFixture().project();
  project.nextId = nextId;
  return project;
}

} // namespace

TEST_CASE("text that is fine in every language is accepted", "[model][rules]") {
  Fixture fixture = makeFixture();
  for (const std::string_view name : {
           "",
           "plain",
           "caf\xC3\xA9",
           "\xE2\x9C\x93",
           "\xF0\x9F\x8E\xB9",
           "\xED\x9F\xBF",
           "\xEF\xBF\xBF",
           "\xF4\x8F\xBF\xBF",
           "tab\tnewline\n",
           "\xE0\xA0\x80",
           "\xF0\x90\x80\x80",
           "\xF1\x80\x80\x80",
       }) {
    applied(fixture.document, SetTrackProperty{
                                  .track = fixture.audio,
                                  .property = NameValue{std::string{name}},
                              });
    REQUIRE(findTrack(fixture.project(), fixture.audio)->name == name);
  }
}

TEST_CASE("commands refuse text that is not UTF-8 or holds U+0000",
          "[model][rules]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  Device effect = makeEffect("effect");
  effect.parameters = {{.id = "p", .value = 0.5}};
  const auto changes =
      applied(document,
              AddDevice{.track = fixture.audio, .device = effect, .index = 0});
  const DeviceId device =
      changes.firstAdded<ObjectKind::Device>().value_or(DeviceId{});
  const ClipId clip = applied(document,
                              AddClip{
                                  .container = fixture.midi,
                                  .clip = makeMidiClip(beat(0), beats(4)),
                              })
                          .firstAdded<ObjectKind::Clip>()
                          .value_or(ClipId{});
  const auto withPlugin = [&](std::string PluginDescriptor::*field,
                              const std::string &text) {
    Device made = makeEffect("other");
    made.*(&Device::plugin).*field = text;
    return AddDevice{.track = fixture.audio, .device = made, .index = 0};
  };

  for (const std::string &bad : badTexts()) {
    CAPTURE(bad);
    const MediaReference media = makeMedia(bad);
    Device badParameter = makeEffect("other");
    badParameter.parameters = {{.id = bad, .value = 0.5}};
    const std::vector<Command> commands{
        AddMedia{media},
        RelinkMedia{.media = fixture.media, .path = bad},
        AddScene{.name = bad, .index = 0},
        RenameScene{.scene = fixture.sceneA, .name = bad},
        AddTrack{.kind = TrackKind::Audio, .name = bad, .index = 0},
        AddTrack{.kind = TrackKind::Return, .name = bad, .index = 0},
        SetTrackProperty{.track = fixture.audio, .property = NameValue{bad}},
        SetTrackProperty{
            .track = fixture.midi,
            .property = InputValue{MidiDeviceInput{bad}},
        },
        SetClipProperty{.clip = clip, .property = NameValue{bad}},
        withPlugin(&PluginDescriptor::format, bad),
        withPlugin(&PluginDescriptor::identifier, bad),
        withPlugin(&PluginDescriptor::name, bad),
        withPlugin(&PluginDescriptor::vendor, bad),
        withPlugin(&PluginDescriptor::version, bad),
        AddDevice{.track = fixture.audio, .device = badParameter, .index = 0},
        SetDeviceProperty{
            .device = device,
            .property = ParameterSettingValue{{.id = bad, .value = 0.5}},
        },
    };
    for (const Command &command : commands) {
      CAPTURE(command.index());
      REQUIRE(refused(document, command) == Fault::TextInvalid);
    }
    // A clip is checked before it gets an identity.
    Clip named = makeMidiClip(beat(8), beats(4));
    named.name = bad;
    REQUIRE(refused(document, AddClip{.container = fixture.midi,
                                      .clip = named}) == Fault::TextInvalid);
    // An envelope for a parameter with such a name has no target.
    REQUIRE(refused(document,
                    AddEnvelope{
                        .track = fixture.audio,
                        .envelope = {
                            .id = EnvelopeId{0},
                            .target = DeviceParameterTarget{.device = device,
                                                            .parameter = bad},
                            .points = {}}}) == Fault::EnvelopeTarget);
  }
  REQUIRE(validate(document.project()).empty());
}

TEST_CASE("validation refuses such text wherever a project holds text",
          "[model][rules]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  Device effect = makeEffect("effect");
  effect.parameters = {{.id = "p", .value = 0.5}};
  applied(document,
          AddDevice{.track = fixture.audio, .device = effect, .index = 0});
  applied(document, AddClip{
                        .container = fixture.midi,
                        .clip = makeMidiClip(beat(0), beats(4)),
                    });
  applied(document, AddClip{
                        .container = fixture.slot(fixture.midi, fixture.sceneA),
                        .clip = makeMidiClip(beat(0), beats(4)),
                    });
  applied(document, SetTrackProperty{
                        .track = fixture.midi,
                        .property = InputValue{MidiDeviceInput{"keys"}},
                    });
  const Project good = document.project();
  REQUIRE(validate(good).empty());
  const std::string bad{"a\0b", 3};

  const auto refusedWith = [&good](Fault fault, const auto &spoil) {
    Project project = good;
    spoil(project);
    const std::vector<Failure> failures = validate(project);
    return hasFault(failures, fault) &&
           !Document::fromProject(std::move(project)).has_value();
  };
  const Fault text = Fault::TextInvalid;
  REQUIRE(refusedWith(text, [&](Project &p) { p.media.front().path = bad; }));
  REQUIRE(refusedWith(text, [&](Project &p) { p.scenes.back().name = bad; }));
  REQUIRE(refusedWith(text, [&](Project &p) { p.tracks.front().name = bad; }));
  REQUIRE(refusedWith(text, [&](Project &p) { p.returns.front().name = bad; }));
  REQUIRE(refusedWith(text, [&](Project &p) { p.master.name = bad; }));
  REQUIRE(refusedWith(text, [&](Project &p) {
    p.tracks.back().mixer.input = MidiDeviceInput{bad};
  }));
  REQUIRE(refusedWith(text, [&](Project &p) {
    p.tracks.back().arrangement.front().name = bad;
  }));
  REQUIRE(refusedWith(text, [&](Project &p) {
    p.tracks.back().slots.front().clip->name = bad;
  }));
  Device &(*firstDevice)(Project &) = [](Project &p) -> Device & {
    return p.tracks.front().devices.front();
  };
  REQUIRE(refusedWith(text,
                      [&](Project &p) { firstDevice(p).plugin.format = bad; }));
  REQUIRE(refusedWith(
      text, [&](Project &p) { firstDevice(p).plugin.identifier = bad; }));
  REQUIRE(
      refusedWith(text, [&](Project &p) { firstDevice(p).plugin.name = bad; }));
  REQUIRE(refusedWith(text,
                      [&](Project &p) { firstDevice(p).plugin.vendor = bad; }));
  REQUIRE(refusedWith(
      text, [&](Project &p) { firstDevice(p).plugin.version = bad; }));
  REQUIRE(refusedWith(
      text, [&](Project &p) { firstDevice(p).parameters.front().id = bad; }));
}

TEST_CASE("a content hash is 64 lower-case hexadecimal digits",
          "[model][rules]") {
  Fixture fixture = makeFixture();
  MediaReference media = makeMedia("media/other.wav");
  media.contentHash = goodHashText();
  applied(fixture.document, AddMedia{media});
  media.contentHash = "0123456789abcdef0123456789abcdef0123456789abcdef"
                      "0123456789abcdef";
  applied(fixture.document, AddMedia{media});

  for (const std::string &hash : {
           std::string{},
           std::string{"00"},
           std::string(63, 'a'),
           std::string(65, 'a'),
           std::string(64, 'A'),
           std::string(64, 'g'),
           std::string(63, 'a') + " ",
           "0x" + std::string(62, 'a'),
       }) {
    CAPTURE(hash);
    media.contentHash = hash;
    REQUIRE(refused(fixture.document, AddMedia{media}) ==
            Fault::ValueOutOfRange);
    Project project = fixture.project();
    project.media.front().contentHash = hash;
    REQUIRE(hasFault(validate(project), Fault::ValueOutOfRange));
  }
}

TEST_CASE("a state reference is empty or the name of a state file",
          "[model][rules]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  Device effect = makeEffect("effect");
  effect.stateRef = goodStateName();
  const DeviceId device =
      applied(document,
              AddDevice{.track = fixture.audio, .device = effect, .index = 0})
          .firstAdded<ObjectKind::Device>()
          .value_or(DeviceId{});
  const auto set = [device](std::string name) {
    return SetDeviceProperty{
        .device = device,
        .property = StateValue{std::move(name)},
    };
  };
  // No stored state, and back.
  applied(document, set(""));
  applied(document, set(goodStateName()));

  for (const std::string &name : {
           std::string{"state.bin"},
           goodHashText(),
           goodHashText() + ".BIN",
           std::string(64, 'A') + ".bin",
           std::string(63, 'a') + ".bin",
           "../" + std::string(61, 'a') + ".bin",
           goodStateName() + "x",
           std::string{".bin"},
       }) {
    CAPTURE(name);
    REQUIRE(refused(document, set(name)) == Fault::ValueOutOfRange);
    effect.stateRef = name;
    REQUIRE(refused(document, AddDevice{.track = fixture.audio,
                                        .device = effect,
                                        .index = 0}) == Fault::ValueOutOfRange);
    Project project = fixture.project();
    project.tracks.front().devices.front().stateRef = name;
    REQUIRE(hasFault(validate(project), Fault::ValueOutOfRange));
  }
  REQUIRE(findDevice(fixture.project(), device).device->stateRef ==
          goodStateName());
}

TEST_CASE("the identity counter has a ceiling that validation enforces",
          "[model][rules]") {
  REQUIRE(maxNextId == 4'611'686'018'427'387'904U);
  REQUIRE(validate(fixtureProjectAt(maxNextId)).empty());
  const std::vector<Failure> failures =
      validate(fixtureProjectAt(maxNextId + 1));
  REQUIRE(failures == std::vector<Failure>{{.fault = Fault::IdsExhausted,
                                            .object = projectRef}});
  REQUIRE(!Document::fromProject(fixtureProjectAt(maxNextId + 1)).has_value());
  REQUIRE(hasFault(validate(fixtureProjectAt(~std::uint64_t{0})),
                   Fault::IdsExhausted));
}

TEST_CASE("a command that needs more identities than are left is refused",
          "[model][rules]") {
  const Fixture fixture = makeFixture();
  const Device effect = makeEffect("effect");
  const Envelope envelope{
      .id = EnvelopeId{0},
      .target = MixerTarget{MixerParameter::Volume},
      .points = {},
  };
  const std::vector<Note> twoNotes{
      makeNote(60, beat(0), beats(1)),
      makeNote(62, beat(1), beats(1)),
  };

  // Each command with the number of identities it takes: the fixture has two
  // regular tracks and two scenes.
  struct Need {
    Command command;
    std::uint64_t identities;
  };
  const std::vector<Need> needs{
      {.command = AddMedia{makeMedia("media/b.wav")}, .identities = 1},
      {.command = AddScene{.name = "C", .index = 0}, .identities = 3},
      {
          .command =
              AddTrack{.kind = TrackKind::Midi, .name = "more", .index = 0},
          .identities = 3,
      },
      {
          .command =
              AddTrack{.kind = TrackKind::Return, .name = "more", .index = 0},
          .identities = 1,
      },
      {
          .command =
              AddClip{
                  .container = fixture.midi,
                  .clip = makeMidiClip(beat(0), beats(4), twoNotes),
              },
          .identities = 3,
      },
      {
          .command =
              AddClip{
                  .container = fixture.audio,
                  .clip = makeAudioClip(fixture.media, beat(0), beats(4)),
              },
          .identities = 1,
      },
      {
          .command =
              AddDevice{.track = fixture.audio, .device = effect, .index = 0},
          .identities = 1,
      },
      {
          .command = AddEnvelope{.track = fixture.audio, .envelope = envelope},
          .identities = 1,
      },
  };
  for (const Need &need : needs) {
    CAPTURE(need.command.index());
    // One identity short: refused, and nothing changes.
    Document tooFew =
        documentOf(fixtureProjectAt(maxNextId - need.identities + 1));
    const Project before = tooFew.project();
    REQUIRE(refused(tooFew, need.command) == Fault::IdsExhausted);
    REQUIRE(tooFew.project() == before);
    // Exactly enough: applied, and the counter ends on the ceiling.
    Document enough = documentOf(fixtureProjectAt(maxNextId - need.identities));
    REQUIRE(enough.perform("last", need.command).has_value());
    REQUIRE(enough.project().nextId == maxNextId);
    REQUIRE(validate(enough.project()).empty());
    // Undo and redo use the identities again and need no new ones.
    REQUIRE(enough.undo().has_value());
    REQUIRE(enough.redo().has_value());
    REQUIRE(enough.project().nextId == maxNextId);
  }

  // Notes added to a clip that is there.
  Project withClip = fixture.project();
  Document base = documentOf(withClip);
  const ClipId clip = applied(base,
                              AddClip{
                                  .container = fixture.midi,
                                  .clip = makeMidiClip(beat(0), beats(4)),
                              })
                          .firstAdded<ObjectKind::Clip>()
                          .value_or(ClipId{});
  withClip = base.project();
  withClip.nextId = maxNextId - 1;
  Document nearlyFull = documentOf(withClip);
  REQUIRE(refused(nearlyFull, AddNotes{.clip = clip, .notes = twoNotes}) ==
          Fault::IdsExhausted);
  REQUIRE(nearlyFull
              .perform("one note",
                       AddNotes{.clip = clip, .notes = {twoNotes.front()}})
              .has_value());
  REQUIRE(nearlyFull.project().nextId == maxNextId);
  // Full: nothing that needs an identity, everything else still works.
  REQUIRE(refused(nearlyFull, AddScene{.name = "D", .index = 0}) ==
          Fault::IdsExhausted);
  REQUIRE(nearlyFull
              .perform("rename", RenameScene{.scene = fixture.sceneA,
                                             .name = "still editable"})
              .has_value());
  REQUIRE(nearlyFull.perform("remove", RemoveClip{clip}).has_value());
}

} // namespace lina::model::testing
