// SPDX-License-Identifier: GPL-3.0-or-later
#include "internal/strict_reader.hpp"

#include <ranges>
#include <utility>
#include <variant>
#include <vector>

namespace lina::format::detail {

namespace {

// The JSON pointer of a node.
std::string pointerOf(const Node &node) {
  std::vector<const Node *> steps;
  for (const Node *step = &node; step->parent != nullptr; step = step->parent) {
    steps.push_back(step);
  }
  std::string pointer;
  for (const Node *step : std::views::reverse(steps)) {
    if (step->inArray) {
      appendPointerStep(pointer, step->index);
    } else {
      appendPointerStep(pointer, step->key);
    }
  }
  return pointer;
}

constexpr const char *negativeZero = "zero is written 0, not -0";

std::string quote(std::string_view text) {
  return "\"" + std::string{text} + "\"";
}

} // namespace

void StrictReader::fail(const Node &at, ErrorCode code, std::string message) {
  if (!error_.has_value()) {
    error_ = Error{
        .code = code,
        .where = pointerOf(at),
        .message = std::move(message),
    };
  }
}

void StrictReader::wrongType(const Node &node, std::string_view wanted) {
  fail(node, ErrorCode::Structure,
       "expected " + std::string{wanted} + ", found " +
           std::string{jsonTypeName(*node.value)});
}

bool StrictReader::expectObject(const Node &node,
                                std::initializer_list<std::string_view> keys) {
  if (failed()) {
    return false;
  }
  const auto *object = std::get_if<JsonValue::Object>(&node.value->data);
  if (object == nullptr) {
    wrongType(node, "an object");
    return false;
  }
  const auto unknown =
      std::ranges::find_if(*object, [&keys](const JsonMember &candidate) {
        return std::ranges::find(keys, candidate.key) == keys.end();
      });
  if (unknown != object->end()) {
    fail(memberNode(node, unknown->value, unknown->key), ErrorCode::Structure,
         "unknown key " + quote(unknown->key));
    return false;
  }
  const auto *const missing =
      std::ranges::find_if(keys, [object](std::string_view key) {
        return findMember(*object, key) == nullptr;
      });
  if (missing != keys.end()) {
    fail(node, ErrorCode::Structure, "missing key " + quote(*missing));
    return false;
  }
  return true;
}

Node member(const Node &object, std::string_view key) {
  static const JsonValue nothing;
  const JsonValue *found = nullptr;
  if (const auto *members =
          std::get_if<JsonValue::Object>(&object.value->data)) {
    found = findMember(*members, key);
  }
  return memberNode(object, found == nullptr ? nothing : *found, key);
}

const JsonValue::Array &StrictReader::array(const Node &node) {
  static const JsonValue::Array empty;
  if (failed()) {
    return empty;
  }
  const auto *elements = std::get_if<JsonValue::Array>(&node.value->data);
  if (elements == nullptr) {
    wrongType(node, "an array");
    return empty;
  }
  return *elements;
}

bool isNull(const Node &node) {
  return std::holds_alternative<std::nullptr_t>(node.value->data);
}

bool StrictReader::boolean(const Node &node) {
  if (failed()) {
    return false;
  }
  const bool *value = std::get_if<bool>(&node.value->data);
  if (value == nullptr) {
    wrongType(node, "true or false");
    return false;
  }
  return *value;
}

std::uint64_t StrictReader::unsignedInteger(const Node &node,
                                            std::uint64_t max) {
  if (failed()) {
    return 0;
  }
  if (const auto *negative = std::get_if<std::int64_t>(&node.value->data)) {
    fail(node, ErrorCode::Value,
         *negative == 0 ? negativeZero : "must not be negative");
    return 0;
  }
  const auto *value = std::get_if<std::uint64_t>(&node.value->data);
  if (value == nullptr) {
    wrongType(node, "an integer");
    return 0;
  }
  if (*value > max) {
    fail(node, ErrorCode::Value,
         std::to_string(*value) + " is above the largest value allowed, " +
             std::to_string(max));
    return 0;
  }
  return *value;
}

std::int64_t StrictReader::signedInteger(const Node &node, std::int64_t min,
                                         std::int64_t max) {
  if (failed()) {
    return 0;
  }
  if (const auto *negative = std::get_if<std::int64_t>(&node.value->data)) {
    if (*negative == 0) {
      fail(node, ErrorCode::Value, negativeZero);
      return 0;
    }
    if (*negative < min) {
      fail(node, ErrorCode::Value,
           std::to_string(*negative) +
               " is below the smallest value allowed, " + std::to_string(min));
      return 0;
    }
    return *negative;
  }
  return static_cast<std::int64_t>(
      unsignedInteger(node, static_cast<std::uint64_t>(max)));
}

double StrictReader::number(const Node &node) {
  if (failed()) {
    return 0.0;
  }
  if (const auto *value = std::get_if<double>(&node.value->data)) {
    return *value;
  }
  if (const auto *value = std::get_if<std::uint64_t>(&node.value->data)) {
    return static_cast<double>(*value);
  }
  if (const auto *value = std::get_if<std::int64_t>(&node.value->data)) {
    if (*value == 0) {
      fail(node, ErrorCode::Value, negativeZero);
    }
    return static_cast<double>(*value);
  }
  wrongType(node, "a number");
  return 0.0;
}

const std::string &StrictReader::string(const Node &node) {
  static const std::string empty;
  if (failed()) {
    return empty;
  }
  const auto *value = std::get_if<std::string>(&node.value->data);
  if (value == nullptr) {
    wrongType(node, "a string");
    return empty;
  }
  return *value;
}

} // namespace lina::format::detail
