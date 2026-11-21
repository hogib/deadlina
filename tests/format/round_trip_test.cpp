// SPDX-License-Identifier: GPL-3.0-or-later
//
// Round trips (docs/design/16-project-file-format-encoding.md, "Testing"):
// write, read, write gives the same document and the same bytes, for built
// and for generated documents; and what cannot be written is refused with the
// object named.
#include "support/documents.hpp"

#include "internal/codec.hpp"
#include "internal/text_rules.hpp"
#include "lina/format/document_io.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace lina::format::testing {

namespace {

// Writes, reads and writes again; checks the document and the bytes survive.
// Returns the bytes.
std::string checkRoundTrip(const model::Document &document) {
  const auto image = writeDocument(document, testWriter);
  REQUIRE(image.has_value());
  const auto loaded = readDocument(image->bytes());
  if (!loaded.has_value()) {
    FAIL(describe(loaded.error()));
  }
  REQUIRE(loaded->formatRead == formatVersion);
  REQUIRE(loaded->writtenBy == testWriter);
  REQUIRE(loaded->migrationWarnings.empty());
  REQUIRE(loaded->document.project() == document.project());
  const auto again = writeDocument(loaded->document, testWriter);
  REQUIRE(again.has_value());
  REQUIRE(again->bytes() == image->bytes());
  REQUIRE(again->stateRefs() == image->stateRefs());
  REQUIRE(again->mediaPaths() == image->mediaPaths());
  return image->bytes();
}

model::Document fromProject(model::Project project) {
  auto document = model::Document::fromProject(std::move(project));
  REQUIRE(document.has_value());
  return std::move(*document);
}

} // namespace

TEST_CASE("format/round-trip: an empty document") {
  const std::string bytes = checkRoundTrip(model::Document{});
  REQUIRE(bytes.starts_with("{\n  \"format\": 1,\n  \"written_by\": \"LINA "
                            "tests 1\",\n  \"next_id\": 2,\n"));
}

TEST_CASE("format/round-trip: the built documents") {
  checkRoundTrip(minimalDocument());
  checkRoundTrip(everythingDocument());
}

TEST_CASE("format/round-trip: the same document always gives the same bytes") {
  const auto first = writeDocument(everythingDocument(), testWriter);
  const auto second = writeDocument(everythingDocument(), testWriter);
  REQUIRE(first.has_value());
  REQUIRE(second.has_value());
  REQUIRE(first->bytes() == second->bytes());

  // Undo history, which is not part of the saved form, makes no difference.
  model::Document edited = everythingDocument();
  mustApply(edited, model::AddScene{.name = "temporary", .index = 0});
  REQUIRE(edited.undo().has_value());
  model::Project sameButCounter = edited.project();
  REQUIRE(sameContent(sameButCounter, everythingDocument().project()));
  // Only the counter moved, and only the counter's line differs.
  sameButCounter.nextId = everythingDocument().project().nextId;
  const auto third = writeDocument(fromProject(sameButCounter), testWriter);
  REQUIRE(third.has_value());
  REQUIRE(third->bytes() == first->bytes());
}

TEST_CASE("format/round-trip: the text is canonical in form") {
  for (const model::Document &document : {
           model::Document{},
           minimalDocument(),
           everythingDocument(),
           generatedDocument(7, 600),
       }) {
    const std::string bytes = checkRoundTrip(document);
    REQUIRE(detail::isValidUtf8(bytes));
    REQUIRE(bytes.ends_with("}\n"));
    REQUIRE(!bytes.ends_with("\n\n"));
    REQUIRE(bytes.find('\r') == std::string::npos);
    REQUIRE(bytes.find('\t') == std::string::npos);
    REQUIRE(bytes.find(" \n") == std::string::npos);
    REQUIRE(bytes.find("\n\n") == std::string::npos);
    // Indentation is a whole number of two-space steps.
    std::size_t lineStart = 0;
    while (lineStart < bytes.size()) {
      const std::size_t indent =
          bytes.find_first_not_of(' ', lineStart) - lineStart;
      REQUIRE(indent % 2 == 0);
      lineStart = bytes.find('\n', lineStart) + 1;
    }
  }
}

