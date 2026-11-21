// SPDX-License-Identifier: GPL-3.0-or-later
//
// The document: transactions, the undo and redo history, and what readers can
// look up.
#include "support/builders.hpp"
#include "support/checks.hpp"

#include "lina/model/document.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace lina::model::testing {

namespace {

Command setVolume(TrackId track, double level) {
  return SetTrackProperty{.track = track, .property = VolumeValue{level}};
}

} // namespace

TEST_CASE("a new document holds a master track and an empty history",
          "[model][document]") {
  const Document document;
  const Project &project = document.project();
  REQUIRE(validate(project).empty());
  REQUIRE(project == makeEmptyProject());
  REQUIRE(project.master.kind == TrackKind::Master);
  REQUIRE(project.master.id.assigned());
  REQUIRE(project.nextId == project.master.id.value + 1);
  REQUIRE(project.tracks.empty());
  REQUIRE(project.returns.empty());
  REQUIRE(project.scenes.empty());
  REQUIRE(project.tempo == Tempo{});
  REQUIRE(project.timeSignature == TimeSignature{});

  REQUIRE(!document.canUndo());
  REQUIRE(!document.canRedo());
  REQUIRE(!document.undoName().has_value());
  REQUIRE(!document.redoName().has_value());
  REQUIRE(!document.inTransaction());
}

TEST_CASE("commands need an open transaction, and only one can be open",
          "[model][document]") {
  Document document;
  const Project before = document.project();
  const TrackId master = before.master.id;

  REQUIRE(document.apply(setVolume(master, -3.0)).error().fault ==
          Fault::NoTransaction);
  REQUIRE(document.commit().error().fault == Fault::NoTransaction);
  REQUIRE(document.rollback().error().fault == Fault::NoTransaction);
  REQUIRE(document.undo().error().fault == Fault::NothingToUndo);
  REQUIRE(document.redo().error().fault == Fault::NothingToRedo);
  REQUIRE(document.project() == before);

  REQUIRE(document.begin("first").has_value());
  REQUIRE(document.inTransaction());
  REQUIRE(document.begin("second").error().fault == Fault::TransactionOpen);
  REQUIRE(document.undo().error().fault == Fault::TransactionOpen);
  REQUIRE(document.redo().error().fault == Fault::TransactionOpen);
  REQUIRE(document.perform("third", setVolume(master, -3.0)).error().fault ==
          Fault::TransactionOpen);
  REQUIRE(document.inTransaction());
  REQUIRE(document.project() == before);
  REQUIRE(document.commit().has_value());
  REQUIRE(!document.inTransaction());
  // Nothing was applied, so nothing was recorded.
  REQUIRE(!document.canUndo());
}

TEST_CASE("one transaction is one named undo step, however many commands",
          "[model][document]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const Project before = fixture.project();

  REQUIRE(document.begin("Drag fader").has_value());
  for (const double level : {-1.0, -2.0, -3.0}) {
    const auto changes = document.apply(setVolume(fixture.audio, level));
    REQUIRE(changes.has_value());
    REQUIRE(
        says(*changes, fixture.audio, ChangeKind::Changed, Property::Volume));
  }
  REQUIRE(document.apply(setVolume(fixture.midi, -9.0)).has_value());
  REQUIRE(document.commit().has_value());
  const Project after = fixture.project();
  REQUIRE(after.tracks.at(0).mixer.volumeDb == -3.0);
  REQUIRE(document.undoName() == "Drag fader");
  REQUIRE(!document.canRedo());

  const auto undone = document.undo();
  REQUIRE(undone.has_value());
  REQUIRE(fixture.project() == before);
  REQUIRE(undone->changes().size() == 2);
  REQUIRE(says(*undone, fixture.audio, ChangeKind::Changed, Property::Volume));
  REQUIRE(says(*undone, fixture.midi, ChangeKind::Changed, Property::Volume));
  REQUIRE(document.redoName() == "Drag fader");
  REQUIRE(document.undoName() == "fixture");

  const auto redone = document.redo();
  REQUIRE(redone.has_value());
  REQUIRE(fixture.project() == after);
  REQUIRE(*redone == *undone);
  REQUIRE(document.undoName() == "Drag fader");
  REQUIRE(!document.canRedo());
}

