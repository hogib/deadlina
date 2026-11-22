// SPDX-License-Identifier: GPL-3.0-or-later
//
// The thin models and controllers over the document: the track list, the
// transport, the layout, the appearance, and values as musicians read and
// type them.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "lina/model/command.hpp"
#include "lina/ui/appearance.hpp"
#include "lina/ui/document_controller.hpp"
#include "lina/ui/testing/fake_playback.hpp"
#include "lina/ui/track_list_model.hpp"
#include "lina/ui/transport_controller.hpp"
#include "lina/ui/units.hpp"
#include "lina/ui/workspace.hpp"
#include "support/generator.hpp"

#include <QAbstractItemModelTester>
#include <QColor>
#include <QSignalSpy>

#include <vector>

using lina::ui::Appearance;
using lina::ui::DocumentController;
using lina::ui::TrackListModel;
using lina::ui::TransportController;
using lina::ui::Workspace;
using lina::ui::testing::FakePlayback;
namespace model = lina::model;

namespace {

constexpr std::int64_t quarter = model::ticksPerQuarter;

model::AddTrack track(model::TrackKind kind, std::string name,
                      std::size_t index) {
  return {.kind = kind, .name = std::move(name), .index = index};
}

struct Fixture {
  FakePlayback playback;
  lina::format::PosixFileSystem files;
  DocumentController document{playback, files, "LINA test"};
};

/// What the model shows, row by row, against the document.
void requireRowsMatch(const TrackListModel &tracks,
                      const DocumentController &document) {
  const auto &expected = document.project().tracks;
  REQUIRE(tracks.rowCount() == static_cast<int>(expected.size()));
  REQUIRE(tracks.count() == tracks.rowCount());
  for (std::size_t row = 0; row < expected.size(); ++row) {
    const QModelIndex at = tracks.index(static_cast<int>(row));
    REQUIRE(tracks.trackAt(static_cast<int>(row)) == expected.at(row).id);
    REQUIRE(
        tracks.data(at, TrackListModel::NameRole).toString().toStdString() ==
        expected.at(row).name);
    REQUIRE(tracks.data(at, TrackListModel::ColourRole).value<QColor>() ==
            QColor::fromRgb(expected.at(row).colour.rgb));
  }
}

} // namespace