TEST_CASE("format/round-trip: generated documents") {
  std::size_t totalBytes = 0;
  std::size_t withClips = 0;
  for (std::uint64_t seed = 1; seed <= 60; ++seed) {
    CAPTURE(seed);
    const model::Document document = generatedDocument(seed, 400);
    totalBytes += checkRoundTrip(document).size();
    for (const model::Track &track : document.project().tracks) {
      withClips += track.arrangement.empty() ? 0 : 1;
    }
  }
  // The generator must have built something worth writing.
  REQUIRE(totalBytes > 60 * 2'000);
  REQUIRE(withClips > 20);
}

TEST_CASE("format/round-trip: a long generated session") {
  const model::Document document = generatedDocument(20261121, 6'000);
  const std::string bytes = checkRoundTrip(document);
  REQUIRE(bytes.size() > 20'000);
}

TEST_CASE("format/round-trip: identities and positions at their limits") {
  // The counter at its ceiling, 2^62, and the two identities just below it.
  constexpr std::uint64_t lastId = model::maxNextId - 1;
  constexpr std::uint64_t maxSize = std::numeric_limits<std::uint64_t>::max();
  model::Project project = model::makeEmptyProject();
  project.nextId = model::maxNextId;
  project.master.id = {lastId};
  project.scenes.push_back({.id = {lastId - 1}, .name = "last"});
  // 2^53 + 1 as an identity: a double could not hold it.
  project.media.push_back({
      .id = {9'007'199'254'740'993},
      .path = "media/a.wav",
      .sizeBytes = maxSize,
      .contentHash = std::string(64, 'f'),
      .sampleRate = std::numeric_limits<std::uint32_t>::max(),
  });
  project.startMarker = {model::maxTicks};
  project.loop = {
      .enabled = true,
      .start = {model::maxTicks - 1},
      .length = {1},
  };
  const std::string bytes = checkRoundTrip(fromProject(project));
  REQUIRE(bytes.find("\"next_id\": 4611686018427387904") != std::string::npos);
  REQUIRE(bytes.find("\"id\": 4611686018427387903") != std::string::npos);
  REQUIRE(bytes.find("\"id\": 4611686018427387902") != std::string::npos);
  REQUIRE(bytes.find("\"id\": 9007199254740993") != std::string::npos);
  REQUIRE(bytes.find("\"size\": 18446744073709551615") != std::string::npos);
  REQUIRE(bytes.find("\"start_marker\": 9007199254740992") !=
          std::string::npos);
  REQUIRE(bytes.find("\"sample_rate\": 4294967295") != std::string::npos);

  // A counter past the ceiling is refused when read, so that no document in
  // use can ever run its counter round to identities already handed out.
  for (const std::string_view counter :
       {"4611686018427387905", "9223372036854775808", "18446744073709551615"}) {
    std::string text = bytes;
    const std::string line = "\"next_id\": 4611686018427387904";
    text.replace(text.find(line), line.size(),
                 "\"next_id\": " + std::string{counter});
    const auto loaded = readDocument(text);
    REQUIRE(!loaded.has_value());
    REQUIRE(loaded.error().code == ErrorCode::Rule);
    REQUIRE(loaded.error().message.find("ids-exhausted at project 0") !=
            std::string::npos);
  }
  // At the ceiling the document is still editable in every way that needs no
  // new identity, and what is saved loads.
  model::Document full = fromProject(project);
  REQUIRE(full.perform("add", model::AddScene{.name = "more", .index = 0})
              .error()
              .fault == model::Fault::IdsExhausted);
  mustApply(full, model::RenameScene{.scene = {lastId - 1}, .name = "renamed"});
  checkRoundTrip(full);
}

TEST_CASE("format/round-trip: sample positions use all of 64 bits") {
  model::Document document = everythingDocument();
  const model::ClipId clip =
      document.project().tracks.front().arrangement.front().id;
  constexpr std::int64_t last = std::numeric_limits<std::int64_t>::max();
  mustApply(document, model::SetClipProperty{
                          .clip = clip,
                          .property =
                              model::RegionValue{
                                  {.start = last - 1, .end = last},
                              },
                      });
  mustApply(document,
            model::SetClipProperty{
                .clip = clip,
                .property =
                    model::WarpValue{
                        {
                            .markers =
                                {
                                    {.sample = last - 1, .tick = {0}},
                                    {.sample = last, .tick = {model::maxTicks}},
                                },
                            .origin = model::SourceTempoOrigin::File,
                        },
                    },
            });
  const std::string bytes = checkRoundTrip(document);
  REQUIRE(bytes.find("{ \"start\": 9223372036854775806, \"end\": "
                     "9223372036854775807 }") != std::string::npos);
  REQUIRE(bytes.find("{ \"sample\": 9223372036854775807, \"tick\": "
                     "9007199254740992 }") != std::string::npos);
}

TEST_CASE("format/round-trip: floating-point values come back bit for bit") {
  const std::vector<double> levels{
      0.0,
      std::numeric_limits<double>::denorm_min(),
      std::numeric_limits<double>::min(),
      std::numeric_limits<double>::epsilon(),
      0.1,
      0.1 + 0.2,
      1.0 / 3.0,
      2.0 / 3.0,
      0.999'999'999'999'999'9,
      1.0,
  };
  model::Document document = everythingDocument();
  const model::DeviceId synth =
      document.project().tracks.at(1).devices.front().id;
  std::size_t index = 0;
  for (const double level : levels) {
    mustApply(document,
              model::SetDeviceProperty{
                  .device = synth,
                  .property =
                      model::ParameterSettingValue{
                          {.id = "p" + std::to_string(index), .value = level},
                      },
              });
    ++index;
  }
  const auto image = writeDocument(document, testWriter);
  REQUIRE(image.has_value());
  const auto loaded = readDocument(image->bytes());
  REQUIRE(loaded.has_value());
  std::vector<double> read;
  for (const model::ParameterValue &parameter :
       loaded->document.project().tracks.at(1).devices.front().parameters) {
    if (parameter.id.starts_with('p')) {
      read.push_back(parameter.value);
    }
  }
  REQUIRE(read == levels);
  checkRoundTrip(document);
}

TEST_CASE("format/round-trip: the image lists the state files it needs") {
  model::Document document = everythingDocument();
  const auto before = writeDocument(document, testWriter);
  REQUIRE(before.has_value());
  REQUIRE(before->stateRefs().size() == 1);
  REQUIRE(before->stateRefs().front().ends_with(".bin"));
  // The media inside the folder, not the file referenced outside it.
  REQUIRE(before->mediaPaths() ==
          std::vector<std::string>{"media/kick-3f9a1c2e.wav"});
  REQUIRE(writeDocument(model::Document{}, testWriter)->stateRefs().empty());

  // Two more devices, one sharing a state file with another: sorted, once.
  const model::DeviceId onMaster = document.project().master.devices.front().id;
  const model::DeviceId onReturn =
      document.project().returns.front().devices.front().id;
  const model::DeviceId onDrums =
      document.project().tracks.front().devices.front().id;
  const auto setState = [&document](model::DeviceId device, std::string name) {
    mustApply(document, model::SetDeviceProperty{
                            .device = device,
                            .property = model::StateValue{std::move(name)},
                        });
  };
  const std::string early = std::string(64, '0') + ".bin";
  const std::string late = std::string(64, 'f') + ".bin";
  setState(onMaster, late);
  setState(onReturn, early);
  setState(onDrums, late);
  const auto after = writeDocument(document, testWriter);
  REQUIRE(after.has_value());
  REQUIRE(after->stateRefs() ==
          std::vector<std::string>{early, before->stateRefs().front(), late});
  checkRoundTrip(document);
}

TEST_CASE("format/round-trip: what the reader would refuse is not written") {
  // The model lets a media path be any text; the format has two shapes.
  for (const std::string_view path : {
           "../outside.wav",
           "media/../../a.wav",
           "media//a.wav",
           "./a.wav",
           "media/",
       }) {
    CAPTURE(path);
    model::Document document = everythingDocument();
    const std::uint64_t id = document.project().nextId;
    mustApply(document, model::AddMedia{
                            {
                                .id = model::MediaId{0},
                                .path = std::string{path},
                                .sizeBytes = 1,
                                .contentHash = std::string(64, 'a'),
                                .sampleRate = 48'000,
                            },
                        });
    const auto image = writeDocument(document, testWriter);
    REQUIRE(!image.has_value());
    REQUIRE(image.error().code == ErrorCode::Value);
    REQUIRE(image.error().where == "media reference " + std::to_string(id));
    REQUIRE(image.error().message.find("a media path is either") !=
            std::string::npos);
    // And the reader does refuse exactly that.
    const std::string text =
        detail::encodeProject(document.project(), testWriter);
    const auto loaded = readDocument(text);
    REQUIRE(!loaded.has_value());
    REQUIRE(loaded.error().where == "/media/2/path");
  }

  // The name of the writing application is text like any other.
  const model::Document document = minimalDocument();
  for (const std::string_view writer :
       {std::string_view{"LINA \xFF"}, std::string_view{"LINA\0", 5}}) {
    const auto image = writeDocument(document, writer);
    REQUIRE(!image.has_value());
    REQUIRE(image.error().code == ErrorCode::Value);
    REQUIRE(image.error().where == "/written_by");
  }
  for (const std::string_view writer :
       {"", "LINA 0.1.0", "LINA \"nightly\" \xE2\x9C\x93\n"}) {
    const auto image = writeDocument(document, writer);
    REQUIRE(image.has_value());
    const auto loaded = readDocument(image->bytes());
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->writtenBy == writer);
    REQUIRE(loaded->document.project() == document.project());
  }
}

TEST_CASE("format/round-trip: only a project the model accepts is written") {
  // A document always holds a valid project, so this takes a project that is
  // not in a document: the check the writer runs before it writes.
  REQUIRE(!detail::findUnwritable(everythingDocument().project(), testWriter)
               .has_value());

  model::Project wrapped = everythingDocument().project();
  wrapped.nextId = 1;
  const Error notRefused{
      .code = ErrorCode::Io,
      .where = "",
      .message = "not refused",
  };
  const Error wrappedError =
      detail::findUnwritable(wrapped, testWriter).value_or(notRefused);
  REQUIRE(wrappedError.code == ErrorCode::Rule);
  REQUIRE(wrappedError.message.find("id-invalid") != std::string::npos);

  model::Project badText = everythingDocument().project();
  badText.scenes.front().name = std::string{"a\0b", 3};
  const Error textError =
      detail::findUnwritable(badText, testWriter).value_or(notRefused);
  REQUIRE(textError.code == ErrorCode::Rule);
  REQUIRE(textError.message.find("text-invalid at scene") != std::string::npos);

  model::Project badState = everythingDocument().project();
  badState.master.devices.front().stateRef = "../state.bin";
  const Error stateError =
      detail::findUnwritable(badState, testWriter).value_or(notRefused);
  REQUIRE(stateError.code == ErrorCode::Rule);
  REQUIRE(stateError.message.find("value-out-of-range at device") !=
          std::string::npos);
}

TEST_CASE(
    "format/round-trip: the writing application does not affect loading") {
  const model::Document document = everythingDocument();
  const auto ours = writeDocument(document, testWriter);
  const auto theirs = writeDocument(document, "Another build 9.9");
  REQUIRE(ours.has_value());
  REQUIRE(theirs.has_value());
  REQUIRE(ours->bytes() != theirs->bytes());
  const auto loaded = readDocument(theirs->bytes());
  REQUIRE(loaded.has_value());
  REQUIRE(loaded->writtenBy == "Another build 9.9");
  REQUIRE(loaded->document.project() == document.project());
  // Saved again by this application, it says so, and nothing else differs.
  const auto resaved = writeDocument(loaded->document, testWriter);
  REQUIRE(resaved.has_value());
  REQUIRE(resaved->bytes() == ours->bytes());
}

} // namespace lina::format::testing