TEST_CASE("a new edit empties the redo history; an edit that changes nothing "
          "does not",
          "[model][document]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  REQUIRE(
      document.perform("Volume", setVolume(fixture.audio, -6.0)).has_value());
  REQUIRE(
      document.perform("Volume", setVolume(fixture.audio, -12.0)).has_value());
  REQUIRE(document.undo().has_value());
  REQUIRE(document.undo().has_value());
  REQUIRE(document.canRedo());

  // The volume is 0 again: asking for 0 changes nothing.
  const auto nothing =
      document.perform("Nothing", setVolume(fixture.audio, 0.0));
  REQUIRE(nothing.has_value());
  REQUIRE(nothing->empty());
  REQUIRE(document.canRedo());
  REQUIRE(document.undoName() == "fixture");

  REQUIRE(document
              .perform("Pan", SetTrackProperty{.track = fixture.audio,
                                               .property = PanValue{0.5}})
              .has_value());
  REQUIRE(!document.canRedo());
  REQUIRE(document.redo().error().fault == Fault::NothingToRedo);
  REQUIRE(document.undoName() == "Pan");
}

TEST_CASE("a refused command leaves its transaction open and the document as "
          "it was",
          "[model][document]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  REQUIRE(document.begin("Mix").has_value());
  REQUIRE(document.apply(setVolume(fixture.audio, -6.0)).has_value());
  const Project middle = fixture.project();

  const auto refusedChange = document.apply(setVolume(fixture.audio, 99.0));
  REQUIRE(refusedChange.error() == Failure{.fault = Fault::ValueOutOfRange,
                                           .object = ObjectRef{fixture.audio}});
  REQUIRE(fixture.project() == middle);
  REQUIRE(document.inTransaction());

  REQUIRE(document.apply(setVolume(fixture.midi, -6.0)).has_value());
  REQUIRE(document.commit().has_value());
  REQUIRE(document.undoName() == "Mix");
  REQUIRE(fixture.project().tracks.at(1).mixer.volumeDb == -6.0);
}

TEST_CASE("rollback takes back everything a transaction applied",
          "[model][document]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const Project before = fixture.project();
  const std::string_view undoBefore = document.undoName().value_or("");

  REQUIRE(document.begin("Abandoned").has_value());
  const auto added = document.apply(AddClip{
      .container = fixture.midi,
      .clip =
          makeMidiClip(beat(0), beats(4), {makeNote(60, beat(0), beats(1))}),
  });
  REQUIRE(added.has_value());
  const ClipId clip = added->firstAdded<ObjectKind::Clip>().value_or(ClipId{});
  REQUIRE(document.apply(setVolume(fixture.midi, -6.0)).has_value());
  REQUIRE(document
              .apply(MoveClip{
                  .clip = clip, .container = fixture.midi, .position = beat(8)})
              .has_value());

  const auto undone = document.rollback();
  REQUIRE(undone.has_value());
  REQUIRE(sameContent(fixture.project(), before));
  REQUIRE(!document.inTransaction());
  REQUIRE(document.undoName() == undoBefore);
  // The clip and its note go; the volume goes back.
  REQUIRE(undone->changes().size() == 3);
  REQUIRE(says(*undone, clip, ChangeKind::Removed));
  REQUIRE(says(*undone, fixture.midi, ChangeKind::Changed, Property::Volume));
  // Identities used inside the abandoned transaction are not handed out again.
  REQUIRE(fixture.project().nextId == before.nextId + 2);
}

TEST_CASE("performing several commands is all or nothing",
          "[model][document]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const Project before = fixture.project();

  const std::array<Command, 3> failing{
      setVolume(fixture.audio, -6.0),
      Command{AddScene{.name = "C", .index = 2}},
      setVolume(fixture.midi, 99.0),
  };
  const auto refusedChanges = document.perform("Batch", failing);
  REQUIRE(refusedChanges.error().fault == Fault::ValueOutOfRange);
  REQUIRE(sameContent(fixture.project(), before));
  REQUIRE(!document.inTransaction());
  REQUIRE(document.undoName() == "fixture");

  // The same scene is added and renamed: the merged change set says added,
  // once.
  const std::array<Command, 2> first{
      setVolume(fixture.audio, -6.0),
      Command{AddScene{.name = "C", .index = 2}},
  };
  const auto changes = document.perform("Batch", first);
  REQUIRE(changes.has_value());
  const SceneId scene =
      changes->firstAdded<ObjectKind::Scene>().value_or(SceneId{});
  REQUIRE(changes->changes().size() == 4);
  REQUIRE(says(*changes, fixture.audio, ChangeKind::Changed, Property::Volume));
  REQUIRE(says(*changes, scene, ChangeKind::Added));

  const std::array<Command, 3> second{
      Command{RenameScene{.scene = scene, .name = "Outro"}},
      Command{RemoveScene{scene}},
      setVolume(fixture.audio, -7.0),
  };
  const Project middle = fixture.project();
  const auto merged = document.perform("Rename and remove", second);
  REQUIRE(merged.has_value());
  // Changed then removed inside one transaction: removed.
  REQUIRE(says(*merged, scene, ChangeKind::Removed));
  REQUIRE(*merged == diff(middle, fixture.project()));
  REQUIRE(document.undoName() == "Rename and remove");
  REQUIRE(document.undo().has_value());
  REQUIRE(findScene(fixture.project(), scene)->name == "C");
  REQUIRE(document.undo().has_value());
  REQUIRE(sameContent(fixture.project(), before));
}

