// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LINA_UI_FAULT_TEXT_HPP
#define LINA_UI_FAULT_TEXT_HPP

#include "lina/model/fault.hpp"

#include <QString>

namespace lina::ui {

/// Why the document refused an edit, in plain words for the user
/// (docs/design/12-interaction-principles.md, principle 9), translated.
[[nodiscard]] QString faultText(model::Fault fault);

} // namespace lina::ui

#endif // LINA_UI_FAULT_TEXT_HPP
