// SPDX-License-Identifier: GPL-3.0-or-later
//
// Whole-number arithmetic wider than 64 bits, for conversions that multiply
// a tick count (up to 2^53) by a rate before dividing.
#ifndef LINA_ENGINE_INTERNAL_WIDE_MATH_HPP
#define LINA_ENGINE_INTERNAL_WIDE_MATH_HPP

namespace lina::engine::internal {

// 128-bit integers are a compiler extension that both supported compilers
// have; the keyword silences the pedantic warning about it.
__extension__ using Wide = __int128;

/// The whole number nearest to numerator / denominator, the greater one on a
/// tie: floor(numerator / denominator + 1/2). The denominator must be
/// positive.
constexpr Wide roundedQuotient(Wide numerator, Wide denominator) {
  const Wide doubled = (2 * numerator) + denominator;
  const Wide quotient = doubled / (2 * denominator);
  return doubled % (2 * denominator) < 0 ? quotient - 1 : quotient;
}

} // namespace lina::engine::internal

#endif // LINA_ENGINE_INTERNAL_WIDE_MATH_HPP
