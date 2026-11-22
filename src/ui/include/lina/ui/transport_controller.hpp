// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LINA_UI_TRANSPORT_CONTROLLER_HPP
#define LINA_UI_TRANSPORT_CONTROLLER_HPP

#include "lina/bridge/playback.hpp"
#include "lina/model/time.hpp"

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace lina::ui {

class DocumentController;

/// The transport as the interface shows and drives it
/// (docs/design/04-transport-and-musical-time.md, "Transport states").
///
///   - play() starts from the start marker; continuePlayback() from where
///     the playhead stopped.
///   - stop() while playing stops and leaves the playhead where it is; stop()
///     while stopped returns the playhead to the start marker. The
///     interface shows which of the two the next stop does (stopReturns).
///   - While playing, the position is read from the engine by poll(), which
///     the interface calls once per frame and never otherwise
///     (docs/design/13-architecture.md, "The interface", item 5).
///
/// The playhead is not part of the document and undo never touches it.
/// The tempo is: each change is a command, and a drag of the tempo is one
/// undo step however far it went.
class TransportController final : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Created by the shell")
  Q_PROPERTY(bool playing READ playing NOTIFY playingChanged FINAL)
  Q_PROPERTY(bool canPlay READ canPlay NOTIFY canPlayChanged FINAL)
  Q_PROPERTY(bool stopReturns READ stopReturns NOTIFY positionChanged FINAL)
  Q_PROPERTY(
      QString positionText READ positionText NOTIFY positionChanged FINAL)
  Q_PROPERTY(QString tempoText READ tempoText NOTIFY tempoChanged FINAL)
  Q_PROPERTY(
      bool tempoDragging READ tempoDragging NOTIFY tempoDraggingChanged FINAL)

public:
  /// Hundredths of a beat per minute for each pixel of vertical drag,
  /// without and with the fine modifier.
  static constexpr double hundredthsPerPixel = 25.0;
  static constexpr double fineHundredthsPerPixel = 1.0;

  TransportController(DocumentController &document, bridge::Playback &playback,
                      QObject *parent = nullptr);

  [[nodiscard]] bool playing() const { return playing_; }
  [[nodiscard]] bool canPlay() const;
  [[nodiscard]] bool stopReturns() const;
  [[nodiscard]] model::TickPosition position() const { return position_; }
  [[nodiscard]] QString positionText() const;
  [[nodiscard]] QString tempoText() const;
  [[nodiscard]] bool tempoDragging() const { return dragStart_.has_value(); }

  Q_INVOKABLE void play();
  Q_INVOKABLE void continuePlayback();
  Q_INVOKABLE void stop();
  /// Space: stops while playing, plays from the start marker while stopped.
  Q_INVOKABLE void togglePlayback();
  /// Reads the engine's position. Called once per frame while playing.
  Q_INVOKABLE void poll();

  /// Sets the tempo from typed text and says whether it was taken; text
  /// that is not a tempo leaves the tempo alone and the notice says why.
  Q_INVOKABLE bool setTempoText(const QString &text);
  /// Moves the tempo by whole beats per minute, or by hundredths when fine.
  Q_INVOKABLE void stepTempo(int steps, bool fine);
  /// Back to 120 beats per minute.
  Q_INVOKABLE void resetTempo();
  Q_INVOKABLE void beginTempoDrag();
  /// `pixelsUp` counts from where the drag began.
  Q_INVOKABLE void dragTempo(double pixelsUp, bool fine);
  Q_INVOKABLE void endTempoDrag();
  Q_INVOKABLE void cancelTempoDrag();

  /// Called when the output comes or goes.
  void refreshOutput();

Q_SIGNALS:
  void playingChanged();
  void canPlayChanged();
  void positionChanged();
  void tempoChanged();
  void tempoDraggingChanged();

private:
  void start(model::TickPosition from);
  void setPosition(model::TickPosition position);
  void setPlaying(bool playing);
  void setTempo(model::Tempo tempo);
  void projectReset();

  DocumentController *document_;
  bridge::Playback *playback_;
  bool playing_{false};
  bool canPlay_{false};
  model::TickPosition position_;
  std::optional<model::Tempo> dragStart_;
};

} // namespace lina::ui

#endif // LINA_UI_TRANSPORT_CONTROLLER_HPP
