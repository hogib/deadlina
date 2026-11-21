// SPDX-License-Identifier: GPL-3.0-or-later
//
// Private to lina_format and its tests: LINA's own canonical JSON writer
// (docs/design/16-project-file-format-encoding.md, "Encoding of the
// document"). The layout is fixed, so the same calls always give the same
// bytes:
//   - a block container puts each element on a line of its own, indented by
//     two spaces per level;
//   - an inline container stays on one line: `{ "a": 1, "b": 2 }`, `[1, 2]`;
//   - an empty container is `{}` or `[]`;
//   - the text ends with one newline.
// Keys come out in the order they are written.
#ifndef LINA_FORMAT_INTERNAL_JSON_WRITER_HPP
#define LINA_FORMAT_INTERNAL_JSON_WRITER_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lina::format::detail {

enum class Layout : std::uint8_t { Block, Inline };

class JsonWriter {
public:
  // A container opened inside an inline container is written inline whatever
  // is asked for.
  void beginObject(Layout layout);
  void endObject();
  void beginArray(Layout layout);
  void endArray();

  // Names the next value; inside an object only.
  void key(std::string_view name);

  void null();
  void boolean(bool value);
  void unsignedInteger(std::uint64_t value);
  void signedInteger(std::int64_t value);
  // The shortest decimal that reads back to the same value, always with a
  // fraction or an exponent so that it is never taken for an integer. The
  // value must be finite.
  void number(double value);
  // A whole number of hundredths as a decimal: 12000 is `120.0`, 9347 is
  // `93.47`.
  void hundredths(std::uint64_t value);
  // `text` must be UTF-8 (`isValidUtf8`).
  void string(std::string_view text);

  // The finished text. Every container must be closed.
  [[nodiscard]] std::string finish();

private:
  struct Frame {
    bool object{false};
    Layout layout{Layout::Block};
    std::size_t count{0};
  };

  // Writes what separates a value from what came before it.
  void beforeValue();
  void begin(bool object, Layout layout);
  void end(bool object);
  void newLine(std::size_t depth);
  void quoted(std::string_view text);

  std::string out_;
  std::vector<Frame> frames_;
  bool afterKey_{false};
};

} // namespace lina::format::detail

#endif // LINA_FORMAT_INTERNAL_JSON_WRITER_HPP
