// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LINA_UI_WORKSPACE_HPP
#define LINA_UI_WORKSPACE_HPP

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <cstdint>
#include <optional>

namespace lina::ui {

/// How the main window is divided, which pane has the keys, and what the
/// help area says (docs/design/12-interaction-principles.md, principles 5
/// and 9, and "Layout").
///
/// The session and the arrangement are stacked, session on top, with a
/// divider between them; either can also fill the area alone. Exactly one
/// pane has keyboard focus, and it is always one that is showing.
class Workspace final : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Created by the shell")
  Q_PROPERTY(Pane focusedPane READ focusedPane WRITE setFocusedPane NOTIFY
                 focusedPaneChanged FINAL)
  Q_PROPERTY(Views views READ views WRITE setViews NOTIFY viewsChanged FINAL)
  Q_PROPERTY(bool sessionVisible READ sessionVisible NOTIFY viewsChanged FINAL)
  Q_PROPERTY(
      bool arrangementVisible READ arrangementVisible NOTIFY viewsChanged FINAL)
  Q_PROPERTY(double split READ split WRITE setSplit NOTIFY splitChanged FINAL)
  Q_PROPERTY(bool dividerDragging READ dividerDragging NOTIFY
                 dividerDraggingChanged FINAL)
  Q_PROPERTY(QString helpTitle READ helpTitle NOTIFY helpChanged FINAL)
  Q_PROPERTY(QString helpText READ helpText NOTIFY helpChanged FINAL)

public:
  enum class Pane : std::uint8_t { Session, Arrangement };
  Q_ENUM(Pane)
  enum class Views : std::uint8_t { Both, SessionOnly, ArrangementOnly };
  Q_ENUM(Views)

  /// The divider stays where both views keep room to be used.
  static constexpr double minimumSplit = 0.15;
  static constexpr double maximumSplit = 0.85;
  static constexpr double defaultSplit = 0.5;
  /// How far one keyboard command moves the divider.
  static constexpr double splitStep = 0.05;

  explicit Workspace(QObject *parent = nullptr);

  [[nodiscard]] Pane focusedPane() const { return focusedPane_; }
  /// Focusing a pane that is hidden shows both views.
  void setFocusedPane(Pane pane);
  /// To the next or previous pane that is showing (F6, Shift+F6).
  Q_INVOKABLE void focusNextPane();
  Q_INVOKABLE void focusPreviousPane();

  [[nodiscard]] Views views() const { return views_; }
  /// Moves the focus to a pane that is showing.
  void setViews(Views views);
  [[nodiscard]] bool sessionVisible() const;
  [[nodiscard]] bool arrangementVisible() const;

  /// The session's share of the height both views have together.
  [[nodiscard]] double split() const { return split_; }
  /// Held between minimumSplit and maximumSplit.
  void setSplit(double split);
  /// The split a drag of the divider gives: `fromSplit` where the drag
  /// began, `pixelsDown` how far it went, `height` the height of the two
  /// views. Held in range.
  Q_INVOKABLE [[nodiscard]] double
  splitAfterDrag(double fromSplit, double pixelsDown, double height) const;
  /// A drag of the divider: begin, then each move with how far the pointer
  /// has gone from where it began, then end or cancel. Cancelling (Escape,
  /// principle 6) puts the divider back where the drag found it; moves
  /// after a cancel do nothing.
  Q_INVOKABLE void beginDividerDrag();
  Q_INVOKABLE void dragDivider(double pixelsDown, double height);
  Q_INVOKABLE void endDividerDrag();
  Q_INVOKABLE void cancelDividerDrag();
  [[nodiscard]] bool dividerDragging() const { return dragStart_.has_value(); }
  Q_INVOKABLE void moveDivider(int steps);
  Q_INVOKABLE void resetDivider();

  /// The help area (principle 9): what the pointer or the keyboard is on.
  [[nodiscard]] QString helpTitle() const { return helpTitle_; }
  [[nodiscard]] QString helpText() const { return helpText_; }
  Q_INVOKABLE void showHelp(const QString &title, const QString &text);
  /// Clears the help area if it still shows `title`, so that leaving one
  /// control after entering the next does not clear the next one's help.
  Q_INVOKABLE void clearHelp(const QString &title);

  /// Asks the interface to give the keys back to the focused pane, after a
  /// text entry was finished or cancelled (principle 5).
  Q_INVOKABLE void returnFocus();

Q_SIGNALS:
  void focusedPaneChanged();
  void viewsChanged();
  void splitChanged();
  void dividerDraggingChanged();
  void helpChanged();
  void focusReturned();

private:
  void setHelp(const QString &title, const QString &text);
  [[nodiscard]] bool isShowing(Pane pane) const;

  Pane focusedPane_{Pane::Session};
  Views views_{Views::Both};
  double split_{defaultSplit};
  std::optional<double> dragStart_;
  QString helpTitle_;
  QString helpText_;
};

} // namespace lina::ui

#endif // LINA_UI_WORKSPACE_HPP
