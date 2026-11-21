// SPDX-License-Identifier: GPL-3.0-or-later
//
// Versions and migration
// (docs/design/16-project-file-format-encoding.md): a chain of single steps
// on the parsed tree, driven here by a history made up for the test, since
// the real format has only its first version.
#include "support/documents.hpp"
#include "support/text_edit.hpp"

#include "internal/migration.hpp"
#include "lina/format/document_io.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace lina::format::testing {

namespace {

using detail::FormatHistory;
using detail::JsonValue;
using detail::Migration;
using detail::MigrationContext;

// A made-up past. Its version 1 called the tempo "bpm" and counted it in
// whole beats per minute; version 2 called it "tempo_bpm" and had no start
// marker yet; version 3 was the "tempo" of today, still without a start
// marker; version 4 is the current shape.
// Each step checks that it is told the version it is given: a step run out
// of turn refuses.
std::optional<Error> expectVersion(const MigrationContext &context,
                                   int version) {
  if (context.fromVersion == version) {
    return std::nullopt;
  }
  return Error{
      .code = ErrorCode::Version,
      .where = "",
      .message = "the step from version " + std::to_string(version) +
                 " was given version " + std::to_string(context.fromVersion),
  };
}

std::optional<Error> renameBpm(JsonValue &root, MigrationContext &context) {
  if (auto wrong = expectVersion(context, 1)) {
    return wrong;
  }
  auto &object = std::get<JsonValue::Object>(root.data);
  for (detail::JsonMember &member : object) {
    if (member.key == "bpm") {
      member.key = "tempo_bpm";
      return std::nullopt;
    }
  }
  return Error{
      .code = ErrorCode::Structure,
      .where = "",
      .message = "version 1 without \"bpm\"",
  };
}

std::optional<Error> tempoAsDecimal(JsonValue &root,
                                    MigrationContext &context) {
  if (auto wrong = expectVersion(context, 2)) {
    return wrong;
  }
  auto &object = std::get<JsonValue::Object>(root.data);
  for (detail::JsonMember &member : object) {
    if (member.key == "tempo_bpm") {
      member.key = "tempo";
      if (const auto *whole = std::get_if<std::uint64_t>(&member.value.data)) {
        member.value.data = static_cast<double>(*whole);
        // Something the user should hear about, made up for the test.
        context.warnings.push_back(
            "the tempo was stored in whole beats per minute by " +
            context.writtenBy + " (format version " +
            std::to_string(context.fileVersion) + ")");
      }
    }
  }
  return std::nullopt;
}

std::optional<Error> addStartMarker(JsonValue &root,
                                    MigrationContext &context) {
  if (auto wrong = expectVersion(context, 3)) {
    return wrong;
  }
  auto &object = std::get<JsonValue::Object>(root.data);
  object.push_back({.key = "start_marker", .value = {std::uint64_t{0}}});
  return std::nullopt;
}

constexpr std::array<Migration, 3> pastSteps{
    renameBpm,
    tempoAsDecimal,
    addStartMarker,
};
constexpr FormatHistory past{.oldest = 1, .migrations = pastSteps};

std::string currentText() {
  const auto image = writeDocument(minimalDocument(), testWriter);
  REQUIRE(image.has_value());
  return image->bytes();
}

// The minimal document as each made-up version would have written it.
std::string asVersion(int version) {
  std::string text = currentText();
  text =
      replaced(text, "\"format\": 1", "\"format\": " + std::to_string(version));
  if (version <= 3) {
    text = replaced(text, "  \"start_marker\": 0,\n", "");
  }
  if (version == 2) {
    text = replaced(text, "\"tempo\": 120.0", "\"tempo_bpm\": 120");
  }
  if (version == 1) {
    text = replaced(text, "\"tempo\": 120.0", "\"bpm\": 120");
  }
  return text;
}

} // namespace

TEST_CASE("format/migration: the real format knows exactly its own version") {
  const FormatHistory history = detail::formatHistory();
  REQUIRE(history.oldest == 1);
  REQUIRE(history.migrations.empty());
  REQUIRE(history.current() == formatVersion);
}

