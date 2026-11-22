// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/ui/units.hpp"

#include <algorithm>
#include <cstdint>

namespace lina::ui {

namespace {

constexpr std::int64_t hundred = 100;
constexpr int partsPerBeat = 4;
/// More whole digits than any tempo in range has.
constexpr qsizetype maxWholeDigits = 4;

bool isDigit(QChar character) {
  return character >= QLatin1Char('0') && character <= QLatin1Char('9');
}

int digitValue(QChar character) {
  return character.unicode() - QLatin1Char('0').unicode();
}

} // namespace

QString formatTempo(model::Tempo tempo) {
  const std::int32_t hundredths = tempo.hundredths();
  return QStringLiteral("%1.%2")
      .arg(hundredths / hundred)
      .arg(hundredths % hundred, 2, 10, QLatin1Char('0'));
}

std::optional<model::Tempo> parseTempo(const QString &text) {
  QString value = text.trimmed();
  if (value.endsWith(QLatin1String("bpm"), Qt::CaseInsensitive)) {
    value.chop(3);
    value = value.trimmed();
  }
  const qsizetype separator = std::max(value.indexOf(QLatin1Char('.')),
                                       value.indexOf(QLatin1Char(',')));
  const QString whole = separator < 0 ? value : value.left(separator);
  const QString fraction = separator < 0 ? QString() : value.mid(separator + 1);
  const auto allDigits = [](const QString &part) {
    return std::ranges::all_of(part, isDigit);
  };
  if (whole.isEmpty() || whole.size() > maxWholeDigits || !allDigits(whole) ||
      !allDigits(fraction)) {
    return std::nullopt;
  }
  std::int64_t hundredths = whole.toLongLong() * hundred;
  for (qsizetype digit = 0; digit < 2 && digit < fraction.size(); ++digit) {
    hundredths +=
        std::int64_t{digitValue(fraction.at(digit))} * (digit == 0 ? 10 : 1);
  }
  if (fraction.size() > 2 && digitValue(fraction.at(2)) >= 5) {
    ++hundredths;
  }
  if (hundredths > model::Tempo::maxHundredths) {
    return std::nullopt;
  }
  return model::Tempo::fromHundredths(static_cast<std::int32_t>(hundredths));
}

model::Tempo offsetTempo(model::Tempo tempo, std::int64_t hundredths) {
  const std::int64_t moved = std::clamp<std::int64_t>(
      tempo.hundredths() + hundredths, model::Tempo::minHundredths,
      model::Tempo::maxHundredths);
  // Inside the range by construction: the fallback is never taken.
  return model::Tempo::fromHundredths(static_cast<std::int32_t>(moved))
      .value_or(tempo);
}

QString formatPosition(model::TickPosition position,
                       model::TimeSignature signature) {
  const auto barBeat = model::toBarBeat(position, signature);
  if (!barBeat) {
    return QStringLiteral("–");
  }
  const std::int64_t part =
      1 + (barBeat->tick * partsPerBeat / signature.beatLength().ticks);
  return QStringLiteral("%1.%2.%3")
      .arg(barBeat->bar)
      .arg(barBeat->beat)
      .arg(part);
}

} // namespace lina::ui
