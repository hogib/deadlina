// SPDX-License-Identifier: GPL-3.0-or-later
//
// Errors as values: their names, how they are shown, and that nothing thrown
// inside the library gets out.
#include "internal/guard.hpp"
#include "lina/format/error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <new>
#include <set>
#include <stdexcept>
#include <string>

namespace lina::format {

TEST_CASE("format/error: every code has a name of its own") {
  std::set<std::string> names;
  for (const ErrorCode code : {
           ErrorCode::Syntax,
           ErrorCode::Structure,
           ErrorCode::Value,
           ErrorCode::Version,
           ErrorCode::Rule,
           ErrorCode::TooLarge,
           ErrorCode::NotFound,
           ErrorCode::AlreadyExists,
           ErrorCode::NotAFile,
           ErrorCode::ReadOnly,
           ErrorCode::Io,
           ErrorCode::Corrupt,
           ErrorCode::Internal,
       }) {
    names.emplace(errorCodeName(code));
  }
  REQUIRE(names.size() == 13);
  REQUIRE(names.contains("syntax"));
  REQUIRE(names.contains("already-exists"));
  REQUIRE(names.contains("not-a-file"));
  REQUIRE(names.contains("read-only"));
  REQUIRE(!names.contains("unknown-error"));
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): on purpose
  REQUIRE(errorCodeName(static_cast<ErrorCode>(200)) == "unknown-error");
}

TEST_CASE("format/error: an error is shown with its place when it has one") {
  REQUIRE(describe({
              .code = ErrorCode::Structure,
              .where = "/tracks/0/name",
              .message = "expected a string, found integer",
          }) == "/tracks/0/name: expected a string, found integer");
  REQUIRE(describe({
              .code = ErrorCode::Rule,
              .where = "",
              .message = "the document breaks the rules",
          }) == "the document breaks the rules");
}

TEST_CASE("format/error: errors compare by everything they hold") {
  const Error error{
      .code = ErrorCode::Value,
      .where = "/tempo",
      .message = "too fast",
  };
  REQUIRE(error == Error{error});
  REQUIRE(error != Error{
                       .code = ErrorCode::Rule,
                       .where = "/tempo",
                       .message = "too fast",
                   });
  REQUIRE(error != Error{
                       .code = ErrorCode::Value,
                       .where = "/tempo",
                       .message = "too slow",
                   });
}

TEST_CASE("format/error: whatever is thrown inside comes out as an error") {
  using Result = std::expected<int, Error>;
  REQUIRE(detail::guarded([] -> Result { return 7; }) == Result{7});

  const Result memory =
      detail::guarded([] -> Result { throw std::bad_alloc{}; });
  REQUIRE(!memory.has_value());
  REQUIRE(memory.error().code == ErrorCode::Internal);
  REQUIRE(memory.error().message == "out of memory");

  const Result other = detail::guarded(
      [] -> Result { throw std::length_error{"string too long"}; });
  REQUIRE(!other.has_value());
  REQUIRE(other.error().code == ErrorCode::Internal);
  REQUIRE(other.error().message == "unexpected failure: string too long");

  // NOLINTNEXTLINE(bugprone-std-exception-baseclass): anything at all
  const Result strange = detail::guarded([] -> Result { throw 42; });
  REQUIRE(!strange.has_value());
  REQUIRE(strange.error().code == ErrorCode::Internal);
  REQUIRE(strange.error().message == "unexpected failure");
}

} // namespace lina::format
