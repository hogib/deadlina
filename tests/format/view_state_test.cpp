// SPDX-License-Identifier: GPL-3.0-or-later
//
// The view state file (docs/design/16-project-file-format-encoding.md,
// "Project folder").
#include "support/text_edit.hpp"

#include "lina/format/view_state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>

namespace lina::format::testing {

namespace {

constexpr std::string_view defaultText = "{\n"
                                         "  \"format\": 1,\n"
                                         "  \"view\": \"session\",\n"
                                         "  \"arrangement_zoom\": 1.0,\n"
                                         "  \"arrangement_scroll\": 0\n"
                                         "}\n";

Error readError(const std::string &text) {
  const auto state = readViewState(text);
  REQUIRE(!state.has_value());
  return state.error();
}

} // namespace

TEST_CASE("format/view: the text is canonical and reads back") {
  const auto text = writeViewState({});
  REQUIRE(text.has_value());
  REQUIRE(*text == defaultText);
  const auto state = readViewState(*text);
  REQUIRE(state.has_value());
  REQUIRE(*state == ViewState{});

  const ViewState other{
      .view = MainView::Arrangement,
      .arrangementZoom = 0.1 + 0.2,
      .arrangementScroll = {model::maxTicks},
  };
  const auto otherText = writeViewState(other);
  REQUIRE(otherText.has_value());
  REQUIRE(*otherText == "{\n"
                        "  \"format\": 1,\n"
                        "  \"view\": \"arrangement\",\n"
                        "  \"arrangement_zoom\": 0.30000000000000004,\n"
                        "  \"arrangement_scroll\": 9007199254740992\n"
                        "}\n");
  const auto back = readViewState(*otherText);
  REQUIRE(back.has_value());
  REQUIRE(*back == other);
  REQUIRE(*back != ViewState{});
}

TEST_CASE("format/view: values that make no sense are not written") {
  for (const double zoom : {
           0.0,
           -1.0,
           std::numeric_limits<double>::infinity(),
           std::numeric_limits<double>::quiet_NaN(),
       }) {
    const auto text = writeViewState({
        .view = MainView::Session,
        .arrangementZoom = zoom,
        .arrangementScroll = model::TickPosition{0},
    });
    REQUIRE(!text.has_value());
    REQUIRE(text.error().code == ErrorCode::Value);
    REQUIRE(text.error().where == "/arrangement_zoom");
  }
  for (const std::int64_t scroll : {std::int64_t{-1}, model::maxTicks + 1}) {
    const auto text = writeViewState({
        .view = MainView::Session,
        .arrangementZoom = 1.0,
        .arrangementScroll = {scroll},
    });
    REQUIRE(!text.has_value());
    REQUIRE(text.error().where == "/arrangement_scroll");
  }
}

TEST_CASE("format/view: reading is strict") {
  const std::string text{defaultText};
  Error error = readError(replaced(text, "\"session\"", "\"mixer\""));
  REQUIRE(error.code == ErrorCode::Value);
  REQUIRE(error.where == "/view");

  error = readError(
      replaced(text, "  \"view\"", "  \"theme\": \"dark\",\n  \"view\""));
  REQUIRE(error.code == ErrorCode::Structure);
  REQUIRE(error.where == "/theme");

  error = readError(replaced(text, "  \"view\": \"session\",\n", ""));
  REQUIRE(error.code == ErrorCode::Structure);
  REQUIRE(error.message == "missing key \"view\"");

  error = readError(replaced(text, "\"format\": 1", "\"format\": 2"));
  REQUIRE(error.code == ErrorCode::Version);
  REQUIRE(error.where == "/format");
  REQUIRE(readError(replaced(text, "\"format\": 1", "\"format\": 0")).code ==
          ErrorCode::Version);
  REQUIRE(
      readError(replaced(text, "\"format\": 1", "\"format\": \"1\"")).code ==
      ErrorCode::Structure);

  for (const std::string_view zoom : {"0.0", "-2.5", "0"}) {
    error = readError(replaced(text, "\"arrangement_zoom\": 1.0",
                               "\"arrangement_zoom\": " + std::string{zoom}));
    REQUIRE(error.code == ErrorCode::Value);
    REQUIRE(error.where == "/arrangement_zoom");
  }
  REQUIRE(readError(replaced(text, "\"arrangement_zoom\": 1.0",
                             "\"arrangement_zoom\": \"wide\""))
              .code == ErrorCode::Structure);

  error = readError(replaced(text, "\"arrangement_scroll\": 0",
                             "\"arrangement_scroll\": 9007199254740993"));
  REQUIRE(error.code == ErrorCode::Value);
  REQUIRE(error.where == "/arrangement_scroll");
  REQUIRE(readError(replaced(text, "\"arrangement_scroll\": 0",
                             "\"arrangement_scroll\": -1"))
              .code == ErrorCode::Value);
  REQUIRE(readError(replaced(text, "\"arrangement_scroll\": 0",
                             "\"arrangement_scroll\": 0.5"))
              .code == ErrorCode::Structure);

  REQUIRE(readError("").code == ErrorCode::Syntax);
  REQUIRE(readError("[]").code == ErrorCode::Structure);
  REQUIRE(readError(text + text).code == ErrorCode::Syntax);
}

} // namespace lina::format::testing