TEST_CASE("the track list follows edits with the smallest signals",
          "[ui][models]") {
  Fixture fixture;
  DocumentController &document = fixture.document;
  TrackListModel tracks(document);
  const QAbstractItemModelTester tester(
      &tracks, QAbstractItemModelTester::FailureReportingMode::Fatal);
  const QSignalSpy inserted(&tracks, &QAbstractItemModel::rowsInserted);
  const QSignalSpy removed(&tracks, &QAbstractItemModel::rowsRemoved);
  const QSignalSpy changed(&tracks, &QAbstractItemModel::dataChanged);
  const QSignalSpy reset(&tracks, &QAbstractItemModel::modelReset);
  const QSignalSpy count(&tracks, &TrackListModel::countChanged);

  REQUIRE(document.perform("Add", track(model::TrackKind::Audio, "Drums", 0)));
  REQUIRE(document.perform("Add", track(model::TrackKind::Midi, "Keys", 1)));
  CHECK(inserted.count() == 2);
  CHECK(count.count() == 2);
  requireRowsMatch(tracks, document);

  const QModelIndex keys = tracks.index(1);
  CHECK(tracks.data(keys, TrackListModel::TrackIdRole).toString() ==
        QString::number(document.project().tracks.at(1).id.value));
  CHECK(tracks.data(keys, TrackListModel::KindRole) == "midi");
  CHECK(tracks.data(keys, TrackListModel::KindTextRole) == "MIDI");
  CHECK(tracks.data(tracks.index(0), TrackListModel::KindRole) == "audio");
  CHECK(tracks.data(tracks.index(0), TrackListModel::KindTextRole) == "Audio");
  CHECK(tracks.data(keys, Qt::DisplayRole) == "Keys");
  CHECK(!tracks.data(keys, Qt::DecorationRole).isValid());
  CHECK(!tracks.data(QModelIndex(), Qt::DisplayRole).isValid());
  CHECK(tracks.rowCount(keys) == 0);
  CHECK(tracks.trackAt(5) == model::TrackId{});
  CHECK(tracks.roleNames().value(TrackListModel::ColourRole) == "trackColour");

  // A rename is one row's data.
  const model::TrackId drums = document.project().tracks.at(0).id;
  REQUIRE(document.perform(
      "Rename", model::SetTrackProperty{drums, model::NameValue{"Beat"}}));
  REQUIRE(changed.count() == 1);
  CHECK(changed.at(0).at(0).value<QModelIndex>().row() == 0);
  REQUIRE(document.perform(
      "Recolour",
      model::SetTrackProperty{drums, model::ColourValue{{0x123456}}}));
  CHECK(changed.count() == 2);
  // A mixer move is not shown here.
  REQUIRE(document.perform(
      "Mute", model::SetTrackProperty{drums, model::MuteValue{true}}));
  CHECK(changed.count() == 2);
  requireRowsMatch(tracks, document);

  // A removal is one row; undoing it is one row back.
  REQUIRE(document.perform("Remove", model::RemoveTrack{drums}));
  CHECK(removed.count() == 1);
  document.undo();
  CHECK(inserted.count() == 3);
  requireRowsMatch(tracks, document);

  // A reorder resets.
  REQUIRE(document.perform("Move", model::MoveTrack{drums, 1}));
  CHECK(reset.count() == 1);
  requireRowsMatch(tracks, document);

  // So does another project.
  document.requestNew();
  document.prompt()->answer(1);
  CHECK(reset.count() == 2);
  CHECK(tracks.count() == 0);
}

TEST_CASE("the track list matches the document after generated edits",
          "[ui][models]") {
  Fixture fixture;
  DocumentController &document = fixture.document;
  TrackListModel tracks(document);
  const QAbstractItemModelTester tester(
      &tracks, QAbstractItemModelTester::FailureReportingMode::Fatal);
  model::testing::CommandGenerator generator(20261122);
  for (int step = 0; step < 600; ++step) {
    CAPTURE(step);
    if (generator.rarely(6)) {
      document.undo();
    } else if (generator.rarely(8)) {
      document.redo();
    } else {
      document.perform("Generated", generator.next(document.project()));
    }
    requireRowsMatch(tracks, document);
  }
  CHECK(fixture.playback.project == document.project());
}

TEST_CASE("play starts at the start marker; stop leaves the playhead, and a "
          "second stop returns it",
          "[ui][transport]") {
  Fixture fixture;
  DocumentController &document = fixture.document;
  FakePlayback &playback = fixture.playback;
  TransportController transport(document, playback);
  const QSignalSpy playing(&transport, &TransportController::playingChanged);
  REQUIRE(document.perform(
      "Move Start Marker",
      model::SetProjectProperty{model::StartMarkerValue{{4 * quarter}}}));

  CHECK(transport.canPlay());
  CHECK(transport.positionText() == "1.1.1");
  CHECK(transport.stopReturns());
  transport.play();
  CHECK(transport.playing());
  CHECK(playing.count() == 1);
  REQUIRE(playback.playedFrom.size() == 1);
  CHECK(playback.playedFrom.at(0) == model::TickPosition{4 * quarter});
  CHECK(transport.positionText() == "2.1.1");
  CHECK(!transport.stopReturns());

  // The engine moves on; the interface reads it once per frame.
  playback.position = {(5 * quarter) + (quarter / 2)};
  CHECK(transport.positionText() == "2.1.1");
  transport.poll();
  CHECK(transport.positionText() == "2.2.3");

  // First stop: the playhead stays.
  playback.position = {6 * quarter};
  transport.stop();
  CHECK(!transport.playing());
  CHECK(playback.stops == 1);
  CHECK(transport.position() == model::TickPosition{6 * quarter});
  CHECK(transport.stopReturns());
  // Polling while stopped reads nothing.
  transport.poll();
  CHECK(transport.position() == model::TickPosition{6 * quarter});

  // Continue starts there.
  transport.continuePlayback();
  CHECK(playback.playedFrom.back() == model::TickPosition{6 * quarter});
  transport.togglePlayback();
  CHECK(!transport.playing());

  // Second stop: back to the start marker.
  transport.stop();
  CHECK(transport.position() == model::TickPosition{4 * quarter});
  CHECK(!transport.stopReturns());

  // Space plays from the start marker.
  transport.togglePlayback();
  CHECK(playback.playedFrom.back() == model::TickPosition{4 * quarter});

  // An engine that stopped by itself is noticed on the next frame, and told
  // to stop, so that nothing keeps running for it.
  const int stops = playback.stops;
  playback.playing = false;
  transport.poll();
  CHECK(!transport.playing());
  CHECK(playback.stops == stops + 1);
}

