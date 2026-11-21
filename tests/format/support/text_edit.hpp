// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test support: changing one place in a document's text.
#ifndef LINA_FORMAT_TESTS_SUPPORT_TEXT_EDIT_HPP
#define LINA_FORMAT_TESTS_SUPPORT_TEXT_EDIT_HPP

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace lina::format::testing {

// The text with the first occurrence of `from` replaced by `to`. `from` must
// occur: a test that edits what is not there would prove nothing, so the
// process stops.
inline std::string replaced(std::string text, std::string_view from,
                            std::string_view to) {
  const auto at = text.find(from);
  if (at == std::string::npos) {
    std::cerr << "text to replace not found: " << from << "\n";
    std::abort();
  }
  text.replace(at, from.size(), to);
  return text;
}

} // namespace lina::format::testing

#endif // LINA_FORMAT_TESTS_SUPPORT_TEXT_EDIT_HPP
