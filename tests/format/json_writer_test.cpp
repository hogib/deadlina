// SPDX-License-Identifier: GPL-3.0-or-later
//
// The canonical writer: its layout, its numbers and its strings, byte for
// byte (docs/design/16-project-file-format-encoding.md, "Encoding of the
// document").
#include "internal/json_tree.hpp"
#include "internal/json_writer.hpp"
#include "internal/text_rules.hpp"

#include <catch2/catch_test_macros.hpp>

#include <charconv>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace lina::format::detail {

namespace {

std::string written(double value) {
  JsonWriter out;
  out.number(value);
  std::string text = out.finish();
  text.pop_back();
  return text;
}

std::string written(std::string_view value) {
  JsonWriter out;
  out.string(value);
  std::string text = out.finish();
  text.pop_back();
  return text;
}

double readBack(const std::string &text) {
  double value = 0.0;
  std::from_chars(text.data(), std::to_address(text.end()), value);
  return value;
}

} // namespace

TEST_CASE("format/writer: a block object puts one member on each line") {
  JsonWriter out;
  out.beginObject(Layout::Block);
  out.key("a");
  out.unsignedInteger(1);
  out.key("b");
  out.beginObject(Layout::Block);
  out.key("c");
  out.boolean(true);
  out.key("d");
  out.null();
  out.endObject();
  out.key("e");
  out.beginArray(Layout::Block);
  out.string("x");
  out.boolean(false);
  out.endArray();
  out.endObject();
  REQUIRE(out.finish() == "{\n"
                          "  \"a\": 1,\n"
                          "  \"b\": {\n"
                          "    \"c\": true,\n"
                          "    \"d\": null\n"
                          "  },\n"
                          "  \"e\": [\n"
                          "    \"x\",\n"
                          "    false\n"
                          "  ]\n"
                          "}\n");
}

TEST_CASE("format/writer: inline containers stay on one line") {
  JsonWriter out;
  out.beginArray(Layout::Block);
  out.beginObject(Layout::Inline);
  out.key("id");
  out.unsignedInteger(7);
  out.key("pair");
  // Asked for as a block, written inline because it is inside an inline
  // container.
  out.beginArray(Layout::Block);
  out.signedInteger(-4);
  out.signedInteger(4);
  out.endArray();
  out.key("nested");
  out.beginObject(Layout::Block);
  out.key("k");
  out.null();
  out.endObject();
  out.endObject();
  out.beginArray(Layout::Inline);
  out.unsignedInteger(1);
  out.endArray();
  out.endArray();
  REQUIRE(out.finish() ==
          "[\n"
          "  { \"id\": 7, \"pair\": [-4, 4], \"nested\": { \"k\": null } },\n"
          "  [1]\n"
          "]\n");
}

TEST_CASE("format/writer: empty containers have no inside") {
  JsonWriter out;
  out.beginObject(Layout::Block);
  out.key("list");
  out.beginArray(Layout::Block);
  out.endArray();
  out.key("object");
  out.beginObject(Layout::Block);
  out.endObject();
  out.key("inline");
  out.beginObject(Layout::Inline);
  out.key("list");
  out.beginArray(Layout::Inline);
  out.endArray();
  out.key("object");
  out.beginObject(Layout::Inline);
  out.endObject();
  out.endObject();
  out.endObject();
  REQUIRE(out.finish() == "{\n"
                          "  \"list\": [],\n"
                          "  \"object\": {},\n"
                          "  \"inline\": { \"list\": [], \"object\": {} }\n"
                          "}\n");
}

TEST_CASE("format/writer: a value alone is followed by one newline") {
  JsonWriter out;
  out.null();
  REQUIRE(out.finish() == "null\n");
}