TEST_CASE("without an output the transport says why it cannot play",
          "[ui][transport]") {
  Fixture fixture;
  fixture.playback.output = false;
  TransportController transport(fixture.document, fixture.playback);
  const QSignalSpy canPlay(&transport, &TransportController::canPlayChanged);
  CHECK(!transport.canPlay());
  transport.play();
  CHECK(!transport.playing());
  CHECK(fixture.playback.playedFrom.empty());
  CHECK(fixture.document.notice().contains("--no-audio"));

  fixture.playback.output = true;
  transport.refreshOutput();
  CHECK(transport.canPlay());
  CHECK(canPlay.count() == 1);
  transport.play();
  CHECK(transport.playing());

  // The output goes away while playing.
  fixture.playback.output = false;
  transport.refreshOutput();
  CHECK(!transport.playing());
  transport.refreshOutput();
  CHECK(canPlay.count() == 2);
}

TEST_CASE("another project stops the transport and starts at its marker",
          "[ui][transport]") {
  Fixture fixture;
  TransportController transport(fixture.document, fixture.playback);
  transport.play();
  REQUIRE(transport.playing());
  fixture.document.requestNew();
  CHECK(!transport.playing());
  CHECK(fixture.playback.stops == 1);
  CHECK(transport.positionText() == "1.1.1");
}

TEST_CASE("the tempo is typed, stepped, reset and dragged through commands",
          "[ui][transport]") {
  Fixture fixture;
  DocumentController &document = fixture.document;
  TransportController transport(document, fixture.playback);
  const QSignalSpy tempo(&transport, &TransportController::tempoChanged);

  CHECK(transport.tempoText() == "120.00");
  CHECK(transport.setTempoText("128,5"));
  CHECK(transport.tempoText() == "128.50");
  CHECK(document.undoText() == "Undo Change Tempo");
  CHECK(tempo.count() == 1);

  CHECK(!transport.setTempoText("fast"));
  CHECK(document.noticeIsError());
  CHECK(document.notice().contains("fast"));
  CHECK(transport.tempoText() == "128.50");
  // The field is told to show the tempo as it is.
  CHECK(tempo.count() == 2);

  transport.stepTempo(1, false);
  CHECK(transport.tempoText() == "129.50");
  transport.stepTempo(-3, true);
  CHECK(transport.tempoText() == "129.47");
  transport.resetTempo();
  CHECK(transport.tempoText() == "120.00");
  // Already 120: no step.
  const QString before = document.undoText();
  transport.resetTempo();
  CHECK(document.undoText() == before);

  // A drag is one step however far it goes, and moves the engine as it goes.
  const std::size_t changeSets = fixture.playback.changeSets.size();
  transport.beginTempoDrag();
  transport.beginTempoDrag();
  transport.dragTempo(4.0, false);
  CHECK(transport.tempoText() == "121.00");
  transport.dragTempo(4.0, false);
  transport.dragTempo(40.0, true);
  CHECK(transport.tempoText() == "120.40");
  transport.dragTempo(1.0e6, false);
  CHECK(transport.tempoText() == "300.00");
  transport.endTempoDrag();
  CHECK(fixture.playback.changeSets.size() == changeSets + 3);
  document.undo();
  CHECK(transport.tempoText() == "120.00");

  // Escape during a drag takes it back.
  transport.beginTempoDrag();
  transport.dragTempo(-8.0, false);
  CHECK(transport.tempoText() == "118.00");
  transport.cancelTempoDrag();
  CHECK(transport.tempoText() == "120.00");
  CHECK(document.undoText() == "Undo Change Tempo");

  // Without a drag these do nothing.
  transport.dragTempo(10.0, false);
  transport.endTempoDrag();
  transport.cancelTempoDrag();
  CHECK(transport.tempoText() == "120.00");

  // A drag cannot start while another gesture is open.
  REQUIRE(document.beginGesture("Other"));
  transport.beginTempoDrag();
  transport.dragTempo(10.0, false);
  document.endGesture();
  CHECK(transport.tempoText() == "120.00");
}

