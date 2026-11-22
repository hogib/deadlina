// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/ui/workspace.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace lina::ui {

Workspace::Workspace(QObject *parent) : QObject(parent) {}

bool Workspace::isShowing(Pane pane) const {
  return pane == Pane::Session ? sessionVisible() : arrangementVisible();
}

void Workspace::setFocusedPane(Pane pane) {
  if (!isShowing(pane)) {
    setViews(Views::Both);
  }
  if (pane != focusedPane_) {
    focusedPane_ = pane;
    Q_EMIT focusedPaneChanged();
  }
  Q_EMIT focusReturned();
}

void Workspace::focusNextPane() {
  if (views_ == Views::Both) {
    setFocusedPane(focusedPane_ == Pane::Session ? Pane::Arrangement
                                                 : Pane::Session);
  } else {
    // One view showing: one pane, which takes the keys back.
    Q_EMIT focusReturned();
  }
}

void Workspace::focusPreviousPane() {
  // Two panes: the previous one is the next one.
  focusNextPane();
}

void Workspace::setViews(Views views) {
  if (views == views_) {
    return;
  }
  views_ = views;
  Q_EMIT viewsChanged();
  if (!isShowing(focusedPane_)) {
    focusedPane_ = sessionVisible() ? Pane::Session : Pane::Arrangement;
    Q_EMIT focusedPaneChanged();
    Q_EMIT focusReturned();
  }
}

bool Workspace::sessionVisible() const {
  return views_ != Views::ArrangementOnly;
}

bool Workspace::arrangementVisible() const {
  return views_ != Views::SessionOnly;
}

void Workspace::setSplit(double split) {
  if (!std::isfinite(split)) {
    return;
  }
  split = std::clamp(split, minimumSplit, maximumSplit);
  if (split != split_) {
    split_ = split;
    Q_EMIT splitChanged();
  }
}

double Workspace::splitAfterDrag(double fromSplit, double pixelsDown,
                                 double height) const {
  if (!(height > 0.0) || !std::isfinite(pixelsDown)) {
    return split_;
  }
  return std::clamp(fromSplit + (pixelsDown / height), minimumSplit,
                    maximumSplit);
}

void Workspace::beginDividerDrag() {
  if (!dragStart_) {
    dragStart_ = split_;
    Q_EMIT dividerDraggingChanged();
  }
}

void Workspace::dragDivider(double pixelsDown, double height) {
  if (dragStart_) {
    setSplit(splitAfterDrag(*dragStart_, pixelsDown, height));
  }
}

void Workspace::endDividerDrag() {
  if (dragStart_) {
    dragStart_.reset();
    Q_EMIT dividerDraggingChanged();
  }
}

void Workspace::cancelDividerDrag() {
  if (const std::optional<double> start =
          std::exchange(dragStart_, std::nullopt)) {
    setSplit(*start);
    Q_EMIT dividerDraggingChanged();
  }
}

void Workspace::moveDivider(int steps) {
  setSplit(split_ + (steps * splitStep));
}

void Workspace::resetDivider() { setSplit(defaultSplit); }

void Workspace::showHelp(const QString &title, const QString &text) {
  setHelp(title, text);
}

void Workspace::clearHelp(const QString &title) {
  if (title == helpTitle_) {
    setHelp({}, {});
  }
}

void Workspace::setHelp(const QString &title, const QString &text) {
  if (title != helpTitle_ || text != helpText_) {
    helpTitle_ = title;
    helpText_ = text;
    Q_EMIT helpChanged();
  }
}

void Workspace::returnFocus() { Q_EMIT focusReturned(); }

} // namespace lina::ui
