// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LINA_UI_SHORTCUT_DISPATCHER_HPP
#define LINA_UI_SHORTCUT_DISPATCHER_HPP

#include "lina/ui/command_registry.hpp"

#include <QKeyCombination>
#include <QPointer>
#include <QQuickItem>
#include <QtQmlIntegration/qqmlintegration.h>

namespace lina::ui {

/// Whether a text field that has the keys uses this combination itself:
/// typing, moving and selecting, and the editing shortcuts (undo, redo,
/// cut, copy, paste, select all, word moves). Function keys and other
/// combinations with Ctrl or Alt are left to commands.
[[nodiscard]] bool textEntryUses(QKeyCombination key);

/// The command a key press runs: the registry's command for the
/// combination, unless a text field that has the keys (`typing`) uses it
/// (docs/design/12-interaction-principles.md, principle 5: transport keys
/// work whichever pane has focus, except while typing).
[[nodiscard]] QString commandForKeyPress(const CommandRegistry &registry,
                                         QKeyCombination key, bool typing);

/// Puts the keyboard of its window through the command registry: every
/// key press that is the shortcut of a command that can run now runs that
/// command, before any item sees it, and is used up. The key of a disabled
/// command goes on to the items. Nothing else in the interface handles
/// shortcuts. Place one in each window.
// Not final: QML derives from a type it creates.
class ShortcutDispatcher : public QQuickItem {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(lina::ui::CommandRegistry *registry READ registry WRITE setRegistry
                 NOTIFY registryChanged FINAL)

public:
  explicit ShortcutDispatcher(QQuickItem *parent = nullptr);
  ~ShortcutDispatcher() override;

  ShortcutDispatcher(const ShortcutDispatcher &) = delete;
  ShortcutDispatcher &operator=(const ShortcutDispatcher &) = delete;
  ShortcutDispatcher(ShortcutDispatcher &&) = delete;
  ShortcutDispatcher &operator=(ShortcutDispatcher &&) = delete;

  [[nodiscard]] CommandRegistry *registry() const { return registry_; }
  void setRegistry(CommandRegistry *registry);

  bool eventFilter(QObject *watched, QEvent *event) override;

Q_SIGNALS:
  void registryChanged();

protected:
  void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
  void watch(QQuickWindow *window);

  QPointer<CommandRegistry> registry_;
  QPointer<QQuickWindow> watched_;
};

} // namespace lina::ui

#endif // LINA_UI_SHORTCUT_DISPATCHER_HPP
