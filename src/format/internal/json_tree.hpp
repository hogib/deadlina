// SPDX-License-Identifier: GPL-3.0-or-later
//
// Private to lina_format and its tests: a parsed JSON text as a plain tree.
// Migrations rewrite this tree and the strict reader maps it to the model
// (docs/design/16-project-file-format-encoding.md, "Versions and migration").
// The parser behind `parseJson` is nlohmann/json; none of its types appear
// here.
#ifndef LINA_FORMAT_INTERNAL_JSON_TREE_HPP
#define LINA_FORMAT_INTERNAL_JSON_TREE_HPP

#include "lina/format/error.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace lina::format::detail {

struct JsonMember;

// One JSON value. An integer literal is kept exactly: as `uint64_t` when it is
// written without a sign, as `int64_t` when written with a minus sign; `-0`
// is therefore a signed zero, which the strict reader refuses as a spelling
// of zero. Only a literal with a fraction or an exponent becomes a `double`.
// Members of an object keep the order of the text.
struct JsonValue {
  using Array = std::vector<JsonValue>;
  using Object = std::vector<JsonMember>;

  std::variant<std::nullptr_t, bool, std::uint64_t, std::int64_t, double,
               std::string, Array, Object>
      data;

  friend bool operator==(const JsonValue &, const JsonValue &) = default;
};

struct JsonMember {
  std::string key;
  JsonValue value;

  friend bool operator==(const JsonMember &, const JsonMember &) = default;
};

// Containers nested deeper than this are refused. A document nests 11 deep.
inline constexpr std::size_t maxJsonDepth = 32;

// The member of an object with this key, or null.
const JsonValue *findMember(const JsonValue::Object &object,
                            std::string_view key);
JsonValue *findMember(JsonValue::Object &object, std::string_view key);

// "null", "boolean", "integer", "number", "string", "array" or "object".
std::string_view jsonTypeName(const JsonValue &value);

// Appends one step to a JSON pointer (RFC 6901).
void appendPointerStep(std::string &pointer, std::string_view key);
void appendPointerStep(std::string &pointer, std::size_t index);

// Parses exactly one JSON text. Refused, with the line and column or the JSON
// pointer of the place: malformed JSON, anything after the value but white
// space, text that is not UTF-8, a byte order mark, an empty input, an object
// with the same key twice, an integer literal outside 64 bits, a number
// outside what a double holds, and nesting deeper than `maxJsonDepth`.
std::expected<JsonValue, Error> parseJson(std::string_view text);

// The "format" member of a document, read without building the tree: the
// first member of that name at the top level, if it is a whole number from 0
// to 2^31 - 1. Parsing stops there, so what follows it is not checked. Empty
// when the text is not an object that starts well enough to get that far.
std::optional<int> peekFormatVersion(std::string_view text);

} // namespace lina::format::detail

#endif // LINA_FORMAT_INTERNAL_JSON_TREE_HPP
