// SPDX-License-Identifier: GPL-3.0-or-later
#include "internal/guard.hpp"

#include <exception>
#include <new>
#include <string>

namespace lina::format::detail {

Error errorFromCurrentException() {
  try {
    throw;
  } catch (const std::bad_alloc &) {
    return {
        .code = ErrorCode::Internal,
        .where = {},
        .message = "out of memory",
    };
  } catch (const std::exception &problem) {
    return {
        .code = ErrorCode::Internal,
        .where = {},
        .message = std::string{"unexpected failure: "} + problem.what(),
    };
  } catch (...) {
    return {
        .code = ErrorCode::Internal,
        .where = {},
        .message = "unexpected failure",
    };
  }
}

} // namespace lina::format::detail
