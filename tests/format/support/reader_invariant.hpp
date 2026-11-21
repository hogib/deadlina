// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test support: what must hold for the reader whatever bytes it is given.
// Shared by the damaged-input tests and the fuzz target.
#ifndef LINA_FORMAT_TESTS_SUPPORT_READER_INVARIANT_HPP
#define LINA_FORMAT_TESTS_SUPPORT_READER_INVARIANT_HPP

#include <cstdint>
#include <string_view>

namespace lina::format::testing {

enum class ReadOutcome : std::uint8_t {
  // The bytes were refused with an error that has a message.
  Refused,
  // The bytes were read as a valid document, which writes to bytes that read
  // back as the same document and write to the same bytes again.
  Accepted,
  // Neither: a defect.
  Broken,
};

ReadOutcome readOutcome(std::string_view bytes);

// The same for the view state reader.
ReadOutcome viewReadOutcome(std::string_view bytes);

} // namespace lina::format::testing

#endif // LINA_FORMAT_TESTS_SUPPORT_READER_INVARIANT_HPP