TEST_CASE("the start marker and the time signature move what is shown",
          "[ui][transport]") {
  Fixture fixture;
  TransportController transport(fixture.document, fixture.playback);
  const QSignalSpy position(&transport, &TransportController::positionChanged);
  REQUIRE(fixture.document.perform(
      "Time Signature",
      model::SetProjectProperty{model::TimeSignatureValue{
          model::TimeSignature::make(6, 8).value_or(model::TimeSignature{})}}));
  CHECK(position.count() == 1);
  REQUIRE(fixture.document.perform(
      "Start Marker",
      model::SetProjectProperty{model::StartMarkerValue{{quarter}}}));
  CHECK(position.count() == 2);
  REQUIRE(fixture.document.perform(
      "Loop", model::SetProjectProperty{model::LoopRangeValue{
                  {.enabled = true, .start = {}, .length = {quarter}}}}));
  CHECK(position.count() == 2);
}

TEST_CASE("tempo and position text", "[ui][units]") {
  using lina::ui::formatPosition;
  using lina::ui::formatTempo;
  using lina::ui::offsetTempo;
  using lina::ui::parseTempo;
  const auto tempo = [](std::int32_t hundredths) {
    return model::Tempo::fromHundredths(hundredths).value_or(model::Tempo{});
  };

  CHECK(formatTempo(tempo(12000)) == "120.00");
  CHECK(formatTempo(tempo(2005)) == "20.05");
  CHECK(formatTempo(tempo(30000)) == "300.00");

  CHECK(parseTempo("128") == tempo(12800));
  CHECK(parseTempo(" 96.5 ") == tempo(9650));
  CHECK(parseTempo("96,25") == tempo(9625));
  CHECK(parseTempo("140 BPM") == tempo(14000));
  CHECK(parseTempo("140bpm") == tempo(14000));
  CHECK(parseTempo("120.") == tempo(12000));
  CHECK(parseTempo("120.004") == tempo(12000));
  CHECK(parseTempo("120.005") == tempo(12001));
  CHECK(parseTempo("299.995") == tempo(30000));
  CHECK(!parseTempo("300.005").has_value());
  CHECK(!parseTempo("19.99").has_value());
  CHECK(!parseTempo("").has_value());
  CHECK(!parseTempo(".5").has_value());
  CHECK(!parseTempo("-120").has_value());
  CHECK(!parseTempo("12e1").has_value());
  CHECK(!parseTempo("120.5a").has_value());
  CHECK(!parseTempo("1,200.5").has_value());
  CHECK(!parseTempo("99999999999").has_value());
  CHECK(!parseTempo("١٢٠").has_value());

  CHECK(offsetTempo(tempo(12000), 50) == tempo(12050));
  CHECK(offsetTempo(tempo(2100), -500) == tempo(2000));
  CHECK(offsetTempo(tempo(29900), 500) == tempo(30000));

  const auto fourFour = model::TimeSignature{};
  CHECK(formatPosition({}, fourFour) == "1.1.1");
  CHECK(formatPosition({quarter / 4}, fourFour) == "1.1.2");
  CHECK(formatPosition({(quarter / 4) - 1}, fourFour) == "1.1.1");
  CHECK(formatPosition({5 * quarter}, fourFour) == "2.2.1");
  const auto sixEight =
      model::TimeSignature::make(6, 8).value_or(model::TimeSignature{});
  CHECK(formatPosition({3 * quarter}, sixEight) == "2.1.1");
  CHECK(formatPosition({-1}, fourFour) == "–");
}

