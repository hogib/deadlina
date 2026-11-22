// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/ui/shortcut_dispatcher.hpp"

#include <QKeyEvent>
#include <QKeySequence>
#include <QQuickWindow>

#include <array>

namespace lina::ui {

namespace {

bool isFunctionKey(Qt::Key key) {
  return key >= Qt::Key_F1 && key <= Qt::Key_F35;
}

bool isTextEntry(const QQuickItem *item) {
  return item != nullptr && (item->inherits("QQuickTextInput") ||
                             item->inherits("QQuickTextEdit"));
}

} // namespace

bool textEntryUses(QKeyCombination key) {
  const Qt::KeyboardModifiers modifiers =
      key.keyboardModifiers() & ~Qt::KeypadModifier;
  const bool commandModifier =
      (modifiers & (Qt::ControlModifier | Qt::AltModifier |
                    Qt::MetaModifier)) != Qt::NoModifier;
  if (!commandModifier) {
    return !isFunctionKey(key.key());
  }
  static constexpr std::array editing{
      QKeySequence::Undo,
      QKeySequence::Redo,
      QKeySequence::Cut,
      QKeySequence::Copy,
      QKeySequence::Paste,
      QKeySequence::SelectAll,
      QKeySequence::MoveToNextWord,
      QKeySequence::MoveToPreviousWord,
      QKeySequence::SelectNextWord,
      QKeySequence::SelectPreviousWord,
      QKeySequence::DeleteStartOfWord,
      QKeySequence::DeleteEndOfWord,
      QKeySequence::MoveToStartOfDocument,
      QKeySequence::MoveToEndOfDocument,
      QKeySequence::SelectStartOfDocument,
      QKeySequence::SelectEndOfDocument,
  };
  const QKeySequence pressed(QKeyCombination(modifiers, key.key()));
  for (const QKeySequence::StandardKey standard : editing) {
    for (const QKeySequence &binding : QKeySequence::keyBindings(standard)) {
      if (pressed.matches(binding) == QKeySequence::ExactMatch) {
        return true;
      }
    }
  }
  return false;
}

QString commandForKeyPress(const CommandRegistry &registry, QKeyCombination key,
                           bool typing) {
  if (typing && textEntryUses(key)) {
    return {};
  }
  return registry.commandForKey(key);
}

ShortcutDispatcher::ShortcutDispatcher(QQuickItem *parent)
    : QQuickItem(parent) {}

ShortcutDispatcher::~ShortcutDispatcher() { watch(nullptr); }

void ShortcutDispatcher::setRegistry(CommandRegistry *registry) {
  if (registry != registry_) {
    registry_ = registry;
    Q_EMIT registryChanged();
  }
}

void ShortcutDispatcher::itemChange(ItemChange change,
                                    const ItemChangeData &value) {
  if (change == ItemSceneChange) {
    // Qt hands the new window over in a union.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
    watch(value.window);
  }
  QQuickItem::itemChange(change, value);
}

void ShortcutDispatcher::watch(QQuickWindow *window) {
  if (watched_ != nullptr) {
    watched_->removeEventFilter(this);
  }
  watched_ = window;
  if (watched_ != nullptr) {
    watched_->installEventFilter(this);
  }
}

bool ShortcutDispatcher::eventFilter(QObject *watched, QEvent *event) {
  const auto *key = event->type() == QEvent::KeyPress
                        ? dynamic_cast<const QKeyEvent *>(event)
                        : nullptr;
  // While a question waits, its own controls have the keyboard.
  // The filter sits on the watched window only.
  if (key == nullptr || registry_ == nullptr || registry_->suspended()) {
    return QQuickItem::eventFilter(watched, event);
  }
  const QString id =
      commandForKeyPress(*registry_, key->keyCombination(),
                         isTextEntry(watched_->activeFocusItem()));
  // A command that cannot run now leaves its key to the items: Escape
  // cancels a drag only while there is one, and otherwise closes a dialog.
  if (id.isEmpty() || !registry_->isEnabled(id)) {
    return QQuickItem::eventFilter(watched, event);
  }
  // A held key runs its command once, and its repeats are used up.
  if (!key->isAutoRepeat()) {
    registry_->trigger(id);
  }
  return true;
}

} // namespace lina::ui