TEST_CASE("a transaction whose commands cancel out leaves no undo step",
          "[model][document]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  REQUIRE(
      document.perform("Volume", setVolume(fixture.audio, -6.0)).has_value());
  REQUIRE(document.undo().has_value());
  const Project before = fixture.project();

  REQUIRE(document.begin("Try").has_value());
  const auto added = document.apply(AddScene{.name = "C", .index = 0});
  REQUIRE(added.has_value());
  const SceneId scene =
      added->firstAdded<ObjectKind::Scene>().value_or(SceneId{});
  REQUIRE(document.apply(RemoveScene{scene}).has_value());
  REQUIRE(document.commit().has_value());

  // Nothing to take back, so no step, and what could be redone still can be.
  REQUIRE(sameContent(fixture.project(), before));
  REQUIRE(document.undoName() == "fixture");
  REQUIRE(document.redoName() == "Volume");
  // The identities used on the way are spent all the same.
  REQUIRE(fixture.project().nextId == before.nextId + 3);

  // The same through perform: an empty change set comes back.
  const std::array<Command, 2> cancelling{
      Command{AddScene{.name = "C", .index = 0}},
      Command{RemoveScene{SceneId{fixture.project().nextId}}},
  };
  const auto changes = document.perform("Try again", cancelling);
  REQUIRE(changes.has_value());
  REQUIRE(changes->empty());
  REQUIRE(document.undoName() == "fixture");
  REQUIRE(document.redoName() == "Volume");
}

TEST_CASE("a property set and set back within a transaction still leaves a "
          "step",
          "[model][document]") {
  // Documented limit: the merged change set names the property, and telling
  // that its value is the old one again would mean comparing documents.
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const Project before = fixture.project();
  const std::array<Command, 2> thereAndBack{
      setVolume(fixture.audio, -6.0),
      setVolume(fixture.audio, 0.0),
  };
  const auto changes = document.perform("There and back", thereAndBack);
  REQUIRE(changes.has_value());
  REQUIRE(says(*changes, fixture.audio, ChangeKind::Changed, Property::Volume));
  REQUIRE(fixture.project() == before);
  REQUIRE(document.undoName() == "There and back");
  REQUIRE(document.undo().has_value());
  REQUIRE(fixture.project() == before);
}

TEST_CASE("commands that change nothing are not remembered",
          "[model][document]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const ClipId clip = document
                          .perform("Add",
                                   AddClip{
                                       .container = fixture.midi,
                                       .clip = makeMidiClip(beat(0), beats(4)),
                                   })
                          .value_or(ChangeSet{})
                          .firstAdded<ObjectKind::Clip>()
                          .value_or(ClipId{});
  REQUIRE(clip.assigned());

  // A drag that reports every pointer position, most of them repeats.
  REQUIRE(document.begin("Drag").has_value());
  std::size_t reported = 0;
  for (int move = 0; move < 1'000; ++move) {
    const auto changes = document.apply(MoveClip{
        .clip = clip,
        .container = fixture.midi,
        .position = beat(move / 250),
    });
    REQUIRE(changes.has_value());
    reported += changes->empty() ? 0 : 1;
  }
  REQUIRE(document.commit().has_value());
  // Positions 0 (where it was), 1, 2 and 3 beats: three real moves.
  REQUIRE(reported == 3);
  REQUIRE(findClip(fixture.project(), clip).clip->extent.position == beat(3));

  // One undo takes the whole drag back.
  REQUIRE(document.undoName() == "Drag");
  REQUIRE(document.undo().has_value());
  REQUIRE(findClip(fixture.project(), clip).clip->extent.position == beat(0));
  REQUIRE(document.undoName() == "Add");
}

TEST_CASE("a copy of a document has its own project and history",
          "[model][document]") {
  const Fixture fixture = makeFixture();
  Document copy = fixture.document;
  REQUIRE(copy.perform("Volume", setVolume(fixture.audio, -6.0)).has_value());
  REQUIRE(fixture.project().tracks.at(0).mixer.volumeDb == 0.0);
  REQUIRE(copy.project().tracks.at(0).mixer.volumeDb == -6.0);
  REQUIRE(copy.undoName() == "Volume");
  REQUIRE(fixture.document.undoName() == "fixture");
}