TEST_CASE("format/migration: every old version is brought up step by step") {
  REQUIRE(past.current() == 4);
  const model::Project expected = minimalDocument().project();
  for (int version = 1; version <= 4; ++version) {
    CAPTURE(version);
    const auto loaded = detail::readDocumentWith(asVersion(version), past);
    if (!loaded.has_value()) {
      FAIL(describe(loaded.error()));
    }
    REQUIRE(loaded->formatRead == version);
    REQUIRE(loaded->writtenBy == testWriter);
    REQUIRE(loaded->document.project() == expected);
    // What a step had to say reaches the caller, with what it was told about
    // the file.
    if (version <= 2) {
      REQUIRE(loaded->migrationWarnings ==
              std::vector<std::string>{
                  "the tempo was stored in whole beats per minute by LINA "
                  "tests 1 (format version " +
                  std::to_string(version) + ")"});
    } else {
      REQUIRE(loaded->migrationWarnings.empty());
    }
  }
  // A file from before "written_by" existed: the steps are told so, and the
  // strict reading of the current shape still asks for it.
  const auto anonymous = detail::readDocumentWith(
      replaced(asVersion(1), "  \"written_by\": \"LINA tests 1\",\n", ""),
      past);
  REQUIRE(!anonymous.has_value());
  REQUIRE(anonymous.error().message == "missing key \"written_by\"");
  // Without the steps the old shapes are not readable: the steps did it.
  const std::array<Migration, 0> none{};
  for (int version = 1; version <= 3; ++version) {
    const FormatHistory alone{.oldest = version, .migrations = none};
    REQUIRE(!detail::readDocumentWith(asVersion(version), alone).has_value());
  }
}

TEST_CASE("format/migration: steps run in order, each exactly once") {
  // A version 2 document must not go through the first step: it has no "bpm"
  // and that step would refuse it.
  REQUIRE(detail::readDocumentWith(asVersion(2), past).has_value());
  // A version 1 document that lacks what its version promises is refused by
  // the step that needs it.
  const auto broken = detail::readDocumentWith(
      replaced(asVersion(1), "  \"bpm\": 120,\n", ""), past);
  REQUIRE(!broken.has_value());
  REQUIRE(broken.error().message == "version 1 without \"bpm\"");
  // What a step leaves behind is still read strictly.
  const auto stray = detail::readDocumentWith(
      replaced(asVersion(3), "\"tempo\": 120.0", "\"tempo_bpm\": 120"), past);
  REQUIRE(!stray.has_value());
  REQUIRE(stray.error().code == ErrorCode::Structure);
  REQUIRE(stray.error().where == "/tempo_bpm");
}

TEST_CASE("format/migration: a newer version is refused clearly") {
  const auto newer =
      readDocument(replaced(currentText(), "\"format\": 1", "\"format\": 2"));
  REQUIRE(!newer.has_value());
  REQUIRE(newer.error().code == ErrorCode::Version);
  REQUIRE(newer.error().where == "/format");
  REQUIRE(newer.error().message ==
          "this project was written by a newer LINA (format version 2); this "
          "one reads up to version 1");

  REQUIRE(detail::readDocumentWith(asVersion(5), past).error().code ==
          ErrorCode::Version);
  const auto far = readDocument(replaced(currentText(), "\"format\": 1",
                                         "\"format\": 18446744073709551615"));
  REQUIRE(!far.has_value());
  REQUIRE(far.error().code == ErrorCode::Version);
  // The rest of a newer document is not even looked at.
  const auto unknownShape = readDocument(R"({"format": 7, "anything": []})");
  REQUIRE(!unknownShape.has_value());
  REQUIRE(unknownShape.error().code == ErrorCode::Version);
}