TEST_CASE("format/writer: integers are written exactly, to both ends") {
  JsonWriter out;
  out.beginArray(Layout::Inline);
  out.unsignedInteger(0);
  out.unsignedInteger(std::numeric_limits<std::uint64_t>::max());
  out.signedInteger(std::numeric_limits<std::int64_t>::min());
  out.signedInteger(std::numeric_limits<std::int64_t>::max());
  out.signedInteger(9'007'199'254'740'993);
  out.endArray();
  REQUIRE(out.finish() == "[0, 18446744073709551615, -9223372036854775808, "
                          "9223372036854775807, 9007199254740993]\n");
}

TEST_CASE("format/writer: a number is the shortest decimal that reads back") {
  REQUIRE(written(0.0) == "0.0");
  REQUIRE(written(1.0) == "1.0");
  REQUIRE(written(-96.0) == "-96.0");
  REQUIRE(written(-6.5) == "-6.5");
  REQUIRE(written(0.1) == "0.1");
  REQUIRE(written(0.1 + 0.2) == "0.30000000000000004");
  REQUIRE(written(1.0 / 3.0) == "0.3333333333333333");
  REQUIRE(written(1e21) == "1e+21");
  REQUIRE(written(123456789.0) == "123456789.0");
  REQUIRE(written(1e-7) == "1e-07");
  REQUIRE(written(5e-324) == "5e-324");
  REQUIRE(written(1.7976931348623157e308) == "1.7976931348623157e+308");

  const std::vector<double> values{
      0.0,
      1.0,
      -6.5,
      0.1,
      0.1 + 0.2,
      1.0 / 3.0,
      2.0 / 3.0,
      1e21,
      1e22,
      1e23,
      1e-7,
      123456789012345680.0,
      4.35,
      0.000001,
      std::numeric_limits<double>::min(),
      std::numeric_limits<double>::denorm_min(),
      std::numeric_limits<double>::max(),
      std::numeric_limits<double>::lowest(),
      std::numeric_limits<double>::epsilon(),
      9007199254740993.0,
  };
  for (const double value : values) {
    const std::string text = written(value);
    CAPTURE(text);
    REQUIRE(readBack(text) == value);
    // Never to be taken for an integer.
    REQUIRE(text.find_first_of(".e") != std::string::npos);
    // And what the writer wrote is what the reader takes.
    const auto parsed = parseJson(text);
    REQUIRE(parsed.has_value());
    REQUIRE(std::get<double>(parsed->data) == value);
  }
}

TEST_CASE("format/writer: hundredths are written as an exact decimal") {
  const auto text = [](std::uint64_t value) {
    JsonWriter out;
    out.hundredths(value);
    return out.finish();
  };
  REQUIRE(text(12'000) == "120.0\n");
  REQUIRE(text(12'050) == "120.5\n");
  REQUIRE(text(12'005) == "120.05\n");
  REQUIRE(text(9'347) == "93.47\n");
  REQUIRE(text(2'000) == "20.0\n");
  REQUIRE(text(30'000) == "300.0\n");
  REQUIRE(text(7) == "0.07\n");
  // The same digits as the shortest decimal of the value as a double.
  for (std::uint64_t value = 2'000; value <= 30'000; ++value) {
    std::string expected = written(static_cast<double>(value) / 100.0);
    expected += '\n';
    REQUIRE(text(value) == expected);
  }
}

TEST_CASE("format/writer: strings escape what JSON requires and no more") {
  REQUIRE(written("plain") == "\"plain\"");
  REQUIRE(written("") == "\"\"");
  REQUIRE(written("say \"hi\" \\ there") == R"("say \"hi\" \\ there")");
  REQUIRE(written("a\b\f\n\r\tz") == R"("a\b\f\n\r\tz")");
  REQUIRE(written(std::string_view{"\0\x01\x1f", 3}) ==
          R"("\u0000\u0001\u001f")");
  // Not escaped: the slash, DEL, and anything outside ASCII.
  REQUIRE(written("a/b\x7f") == "\"a/b\x7f\"");
  REQUIRE(written("caf\xC3\xA9 \xE2\x9C\x93 \xF0\x9F\x8E\xB9") ==
          "\"caf\xC3\xA9 \xE2\x9C\x93 \xF0\x9F\x8E\xB9\"");
  // Keys are escaped the same way.
  JsonWriter out;
  out.beginObject(Layout::Inline);
  out.key("a\"b");
  out.null();
  out.endObject();
  REQUIRE(out.finish() == "{ \"a\\\"b\": null }\n");
}

TEST_CASE("format/writer: every escaped string reads back as it was") {
  std::string everyByte;
  for (int byte = 0; byte < 0x80; ++byte) {
    everyByte += static_cast<char>(byte);
  }
  everyByte += "\xC2\x80\xDF\xBF\xE0\xA0\x80\xED\x9F\xBF\xEE\x80\x80"
               "\xF0\x90\x80\x80\xF4\x8F\xBF\xBF";
  const auto parsed = parseJson(written(everyByte));
  REQUIRE(parsed.has_value());
  REQUIRE(std::get<std::string>(parsed->data) == everyByte);
}

TEST_CASE("format/text: UTF-8 is checked strictly") {
  REQUIRE(isValidUtf8(""));
  REQUIRE(isValidUtf8("ascii"));
  REQUIRE(isValidUtf8("\xC2\x80"));
  REQUIRE(isValidUtf8("\xDF\xBF"));
  REQUIRE(isValidUtf8("\xE0\xA0\x80"));
  REQUIRE(isValidUtf8("\xE1\x80\x80"));
  REQUIRE(isValidUtf8("\xED\x9F\xBF"));
  REQUIRE(isValidUtf8("\xEF\xBF\xBF"));
  REQUIRE(isValidUtf8("\xF0\x90\x80\x80"));
  REQUIRE(isValidUtf8("\xF1\x80\x80\x80"));
  REQUIRE(isValidUtf8("\xF4\x8F\xBF\xBF"));
  REQUIRE(isValidUtf8(std::string_view{"a\0b", 3}));

  // A continuation byte with nothing to continue; bytes that never occur.
  REQUIRE(!isValidUtf8("\x80"));
  REQUIRE(!isValidUtf8("\xBF"));
  REQUIRE(!isValidUtf8("\xFF"));
  REQUIRE(!isValidUtf8("\xFE"));
  REQUIRE(!isValidUtf8("\xF8\x88\x80\x80\x80"));
  // Overlong forms.
  REQUIRE(!isValidUtf8("\xC0\x80"));
  REQUIRE(!isValidUtf8("\xC1\xBF"));
  REQUIRE(!isValidUtf8("\xE0\x9F\xBF"));
  REQUIRE(!isValidUtf8("\xF0\x8F\xBF\xBF"));
  // Surrogates, and past the last code point.
  REQUIRE(!isValidUtf8("\xED\xA0\x80"));
  REQUIRE(!isValidUtf8("\xED\xBF\xBF"));
  REQUIRE(!isValidUtf8("\xF4\x90\x80\x80"));
  REQUIRE(!isValidUtf8("\xF5\x80\x80\x80"));
  // Cut short, or continued by the wrong byte.
  REQUIRE(!isValidUtf8("\xC2"));
  REQUIRE(!isValidUtf8("\xE2\x9C"));
  REQUIRE(!isValidUtf8("\xF0\x9F\x8E"));
  REQUIRE(!isValidUtf8("\xC2\x41"));
  REQUIRE(!isValidUtf8("\xE2\x9C\x41"));
  REQUIRE(!isValidUtf8("\xE2\xC0\x80"));
}

TEST_CASE("format/text: the two shapes of a media path") {
  // Inside the project folder: relative and normalised.
  for (const std::string_view path : {
           "media/kick.wav",
           "kick.wav",
           "a/..b/c..d/...wav",
           "media/a b/c.wav",
           ".hidden",
           "...",
       }) {
    CAPTURE(path);
    REQUIRE(isProjectMediaPath(path));
    REQUIRE(!isExternalMediaPath(path));
    REQUIRE(isAllowedMediaPath(path));
  }
  // A reference to a file outside: absolute.
  for (const std::string_view path :
       {"/home/someone/kick.wav", "/", "/home/../kick.wav", "//server/a.wav"}) {
    CAPTURE(path);
    REQUIRE(isExternalMediaPath(path));
    REQUIRE(!isProjectMediaPath(path));
    REQUIRE(isAllowedMediaPath(path));
  }
  // Neither.
  for (const std::string_view path : {
           std::string_view{},
           std::string_view{".."},
           std::string_view{"../kick.wav"},
           std::string_view{"media/../../kick.wav"},
           std::string_view{"media/.."},
           std::string_view{"."},
           std::string_view{"./kick.wav"},
           std::string_view{"media/./kick.wav"},
           std::string_view{"media//kick.wav"},
           std::string_view{"media/"},
           std::string_view{"media/a\0.wav", 12},
           std::string_view{"/abs\0", 5},
       }) {
    CAPTURE(path);
    REQUIRE(!isAllowedMediaPath(path));
  }
}

TEST_CASE("format/text: a content hash is 64 lower-case hexadecimal digits") {
  REQUIRE(isContentHash(
      "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
  REQUIRE(isContentHash(std::string(64, 'f')));
  for (const std::string &text : {
           std::string{},
           std::string{"00"},
           std::string(63, 'a'),
           std::string(65, 'a'),
           std::string(64, 'A'),
           std::string(64, 'g'),
           std::string(63, 'a') + "\n",
       }) {
    REQUIRE(!isContentHash(text));
  }
}

} // namespace lina::format::detail
