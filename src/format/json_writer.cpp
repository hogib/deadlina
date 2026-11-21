// SPDX-License-Identifier: GPL-3.0-or-later
#include "internal/json_writer.hpp"

#include <array>
#include <charconv>
#include <memory>
#include <utility>

namespace lina::format::detail {

namespace {

constexpr std::string_view hexDigits = "0123456789abcdef";

// Appends a number through the locale-independent conversion of the standard
// library and returns what was appended.
template <class Number>
std::string_view appendNumber(std::string &out, Number value,
                              std::array<char, 32> &buffer) {
  char *const first = buffer.data();
  const auto result =
      std::to_chars(first, std::to_address(buffer.end()), value);
  const std::string_view text{first, result.ptr};
  out += text;
  return text;
}

} // namespace

void JsonWriter::newLine(std::size_t depth) {
  out_ += '\n';
  out_.append(2 * depth, ' ');
}

void JsonWriter::beforeValue() {
  if (afterKey_) {
    afterKey_ = false;
    return;
  }
  if (frames_.empty()) {
    return;
  }
  Frame &frame = frames_.back();
  if (frame.count > 0) {
    out_ += ',';
  }
  if (frame.layout == Layout::Block) {
    newLine(frames_.size());
  } else if (frame.count > 0 || frame.object) {
    out_ += ' ';
  }
  ++frame.count;
}

void JsonWriter::begin(bool object, Layout layout) {
  beforeValue();
  const bool insideInline =
      !frames_.empty() && frames_.back().layout == Layout::Inline;
  frames_.push_back({
      .object = object,
      .layout = insideInline ? Layout::Inline : layout,
      .count = 0,
  });
  out_ += object ? '{' : '[';
}

void JsonWriter::end(bool object) {
  const Frame frame = frames_.back();
  frames_.pop_back();
  if (frame.count > 0) {
    if (frame.layout == Layout::Block) {
      newLine(frames_.size());
    } else if (object) {
      out_ += ' ';
    }
  }
  out_ += object ? '}' : ']';
}

void JsonWriter::beginObject(Layout layout) { begin(true, layout); }
void JsonWriter::endObject() { end(true); }
void JsonWriter::beginArray(Layout layout) { begin(false, layout); }
void JsonWriter::endArray() { end(false); }

void JsonWriter::key(std::string_view name) {
  beforeValue();
  quoted(name);
  out_ += ": ";
  afterKey_ = true;
}

void JsonWriter::null() {
  beforeValue();
  out_ += "null";
}

void JsonWriter::boolean(bool value) {
  beforeValue();
  out_ += value ? "true" : "false";
}

void JsonWriter::unsignedInteger(std::uint64_t value) {
  beforeValue();
  std::array<char, 32> buffer{};
  appendNumber(out_, value, buffer);
}

void JsonWriter::signedInteger(std::int64_t value) {
  beforeValue();
  std::array<char, 32> buffer{};
  appendNumber(out_, value, buffer);
}

void JsonWriter::number(double value) {
  beforeValue();
  std::array<char, 32> buffer{};
  const std::string_view text = appendNumber(out_, value, buffer);
  if (text.find_first_of(".e") == std::string_view::npos) {
    out_ += ".0";
  }
}

void JsonWriter::hundredths(std::uint64_t value) {
  beforeValue();
  std::array<char, 32> buffer{};
  appendNumber(out_, value / 100, buffer);
  out_ += '.';
  const std::uint64_t fraction = value % 100;
  appendNumber(out_, fraction / 10, buffer);
  if (fraction % 10 != 0) {
    appendNumber(out_, fraction % 10, buffer);
  }
}

void JsonWriter::string(std::string_view text) {
  beforeValue();
  quoted(text);
}

void JsonWriter::quoted(std::string_view text) {
  out_ += '"';
  for (const char character : text) {
    switch (character) {
    case '"':
      out_ += "\\\"";
      break;
    case '\\':
      out_ += "\\\\";
      break;
    case '\b':
      out_ += "\\b";
      break;
    case '\f':
      out_ += "\\f";
      break;
    case '\n':
      out_ += "\\n";
      break;
    case '\r':
      out_ += "\\r";
      break;
    case '\t':
      out_ += "\\t";
      break;
    default:
      if (const std::size_t code = static_cast<unsigned char>(character);
          code < 0x20) {
        out_ += "\\u00";
        out_ += hexDigits.at(code / 16);
        out_ += hexDigits.at(code % 16);
      } else {
        out_ += character;
      }
    }
  }
  out_ += '"';
}

std::string JsonWriter::finish() {
  out_ += '\n';
  return std::exchange(out_, {});
}

} // namespace lina::format::detail
