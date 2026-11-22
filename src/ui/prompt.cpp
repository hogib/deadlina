// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/ui/prompt.hpp"

#include <stdexcept>
#include <utility>

namespace lina::ui {

Prompt::Prompt(QObject *parent) : QObject(parent) {}

void Prompt::ask(const QString &title, const QString &text,
                 const QStringList &choices, int cancelChoice,
                 Continuation continuation) {
  if (active()) {
    throw std::logic_error("Prompt::ask while a question is waiting");
  }
  if (choices.isEmpty() || cancelChoice < 0 || cancelChoice >= choices.size() ||
      !continuation) {
    throw std::invalid_argument("Prompt::ask: bad choices or continuation");
  }
  title_ = title;
  text_ = text;
  choices_ = choices;
  cancelChoice_ = cancelChoice;
  waiting_ = std::move(continuation);
  Q_EMIT changed();
}

void Prompt::answer(int choice) {
  if (!active()) {
    return;
  }
  if (choice < 0 || choice >= choices_.size()) {
    choice = cancelChoice_;
  }
  const Continuation continuation = std::exchange(waiting_, nullptr);
  title_.clear();
  text_.clear();
  choices_.clear();
  cancelChoice_ = 0;
  Q_EMIT changed();
  continuation(choice);
}

} // namespace lina::ui
