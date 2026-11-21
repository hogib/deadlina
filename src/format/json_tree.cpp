// SPDX-License-Identifier: GPL-3.0-or-later
#include "internal/json_tree.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <memory>
#include <optional>
#include <ranges>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace lina::format::detail {

namespace {

constexpr std::string_view byteOrderMark = "\xEF\xBB\xBF";

// Receives the parser's events and builds the tree, refusing on the way what
// the parser itself lets through: repeated keys, deep nesting, and integer
// literals that it would quietly turn into floating point.
class TreeBuilder {
public:
  // The names nlohmann's SAX interface asks for.
  using number_integer_t = std::int64_t;
  using number_unsigned_t = std::uint64_t;
  using number_float_t = double;
  using string_t = std::string;
  using binary_t = nlohmann::json::binary_t;

  explicit TreeBuilder(std::string_view text) : text_(text) {}

  bool null() { return place({.data = nullptr}) != nullptr; }
  bool boolean(bool value) { return place({.data = value}) != nullptr; }
  bool number_integer(number_integer_t value) {
    return place({.data = value}) != nullptr;
  }
  bool number_unsigned(number_unsigned_t value) {
    return place({.data = value}) != nullptr;
  }
  bool number_float(number_float_t /*parsed*/, const string_t &literal) {
    if (literal.find_first_of(".eE") == std::string::npos) {
      return refuse(ErrorCode::Value,
                    "the integer " + literal + " does not fit in 64 bits");
    }
    // Converted again from the literal, with the conversion of the standard
    // library that no language setting of the system can reach.
    double value = 0.0;
    const char *const first = literal.data();
    const char *const last = std::to_address(literal.end());
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc{}) {
      return refuse(ErrorCode::Value,
                    "the number " + literal + " is outside what can be stored");
    }
    return place({.data = value}) != nullptr;
  }
  bool string(string_t &value) {
    return place({.data = std::move(value)}) != nullptr;
  }
  // Never called for JSON text.
  // NOLINTNEXTLINE(readability-convert-member-functions-to-static): SAX
  bool binary(binary_t & /*value*/) { return false; }

  bool start_object(std::size_t /*elements*/) {
    return open({.data = JsonValue::Object{}});
  }
  bool key(string_t &name) {
    Frame &frame = frames_.back();
    if (!frame.keys.insert(name).second) {
      std::string where = containerPointer();
      appendPointerStep(where, name);
      error_ = Error{
          .code = ErrorCode::Structure,
          .where = std::move(where),
          .message = "the key \"" + name + "\" appears twice",
      };
      return false;
    }
    std::get<JsonValue::Object>(frame.container->data)
        .push_back({.key = std::move(name), .value = {}});
    return true;
  }
  bool end_object() {
    frames_.pop_back();
    return true;
  }
  bool start_array(std::size_t /*elements*/) {
    return open({.data = JsonValue::Array{}});
  }
  bool end_array() {
    frames_.pop_back();
    return true;
  }

  bool parse_error(std::size_t position, const std::string & /*lastToken*/,
                   const nlohmann::json::exception &problem) {
    // The parser's own text reads "[json.exception.<kind>.<number>] parse
    // error at line L, column C: <what>"; only <what> is kept, and the place
    // is worked out again here.
    std::string message = problem.what();
    if (const auto bracket = message.find("] "); bracket != std::string::npos) {
      message.erase(0, bracket + 2);
    }
    if (message.starts_with("parse error")) {
      message.erase(0, message.find(": ") + 2);
    }
    // 406 is the parser's "number overflow": well-formed, but too large.
    constexpr int numberOverflow = 406;
    error_ = Error{
        .code =
            problem.id == numberOverflow ? ErrorCode::Value : ErrorCode::Syntax,
        .where = lineAndColumn(position),
        .message = std::move(message),
    };
    return false;
  }