TEST_CASE("one pane has the keys, and it is always one that shows",
          "[ui][workspace]") {
  Workspace workspace;
  const QSignalSpy focused(&workspace, &Workspace::focusedPaneChanged);
  const QSignalSpy returned(&workspace, &Workspace::focusReturned);
  const QSignalSpy views(&workspace, &Workspace::viewsChanged);

  CHECK(workspace.focusedPane() == Workspace::Pane::Session);
  workspace.focusNextPane();
  CHECK(workspace.focusedPane() == Workspace::Pane::Arrangement);
  workspace.focusPreviousPane();
  CHECK(workspace.focusedPane() == Workspace::Pane::Session);
  CHECK(focused.count() == 2);
  CHECK(returned.count() == 2);

  // Hiding the pane that has the keys moves them.
  workspace.setViews(Workspace::Views::ArrangementOnly);
  CHECK(!workspace.sessionVisible());
  CHECK(workspace.arrangementVisible());
  CHECK(workspace.focusedPane() == Workspace::Pane::Arrangement);
  workspace.setViews(Workspace::Views::ArrangementOnly);
  CHECK(views.count() == 1);
  // With one pane, F6 gives it the keys back.
  workspace.focusNextPane();
  CHECK(workspace.focusedPane() == Workspace::Pane::Arrangement);
  CHECK(returned.count() == 4);

  // Going to a hidden pane shows both.
  workspace.setFocusedPane(Workspace::Pane::Session);
  CHECK(workspace.views() == Workspace::Views::Both);
  CHECK(workspace.focusedPane() == Workspace::Pane::Session);

  workspace.setViews(Workspace::Views::SessionOnly);
  CHECK(workspace.focusedPane() == Workspace::Pane::Session);
  CHECK(!workspace.arrangementVisible());
  // Hiding the arrangement while it has the keys gives them to the session.
  workspace.setFocusedPane(Workspace::Pane::Arrangement);
  workspace.setViews(Workspace::Views::SessionOnly);
  CHECK(workspace.focusedPane() == Workspace::Pane::Session);

  workspace.returnFocus();
  CHECK(returned.count() == 8);
}

TEST_CASE("the divider stays where both views keep room", "[ui][workspace]") {
  Workspace workspace;
  const QSignalSpy split(&workspace, &Workspace::splitChanged);
  CHECK(workspace.split() == Workspace::defaultSplit);

  workspace.setSplit(0.7);
  CHECK(workspace.split() == Catch::Approx(0.7));
  workspace.setSplit(0.7);
  CHECK(split.count() == 1);
  workspace.setSplit(2.0);
  CHECK(workspace.split() == Workspace::maximumSplit);
  workspace.setSplit(-1.0);
  CHECK(workspace.split() == Workspace::minimumSplit);
  workspace.setSplit(std::numeric_limits<double>::quiet_NaN());
  CHECK(workspace.split() == Workspace::minimumSplit);

  CHECK(workspace.splitAfterDrag(0.5, 100.0, 1000.0) == Catch::Approx(0.6));
  CHECK(workspace.splitAfterDrag(0.5, -1000.0, 1000.0) ==
        Workspace::minimumSplit);
  // A view with no height: the split stays.
  CHECK(workspace.splitAfterDrag(0.5, 10.0, 0.0) == workspace.split());
  CHECK(workspace.splitAfterDrag(0.5, std::numeric_limits<double>::infinity(),
                                 100.0) == workspace.split());

  workspace.resetDivider();
  workspace.moveDivider(2);
  CHECK(workspace.split() == Catch::Approx(0.6));
  workspace.moveDivider(-1);
  CHECK(workspace.split() == Catch::Approx(0.55));
}

