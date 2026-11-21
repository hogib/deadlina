// SPDX-License-Identifier: GPL-3.0-or-later
#include "internal/text_rules.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ranges>

namespace lina::format::detail {

namespace {

// What a lead byte announces: how many continuation bytes follow and the
// smallest and largest value the first of them may have (Unicode, table 3-7).
// Nothing follows a byte that cannot start a sequence.
struct Sequence {
  std::size_t following;
  std::uint8_t firstLow;
  std::uint8_t firstHigh;
};

constexpr std::uint8_t continuationLow = 0x80;
constexpr std::uint8_t continuationHigh = 0xBF;

Sequence sequenceFor(std::uint8_t lead) {
  if (lead >= 0xC2 && lead <= 0xDF) {
    return {
        .following = 1,
        .firstLow = continuationLow,
        .firstHigh = continuationHigh,
    };
  }
  if (lead >= 0xE0 && lead <= 0xEF) {
    // E0 would otherwise allow overlong forms, ED the surrogates.
    return {
        .following = 2,
        .firstLow = lead == 0xE0 ? std::uint8_t{0xA0} : continuationLow,
        .firstHigh = lead == 0xED ? std::uint8_t{0x9F} : continuationHigh,
    };
  }
  if (lead >= 0xF0 && lead <= 0xF4) {
    // F0 would otherwise allow overlong forms, F4 values past U+10FFFF.
    return {
        .following = 3,
        .firstLow = lead == 0xF0 ? std::uint8_t{0x90} : continuationLow,
        .firstHigh = lead == 0xF4 ? std::uint8_t{0x8F} : continuationHigh,
    };
  }
  return {.following = 0, .firstLow = 0, .firstHigh = 0};
}

} // namespace

bool isValidUtf8(std::string_view text) {
  std::size_t following = 0;
  std::uint8_t low = continuationLow;
  std::uint8_t high = continuationHigh;
  for (const char character : text) {
    const auto byte = static_cast<std::uint8_t>(character);
    if (following > 0) {
      if (byte < low || byte > high) {
        return false;
      }
      --following;
      low = continuationLow;
      high = continuationHigh;
      continue;
    }
    if (byte < 0x80) {
      continue;
    }
    const Sequence sequence = sequenceFor(byte);
    if (sequence.following == 0) {
      return false;
    }
    following = sequence.following;
    low = sequence.firstLow;
    high = sequence.firstHigh;
  }
  return following == 0;
}

bool isContentHash(std::string_view text) {
  constexpr std::string_view digits = "0123456789abcdef";
  constexpr std::size_t length = 64;
  return text.size() == length &&
         text.find_first_not_of(digits) == std::string_view::npos;
}

bool isProjectMediaPath(std::string_view path) {
  if (path.empty() || path.contains('\0')) {
    return false;
  }
  return std::ranges::none_of(
      std::views::split(path, '/'), [](const auto &part) {
        const std::string_view segment{part.begin(), part.end()};
        return segment.empty() || segment == "." || segment == "..";
      });
}

bool isExternalMediaPath(std::string_view path) {
  return path.starts_with('/') && !path.contains('\0');
}

} // namespace lina::format::detail
