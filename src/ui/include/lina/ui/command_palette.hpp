// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LINA_UI_COMMAND_PALETTE_HPP
#define LINA_UI_COMMAND_PALETTE_HPP

#include "lina/ui/command_registry.hpp"

#include <QSortFilterProxyModel>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace lina::ui {

/// How well a command name matches what was typed into the command list,
/// or nothing when it does not match. Higher is better.
///
/// The query is split into words; every word must be found in the name. A
/// word that starts a word of the name scores 3, one found inside a word 2,
/// and one whose letters appear in order but apart 1. An empty query
/// matches everything with 0. Comparison is by Unicode case folding, which
/// does not depend on the system's language (docs/design/11, "Language").
[[nodiscard]] std::optional<int> matchScore(const QString &name,
                                            const QString &query);

/// The searchable command list (docs/design/12-interaction-principles.md,
/// principle 4): the commands of a registry that match a query, best match
/// first and otherwise in the order of registration.
class CommandPalette final : public QSortFilterProxyModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Created by the shell")
  Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged FINAL)
  Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
  explicit CommandPalette(CommandRegistry &registry, QObject *parent = nullptr);

  [[nodiscard]] QString query() const { return query_; }
  void setQuery(const QString &query);

  [[nodiscard]] int count() const { return rowCount(); }

  /// Runs the command shown in `row` and says whether it ran (it may be
  /// disabled).
  Q_INVOKABLE bool triggerRow(int row);

  /// The id of the command shown in `row`, or an empty string.
  Q_INVOKABLE [[nodiscard]] QString idAt(int row) const;

Q_SIGNALS:
  void queryChanged();
  void countChanged();

protected:
  [[nodiscard]] bool
  filterAcceptsRow(int sourceRow,
                   const QModelIndex &sourceParent) const override;
  [[nodiscard]] bool lessThan(const QModelIndex &left,
                              const QModelIndex &right) const override;

private:
  [[nodiscard]] int scoreOf(int sourceRow) const;

  CommandRegistry *registry_;
  QString query_;
};

} // namespace lina::ui

#endif // LINA_UI_COMMAND_PALETTE_HPP