TEST_CASE("the help area shows what was pointed at last", "[ui][workspace]") {
  Workspace workspace;
  const QSignalSpy help(&workspace, &Workspace::helpChanged);
  workspace.showHelp("Play", "Starts playing.");
  CHECK(workspace.helpTitle() == "Play");
  CHECK(workspace.helpText() == "Starts playing.");
  workspace.showHelp("Play", "Starts playing.");
  CHECK(help.count() == 1);
  workspace.showHelp("Play", "Starts playing from the start marker.");
  CHECK(help.count() == 2);
  CHECK(workspace.helpText() == "Starts playing from the start marker.");
  // Entering the next control before leaving the last one.
  workspace.showHelp("Stop", "Stops.");
  workspace.clearHelp("Play");
  CHECK(workspace.helpTitle() == "Stop");
  workspace.clearHelp("Stop");
  CHECK(workspace.helpTitle().isEmpty());
  CHECK(help.count() == 4);
}

TEST_CASE("light or dark follows the desktop unless the user picks one",
          "[ui][appearance]") {
  Appearance appearance;
  const QSignalSpy colours(&appearance, &Appearance::coloursChanged);
  const QSignalSpy mode(&appearance, &Appearance::modeChanged);

  appearance.setDesktopScheme(Qt::ColorScheme::Light);
  CHECK(!appearance.dark());
  CHECK(appearance.window() == lina::ui::lightColours().window);
  appearance.setDesktopScheme(Qt::ColorScheme::Dark);
  CHECK(appearance.dark());
  CHECK(colours.count() == 1);
  CHECK(appearance.window() == lina::ui::darkColours().window);
  CHECK(appearance.text() == lina::ui::darkColours().text);
  // An unknown scheme is light.
  appearance.setDesktopScheme(Qt::ColorScheme::Unknown);
  CHECK(!appearance.dark());

  appearance.setMode(Appearance::Mode::Dark);
  CHECK(appearance.dark());
  appearance.setMode(Appearance::Mode::Dark);
  CHECK(mode.count() == 1);
  // The desktop no longer decides.
  appearance.setDesktopScheme(Qt::ColorScheme::Light);
  CHECK(appearance.dark());
  appearance.setMode(Appearance::Mode::Light);
  CHECK(!appearance.dark());
  appearance.setDesktopScheme(Qt::ColorScheme::Dark);
  CHECK(!appearance.dark());
  appearance.setMode(Appearance::Mode::FollowDesktop);
  CHECK(appearance.dark());
  // Dark to dark: no colour change.
  const int before = static_cast<int>(colours.count());
  appearance.setMode(Appearance::Mode::Dark);
  CHECK(colours.count() == before);

  for (const QColor &colour : {
           appearance.surface(),
           appearance.raised(),
           appearance.mutedText(),
           appearance.divider(),
           appearance.accent(),
           appearance.onAccent(),
           appearance.focus(),
           appearance.warning(),
           appearance.error(),
       }) {
    CHECK(colour.isValid());
  }
}

