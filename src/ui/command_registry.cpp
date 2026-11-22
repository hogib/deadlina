// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/ui/command_registry.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace lina::ui {

namespace {

/// The keypad sends the same keys as the main block with a modifier of its
/// own, which no shortcut is written with.
QKeyCombination normalised(QKeyCombination key) {
  return QKeyCombination(key.keyboardModifiers() & ~Qt::KeypadModifier,
                         key.key());
}

bool isLetter(Qt::Key key) { return key >= Qt::Key_A && key <= Qt::Key_Z; }

/// The first chord of a shortcut. Every shortcut in a registry has exactly
/// one (add() checks), and QKeySequence offers no checked access to it.
QKeyCombination firstChord(const QKeySequence &shortcut) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  return shortcut[0];
}

} // namespace

UserCommand::UserCommand(CommandRegistry &registry, CommandSpec spec)
    : QObject(&registry), registry_(&registry), spec_(std::move(spec)) {}

QString UserCommand::shortcut() const {
  return spec_.shortcuts.isEmpty()
             ? QString()
             : spec_.shortcuts.front().toString(QKeySequence::NativeText);
}

bool UserCommand::trigger() { return registry_->trigger(spec_.id); }

CommandRegistry::CommandRegistry(QObject *parent)
    : QAbstractListModel(parent) {}

void CommandRegistry::add(CommandSpec spec) {
  if (spec.id.isEmpty() || command(spec.id) != nullptr) {
    throw std::invalid_argument("command id empty or repeated: " +
                                spec.id.toStdString());
  }
  if (spec.name.isEmpty() || !spec.action) {
    throw std::invalid_argument("command without name or action: " +
                                spec.id.toStdString());
  }
  for (const QKeySequence &shortcut : spec.shortcuts) {
    if (shortcut.count() != 1) {
      throw std::invalid_argument("shortcut is not a single chord: " +
                                  spec.id.toStdString());
    }
    if (!exactCommandForKey(normalised(firstChord(shortcut))).isEmpty()) {
      throw std::invalid_argument("shortcut taken twice: " +
                                  shortcut.toString().toStdString());
    }
  }
  const auto row = static_cast<int>(commands_.size());
  beginInsertRows({}, row, row);
  // Owned by this registry as its child, which is Qt's ownership idiom:
  // hence the bare `new`. QML reads it and never owns it.
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  commands_.push_back(new UserCommand(*this, std::move(spec)));
  endInsertRows();
}

UserCommand *CommandRegistry::command(const QString &id) const {
  const auto found = std::ranges::find(commands_, id, &UserCommand::commandId);
  return found == commands_.end() ? nullptr : *found;
}

std::size_t CommandRegistry::indexOf(const QString &id) const {
  const auto found = std::ranges::find(commands_, id, &UserCommand::commandId);
  if (found == commands_.end()) {
    throw std::invalid_argument("unknown command: " + id.toStdString());
  }
  return static_cast<std::size_t>(found - commands_.begin());
}

void CommandRegistry::changed(std::size_t row, const QList<int> &roles) {
  const QModelIndex at = index(static_cast<int>(row));
  Q_EMIT dataChanged(at, at, roles);
  Q_EMIT commands_.at(row)->changed();
}

void CommandRegistry::setEnabled(const QString &id, bool enabled) {
  const std::size_t row = indexOf(id);
  if (commands_.at(row)->enabled_ != enabled) {
    commands_.at(row)->enabled_ = enabled;
    changed(row, {EnabledRole});
  }
}

void CommandRegistry::setName(const QString &id, const QString &name) {
  const std::size_t row = indexOf(id);
  if (commands_.at(row)->spec_.name != name) {
    commands_.at(row)->spec_.name = name;
    changed(row, {NameRole, Qt::DisplayRole});
  }
}

bool CommandRegistry::isEnabled(const QString &id) const {
  return commands_.at(indexOf(id))->enabled();
}

QString CommandRegistry::name(const QString &id) const {
  return commands_.at(indexOf(id))->name();
}

QStringList CommandRegistry::ids() const {
  QStringList result;
  for (const UserCommand *command : commands_) {
    result.append(command->commandId());
  }
  return result;
}

QString CommandRegistry::exactCommandForKey(QKeyCombination key) const {
  for (const UserCommand *command : commands_) {
    for (const QKeySequence &shortcut : command->shortcuts()) {
      if (firstChord(shortcut) == key) {
        return command->commandId();
      }
    }
  }
  return {};
}

QString CommandRegistry::commandForKey(QKeyCombination key) const {
  const QKeyCombination wanted = normalised(key);
  QString found = exactCommandForKey(wanted);
  if (found.isEmpty() &&
      wanted.keyboardModifiers().testFlag(Qt::ShiftModifier) &&
      !isLetter(wanted.key())) {
    found = exactCommandForKey(QKeyCombination(
        wanted.keyboardModifiers() & ~Qt::ShiftModifier, wanted.key()));
  }
  return found;
}

void CommandRegistry::setSuspended(bool suspended) {
  if (suspended_ != suspended) {
    suspended_ = suspended;
    Q_EMIT suspendedChanged();
  }
}

bool CommandRegistry::trigger(const QString &id) {
  const UserCommand *found = command(id);
  if (found == nullptr || !found->enabled() || suspended_) {
    return false;
  }
  // The action may rename this command, which touches its spec; it runs on
  // a copy.
  const std::function<void()> action = found->spec_.action;
  action();
  Q_EMIT triggered(id);
  return true;
}

int CommandRegistry::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : static_cast<int>(commands_.size());
}

QVariant CommandRegistry::data(const QModelIndex &index, int role) const {
  if (!checkIndex(index, CheckIndexOption::IndexIsValid |
                             CheckIndexOption::ParentIsInvalid)) {
    return {};
  }
  const UserCommand &command =
      *commands_.at(static_cast<std::size_t>(index.row()));
  switch (role) {
  case IdRole:
    return command.commandId();
  case Qt::DisplayRole:
  case NameRole:
    return command.name();
  case DescriptionRole:
    return command.description();
  case ShortcutRole:
    return command.shortcut();
  case EnabledRole:
    return command.enabled();
  default:
    return {};
  }
}

QHash<int, QByteArray> CommandRegistry::roleNames() const {
  return {
      {IdRole, "commandId"},
      {NameRole, "name"},
      {DescriptionRole, "description"},
      {ShortcutRole, "shortcut"},
      {EnabledRole, "available"},
  };
}

} // namespace lina::ui
