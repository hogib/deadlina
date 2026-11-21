// SPDX-License-Identifier: GPL-3.0-or-later
//
// Private to lina_format: reading a parsed tree strictly
// (docs/design/16-project-file-format-encoding.md, "Reading is strict"). An
// unknown key, a missing key or a value of the wrong type is an error that
// names the place by JSON pointer. The first error wins; after it every
// accessor returns a harmless default, so mapping code needs no check after
// each field and asks once at the end.
#ifndef LINA_FORMAT_INTERNAL_STRICT_READER_HPP
#define LINA_FORMAT_INTERNAL_STRICT_READER_HPP

#include "internal/json_tree.hpp"
#include "lina/format/error.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace lina::format::detail {

// A value in the tree and the way to it from the root. Nodes live on the stack
// of the code that walks the tree.
struct Node {
  const JsonValue *value{nullptr};
  const Node *parent{nullptr};
  // The member name when the parent is an object.
  std::string_view key;
  // The element number when the parent is an array.
  std::size_t index{0};
  bool inArray{false};
};

// One name of an enumeration as the file spells it.
template <class Enumeration> struct EnumName {
  Enumeration value;
  std::string_view name;
};

class StrictReader {
public:
  [[nodiscard]] bool failed() const { return error_.has_value(); }
  // The recorded error, if any; the reader is spent afterwards.
  [[nodiscard]] std::optional<Error> takeError() { return std::move(error_); }

  // Records an error at a node unless one is recorded already.
  void fail(const Node &at, ErrorCode code, std::string message);

  // Checks that the node is an object with exactly these keys.
  bool expectObject(const Node &node,
                    std::initializer_list<std::string_view> keys);
  // The elements of an array node; empty after a failure.
  const JsonValue::Array &array(const Node &node);

  bool boolean(const Node &node);
  // An integer literal from zero to `max`. Here and below, `-0` is refused:
  // zero has one spelling.
  std::uint64_t unsignedInteger(const Node &node, std::uint64_t max);
  // An integer literal from `min` (zero or less) to `max` (zero or more).
  std::int64_t signedInteger(const Node &node, std::int64_t min,
                             std::int64_t max);
  // Any number: a literal with a fraction or exponent, or an integer literal,
  // which a hand edit may well leave where a decimal is usual.
  double number(const Node &node);
  const std::string &string(const Node &node);

  // A string that is one of the names in `names`.
  template <class Enumeration, std::size_t Count>
  Enumeration
  enumerator(const Node &node,
             const std::array<EnumName<Enumeration>, Count> &names) {
    const std::string &text = string(node);
    const auto *found =
        std::ranges::find(names, text, &EnumName<Enumeration>::name);
    if (found == names.end()) {
      fail(node, ErrorCode::Value, "\"" + text + "\" is not a known name here");
      return names.front().value;
    }
    return found->value;
  }

private:
  void wrongType(const Node &node, std::string_view wanted);

  std::optional<Error> error_;
};

// The name of an enumerator that is in the table.
template <class Enumeration, std::size_t Count>
std::string_view nameOf(const std::array<EnumName<Enumeration>, Count> &names,
                        Enumeration value) {
  return std::ranges::find(names, value, &EnumName<Enumeration>::value)->name;
}

// The member of an object node; a null value when there is none, which
// `expectObject` has reported by then.
Node member(const Node &object, std::string_view key);

bool isNull(const Node &node);

// The node of the root of a tree.
inline Node rootNode(const JsonValue &root) {
  return {
      .value = &root,
      .parent = nullptr,
      .key = {},
      .index = 0,
      .inArray = false,
  };
}
// The node of the member `key` of an object node.
inline Node memberNode(const Node &object, const JsonValue &member,
                       std::string_view key) {
  return {
      .value = &member,
      .parent = &object,
      .key = key,
      .index = 0,
      .inArray = false,
  };
}
// The node of element `index` of an array node.
inline Node elementNode(const Node &array, const JsonValue &element,
                        std::size_t index) {
  return {
      .value = &element,
      .parent = &array,
      .key = {},
      .index = index,
      .inArray = true,
  };
}

} // namespace lina::format::detail

#endif // LINA_FORMAT_INTERNAL_STRICT_READER_HPP