  [[nodiscard]] JsonValue takeRoot() { return std::move(root_); }
  [[nodiscard]] Error takeError() {
    return std::move(error_).value_or(Error{
        .code = ErrorCode::Syntax,
        .where = {},
        .message = "not a JSON text",
    });
  }

private:
  struct Frame {
    JsonValue *container{nullptr};
    // The keys seen so far when the container is an object.
    std::unordered_set<std::string> keys;
  };

  // Puts a value where the next one belongs and returns where it now is.
  JsonValue *place(JsonValue value) {
    if (frames_.empty()) {
      root_ = std::move(value);
      return &root_;
    }
    JsonValue &container = *frames_.back().container;
    if (auto *array = std::get_if<JsonValue::Array>(&container.data)) {
      array->push_back(std::move(value));
      return &array->back();
    }
    JsonValue &slot = std::get<JsonValue::Object>(container.data).back().value;
    slot = std::move(value);
    return &slot;
  }

  bool open(JsonValue container) {
    if (frames_.size() >= maxJsonDepth) {
      return refuse(ErrorCode::Structure, "nested too deeply");
    }
    // A container only grows while it is the innermost open one, so the
    // address of an open container never changes.
    frames_.push_back({.container = place(std::move(container)), .keys = {}});
    return true;
  }

  bool refuse(ErrorCode code, std::string message) {
    error_ =
        Error{.code = code, .where = pointer(), .message = std::move(message)};
    return false;
  }

  // The JSON pointer of the innermost open container. Every container but
  // the innermost has its open child as its last element or member.
  [[nodiscard]] std::string containerPointer() const {
    std::string path;
    for (const Frame &frame : frames_ | std::views::take(frames_.size() - 1)) {
      if (const auto *array =
              std::get_if<JsonValue::Array>(&frame.container->data)) {
        appendPointerStep(path, array->size() - 1);
      } else {
        appendPointerStep(
            path,
            std::get<JsonValue::Object>(frame.container->data).back().key);
      }
    }
    return path;
  }

  // The JSON pointer of the value being read, which is not in the tree yet.
  [[nodiscard]] std::string pointer() const {
    if (frames_.empty()) {
      return {};
    }
    std::string path = containerPointer();
    const JsonValue &container = *frames_.back().container;
    if (const auto *array = std::get_if<JsonValue::Array>(&container.data)) {
      appendPointerStep(path, array->size());
    } else {
      appendPointerStep(path,
                        std::get<JsonValue::Object>(container.data).back().key);
    }
    return path;
  }

  [[nodiscard]] std::string lineAndColumn(std::size_t position) const {
    // `position` counts the characters read, the offending one included.
    const std::string_view before =
        text_.substr(0, std::min(position, text_.size()));
    const auto line = 1 + std::ranges::count(before, '\n');
    const auto lineStart = before.rfind('\n');
    const std::size_t column =
        lineStart == std::string_view::npos
            ? std::max<std::size_t>(position, 1)
            : std::max<std::size_t>(position - lineStart - 1, 1);
    return "line " + std::to_string(line) + ", column " +
           std::to_string(column);
  }

  std::string_view text_;
  JsonValue root_;
  std::vector<Frame> frames_;
  std::optional<Error> error_;
};

// Receives the parser's events only as far as the top-level "format" member
// and stops the parser there.
class VersionPeeker {
public:
  using number_integer_t = std::int64_t;
  using number_unsigned_t = std::uint64_t;
  using number_float_t = double;
  using string_t = std::string;
  using binary_t = nlohmann::json::binary_t;

  [[nodiscard]] std::optional<int> found() const { return found_; }

