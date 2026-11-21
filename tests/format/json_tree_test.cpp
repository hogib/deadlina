// SPDX-License-Identifier: GPL-3.0-or-later
//
// The parsed tree: what `parseJson` keeps exactly and what it refuses, with
// the place it names.
#include "internal/json_tree.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <string>

namespace lina::format::detail {

namespace {

Error refusal(std::string_view text) {
  const auto parsed = parseJson(text);
  REQUIRE(!parsed.has_value());
  REQUIRE(!parsed.error().message.empty());
  return parsed.error();
}

template <class Type> Type parsedAs(std::string_view text) {
  const auto parsed = parseJson(text);
  REQUIRE(parsed.has_value());
  REQUIRE(std::holds_alternative<Type>(parsed->data));
  return std::get<Type>(parsed->data);
}

std::string repeated(std::string_view piece, std::size_t count) {
  std::string text;
  for (std::size_t index = 0; index < count; ++index) {
    text += piece;
  }
  return text;
}

} // namespace

TEST_CASE("format/tree: every kind of value is kept") {
  const auto parsed = parseJson(
      R"( {"n": null, "t": true, "u": 7, "i": -7, "d": 0.5, "s": "x",)"
      R"( "a": [1, [2]], "o": {"k": false}} )");
  REQUIRE(parsed.has_value());
  const auto &object = std::get<JsonValue::Object>(parsed->data);
  REQUIRE(object.size() == 8);
  // Members keep the order of the text.
  REQUIRE(object.front().key == "n");
  REQUIRE(object.back().key == "o");
  REQUIRE(*findMember(object, "n") == JsonValue{nullptr});
  REQUIRE(*findMember(object, "t") == JsonValue{true});
  REQUIRE(*findMember(object, "u") == JsonValue{std::uint64_t{7}});
  REQUIRE(*findMember(object, "i") == JsonValue{std::int64_t{-7}});
  REQUIRE(*findMember(object, "d") == JsonValue{0.5});
  REQUIRE(*findMember(object, "s") == JsonValue{std::string{"x"}});
  REQUIRE(findMember(object, "missing") == nullptr);
  const auto &array = std::get<JsonValue::Array>(findMember(object, "a")->data);
  REQUIRE(array.size() == 2);
  REQUIRE(std::get<JsonValue::Array>(array.back().data).size() == 1);

  REQUIRE(jsonTypeName(*findMember(object, "n")) == "null");
  REQUIRE(jsonTypeName(*findMember(object, "t")) == "boolean");
  REQUIRE(jsonTypeName(*findMember(object, "u")) == "integer");
  REQUIRE(jsonTypeName(*findMember(object, "i")) == "integer");
  REQUIRE(jsonTypeName(*findMember(object, "d")) == "number");
  REQUIRE(jsonTypeName(*findMember(object, "s")) == "string");
  REQUIRE(jsonTypeName(*findMember(object, "a")) == "array");
  REQUIRE(jsonTypeName(*findMember(object, "o")) == "object");

  // The other overload finds the same member, to change it.
  JsonValue copy = *parsed;
  auto &members = std::get<JsonValue::Object>(copy.data);
  findMember(members, "u")->data = std::uint64_t{8};
  REQUIRE(copy != *parsed);
  REQUIRE(findMember(members, "missing") == nullptr);
}

TEST_CASE("format/tree: integer literals are exact up to 64 bits") {
  REQUIRE(parsedAs<std::uint64_t>("0") == 0);
  // A zero with a minus sign is kept apart, for the strict reader to refuse.
  REQUIRE(parsedAs<std::int64_t>("-0") == 0);
  REQUIRE(parsedAs<std::uint64_t>("9007199254740992") ==
          9'007'199'254'740'992U);
  // One past what a double can count: still exact.
  REQUIRE(parsedAs<std::uint64_t>("9007199254740993") ==
          9'007'199'254'740'993U);
  REQUIRE(parsedAs<std::uint64_t>("9223372036854775808") ==
          9'223'372'036'854'775'808U);
  REQUIRE(parsedAs<std::uint64_t>("18446744073709551615") ==
          std::numeric_limits<std::uint64_t>::max());
  REQUIRE(parsedAs<std::int64_t>("-1") == -1);
  REQUIRE(parsedAs<std::int64_t>("-9223372036854775808") ==
          std::numeric_limits<std::int64_t>::min());

  // Outside 64 bits an integer is refused, never rounded to a double.
  Error error = refusal("18446744073709551616");
  REQUIRE(error.code == ErrorCode::Value);
  REQUIRE(error.message.find("does not fit in 64 bits") != std::string::npos);
  REQUIRE(refusal("-9223372036854775809").code == ErrorCode::Value);
  error = refusal("{\"a\": [0, 123456789012345678901234567890]}");
  REQUIRE(error.code == ErrorCode::Value);
  REQUIRE(error.where == "/a/1");
  REQUIRE(refusal(repeated("9", 10'000)).code == ErrorCode::Value);
}

TEST_CASE("format/tree: numbers with a fraction or exponent are doubles") {
  REQUIRE(parsedAs<double>("0.5") == 0.5);
  REQUIRE(parsedAs<double>("1.0") == 1.0);
  REQUIRE(parsedAs<double>("-0.0") == 0.0);
  REQUIRE(parsedAs<double>("1e2") == 100.0);
  REQUIRE(parsedAs<double>("1E+2") == 100.0);
  REQUIRE(parsedAs<double>("0.30000000000000004") == 0.1 + 0.2);
  REQUIRE(parsedAs<double>("5e-324") ==
          std::numeric_limits<double>::denorm_min());
  REQUIRE(parsedAs<double>("1.7976931348623157e308") ==
          std::numeric_limits<double>::max());

  // Too large or too small for a double.
  REQUIRE(!refusal("1e309").message.empty());
  REQUIRE(!refusal("-1e309").message.empty());
  REQUIRE(!refusal("1e999999").message.empty());
  const Error tiny = refusal("[1e-400]");
  REQUIRE(tiny.code == ErrorCode::Value);
  REQUIRE(tiny.where == "/0");
  REQUIRE(tiny.message.find("1e-400") != std::string::npos);
  REQUIRE(refusal("0." + repeated("0", 400) + "1e-10").code ==
          ErrorCode::Value);
}

TEST_CASE("format/tree: strings are decoded") {
  REQUIRE(parsedAs<std::string>(R"("a\"b\\c\/d\b\f\n\r\t")") ==
          "a\"b\\c/d\b\f\n\r\t");
  REQUIRE(parsedAs<std::string>(R"("\u0041\u00e9\u2713")") ==
          "A\xC3\xA9\xE2\x9C\x93");
  REQUIRE(parsedAs<std::string>(R"("\ud83c\udfb9")") == "\xF0\x9F\x8E\xB9");
  REQUIRE(parsedAs<std::string>(R"("\u0000")") == std::string(1, '\0'));
}

TEST_CASE("format/tree: malformed text is refused with line and column") {
  Error error = refusal("{\n  \"a\": tru\n}");
  REQUIRE(error.code == ErrorCode::Syntax);
  REQUIRE(error.where == "line 3, column 1");
  REQUIRE(error.message.find("invalid literal") != std::string::npos);

  error = refusal("[1, 2");
  REQUIRE(error.code == ErrorCode::Syntax);
  REQUIRE(error.where == "line 1, column 6");

  REQUIRE(refusal("").where == "line 1, column 1");
  REQUIRE(refusal("").message == "the file is empty");
  REQUIRE(refusal(" \n\t ").code == ErrorCode::Syntax);
  REQUIRE(refusal(std::string_view{"\0", 1}).code == ErrorCode::Syntax);
  REQUIRE(refusal("{").code == ErrorCode::Syntax);
  REQUIRE(refusal("}").code == ErrorCode::Syntax);
  REQUIRE(refusal("[1,]").code == ErrorCode::Syntax);
  REQUIRE(refusal("{\"a\": 1,}").code == ErrorCode::Syntax);
  REQUIRE(refusal("{a: 1}").code == ErrorCode::Syntax);
  REQUIRE(refusal("{'a': 1}").code == ErrorCode::Syntax);
  REQUIRE(refusal("[1] // note").code == ErrorCode::Syntax);
  REQUIRE(refusal("/* note */ [1]").code == ErrorCode::Syntax);
  REQUIRE(refusal("01").code == ErrorCode::Syntax);
  REQUIRE(refusal("+1").code == ErrorCode::Syntax);
  REQUIRE(refusal(".5").code == ErrorCode::Syntax);
  REQUIRE(refusal("1.").code == ErrorCode::Syntax);
  REQUIRE(refusal("0x10").code == ErrorCode::Syntax);
  REQUIRE(refusal("\"a\nb\"").code == ErrorCode::Syntax);
  REQUIRE(refusal("\"abc").code == ErrorCode::Syntax);
  REQUIRE(refusal("\"\\x\"").code == ErrorCode::Syntax);
}

TEST_CASE("format/tree: tokens that are not JSON numbers are refused") {
  for (const std::string_view token :
       {"NaN", "nan", "Infinity", "-Infinity", "inf", "-inf", "1e", "--1"}) {
    CAPTURE(token);
    REQUIRE(refusal(token).code == ErrorCode::Syntax);
    REQUIRE(refusal("{\"gain\": " + std::string{token} + "}").code ==
            ErrorCode::Syntax);
  }
}

TEST_CASE("format/tree: anything after the value is refused") {
  REQUIRE(parseJson("[1]\n\n  ").has_value());
  Error error = refusal("[1]\n[2]");
  REQUIRE(error.code == ErrorCode::Syntax);
  REQUIRE(error.where == "line 2, column 1");
  REQUIRE(refusal("{} x").code == ErrorCode::Syntax);
  REQUIRE(refusal("1 2").code == ErrorCode::Syntax);
  // A null character does not end the text early.
  error = refusal(std::string_view{"{}\0", 3});
  REQUIRE(error.code == ErrorCode::Syntax);
  REQUIRE(error.where == "line 1, column 3");
  REQUIRE(error.message == "the file holds a null character");
  REQUIRE(refusal(std::string_view{"[1]\n\0[2", 7}).where ==
          "line 2, column 1");
  REQUIRE(refusal(std::string_view{"[1,\0 2]", 7}).code == ErrorCode::Syntax);
  REQUIRE(refusal(std::string_view{"\"a\0b\"", 5}).code == ErrorCode::Syntax);
}

TEST_CASE("format/tree: text that is not UTF-8 is refused") {
  REQUIRE(refusal("\"\xFF\"").code == ErrorCode::Syntax);
  REQUIRE(refusal("\"\xC0\x80\"").code == ErrorCode::Syntax);
  REQUIRE(refusal("\"\xED\xA0\x80\"").code == ErrorCode::Syntax);
  REQUIRE(refusal("\"\xE2\x9C\"").code == ErrorCode::Syntax);
  REQUIRE(refusal("\"\xF4\x90\x80\x80\"").code == ErrorCode::Syntax);
  // A surrogate half written as an escape is no better.
  REQUIRE(refusal(R"("\ud83c")").code == ErrorCode::Syntax);
  REQUIRE(refusal(R"("\udfb9")").code == ErrorCode::Syntax);
  // Outside strings too.
  REQUIRE(refusal("[\xC3\xA9]").code == ErrorCode::Syntax);
}

TEST_CASE("format/tree: a byte order mark is refused") {
  const Error error = refusal("\xEF\xBB\xBF{}");
  REQUIRE(error.code == ErrorCode::Syntax);
  REQUIRE(error.message.find("byte order mark") != std::string::npos);
  REQUIRE(refusal("\xEF\xBB\xBF").code == ErrorCode::Syntax);
  REQUIRE(refusal("\xFF\xFE{\0}\0").code == ErrorCode::Syntax);
}

TEST_CASE("format/tree: a key that appears twice is refused") {
  Error error = refusal(R"({"a": 1, "b": 2, "a": 3})");
  REQUIRE(error.code == ErrorCode::Structure);
  REQUIRE(error.where == "/a");
  REQUIRE(error.message == "the key \"a\" appears twice");

  error = refusal(R"({"list": [{}, {"x": {"k/1": 1, "k~2": 2, "k/1": 3}}]})");
  REQUIRE(error.code == ErrorCode::Structure);
  // The place is given as a JSON pointer, with its own escapes.
  REQUIRE(error.where == "/list/1/x/k~11");

  // The same key at different places is fine.
  REQUIRE(
      parseJson(R"({"a": {"a": 1}, "b": [{"a": 2}, {"a": 3}]})").has_value());
  // Spelling the key with an escape does not hide it.
  REQUIRE(refusal(R"({"a": 1, "\u0061": 2})").code == ErrorCode::Structure);
  REQUIRE(refusal(R"({"": 1, "": 2})").code == ErrorCode::Structure);
}

TEST_CASE("format/tree: nesting is limited") {
  const std::string deepest =
      repeated("[", maxJsonDepth) + repeated("]", maxJsonDepth);
  REQUIRE(parseJson(deepest).has_value());

  const Error error = refusal("[" + deepest + "]");
  REQUIRE(error.code == ErrorCode::Structure);
  REQUIRE(error.message == "nested too deeply");
  REQUIRE(error.where == repeated("/0", maxJsonDepth));

  REQUIRE(refusal(repeated("{\"a\":", maxJsonDepth + 1) + "1" +
                  repeated("}", maxJsonDepth + 1))
              .where == repeated("/a", maxJsonDepth));

  // Far past the limit, closed or not: an error, not a stack overflow.
  REQUIRE(refusal(repeated("[", 200'000)).code == ErrorCode::Structure);
  REQUIRE(refusal(repeated("{\"a\":", 200'000)).code == ErrorCode::Structure);
  REQUIRE(refusal(repeated("[", 200'000) + repeated("]", 200'000)).code ==
          ErrorCode::Structure);
}

TEST_CASE("format/tree: JSON pointers escape their own syntax") {
  std::string pointer;
  appendPointerStep(pointer, "tracks");
  appendPointerStep(pointer, std::size_t{12});
  appendPointerStep(pointer, "a/b~c");
  appendPointerStep(pointer, "");
  REQUIRE(pointer == "/tracks/12/a~1b~0c/");
}

TEST_CASE("format/tree: the format version is peeked without the rest") {
  REQUIRE(peekFormatVersion("{\"format\": 1}") == 1);
  REQUIRE(peekFormatVersion("{\"format\": 0, \"x\": [1, {\"format\": 9}]}") ==
          0);
  REQUIRE(peekFormatVersion("{\"format\": 2147483647}") == 2'147'483'647);
  // It need not come first, and only the top level counts.
  REQUIRE(peekFormatVersion(
              "{\"a\": {\"format\": 7}, \"b\": [[{\"format\": 8}], 2, null, "
              "true, -1, 1.5, \"s\"], \"format\": 3}") == 3);
  // What follows it is not looked at: a file torn after its version still
  // tells the version.
  REQUIRE(peekFormatVersion("{\"format\": 4, \"tracks\": [{\"na") == 4);
  REQUIRE(peekFormatVersion("{\"format\": 5 this is not JSON") == 5);

  for (const std::string_view text : {
           "",
           "[]",
           "7",
           "null",
           "\"format\"",
           "{}",
           "{\"other\": 1}",
           R"({"format": "1"})",
           "{\"format\": -1}",
           "{\"format\": 1.0}",
           "{\"format\": null}",
           "{\"format\": true}",
           "{\"format\": [1]}",
           R"({"format": {"format": 1}})",
           "{\"format\": 2147483648}",
           "{\"format\": 18446744073709551616}",
           "[{\"format\": 1}]",
           R"({"a": {"format": 1}})",
           "{\"a\": [1, 2",
           "not json",
           "\xEF\xBB\xBF{\"format\": 1}",
       }) {
    CAPTURE(text);
    REQUIRE(!peekFormatVersion(text).has_value());
  }
}

} // namespace lina::format::detail
