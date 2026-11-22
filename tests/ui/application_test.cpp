// SPDX-License-Identifier: GPL-3.0-or-later
//
// The real application object graph: Qt, the engine framework's message
// thread, the event-loop bridge, the engine, the shell and the window, on
// Qt's offscreen platform and with no audio output, as automated runs must.
// It comes up, draws, takes keys, and goes down again without a JUCE
// assertion, a leaked object or a QML warning.
#include <catch2/catch_test_macros.hpp>

#include "lina/app/application.hpp"
#include "lina/engine/testing/test_environment.hpp"
#include "lina/model/command.hpp"
#include "lina/ui/document_controller.hpp"
#include "lina/ui/testing/fake_playback.hpp"
#include "lina/ui/testing/headless.hpp"

#include <QKeyEvent>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QTest>

#include <functional>
#include <string>
#include <vector>

using lina::app::Application;
using lina::app::Options;
using lina::app::parseOptions;

namespace {

// qInstallMessageHandler takes a plain function, so what it collects has to
// live in a static: Qt forces this one.
std::vector<std::string> &warnings() {
  static std::vector<std::string> collected;
  return collected;
}

void collectWarnings(QtMsgType type, const QMessageLogContext & /*context*/,
                     const QString &message) {
  if (type != QtDebugMsg && type != QtInfoMsg) {
    warnings().push_back(message.toStdString());
  }
}

/// Every item under `item`, depth first.
void forEachItem(QQuickItem *item, const std::function<void(QQuickItem *)> &f) {
  f(item);
  for (QQuickItem *child : item->childItems()) {
    forEachItem(child, f);
  }
}

QString accessibleName(QQuickItem *item) {
  QQmlContext *context = qmlContext(item);
  if (context == nullptr) {
    return {};
  }
  QQmlExpression expression(context, item, QStringLiteral("Accessible.name"));
  return expression.evaluate().toString();
}

/// What automated runs start with: no audio output.
Options withoutAudio() {
  Options options;
  options.audio = false;
  return options;
}

void press(QQuickWindow &window, QKeyCombination key) {
  QKeyEvent event(QEvent::KeyPress, key.key(), key.keyboardModifiers());
  QCoreApplication::sendEvent(&window, &event);
}

} // namespace

TEST_CASE("the application comes up offscreen without audio and goes down "
          "cleanly",
          "[app]") {
  const auto scratch = lina::ui::testing::enterHeadless("app-start");
  const lina::engine::testing::AssertionCounter assertions;
  qInstallMessageHandler(collectWarnings);
  const lina::ui::testing::GuiApplication qt;
  {
    Application lina(withoutAudio(), scratch / "engine");
    QQuickWindow *window = lina.window();
    REQUIRE(window != nullptr);
    CHECK(window->isVisible());
    CHECK(!lina.hasAudioOutput());
    CHECK(!lina.shell().transport()->canPlay());
    CHECK(window->title() == "Untitled – LINA");

    bool drawn = false;
    QObject::connect(window, &QQuickWindow::frameSwapped,
                     [&drawn] { drawn = true; });
    window->update();
    REQUIRE(lina::ui::testing::spinUntil([&] { return drawn; }));

    // The layout of doc 12: session above arrangement, a divider between.
    auto *session = window->findChild<QQuickItem *>("session");
    auto *arrangement = window->findChild<QQuickItem *>("arrangement");
    auto *divider = window->findChild<QQuickItem *>("divider");
    REQUIRE(session != nullptr);
    REQUIRE(arrangement != nullptr);
    REQUIRE(divider != nullptr);
    CHECK(session->y() < divider->y());
    CHECK(divider->y() < arrangement->y());
    CHECK(session->hasActiveFocus());

    // Every control and every pane the keyboard reaches has a name.
    int named = 0;
    forEachItem(window->contentItem(), [&](QQuickItem *item) {
      if (item->activeFocusOnTab() || item->inherits("QQuickAbstractButton") ||
          item->inherits("QQuickTextInput")) {
        CAPTURE(item->metaObject()->className(),
                item->objectName().toStdString());
        CHECK(!accessibleName(item).isEmpty());
        ++named;
      }
    });
    CHECK(named >= 9);

    // Keys reach commands through the window.
    press(*window, Qt::ControlModifier | Qt::Key_T);
    CHECK(lina.shell().tracks()->count() == 1);
    CHECK(window->title() == "*Untitled – LINA");
    // Space without an output: nothing plays, and the notice says why.
    auto *document = lina.shell().document();
    press(*window, QKeyCombination(Qt::Key_Space));
    CHECK(!lina.shell().transport()->playing());
    CHECK(document->notice().contains("--no-audio"));
    // So does a click on Play.
    document->dismissNotice();
    auto *play = window->findChild<QQuickItem *>("playButton");
    REQUIRE(play != nullptr);
    CHECK(play->isEnabled());
    const QPointF centre =
        play->mapToScene(QPointF(play->width() / 2, play->height() / 2));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centre.toPoint());
    CHECK(!lina.shell().transport()->playing());
    CHECK(document->notice().contains("--no-audio"));
  }
  qInstallMessageHandler(nullptr);
  for (const std::string &warning : warnings()) {
    UNSCOPED_INFO(warning);
  }
  CHECK(warnings().empty());
  CHECK(assertions.count() == 0);
}

