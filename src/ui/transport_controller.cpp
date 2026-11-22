// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/ui/transport_controller.hpp"

#include "lina/model/command.hpp"
#include "lina/ui/document_controller.hpp"
#include "lina/ui/units.hpp"

#include <cmath>
#include <utility>

namespace lina::ui {

namespace {

constexpr std::int64_t wholeStep = 100;

bool touchesTransport(const model::ChangeSet &changes,
                      model::Property property) {
  const model::Change *project = changes.find(model::projectRef);
  return project != nullptr && project->properties.contains(property);
}

} // namespace

TransportController::TransportController(DocumentController &document,
                                         bridge::Playback &playback,
                                         QObject *parent)
    : QObject(parent), document_(&document), playback_(&playback),
      canPlay_(playback.hasOutput()),
      position_(document.project().startMarker) {
  connect(document_, &DocumentController::projectAboutToReset, this, [this] {
    if (playing_) {
      playback_->stop();
      setPlaying(false);
    }
  });
  connect(document_, &DocumentController::projectReset, this,
          &TransportController::projectReset);
  connect(document_, &DocumentController::projectChanged, this,
          [this](const model::ChangeSet &changes) {
            if (touchesTransport(changes, model::Property::Tempo)) {
              Q_EMIT tempoChanged();
            }
            if (touchesTransport(changes, model::Property::StartMarker) ||
                touchesTransport(changes, model::Property::TimeSignature)) {
              Q_EMIT positionChanged();
            }
          });
}

bool TransportController::canPlay() const { return canPlay_; }

bool TransportController::stopReturns() const {
  return !playing_ && position_ != document_->project().startMarker;
}

QString TransportController::positionText() const {
  return formatPosition(position_, document_->project().timeSignature);
}

QString TransportController::tempoText() const {
  return formatTempo(document_->project().tempo);
}

void TransportController::refreshOutput() {
  const bool has = playback_->hasOutput();
  if (has != canPlay_) {
    canPlay_ = has;
    Q_EMIT canPlayChanged();
  }
  if (!has && playing_) {
    setPlaying(false);
  }
}

void TransportController::play() { start(document_->project().startMarker); }

void TransportController::continuePlayback() { start(position_); }

void TransportController::start(model::TickPosition from) {
  refreshOutput();
  if (!canPlay_) {
    document_->notify(tr("There is no sound output, so the transport cannot "
                         "play. LINA was started without audio; start it "
                         "again without --no-audio to play."),
                      false);
    return;
  }
  playback_->play(document_->project(), from);
  setPosition(from);
  setPlaying(true);
}

void TransportController::stop() {
  if (playing_) {
    const bridge::PlaybackState state = playback_->state(document_->project());
    playback_->stop();
    setPlaying(false);
    if (state.playing) {
      setPosition(state.position);
    }
    return;
  }
  setPosition(document_->project().startMarker);
}

void TransportController::togglePlayback() {
  if (playing_) {
    stop();
  } else {
    play();
  }
}

void TransportController::poll() {
  if (!playing_) {
    return;
  }
  const bridge::PlaybackState state = playback_->state(document_->project());
  if (!state.playing) {
    // The engine stopped by itself; the playhead stays where it was seen
    // last, and the engine is told, so that nothing keeps running for it.
    playback_->stop();
    setPlaying(false);
    return;
  }
  setPosition(state.position);
}

void TransportController::setPosition(model::TickPosition position) {
  if (position != position_) {
    position_ = position;
    Q_EMIT positionChanged();
  }
}

void TransportController::setPlaying(bool playing) {
  if (playing != playing_) {
    playing_ = playing;
    Q_EMIT playingChanged();
    // Whether the next stop returns to the start marker depends on it.
    Q_EMIT positionChanged();
  }
}

void TransportController::setTempo(model::Tempo tempo) {
  if (tempo == document_->project().tempo) {
    return;
  }
  document_->perform(tr("Change Tempo"),
                     model::SetProjectProperty{model::TempoValue{tempo}});
}

bool TransportController::setTempoText(const QString &text) {
  const auto tempo = parseTempo(text);
  if (!tempo) {
    document_->notify(tr("“%1” is not a tempo LINA can use. Type a number of "
                         "beats per minute from 20 to 300, such as 120 or "
                         "96.5.")
                          .arg(text.trimmed()),
                      true);
    // The field shows the tempo as it is.
    Q_EMIT tempoChanged();
    return false;
  }
  setTempo(*tempo);
  return true;
}

void TransportController::stepTempo(int steps, bool fine) {
  setTempo(
      offsetTempo(document_->project().tempo,
                  static_cast<std::int64_t>(steps) * (fine ? 1 : wholeStep)));
}

void TransportController::resetTempo() { setTempo(model::Tempo{}); }

void TransportController::beginTempoDrag() {
  if (dragStart_ || !document_->beginGesture(tr("Change Tempo"))) {
    return;
  }
  dragStart_ = document_->project().tempo;
  Q_EMIT tempoDraggingChanged();
}

void TransportController::dragTempo(double pixelsUp, bool fine) {
  if (!dragStart_) {
    return;
  }
  const double rate = fine ? fineHundredthsPerPixel : hundredthsPerPixel;
  const auto offset = static_cast<std::int64_t>(std::lround(pixelsUp * rate));
  const model::Tempo target = offsetTempo(*dragStart_, offset);
  if (target != document_->project().tempo) {
    document_->applyInGesture(
        model::SetProjectProperty{model::TempoValue{target}});
  }
}

void TransportController::endTempoDrag() {
  if (dragStart_) {
    dragStart_.reset();
    document_->endGesture();
    Q_EMIT tempoDraggingChanged();
  }
}

void TransportController::cancelTempoDrag() {
  if (dragStart_) {
    dragStart_.reset();
    document_->cancelGesture();
    Q_EMIT tempoDraggingChanged();
  }
}

void TransportController::projectReset() {
  if (std::exchange(dragStart_, std::nullopt)) {
    Q_EMIT tempoDraggingChanged();
  }
  setPlaying(false);
  setPosition(document_->project().startMarker);
  Q_EMIT tempoChanged();
  Q_EMIT positionChanged();
}

} // namespace lina::ui