TEST_CASE("content equality ignores the identity counter and nothing else",
          "[model][document]") {
  const Fixture fixture = makeFixture();
  Project other = fixture.project();
  REQUIRE(sameContent(fixture.project(), other));
  other.nextId += 10;
  REQUIRE(sameContent(fixture.project(), other));
  REQUIRE(fixture.project() != other);

  const auto differs = [&fixture](auto change) {
    Project changed = fixture.project();
    change(changed);
    return !sameContent(fixture.project(), changed);
  };
  REQUIRE(differs([](Project &p) {
    p.tempo = Tempo::fromHundredths(9'000).value_or(Tempo{});
  }));
  REQUIRE(differs([](Project &p) {
    p.timeSignature = TimeSignature::make(3, 4).value_or(TimeSignature{});
  }));
  REQUIRE(differs([](Project &p) { p.loop.enabled = true; }));
  REQUIRE(differs([](Project &p) { p.startMarker = beat(1); }));
  REQUIRE(differs(
      [](Project &p) { p.launchQuantisation = LaunchQuantisation::None; }));
  REQUIRE(differs([](Project &p) { p.media.at(0).sizeBytes = 7; }));
  REQUIRE(differs([](Project &p) { p.scenes.at(0).name = "x"; }));
  REQUIRE(differs([](Project &p) { p.tracks.at(0).name = "x"; }));
  REQUIRE(differs([](Project &p) { p.returns.at(0).mixer.mute = true; }));
  REQUIRE(differs([](Project &p) { p.master.mixer.volumeDb = -1.0; }));
}

TEST_CASE("lookups find objects by identity and return null for strangers",
          "[model][document]") {
  const Fixture fixture = makeFixture();
  const Project &project = fixture.project();
  const std::uint64_t unknown = project.nextId;

  REQUIRE(findScene(project, fixture.sceneB) == &project.scenes.at(1));
  REQUIRE(findScene(project, SceneId{unknown}) == nullptr);
  REQUIRE(findMedia(project, fixture.media) == &project.media.at(0));
  REQUIRE(findMedia(project, MediaId{unknown}) == nullptr);
  REQUIRE(findTrack(project, fixture.midi) == &project.tracks.at(1));
  REQUIRE(findTrack(project, fixture.returnTrack) == &project.returns.at(0));
  REQUIRE(findTrack(project, project.master.id) == &project.master);
  REQUIRE(findTrack(project, TrackId{unknown}) == nullptr);

  const SlotId slot = fixture.slot(fixture.midi, fixture.sceneB);
  const SlotLocation location = findSlot(project, slot);
  REQUIRE(location.track == &project.tracks.at(1));
  REQUIRE(location.slot == &project.tracks.at(1).slots.at(1));
  REQUIRE(findSlot(project, SlotId{unknown}).slot == nullptr);
  REQUIRE(findSlot(project, SlotId{unknown}).track == nullptr);
  REQUIRE(findClip(project, ClipId{unknown}).clip == nullptr);
  REQUIRE(findDevice(project, DeviceId{unknown}).track == nullptr);
  REQUIRE(!fixture.slot(TrackId{unknown}, fixture.sceneA).assigned());
  REQUIRE(!fixture.slot(fixture.midi, SceneId{unknown}).assigned());

  // An audio clip has no notes to find.
  const Clip audio = makeAudioClip(fixture.media, beat(0), beats(1));
  REQUIRE(findNote(audio, NoteId{1}) == nullptr);
}

TEST_CASE("every fault has its own name", "[model][document]") {
  constexpr std::array faults{
      Fault::UnknownObject,   Fault::IndexOutOfRange, Fault::ValueOutOfRange,
      Fault::WrongTrackKind,  Fault::WrongClipKind,   Fault::SlotOccupied,
      Fault::ClipOverlap,     Fault::NoteOverlap,     Fault::IdInvalid,
      Fault::MediaInUse,      Fault::MediaMissing,    Fault::DeviceChainOrder,
      Fault::SlotsMismatch,   Fault::SendsMismatch,   Fault::OrderViolation,
      Fault::EnvelopeTarget,  Fault::WrongInputKind,  Fault::NoTransaction,
      Fault::TransactionOpen, Fault::NothingToUndo,   Fault::NothingToRedo,
      Fault::IdsExhausted,    Fault::TextInvalid,
  };
  std::set<std::string_view> names;
  for (const Fault fault : faults) {
    const std::string_view name = faultName(fault);
    REQUIRE(!name.empty());
    REQUIRE(name != "unknown-fault");
    names.insert(name);
  }
  REQUIRE(names.size() == faults.size());
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): on purpose
  const auto outside = static_cast<Fault>(250);
  REQUIRE(faultName(outside) == "unknown-fault");
}

} // namespace lina::model::testing
