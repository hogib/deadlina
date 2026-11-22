// SPDX-License-Identifier: GPL-3.0-or-later
//
// The command registry, the searchable command list over it, and how a key
// press becomes a command.
#include <catch2/catch_test_macros.hpp>

#include "lina/ui/command_palette.hpp"
#include "lina/ui/command_registry.hpp"
#include "lina/ui/shortcut_dispatcher.hpp"
#include "lina/ui/testing/headless.hpp"

#include <QAbstractItemModelTester>
#include <QSignalSpy>

#include <stdexcept>

using lina::ui::commandForKeyPress;
using lina::ui::CommandPalette;
using lina::ui::CommandRegistry;
using lina::ui::CommandSpec;
using lina::ui::matchScore;
using lina::ui::textEntryUses;

namespace {

QKeySequence chord(QKeyCombination combination) { return {combination}; }

/// A registry with a few commands that count how often they ran.
struct Commands {
  Commands() {
    registry.add({
        .id = "track.add",
        .name = "Add Audio Track",
        .description = "Adds a track.",
        .shortcuts = {chord(Qt::ControlModifier | Qt::Key_T)},
        .action = [this] { ++added; },
    });
    registry.add({
        .id = "transport.toggle",
        .name = "Play or Stop",
        .description = "Plays or stops.",
        .shortcuts = {chord(QKeyCombination(Qt::Key_Space))},
        .action = [this] { ++toggled; },
    });
    registry.add({
        .id = "view.larger",
        .name = "Make Everything Larger",
        .description = "Larger.",
        .shortcuts =
            {
                chord(Qt::ControlModifier | Qt::Key_Plus),
                chord(Qt::ControlModifier | Qt::Key_Equal),
            },
        .action = [this] { ++larger; },
    });
    registry.add({
        .id = "view.tempo",
        .name = "Set Tempo…",
        .description = "Tempo.",
        .shortcuts = {},
        .action = [] {},
    });
  }

  int added = 0;
  int toggled = 0;
  int larger = 0;
  CommandRegistry registry;
};

} // namespace

TEST_CASE("a registered command runs by its id, once per trigger",
          "[ui][commands]") {
  Commands commands;
  const QSignalSpy triggered(&commands.registry, &CommandRegistry::triggered);

  CHECK(commands.registry.trigger("track.add"));
  CHECK(commands.registry.trigger("track.add"));
  CHECK(commands.added == 2);
  REQUIRE(triggered.count() == 2);
  CHECK(triggered.at(0).at(0).toString() == "track.add");
  CHECK(commands.registry.ids() == QStringList{"track.add", "transport.toggle",
                                               "view.larger", "view.tempo"});

  // Through its object, as a button runs it.
  auto *command = commands.registry.command("track.add");
  REQUIRE(command != nullptr);
  CHECK(command->trigger());
  CHECK(commands.added == 3);
  CHECK(command->commandId() == "track.add");
  CHECK(command->description() == "Adds a track.");
  CHECK(command->shortcut() ==
        QKeySequence("Ctrl+T").toString(QKeySequence::NativeText));
  CHECK(commands.registry.command("view.tempo")->shortcut().isEmpty());
  CHECK(commands.registry.command("no.such") == nullptr);
}

TEST_CASE("a disabled command, an unknown one and a suspended registry do "
          "not run",
          "[ui][commands]") {
  Commands commands;
  auto *command = commands.registry.command("track.add");
  const QSignalSpy changed(command, &lina::ui::UserCommand::changed);

  commands.registry.setEnabled("track.add", false);
  CHECK(!commands.registry.isEnabled("track.add"));
  CHECK(!command->enabled());
  CHECK(changed.count() == 1);
  // Setting the same state again says nothing.
  commands.registry.setEnabled("track.add", false);
  CHECK(changed.count() == 1);
  CHECK(!commands.registry.trigger("track.add"));
  CHECK(!command->trigger());
  CHECK(commands.added == 0);

  CHECK(!commands.registry.trigger("no.such"));

  const QSignalSpy suspended(&commands.registry,
                             &CommandRegistry::suspendedChanged);
  commands.registry.setSuspended(true);
  commands.registry.setSuspended(true);
  CHECK(suspended.count() == 1);
  CHECK(commands.registry.suspended());
  CHECK(!commands.registry.trigger("transport.toggle"));
  commands.registry.setSuspended(false);
  CHECK(commands.registry.trigger("transport.toggle"));
  CHECK(commands.toggled == 1);
}

TEST_CASE("naming a command that was never registered is a mistake",
          "[ui][commands]") {
  Commands commands;
  CHECK_THROWS_AS(commands.registry.setEnabled("no.such", true),
                  std::invalid_argument);
  CHECK_THROWS_AS(commands.registry.setName("no.such", "x"),
                  std::invalid_argument);
  CHECK_THROWS_AS(static_cast<void>(commands.registry.isEnabled("no.such")),
                  std::invalid_argument);
  CHECK_THROWS_AS(static_cast<void>(commands.registry.name("no.such")),
                  std::invalid_argument);
}