TEST_CASE("the application opens a project named on the command line",
          "[app]") {
  const auto scratch = lina::ui::testing::enterHeadless("app-open");
  const lina::engine::testing::AssertionCounter assertions;
  const lina::ui::testing::GuiApplication qt;
  {
    // Written without a second application: ThreadSanitizer cannot follow
    // two QML engines' loader threads in one process (tests/ui/CMakeLists.txt).
    lina::ui::testing::FakePlayback playback;
    lina::format::PosixFileSystem files;
    lina::ui::DocumentController writer(playback, files, "LINA test");
    REQUIRE(writer.perform(
        "Add MIDI Track",
        lina::model::AddTrack{
            .kind = lina::model::TrackKind::Midi, .name = "Keys", .index = 0}));
    REQUIRE(writer.saveAsFolder(scratch / "Song"));
  }
  {
    Options options = withoutAudio();
    options.project = QString::fromStdString((scratch / "Song").string());
    Application lina(options, scratch / "engine");
    CHECK(lina.shell().tracks()->count() == 1);
    CHECK(lina.window()->title() == "Song – LINA");
  }
  CHECK(assertions.count() == 0);
}

TEST_CASE("with audio, the application opens its output and closes it first",
          "[app]") {
  // The output is the engine's own manual device, run in step with real
  // time; no sound device of the machine is opened. Nothing here depends on
  // how much time passes.
  const auto scratch = lina::ui::testing::enterHeadless("app-audio");
  const lina::engine::testing::AssertionCounter assertions;
  const lina::ui::testing::GuiApplication qt;
  {
    Options options;
    options.audio = true;
    Application lina(options, scratch / "engine");
    CHECK(lina.hasAudioOutput());
    const auto *transport = lina.shell().transport();
    CHECK(transport->canPlay());
    lina.shell().run("transport.play");
    CHECK(transport->playing());
    CHECK(lina.shell().document()->notice().isEmpty());

    // Shutting down closes the output before anything else goes, and the
    // transport learns that it stopped.
    lina.closeAudioOutput();
    CHECK(!lina.hasAudioOutput());
    CHECK(!transport->playing());
    CHECK(!transport->canPlay());
  }
  CHECK(assertions.count() == 0);
}

TEST_CASE("the command line", "[app]") {
  const auto parse = [](QStringList arguments, const QString &noAudio = {}) {
    arguments.prepend("lina");
    return parseOptions(arguments, noAudio);
  };
  auto plain = parse({});
  REQUIRE(plain.has_value());
  CHECK(plain->audio);
  CHECK(!plain->quitAfterStart);
  CHECK(!plain->project.has_value());
  CHECK(!plain->showHelp);
  CHECK(!plain->showVersion);
  CHECK(plain->helpText.contains("--no-audio"));
  // A switch for tests is not offered to users.
  CHECK(!plain->helpText.contains("--quit-after-start"));

  auto quiet = parse({"--no-audio", "--quit-after-start", "/tmp/Song"});
  REQUIRE(quiet.has_value());
  CHECK(!quiet->audio);
  CHECK(quiet->quitAfterStart);
  CHECK(quiet->project == "/tmp/Song");

  CHECK(!parse({}, "1")->audio);
  CHECK(!parse({}, "yes")->audio);
  CHECK(parse({}, "0")->audio);
  CHECK(parse({"--help"})->showHelp);
  CHECK(parse({"--version"})->showVersion);

  CHECK(!parse({"--loud"}).has_value());
  CHECK(!parse({"one", "two"}).has_value());
}
