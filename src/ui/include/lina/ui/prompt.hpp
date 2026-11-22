// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LINA_UI_PROMPT_HPP
#define LINA_UI_PROMPT_HPP

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>

namespace lina::ui {

/// A question the user has to answer before something can go on: what to
/// do with unsaved changes, whether to open a recovery copy. Reserved for
/// what cannot be undone (docs/design/12-interaction-principles.md,
/// principle 3); edits never ask.
///
/// One question at a time. The interface shows it while `active` and calls
/// answer() with the index of the choice picked; the waiting continuation
/// then runs. The question is over before the continuation runs, so the
/// continuation may ask the next one.
class Prompt final : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by the document controller")
  Q_PROPERTY(bool active READ active NOTIFY changed FINAL)
  Q_PROPERTY(QString title READ title NOTIFY changed FINAL)
  Q_PROPERTY(QString text READ text NOTIFY changed FINAL)
  Q_PROPERTY(QStringList choices READ choices NOTIFY changed FINAL)
  Q_PROPERTY(int cancelChoice READ cancelChoice NOTIFY changed FINAL)

public:
  using Continuation = std::function<void(int)>;

  explicit Prompt(QObject *parent = nullptr);

  /// Asks a question. `cancelChoice` is the choice that Escape or closing
  /// the question picks; it must be one of `choices`. Throws
  /// std::logic_error while another question is waiting, and
  /// std::invalid_argument for no choices or a cancel choice out of range.
  void ask(const QString &title, const QString &text,
           const QStringList &choices, int cancelChoice,
           Continuation continuation);

  [[nodiscard]] bool active() const { return static_cast<bool>(waiting_); }
  [[nodiscard]] QString title() const { return title_; }
  [[nodiscard]] QString text() const { return text_; }
  [[nodiscard]] QStringList choices() const { return choices_; }
  [[nodiscard]] int cancelChoice() const { return cancelChoice_; }

  /// Ends the question with a choice. An index out of range counts as the
  /// cancel choice. Does nothing when no question is waiting.
  Q_INVOKABLE void answer(int choice);

Q_SIGNALS:
  void changed();

private:
  QString title_;
  QString text_;
  QStringList choices_;
  int cancelChoice_{0};
  Continuation waiting_;
};

} // namespace lina::ui

#endif // LINA_UI_PROMPT_HPP