TEST_CASE("format/migration: a version that never existed is refused") {
  const auto zero =
      readDocument(replaced(currentText(), "\"format\": 1", "\"format\": 0"));
  REQUIRE(!zero.has_value());
  REQUIRE(zero.error().code == ErrorCode::Version);
  REQUIRE(zero.error().message ==
          "format version 0 is not one LINA ever wrote");

  const std::array<Migration, 1> one{addStartMarker};
  const FormatHistory later{.oldest = 3, .migrations = one};
  REQUIRE(detail::readDocumentWith(asVersion(2), later).error().code ==
          ErrorCode::Version);
  REQUIRE(detail::readDocumentWith(asVersion(3), later).has_value());
}

TEST_CASE("format/migration: the version must be a plain number") {
  const std::string text = currentText();
  for (const std::string_view spelling :
       {"\"1\"", "1.0", "-1", "null", "[1]", "true"}) {
    CAPTURE(spelling);
    const auto loaded = readDocument(replaced(
        text, "\"format\": 1", "\"format\": " + std::string{spelling}));
    REQUIRE(!loaded.has_value());
    REQUIRE(loaded.error().code == ErrorCode::Structure);
    REQUIRE(loaded.error().where == "/format");
  }
  const auto missing = readDocument(replaced(text, "  \"format\": 1,\n", ""));
  REQUIRE(!missing.has_value());
  REQUIRE(missing.error().code == ErrorCode::Structure);
  REQUIRE(missing.error().message == "missing key \"format\"");
}

TEST_CASE("format/migration: a step that fails or throws is an error") {
  const std::array<Migration, 1> throwing{
      [](JsonValue & /*root*/,
         MigrationContext & /*context*/) -> std::optional<Error> {
        throw std::runtime_error("the step blew up");
      },
  };
  const FormatHistory history{.oldest = 0, .migrations = throwing};
  const auto loaded = detail::readDocumentWith(
      replaced(currentText(), "\"format\": 1", "\"format\": 0"), history);
  REQUIRE(!loaded.has_value());
  REQUIRE(loaded.error().code == ErrorCode::Internal);
  REQUIRE(loaded.error().message.find("the step blew up") != std::string::npos);
}

TEST_CASE("format/migration: a document past the size limit is not parsed") {
  REQUIRE(maxDocumentBytes == 268'435'456U);
  const std::string text = currentText();
  REQUIRE(detail::readDocumentWith(text, detail::formatHistory(), text.size())
              .has_value());
  const auto tooLarge =
      detail::readDocumentWith(text, detail::formatHistory(), text.size() - 1);
  REQUIRE(!tooLarge.has_value());
  REQUIRE(tooLarge.error().code == ErrorCode::TooLarge);
  REQUIRE(tooLarge.error().message.find(std::to_string(text.size())) !=
          std::string::npos);
  // Checked before anything else: what the bytes are does not matter.
  REQUIRE(detail::readDocumentWith("not json", detail::formatHistory(), 3)
              .error()
              .code == ErrorCode::TooLarge);
}

TEST_CASE(
    "format/migration: a step cannot smuggle a bad tree past the reader") {
  // Replaces the document by something that is not an object.
  const std::array<Migration, 1> flatten{
      [](JsonValue &root,
         MigrationContext & /*context*/) -> std::optional<Error> {
        root.data = std::uint64_t{7};
        return std::nullopt;
      },
  };
  const std::string old =
      replaced(currentText(), "\"format\": 1", "\"format\": 0");
  const auto flat = detail::readDocumentWith(
      old, FormatHistory{.oldest = 0, .migrations = flatten});
  REQUIRE(!flat.has_value());
  REQUIRE(flat.error().code == ErrorCode::Structure);

  // Drops the version member.
  const std::array<Migration, 1> forget{
      [](JsonValue &root,
         MigrationContext & /*context*/) -> std::optional<Error> {
        auto &object = std::get<JsonValue::Object>(root.data);
        std::erase_if(object, [](const detail::JsonMember &member) {
          return member.key == "format";
        });
        return std::nullopt;
      },
  };
  const auto forgotten = detail::readDocumentWith(
      old, FormatHistory{.oldest = 0, .migrations = forget});
  REQUIRE(!forgotten.has_value());
  REQUIRE(forgotten.error().message == "missing key \"format\"");
}

} // namespace lina::format::testing