TEST_CASE("a command is registered once, with a name, an action and keys "
          "of its own",
          "[ui][commands]") {
  Commands commands;
  const auto noop = [] {};
  const auto spec = [&noop](QString id, QString name,
                            QList<QKeySequence> shortcuts = {}) {
    return CommandSpec{
        .id = std::move(id),
        .name = std::move(name),
        .description = "Does it.",
        .shortcuts = std::move(shortcuts),
        .action = noop,
    };
  };
  CHECK_THROWS_AS(commands.registry.add(spec("", "x")), std::invalid_argument);
  CHECK_THROWS_AS(commands.registry.add(spec("track.add", "x")),
                  std::invalid_argument);
  CHECK_THROWS_AS(commands.registry.add(spec("a", "")), std::invalid_argument);
  CommandSpec noAction = spec("a", "x");
  noAction.action = nullptr;
  CHECK_THROWS_AS(commands.registry.add(noAction), std::invalid_argument);
  CHECK_THROWS_AS(
      commands.registry.add(spec("a", "x", {QKeySequence("Ctrl+X, Ctrl+Y")})),
      std::invalid_argument);
  CHECK_THROWS_AS(
      commands.registry.add(spec("a", "x", {QKeySequence("Ctrl+T")})),
      std::invalid_argument);
  // Shift+Space differs from Space: it is free.
  CHECK_NOTHROW(
      commands.registry.add(spec("a", "x", {QKeySequence("Shift+Space")})));
  CHECK(commands.registry.rowCount() == 5);
}

TEST_CASE("a key combination finds its command", "[ui][commands]") {
  const Commands commands;
  const CommandRegistry &registry = commands.registry;
  CHECK(registry.commandForKey(Qt::ControlModifier | Qt::Key_T) == "track.add");
  CHECK(registry.commandForKey(QKeyCombination(Qt::Key_Space)) ==
        "transport.toggle");
  // The keypad's own modifier is not part of any shortcut.
  CHECK(registry.commandForKey(Qt::ControlModifier | Qt::KeypadModifier |
                               Qt::Key_Plus) == "view.larger");
  // "+" needs Shift on many layouts.
  CHECK(registry.commandForKey(Qt::ControlModifier | Qt::ShiftModifier |
                               Qt::Key_Plus) == "view.larger");
  CHECK(registry.commandForKey(Qt::ControlModifier | Qt::Key_Equal) ==
        "view.larger");
  // With a letter, Shift makes another combination.
  CHECK(registry
            .commandForKey(Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_T)
            .isEmpty());
  CHECK(registry.commandForKey(QKeyCombination(Qt::Key_Q)).isEmpty());
}

TEST_CASE("a text field keeps the keys it types and edits with",
          "[ui][commands]") {
  // The desktop's editing shortcuts come from Qt's platform theme.
  lina::ui::testing::enterHeadless("ui-commands-text-field");
  const lina::ui::testing::GuiApplication qt;
  const Commands commands;
  const CommandRegistry &registry = commands.registry;
  // Typing.
  CHECK(textEntryUses(QKeyCombination(Qt::Key_Space)));
  CHECK(textEntryUses(QKeyCombination(Qt::Key_A)));
  CHECK(textEntryUses(Qt::ShiftModifier | Qt::Key_A));
  CHECK(textEntryUses(QKeyCombination(Qt::Key_Left)));
  // Editing shortcuts.
  CHECK(textEntryUses(Qt::ControlModifier | Qt::Key_Z));
  CHECK(textEntryUses(Qt::ControlModifier | Qt::Key_C));
  CHECK(textEntryUses(Qt::ControlModifier | Qt::Key_A));
  CHECK(textEntryUses(Qt::ControlModifier | Qt::Key_Right));
  // The context menu key, past the function keys, is the field's too.
  CHECK(textEntryUses(QKeyCombination(Qt::Key_Menu)));
  // Commands.
  CHECK(!textEntryUses(QKeyCombination(Qt::Key_F6)));
  CHECK(!textEntryUses(Qt::ControlModifier | Qt::Key_T));
  CHECK(!textEntryUses(Qt::ControlModifier | Qt::Key_S));

  CHECK(commandForKeyPress(registry, QKeyCombination(Qt::Key_Space), false) ==
        "transport.toggle");
  // Transport keys work whichever pane has the keys, except while typing.
  CHECK(commandForKeyPress(registry, QKeyCombination(Qt::Key_Space), true)
            .isEmpty());
  CHECK(commandForKeyPress(registry, Qt::ControlModifier | Qt::Key_T, true) ==
        "track.add");
}

