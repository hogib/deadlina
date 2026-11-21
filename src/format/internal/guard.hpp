// SPDX-License-Identifier: GPL-3.0-or-later
//
// Private to lina_format and its tests: no exception leaves the library.
// Every public entry point runs its body through `guarded`, which turns
// whatever is thrown, running out of memory included, into an `Error`.
#ifndef LINA_FORMAT_INTERNAL_GUARD_HPP
#define LINA_FORMAT_INTERNAL_GUARD_HPP

#include "lina/format/error.hpp"

#include <expected>
#include <utility>

namespace lina::format::detail {

// The exception being handled, as an error. Call inside a `catch` only.
Error errorFromCurrentException();

// Runs `body`, which returns a `std::expected<..., Error>`.
template <class Body> auto guarded(Body &&body) -> decltype(body()) {
  try {
    return std::forward<Body>(body)();
  } catch (...) {
    return std::unexpected(errorFromCurrentException());
  }
}

} // namespace lina::format::detail

#endif // LINA_FORMAT_INTERNAL_GUARD_HPP
