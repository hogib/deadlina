// SPDX-License-Identifier: GPL-3.0-or-later
//
// libFuzzer entry point (docs/design/16-project-file-format-encoding.md,
// "Testing"): arbitrary bytes go to the document reader and the view state
// reader. A crash, a sanitizer report or a broken invariant is a defect.
#include "support/reader_invariant.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

// NOLINTNEXTLINE(readability-identifier-naming): the name libFuzzer looks for
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,
                                      std::size_t size) {
  using lina::format::testing::ReadOutcome;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): bytes as text
  const std::string_view bytes{reinterpret_cast<const char *>(data), size};
  if (lina::format::testing::readOutcome(bytes) == ReadOutcome::Broken ||
      lina::format::testing::viewReadOutcome(bytes) == ReadOutcome::Broken) {
    std::abort();
  }
  return 0;
}