TEST_CASE("text is readable in both appearances", "[ui][appearance]") {
  using lina::ui::contrastRatio;
  CHECK(contrastRatio(Qt::black, Qt::white) == Catch::Approx(21.0));
  CHECK(contrastRatio(Qt::white, Qt::white) == Catch::Approx(1.0));
  for (const auto *colours :
       {&lina::ui::lightColours(), &lina::ui::darkColours()}) {
    // WCAG AA: 4.5 for text, 3 for marks that must be seen.
    for (const QColor &background :
         {colours->window, colours->surface, colours->raised}) {
      CHECK(contrastRatio(colours->text, background) >= 7.0);
      CHECK(contrastRatio(colours->mutedText, background) >= 4.5);
      CHECK(contrastRatio(colours->warning, background) >= 4.5);
      CHECK(contrastRatio(colours->error, background) >= 4.5);
      CHECK(contrastRatio(colours->focus, background) >= 3.0);
    }
    CHECK(contrastRatio(colours->onAccent, colours->accent) >= 4.5);
  }
}

TEST_CASE("everything can be made larger or smaller in steps",
          "[ui][appearance]") {
  Appearance appearance;
  const QSignalSpy scale(&appearance, &Appearance::scaleChanged);
  CHECK(appearance.scale() == 1.0);
  CHECK(appearance.unit() == Appearance::baseUnit);
  CHECK(appearance.fontPointSize() == Appearance::fallbackPointSize);
  appearance.larger();
  CHECK(appearance.scale() == 1.125);
  CHECK(appearance.fontPointSize() ==
        Catch::Approx(Appearance::fallbackPointSize * 1.125));
  for (int step = 0; step < 20; ++step) {
    appearance.larger();
  }
  CHECK(appearance.scale() == Appearance::scaleSteps.back());
  CHECK(!appearance.canGrow());
  CHECK(appearance.canShrink());
  for (int step = 0; step < 20; ++step) {
    appearance.smaller();
  }
  CHECK(appearance.scale() == Appearance::scaleSteps.front());
  CHECK(!appearance.canShrink());
  appearance.resetScale();
  CHECK(appearance.scale() == 1.0);
  // 1 + 5 up to the top, 8 down to the bottom, 1 back.
  CHECK(scale.count() == 15);
}

TEST_CASE("animations stop when the desktop asks for less motion",
          "[ui][appearance]") {
  Appearance appearance;
  const QSignalSpy motion(&appearance, &Appearance::motionChanged);
  CHECK(!appearance.reducedMotion());
  CHECK(appearance.animationMs() == Appearance::animationLength);
  appearance.setDesktopMotion(Qt::MotionPreference::ReducedMotion);
  appearance.setDesktopMotion(Qt::MotionPreference::ReducedMotion);
  CHECK(motion.count() == 1);
  CHECK(appearance.reducedMotion());
  CHECK(appearance.animationMs() == 0);
}

TEST_CASE("a row read while another project comes in is empty",
          "[ui][models]") {
  Fixture fixture;
  DocumentController &document = fixture.document;
  REQUIRE(document.perform("Add", track(model::TrackKind::Audio, "Drums", 0)));
  // Connected before the model: runs while the model still has the rows of
  // the project that went.
  QVariant seen{QStringLiteral("not read")};
  TrackListModel *model = nullptr;
  QObject::connect(&document, &DocumentController::projectReset, [&] {
    seen = model->data(model->index(0), TrackListModel::NameRole);
  });
  TrackListModel tracks(document);
  model = &tracks;
  CHECK(tracks.trackAt(-1) == model::TrackId{});
  document.requestNew();
  document.prompt()->answer(1);
  CHECK(!seen.isValid());
  CHECK(tracks.count() == 0);
}

TEST_CASE("a stop after the engine stopped by itself keeps the playhead",
          "[ui][transport]") {
  Fixture fixture;
  TransportController transport(fixture.document, fixture.playback);
  transport.play();
  fixture.playback.position = {3 * quarter};
  transport.poll();
  // Stopped underneath, before the next frame noticed.
  fixture.playback.playing = false;
  fixture.playback.position = {9 * quarter};
  transport.stop();
  CHECK(!transport.playing());
  CHECK(transport.position() == model::TickPosition{3 * quarter});
}
