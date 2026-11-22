// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LINA_UI_COMMAND_REGISTRY_HPP
#define LINA_UI_COMMAND_REGISTRY_HPP

#include <QAbstractListModel>
#include <QKeyCombination>
#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>
#include <vector>

namespace lina::ui {

class CommandRegistry;

/// One user command (docs/design/12-interaction-principles.md, principle 4)
/// as a registry describes it.
struct CommandSpec {
  /// Stable and never shown: "track.addAudio".
  QString id;
  /// What the user sees, translated: "Add Audio Track".
  QString name;
  /// One sentence for the help area, translated.
  QString description;
  /// Default key combinations, each a single chord. The first is the one
  /// shown; the others are alternatives for keyboard layouts.
  QList<QKeySequence> shortcuts;
  std::function<void()> action;
};

/// A registered command as the interface binds to it: a button shows its
/// name and runs it, the help area shows its description.
class UserCommand final : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Registered in a CommandRegistry")
  Q_PROPERTY(QString commandId READ commandId CONSTANT FINAL)
  Q_PROPERTY(QString name READ name NOTIFY changed FINAL)
  Q_PROPERTY(QString description READ description CONSTANT FINAL)
  Q_PROPERTY(QString shortcut READ shortcut CONSTANT FINAL)
  Q_PROPERTY(bool enabled READ enabled NOTIFY changed FINAL)

public:
  UserCommand(CommandRegistry &registry, CommandSpec spec);

  [[nodiscard]] QString commandId() const { return spec_.id; }
  [[nodiscard]] QString name() const { return spec_.name; }
  [[nodiscard]] QString description() const { return spec_.description; }
  /// The first shortcut in the desktop's notation, or an empty string.
  [[nodiscard]] QString shortcut() const;
  [[nodiscard]] bool enabled() const { return enabled_; }
  [[nodiscard]] const QList<QKeySequence> &shortcuts() const {
    return spec_.shortcuts;
  }

  /// Runs the command through its registry; see CommandRegistry::trigger.
  Q_INVOKABLE bool trigger();

Q_SIGNALS:
  void changed();

private:
  friend class CommandRegistry;

  CommandRegistry *registry_;
  CommandSpec spec_;
  bool enabled_{true};
};

/// Every command of the interface, registered once. Buttons, the command
/// list and the keyboard all run commands through here and nowhere else.
///
/// As a list model it shows every command in the order of registration,
/// with its name, description, shortcut and whether it can run now.
class CommandRegistry final : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Created by the shell")
  Q_PROPERTY(bool suspended READ suspended NOTIFY suspendedChanged FINAL)

public:
  // Item roles are plain ints in Qt's model interface.
  static constexpr int IdRole = Qt::UserRole + 1;
  static constexpr int NameRole = Qt::UserRole + 2;
  static constexpr int DescriptionRole = Qt::UserRole + 3;
  static constexpr int ShortcutRole = Qt::UserRole + 4;
  /// Named "available" for QML, where "enabled" is an item's own.
  static constexpr int EnabledRole = Qt::UserRole + 5;

  explicit CommandRegistry(QObject *parent = nullptr);

  /// Adds a command, enabled. Throws std::invalid_argument for an empty or
  /// repeated id, an empty name, a missing action, or a shortcut that is not
  /// a single chord or that another command has already.
  void add(CommandSpec spec);

  /// The command with this id, or null. The object lives as long as the
  /// registry.
  Q_INVOKABLE [[nodiscard]] lina::ui::UserCommand *
  command(const QString &id) const;

  /// The following throw std::invalid_argument for an id that was never
  /// added: naming a command that does not exist is a mistake in the
  /// program.
  void setEnabled(const QString &id, bool enabled);
  void setName(const QString &id, const QString &name);
  [[nodiscard]] bool isEnabled(const QString &id) const;
  [[nodiscard]] QString name(const QString &id) const;

  /// Ids in the order of registration.
  [[nodiscard]] QStringList ids() const;

  /// The command a key combination runs, or an empty string. A combination
  /// with Shift that has no command of its own also tries the same key
  /// without Shift, unless the key is a letter: on many layouts "+" needs
  /// Shift, and Ctrl+Shift+= arrives as Ctrl+Shift++.
  [[nodiscard]] QString commandForKey(QKeyCombination key) const;

  /// While suspended (a question waits for an answer), no command runs.
  [[nodiscard]] bool suspended() const { return suspended_; }
  void setSuspended(bool suspended);

  /// Runs a command when it exists, is enabled and the registry is not
  /// suspended, and says whether it ran.
  Q_INVOKABLE bool trigger(const QString &id);

  [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex &index,
                              int role = Qt::DisplayRole) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
  void suspendedChanged();
  /// After a command ran.
  void triggered(const QString &id);

private:
  [[nodiscard]] std::size_t indexOf(const QString &id) const;
  [[nodiscard]] QString exactCommandForKey(QKeyCombination key) const;
  void changed(std::size_t row, const QList<int> &roles);

  /// Children of this object, in the order of registration.
  std::vector<UserCommand *> commands_;
  bool suspended_{false};
};

} // namespace lina::ui

#endif // LINA_UI_COMMAND_REGISTRY_HPP
