// SPDX-License-Identifier: GPL-3.0-or-later
//
// Values as musicians read and type them
// (docs/design/12-interaction-principles.md, principle 8). Text written here
// does not depend on the system's language settings: a tempo is always
// written with a full stop, and read with a full stop or a comma
// (docs/design/11-non-functional-requirements.md, "Language").
#ifndef LINA_UI_UNITS_HPP
#define LINA_UI_UNITS_HPP

#include "lina/model/time.hpp"

#include <QString>

#include <optional>

namespace lina::ui {

/// "120.00": beats per minute with two decimals.
[[nodiscard]] QString formatTempo(model::Tempo tempo);

/// Reads a tempo as typed: "128", "128.5", "128,25", " 96 bpm ". More than
/// two decimals are rounded to the nearest hundredth. Empty for anything
/// else and for a tempo outside 20 to 300.
[[nodiscard]] std::optional<model::Tempo> parseTempo(const QString &text);

/// A tempo moved by `hundredths`, held inside 20 to 300.
[[nodiscard]] model::Tempo offsetTempo(model::Tempo tempo,
                                       std::int64_t hundredths);

/// "bar.beat.part", each counted from 1: the bar, the beat of the time
/// signature, and which quarter of that beat. Position zero is "1.1.1".
[[nodiscard]] QString formatPosition(model::TickPosition position,
                                     model::TimeSignature signature);

} // namespace lina::ui

#endif // LINA_UI_UNITS_HPP