TEST_CASE("the registry is a list model of every command", "[ui][commands]") {
  Commands commands;
  const QAbstractItemModelTester tester(
      &commands.registry,
      QAbstractItemModelTester::FailureReportingMode::Fatal);
  CommandRegistry &registry = commands.registry;
  REQUIRE(registry.rowCount() == 4);
  const QModelIndex first = registry.index(0);
  CHECK(registry.data(first, CommandRegistry::IdRole) == "track.add");
  CHECK(registry.data(first, Qt::DisplayRole) == "Add Audio Track");
  CHECK(registry.data(first, CommandRegistry::DescriptionRole) ==
        "Adds a track.");
  CHECK(registry.data(first, CommandRegistry::ShortcutRole).toString() ==
        registry.command("track.add")->shortcut());
  CHECK(registry.data(first, CommandRegistry::EnabledRole) == true);
  CHECK(!registry.data(first, Qt::DecorationRole).isValid());
  CHECK(!registry.data(registry.index(9), Qt::DisplayRole).isValid());
  CHECK(registry.rowCount(first) == 0);
  CHECK(registry.roleNames().value(CommandRegistry::EnabledRole) ==
        "available");

  const QSignalSpy changed(&registry, &QAbstractItemModel::dataChanged);
  registry.setName("track.add", "Add Track");
  registry.setName("track.add", "Add Track");
  REQUIRE(changed.count() == 1);
  CHECK(changed.at(0).at(0).value<QModelIndex>().row() == 0);
  CHECK(registry.name("track.add") == "Add Track");
}

TEST_CASE("a command that renames itself while it runs", "[ui][commands]") {
  CommandRegistry registry;
  int runs = 0;
  registry.add({
      .id = "undo",
      .name = "Undo",
      .description = "Undoes.",
      .shortcuts = {},
      .action =
          [&] {
            ++runs;
            registry.setName("undo", QStringLiteral("Undo %1").arg(runs));
          },
  });
  CHECK(registry.trigger("undo"));
  CHECK(registry.trigger("undo"));
  CHECK(registry.name("undo") == "Undo 2");
}

TEST_CASE("a typed query matches command names by words", "[ui][commands]") {
  CHECK(matchScore("Add Audio Track", "") == 0);
  CHECK(matchScore("Add Audio Track", "aud") == 3);
  CHECK(matchScore("Add Audio Track", "AUDIO track") == 6);
  CHECK(matchScore("Add Audio Track", "udi") == 2);
  CHECK(matchScore("Add Audio Track", "adtr") == 1);
  CHECK(!matchScore("Add Audio Track", "midi").has_value());
  CHECK(!matchScore("Add Audio Track", "audio midi").has_value());
  // Case is folded by Unicode, not by the system's language.
  CHECK(matchScore("Spur ändern", "ÄND") == 3);
  CHECK(matchScore("Iğdır", "IĞ") == 3);
  CHECK(matchScore("Appearance: Dark", "dark") == 3);
}

TEST_CASE("the command list shows the best matches first", "[ui][commands]") {
  Commands commands;
  CommandPalette palette(commands.registry);
  const QAbstractItemModelTester tester(
      &palette, QAbstractItemModelTester::FailureReportingMode::Fatal);
  const QSignalSpy count(&palette, &CommandPalette::countChanged);
  const QSignalSpy query(&palette, &CommandPalette::queryChanged);

  // Everything, in the order of registration.
  REQUIRE(palette.count() == 4);
  CHECK(palette.idAt(0) == "track.add");
  CHECK(palette.idAt(3) == "view.tempo");
  CHECK(palette.idAt(4).isEmpty());
  CHECK(palette.idAt(-1).isEmpty());

  palette.setQuery("a");
  palette.setQuery("a");
  CHECK(query.count() == 1);
  CHECK(palette.query() == "a");
  // "Add" starts a word; "Play" and "Make" have an a inside, and keep
  // their order; "Set Tempo…" has none.
  REQUIRE(palette.count() == 3);
  CHECK(palette.idAt(0) == "track.add");
  CHECK(palette.idAt(1) == "transport.toggle");
  CHECK(palette.idAt(2) == "view.larger");
  CHECK(count.count() >= 1);

  palette.setQuery("stop");
  REQUIRE(palette.count() == 1);
  CHECK(palette.triggerRow(0));
  CHECK(commands.toggled == 1);
  CHECK(!palette.triggerRow(1));

  // A disabled command is listed and does not run.
  commands.registry.setEnabled("transport.toggle", false);
  CHECK(palette.count() == 1);
  CHECK(!palette.triggerRow(0));

  palette.setQuery("nothing like it");
  CHECK(palette.count() == 0);

  // A renamed command is found by its new name.
  palette.setQuery("undo");
  CHECK(palette.count() == 0);
  commands.registry.setName("view.tempo", "Undo Change Tempo");
  CHECK(palette.count() == 1);
}
