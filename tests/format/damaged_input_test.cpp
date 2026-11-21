// SPDX-License-Identifier: GPL-3.0-or-later
//
// Damaged input (docs/design/10-project-file-format-requirements.md, "Opening
// untrusted projects"): whatever the bytes, the reader gives an error or a
// valid document, and never crashes. The sanitizer presets run these too.
#include "support/documents.hpp"
#include "support/reader_invariant.hpp"
#include "support/scratch.hpp"
#include "support/text_edit.hpp"

#include "lina/format/document_io.hpp"
#include "lina/format/view_state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>

namespace lina::format::testing {

namespace {

struct Tally {
  std::size_t refused{0};
  std::size_t accepted{0};

  void count(ReadOutcome outcome) {
    refused += outcome == ReadOutcome::Refused ? 1 : 0;
    accepted += outcome == ReadOutcome::Accepted ? 1 : 0;
  }
};

std::string repeated(std::string_view piece, std::size_t count) {
  std::string text;
  for (std::size_t index = 0; index < count; ++index) {
    text += piece;
  }
  return text;
}

// Changes one to three places of a text: a byte replaced, inserted or
// removed, or a run of the text copied over another. Only the raw output of
// the generator is used, so every compiler walks the same sequence.
std::string mutated(std::string text, std::mt19937_64 &random) {
  constexpr std::string_view interesting = "{}[]\",:0-9.eE+ \n\\u\x00\xFF\x80";
  const std::size_t edits = 1 + (random() % 3);
  for (std::size_t edit = 0; edit < edits && !text.empty(); ++edit) {
    const std::size_t at = random() % text.size();
    const std::uint64_t pick = random();
    const char byte = pick % 2 == 0
                          ? interesting.at((pick >> 8U) % interesting.size())
                          : static_cast<char>((pick >> 8U) % 256);
    switch (random() % 4) {
    case 0:
      text.at(at) = byte;
      break;
    case 1:
      text.insert(at, 1, byte);
      break;
    case 2:
      text.erase(at, 1 + (random() % 4));
      break;
    default: {
      const std::size_t from = random() % text.size();
      const std::size_t length = random() % 24;
      text.replace(at, length, text.substr(from, length));
      break;
    }
    }
  }
  return text;
}

void checkMutations(const std::string &text, std::uint64_t seed,
                    std::size_t rounds) {
  std::mt19937_64 random{seed};
  Tally tally;
  for (std::size_t round = 0; round < rounds; ++round) {
    const std::string damaged = mutated(text, random);
    const ReadOutcome outcome = readOutcome(damaged);
    if (outcome == ReadOutcome::Broken) {
      CAPTURE(round, damaged);
      FAIL("the reader broke its promise");
    }
    tally.count(outcome);
  }
  // Both happen: most damage is refused, and some edits (a letter of a name)
  // leave a valid document.
  REQUIRE(tally.refused > rounds / 2);
  REQUIRE(tally.accepted > 0);
  REQUIRE(tally.refused + tally.accepted == rounds);
}

} // namespace

TEST_CASE("format/damaged: a file cut short at any byte is refused") {
  const std::string text = readText(goldenFile("v1/minimal.lina"));
  REQUIRE(text.size() > 1'000);
  for (std::size_t length = 0; length < text.size(); ++length) {
    const std::string_view cut = std::string_view{text}.substr(0, length);
    const ReadOutcome outcome = readOutcome(cut);
    CAPTURE(length);
    // Only the final newline can go missing without harm.
    REQUIRE(outcome == (length == text.size() - 1 ? ReadOutcome::Accepted
                                                  : ReadOutcome::Refused));
  }
  REQUIRE(readOutcome(text) == ReadOutcome::Accepted);
}

TEST_CASE("format/damaged: a file with its start missing is refused") {
  const std::string text = readText(goldenFile("v1/minimal.lina"));
  for (std::size_t skipped = 1; skipped < text.size(); skipped += 7) {
    REQUIRE(readOutcome(std::string_view{text}.substr(skipped)) ==
            ReadOutcome::Refused);
  }
}

TEST_CASE("format/damaged: changed bytes in the small file") {
  checkMutations(readText(goldenFile("v1/minimal.lina")), 20261121, 4'000);
}

TEST_CASE("format/damaged: changed bytes in the large file") {
  checkMutations(readText(goldenFile("v1/everything.lina")), 16, 1'500);
}

TEST_CASE("format/damaged: every single byte of the small file changed") {
  const std::string text = readText(goldenFile("v1/minimal.lina"));
  Tally tally;
  for (std::size_t at = 0; at < text.size(); ++at) {
    for (const char byte : {'\0', '"', '9', '\xFF', '}'}) {
      std::string damaged = text;
      damaged.at(at) = byte;
      const ReadOutcome outcome = readOutcome(damaged);
      REQUIRE(outcome != ReadOutcome::Broken);
      tally.count(outcome);
    }
  }
  REQUIRE(tally.refused > tally.accepted);
  REQUIRE(tally.accepted > 0);
}

TEST_CASE("format/damaged: deep nesting is refused without a deep stack") {
  const std::string text = readText(goldenFile("v1/minimal.lina"));
  for (const std::size_t depth : {33U, 1'000U, 300'000U}) {
    CAPTURE(depth);
    REQUIRE(readOutcome(repeated("[", depth)) == ReadOutcome::Refused);
    REQUIRE(readOutcome(repeated("[", depth) + repeated("]", depth)) ==
            ReadOutcome::Refused);
    REQUIRE(readOutcome(repeated("{\"a\":", depth)) == ReadOutcome::Refused);
    // Inside a document, where a list is expected.
    REQUIRE(readOutcome(replaced(text, "\"media\": []",
                                 "\"media\": " + repeated("[", depth) +
                                     repeated("]", depth))) ==
            ReadOutcome::Refused);
  }
}

TEST_CASE("format/damaged: huge and impossible numbers are refused") {
  const std::string text = readText(goldenFile("v1/minimal.lina"));
  const std::string digits = repeated("9", 5'000);
  std::string exponent = digits;
  exponent += 'e';
  exponent += digits;
  for (const std::string &number : {
           std::string{"1e999999999"},
           std::string{"-1e999999999"},
           std::string{"1e-999999999"},
           digits,
           "-" + digits,
           digits + ".5",
           exponent,
           std::string{"NaN"},
           std::string{"Infinity"},
           std::string{"-Infinity"},
           std::string{"nan"},
           std::string{"inf"},
           std::string{"0x7f"},
           std::string{"1e"},
           std::string{"18446744073709551616"},
           std::string{"-9223372036854775809"},
           std::string{"340282366920938463463374607431768211456"},
       }) {
    CAPTURE(number.substr(0, 40));
    for (const std::string_view field : {
             "\"tempo\": 120.0",
             "\"next_id\": 8",
             "\"start_marker\": 0",
             "\"volume_db\": 0.0",
             "\"pitch\": 60",
             "\"format\": 1",
         }) {
      const std::string key{field.substr(0, field.find(':') + 2)};
      REQUIRE(readOutcome(replaced(text, field, key + number)) ==
              ReadOutcome::Refused);
    }
  }
}

TEST_CASE("format/damaged: byte order marks and other encodings are refused") {
  const std::string text = readText(goldenFile("v1/minimal.lina"));
  REQUIRE(readOutcome("\xEF\xBB\xBF" + text) == ReadOutcome::Refused);
  REQUIRE(readOutcome("\xFE\xFF" + text) == ReadOutcome::Refused);
  REQUIRE(readOutcome("\xFF\xFE" + text) == ReadOutcome::Refused);
  // The text as UTF-16.
  std::string wide;
  for (const char character : text) {
    wide += character;
    wide += '\0';
  }
  REQUIRE(readOutcome(wide) == ReadOutcome::Refused);
  REQUIRE(readOutcome("\xFF\xFE" + wide) == ReadOutcome::Refused);
}

TEST_CASE("format/damaged: text that is not UTF-8 is refused") {
  const std::string text = readText(goldenFile("v1/minimal.lina"));
  for (const std::string_view bad : {
           "\xFF",
           "\xC0\x80",
           "\xED\xA0\x80",
           "\xE2\x9C",
           "\xF4\x90\x80\x80",
           "\x80",
           "Key\xC3",
           "\\ud800",
           "\\udc00x",
           "\\u12",
           "\\q",
       }) {
    const auto loaded = readDocument(replaced(
        text, R"("name": "Keys")", R"("name": ")" + std::string{bad} + "\""));
    REQUIRE(!loaded.has_value());
    REQUIRE(loaded.error().code == ErrorCode::Syntax);
    REQUIRE(loaded.error().where.starts_with("line "));
  }
  // The same bytes as well-formed UTF-8 are a name like any other.
  const auto fine = readDocument(
      replaced(text, R"("name": "Keys")", "\"name\": \"Kl\xC3\xA4vier\""));
  REQUIRE(fine.has_value());
  REQUIRE(fine->document.project().tracks.front().name == "Kl\xC3\xA4vier");
}

TEST_CASE("format/damaged: empty and nearly empty files are refused") {
  for (const std::string_view text : {
           "",
           " ",
           "\n",
           "{",
           "{}",
           "[]",
           "null",
           "0",
           "\"\"",
           "{\"format\"",
           "{\"format\": 1}",
           "{\"format\": 1,}",
       }) {
    CAPTURE(text);
    REQUIRE(readOutcome(text) == ReadOutcome::Refused);
  }
  REQUIRE(readOutcome(std::string(1, '\0')) == ReadOutcome::Refused);
  REQUIRE(readOutcome(std::string(4'096, '\0')) == ReadOutcome::Refused);
  const auto empty = readDocument("");
  REQUIRE(!empty.has_value());
  REQUIRE(empty.error().code == ErrorCode::Syntax);
  REQUIRE(empty.error().message == "the file is empty");
}

TEST_CASE("format/damaged: two documents in one file are refused") {
  const std::string text = readText(goldenFile("v1/minimal.lina"));
  REQUIRE(readOutcome(text + text) == ReadOutcome::Refused);
  REQUIRE(readOutcome(text + "x") == ReadOutcome::Refused);
  REQUIRE(readOutcome(text + std::string(1, '\0')) == ReadOutcome::Refused);
  REQUIRE(readOutcome(text + "\n \n") == ReadOutcome::Accepted);
}

TEST_CASE("format/damaged: the view state reader keeps the same promise") {
  const auto text = writeViewState({});
  REQUIRE(text.has_value());
  REQUIRE(viewReadOutcome(*text) == ReadOutcome::Accepted);
  for (std::size_t length = 0; length + 1 < text->size(); ++length) {
    REQUIRE(viewReadOutcome(std::string_view{*text}.substr(0, length)) ==
            ReadOutcome::Refused);
  }
  // The same walk every run, on purpose.
  // NOLINTBEGIN(bugprone-random-generator-seed,cert-msc32-c,cert-msc51-cpp)
  std::mt19937_64 random{4};
  // NOLINTEND(bugprone-random-generator-seed,cert-msc32-c,cert-msc51-cpp)
  Tally tally;
  for (int round = 0; round < 3'000; ++round) {
    const ReadOutcome outcome = viewReadOutcome(mutated(*text, random));
    REQUIRE(outcome != ReadOutcome::Broken);
    tally.count(outcome);
  }
  REQUIRE(tally.refused > 0);
  REQUIRE(tally.accepted > 0);
  // A document is not a view state, and the other way round.
  REQUIRE(viewReadOutcome(readText(goldenFile("v1/minimal.lina"))) ==
          ReadOutcome::Refused);
  REQUIRE(readOutcome(*text) == ReadOutcome::Refused);
}

} // namespace lina::format::testing
