// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/ui/track_list_model.hpp"

#include "lina/model/project.hpp"
#include "lina/ui/document_controller.hpp"

#include <QColor>

#include <algorithm>
#include <iterator>

namespace lina::ui {

namespace {

bool isTrack(const model::Change &change) {
  return change.object.kind == model::ObjectKind::Track;
}

bool changesStructure(const model::ChangeSet &changes) {
  return std::ranges::any_of(changes.changes(), [](const model::Change &c) {
    if (c.object == model::projectRef) {
      return c.properties.contains(model::Property::TrackOrder);
    }
    return isTrack(c) && c.kind != model::ChangeKind::Changed;
  });
}

bool contains(const std::vector<model::TrackId> &ids, model::TrackId id) {
  return std::ranges::find(ids, id) != ids.end();
}

} // namespace

TrackListModel::TrackListModel(DocumentController &document, QObject *parent)
    : QAbstractListModel(parent), document_(&document), rows_(currentIds()) {
  connect(document_, &DocumentController::projectReset, this,
          &TrackListModel::rebuild);
  connect(document_, &DocumentController::projectChanged, this,
          &TrackListModel::follow);
}

std::vector<model::TrackId> TrackListModel::currentIds() const {
  std::vector<model::TrackId> ids;
  for (const model::Track &track : document_->project().tracks) {
    ids.push_back(track.id);
  }
  return ids;
}

model::TrackId TrackListModel::trackAt(int row) const {
  if (row < 0 || row >= count()) {
    return {};
  }
  return rows_.at(static_cast<std::size_t>(row));
}

void TrackListModel::rebuild() {
  const int before = count();
  beginResetModel();
  rows_ = currentIds();
  endResetModel();
  if (count() != before) {
    Q_EMIT countChanged();
  }
}

void TrackListModel::follow(const model::ChangeSet &changes) {
  const int before = count();
  if (changesStructure(changes)) {
    followStructure();
  }
  followData(changes);
  if (count() != before) {
    Q_EMIT countChanged();
  }
}

void TrackListModel::followStructure() {
  const std::vector<model::TrackId> wanted = currentIds();
  for (auto row = static_cast<int>(rows_.size()) - 1; row >= 0; --row) {
    if (!contains(wanted, rows_.at(static_cast<std::size_t>(row)))) {
      beginRemoveRows({}, row, row);
      rows_.erase(rows_.begin() + row);
      endRemoveRows();
    }
  }
  std::vector<model::TrackId> kept;
  std::ranges::copy_if(
      wanted, std::back_inserter(kept),
      [this](model::TrackId id) { return contains(rows_, id); });
  if (kept != rows_) {
    // A reorder: rare, and a reset is the honest signal for it.
    beginResetModel();
    rows_ = wanted;
    endResetModel();
    return;
  }
  for (std::size_t row = 0; row < wanted.size(); ++row) {
    if (row < rows_.size() && rows_.at(row) == wanted.at(row)) {
      continue;
    }
    const auto at = static_cast<int>(row);
    beginInsertRows({}, at, at);
    rows_.insert(rows_.begin() + at, wanted.at(row));
    endInsertRows();
  }
}

void TrackListModel::followData(const model::ChangeSet &changes) {
  for (const model::Change &change : changes.changes()) {
    if (!isTrack(change) || change.kind != model::ChangeKind::Changed) {
      continue;
    }
    const auto found =
        std::ranges::find(rows_, model::TrackId{change.object.id});
    QList<int> roles;
    if (change.properties.contains(model::Property::Name)) {
      roles << NameRole << Qt::DisplayRole;
    }
    if (change.properties.contains(model::Property::Colour)) {
      roles << ColourRole;
    }
    // Return tracks and the master are not rows.
    if (found != rows_.end() && !roles.isEmpty()) {
      const QModelIndex at = index(static_cast<int>(found - rows_.begin()));
      Q_EMIT dataChanged(at, at, roles);
    }
  }
}

int TrackListModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : count();
}

QVariant TrackListModel::data(const QModelIndex &index, int role) const {
  if (!checkIndex(index, CheckIndexOption::IndexIsValid |
                             CheckIndexOption::ParentIsInvalid)) {
    return {};
  }
  const model::Track *track = model::findTrack(
      document_->project(), rows_.at(static_cast<std::size_t>(index.row())));
  if (track == nullptr) {
    return {};
  }
  const bool midi = track->kind == model::TrackKind::Midi;
  switch (role) {
  case TrackIdRole:
    return QString::number(track->id.value);
  case Qt::DisplayRole:
  case NameRole:
    return QString::fromStdString(track->name);
  case KindRole:
    return midi ? QStringLiteral("midi") : QStringLiteral("audio");
  case KindTextRole:
    return midi ? tr("MIDI") : tr("Audio");
  case ColourRole:
    return QColor::fromRgb(track->colour.rgb);
  default:
    return {};
  }
}

QHash<int, QByteArray> TrackListModel::roleNames() const {
  return {
      {TrackIdRole, "trackId"},    {NameRole, "name"},
      {KindRole, "kind"},          {KindTextRole, "kindText"},
      {ColourRole, "trackColour"},
  };
}

} // namespace lina::ui
