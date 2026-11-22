// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/ui/command_palette.hpp"

#include <QStringList>

#include <algorithm>

namespace lina::ui {

namespace {

constexpr int wordStartScore = 3;
constexpr int insideScore = 2;
constexpr int scatteredScore = 1;

QStringList wordsOf(const QString &folded) {
  QStringList words;
  QString current;
  for (const QChar character : folded) {
    if (character.isLetterOrNumber()) {
      current.append(character);
    } else if (!current.isEmpty()) {
      words.append(current);
      current.clear();
    }
  }
  if (!current.isEmpty()) {
    words.append(current);
  }
  return words;
}

/// Whether the characters of `needle` appear in `haystack` in this order.
bool inOrder(const QString &needle, const QString &haystack) {
  qsizetype from = 0;
  for (const QChar character : needle) {
    from = haystack.indexOf(character, from);
    if (from < 0) {
      return false;
    }
    ++from;
  }
  return true;
}

} // namespace

std::optional<int> matchScore(const QString &name, const QString &query) {
  const QString foldedName = name.toCaseFolded();
  const QStringList nameWords = wordsOf(foldedName);
  int score = 0;
  for (const QString &word :
       query.toCaseFolded().split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
    const bool startsWord = std::ranges::any_of(
        nameWords, [&word](const QString &w) { return w.startsWith(word); });
    if (startsWord) {
      score += wordStartScore;
    } else if (foldedName.contains(word)) {
      score += insideScore;
    } else if (inOrder(word, foldedName)) {
      score += scatteredScore;
    } else {
      return std::nullopt;
    }
  }
  return score;
}

CommandPalette::CommandPalette(CommandRegistry &registry, QObject *parent)
    : QSortFilterProxyModel(parent), registry_(&registry) {
  setSourceModel(&registry);
  // Commands are renamed while the list is open ("Undo Add Track"); the
  // filter and the order follow.
  setDynamicSortFilter(true);
  sort(0);
  connect(this, &QAbstractItemModel::rowsInserted, this,
          &CommandPalette::countChanged);
  connect(this, &QAbstractItemModel::rowsRemoved, this,
          &CommandPalette::countChanged);
  connect(this, &QAbstractItemModel::modelReset, this,
          &CommandPalette::countChanged);
  connect(this, &QAbstractItemModel::layoutChanged, this,
          &CommandPalette::countChanged);
}

void CommandPalette::setQuery(const QString &query) {
  if (query_ == query) {
    return;
  }
  query_ = query;
  // Filters and sorts again: scores change even for rows that stay.
  invalidate();
  Q_EMIT queryChanged();
}

bool CommandPalette::triggerRow(int row) {
  const QString id = idAt(row);
  return !id.isEmpty() && registry_->trigger(id);
}

QString CommandPalette::idAt(int row) const {
  if (row < 0 || row >= rowCount()) {
    return {};
  }
  return data(index(row, 0), CommandRegistry::IdRole).toString();
}

int CommandPalette::scoreOf(int sourceRow) const {
  const QString name =
      registry_->data(registry_->index(sourceRow), CommandRegistry::NameRole)
          .toString();
  return matchScore(name, query_).value_or(-1);
}

bool CommandPalette::filterAcceptsRow(int sourceRow,
                                      const QModelIndex &sourceParent) const {
  // A list: every row has the invalid parent.
  static_cast<void>(sourceParent);
  return scoreOf(sourceRow) >= 0;
}

bool CommandPalette::lessThan(const QModelIndex &left,
                              const QModelIndex &right) const {
  const int leftScore = scoreOf(left.row());
  const int rightScore = scoreOf(right.row());
  if (leftScore != rightScore) {
    return leftScore > rightScore;
  }
  return left.row() < right.row();
}

} // namespace lina::ui