  bool null() { return scalar(); }
  bool boolean(bool /*value*/) { return scalar(); }
  bool number_integer(number_integer_t /*value*/) { return scalar(); }
  bool number_unsigned(number_unsigned_t value) {
    constexpr number_unsigned_t largest = 0x7FFFFFFF;
    if (atFormat_ && value <= largest) {
      found_ = static_cast<int>(value);
    }
    return scalar();
  }
  bool number_float(number_float_t /*value*/, const string_t & /*literal*/) {
    return scalar();
  }
  bool string(string_t & /*value*/) { return scalar(); }
  // NOLINTNEXTLINE(readability-convert-member-functions-to-static): SAX
  bool binary(binary_t & /*value*/) { return false; }
  bool start_object(std::size_t /*elements*/) { return open(); }
  bool key(string_t &name) {
    atFormat_ = depth_ == 1 && name == "format";
    return true;
  }
  bool end_object() { return close(); }
  bool start_array(std::size_t /*elements*/) { return open(); }
  bool end_array() { return close(); }
  // NOLINTNEXTLINE(readability-convert-member-functions-to-static): SAX
  bool parse_error(std::size_t /*position*/, const std::string & /*lastToken*/,
                   const nlohmann::json::exception & /*problem*/) {
    return false;
  }

private:
  // A value that is not a container: the answer if it is the "format"
  // member, whatever its type, and otherwise of no interest.
  bool scalar() {
    const bool goOn = !atFormat_ && depth_ > 0;
    atFormat_ = false;
    return goOn;
  }
  bool open() {
    // The "format" member holding a container is an answer too: none.
    if (atFormat_) {
      return false;
    }
    ++depth_;
    return true;
  }
  bool close() {
    --depth_;
    return depth_ > 0;
  }

  std::size_t depth_{0};
  bool atFormat_{false};
  std::optional<int> found_;
};

} // namespace

std::optional<int> peekFormatVersion(std::string_view text) {
  if (text.starts_with(byteOrderMark)) {
    return std::nullopt;
  }
  VersionPeeker peeker;
  nlohmann::json::sax_parse(text.begin(), text.end(), &peeker);
  return peeker.found();
}

const JsonValue *findMember(const JsonValue::Object &object,
                            std::string_view key) {
  const auto found = std::ranges::find(object, key, &JsonMember::key);
  return found == object.end() ? nullptr : &found->value;
}

JsonValue *findMember(JsonValue::Object &object, std::string_view key) {
  const auto found = std::ranges::find(object, key, &JsonMember::key);
  return found == object.end() ? nullptr : &found->value;
}

std::string_view jsonTypeName(const JsonValue &value) {
  constexpr std::array<std::string_view, 8> names{
      "null",   "boolean", "integer", "integer",
      "number", "string",  "array",   "object",
  };
  return names.at(value.data.index());
}

void appendPointerStep(std::string &pointer, std::string_view key) {
  pointer += '/';
  for (const char character : key) {
    if (character == '~') {
      pointer += "~0";
    } else if (character == '/') {
      pointer += "~1";
    } else {
      pointer += character;
    }
  }
}

void appendPointerStep(std::string &pointer, std::size_t index) {
  pointer += '/';
  pointer += std::to_string(index);
}

std::expected<JsonValue, Error> parseJson(std::string_view text) {
  if (text.empty()) {
    return std::unexpected(Error{
        .code = ErrorCode::Syntax,
        .where = "line 1, column 1",
        .message = "the file is empty",
    });
  }
  if (text.starts_with(byteOrderMark)) {
    return std::unexpected(Error{
        .code = ErrorCode::Syntax,
        .where = "line 1, column 1",
        .message = "the file starts with a byte order mark",
    });
  }
  // The parser would take a null character for the end of the text and
  // ignore whatever follows it.
  if (const auto null = text.find('\0'); null != std::string_view::npos) {
    const std::string_view before = text.substr(0, null);
    const auto lineStart = before.rfind('\n');
    return std::unexpected(Error{
        .code = ErrorCode::Syntax,
        .where = "line " +
                 std::to_string(1 + std::ranges::count(before, '\n')) +
                 ", column " +
                 std::to_string(lineStart == std::string_view::npos
                                    ? null + 1
                                    : null - lineStart),
        .message = "the file holds a null character",
    });
  }
  TreeBuilder builder{text};
  if (!nlohmann::json::sax_parse(text.begin(), text.end(), &builder)) {
    return std::unexpected(builder.takeError());
  }
  return builder.takeRoot();
}

} // namespace lina::format::detail
