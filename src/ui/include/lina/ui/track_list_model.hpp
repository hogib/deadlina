// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LINA_UI_TRACK_LIST_MODEL_HPP
#define LINA_UI_TRACK_LIST_MODEL_HPP

#include "lina/model/change_set.hpp"
#include "lina/model/ids.hpp"

#include <QAbstractListModel>
#include <QtQmlIntegration/qqmlintegration.h>

#include <vector>

namespace lina::ui {

class DocumentController;

/// The audio and MIDI tracks of the open project, in display order, for
/// track headers: a thin adapter over the document and its change sets
/// (docs/design/13-architecture.md, "The interface", item 6). It holds the
/// identities of its rows and nothing else, and reads every value from the
/// document when asked.
///
/// A change set turns into the smallest signals the list model protocol
/// has: rows removed and inserted for tracks that came and went, data
/// changed for a renamed or recoloured track. Reordering resets the model.
class TrackListModel final : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Created by the shell")
  Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
  // Item roles are plain ints in Qt's model interface.
  /// The identity as text: identities go up to 2^62, past what a QML
  /// number holds exactly.
  static constexpr int TrackIdRole = Qt::UserRole + 1;
  static constexpr int NameRole = Qt::UserRole + 2;
  /// "audio" or "midi"; not shown.
  static constexpr int KindRole = Qt::UserRole + 3;
  /// "Audio" or "MIDI", translated.
  static constexpr int KindTextRole = Qt::UserRole + 4;
  static constexpr int ColourRole = Qt::UserRole + 5;

  explicit TrackListModel(DocumentController &document,
                          QObject *parent = nullptr);

  [[nodiscard]] int count() const { return static_cast<int>(rows_.size()); }
  [[nodiscard]] model::TrackId trackAt(int row) const;

  [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex &index,
                              int role = Qt::DisplayRole) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
  void countChanged();

private:
  void rebuild();
  void follow(const model::ChangeSet &changes);
  /// Rows removed and inserted until they are the document's tracks.
  void followStructure();
  /// Renamed and recoloured rows.
  void followData(const model::ChangeSet &changes);
  [[nodiscard]] std::vector<model::TrackId> currentIds() const;

  DocumentController *document_;
  std::vector<model::TrackId> rows_;
};

} // namespace lina::ui

#endif // LINA_UI_TRACK_LIST_MODEL_HPP
