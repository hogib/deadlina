// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/format/error.hpp"

namespace lina::format {

std::string_view errorCodeName(ErrorCode code) {
  switch (code) {
  case ErrorCode::Syntax:
    return "syntax";
  case ErrorCode::Structure:
    return "structure";
  case ErrorCode::Value:
    return "value";
  case ErrorCode::Version:
    return "version";
  case ErrorCode::Rule:
    return "rule";
  case ErrorCode::TooLarge:
    return "too-large";
  case ErrorCode::NotFound:
    return "not-found";
  case ErrorCode::AlreadyExists:
    return "already-exists";
  case ErrorCode::NotAFile:
    return "not-a-file";
  case ErrorCode::ReadOnly:
    return "read-only";
  case ErrorCode::Io:
    return "io";
  case ErrorCode::Corrupt:
    return "corrupt";
  case ErrorCode::Internal:
    return "internal";
  }
  return "unknown-error";
}

std::string describe(const Error &error) {
  if (error.where.empty()) {
    return error.message;
  }
  return error.where + ": " + error.message;
}

} // namespace lina::format
