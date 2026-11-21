// SPDX-License-Identifier: GPL-3.0-or-later
//
// Strict reading (docs/design/16-project-file-format-encoding.md, "Reading is
// strict"): one thing wrong in an otherwise good document is an error that
// names the place, and nothing is loaded.
#include "support/documents.hpp"
#include "support/text_edit.hpp"

#include "lina/format/document_io.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace lina::format::testing {

namespace {

std::string minimalText() {
  const auto image = writeDocument(minimalDocument(), testWriter);
  REQUIRE(image.has_value());
  return image->bytes();
}

std::string everythingText() {
  const auto image = writeDocument(everythingDocument(), testWriter);
  REQUIRE(image.has_value());
  return image->bytes();
}

// One edit of a document's text and the error it must give.
struct Damage {
  std::string_view from;
  std::string_view to;
  ErrorCode code;
  std::string_view where;
  // Part of the message.
  std::string_view says;
};

void checkDamage(const std::string &text, const std::vector<Damage> &cases) {
  REQUIRE(readDocument(text).has_value());
  for (const Damage &damage : cases) {
    CAPTURE(damage.from, damage.to);
    const auto loaded = readDocument(replaced(text, damage.from, damage.to));
    REQUIRE(!loaded.has_value());
    CAPTURE(describe(loaded.error()));
    REQUIRE(loaded.error().code == damage.code);
    REQUIRE(loaded.error().where == damage.where);
    REQUIRE(loaded.error().message.find(damage.says) != std::string::npos);
  }
}

constexpr ErrorCode structure = ErrorCode::Structure;
constexpr ErrorCode value = ErrorCode::Value;
constexpr ErrorCode rule = ErrorCode::Rule;

} // namespace

TEST_CASE("format/strict: unknown and missing keys are errors") {
  checkDamage(
      minimalText(),
      {
          {
              .from = "  \"tempo\"",
              .to = "  \"swing\": 0.5,\n  \"tempo\"",
              .code = structure,
              .where = "/swing",
              .says = "unknown key \"swing\"",
          },
          {
              .from = "  \"start_marker\": 0,\n",
              .to = "",
              .code = structure,
              .where = "",
              .says = "missing key \"start_marker\"",
          },
          {
              .from = "\"start_marker\"",
              .to = "\"start\"",
              .code = structure,
              .where = "/start",
              .says = "unknown key",
          },
          {
              .from = R"({ "id": 2, "name": "Intro" })",
              .to = "{ \"id\": 2 }",
              .code = structure,
              .where = "/scenes/0",
              .says = "missing key \"name\"",
          },
          {
              .from = R"("name": "Intro" })",
              .to = R"("name": "Intro", "colour": 1 })",
              .code = structure,
              .where = "/scenes/0/colour",
              .says = "unknown key \"colour\"",
          },
          {
              .from = "\"solo\": false,\n",
              .to = "",
              .code = structure,
              .where = "/tracks/0/mixer",
              .says = "missing key \"solo\"",
          },
          {
              .from = "\"pitch\": 60, ",
              .to = "",
              .code = structure,
              .where = "/tracks/0/slots/0/clip/content/notes/0",
              .says = "missing key \"pitch\"",
          },
          {
              .from = "\"velocity\": 90 }",
              .to = R"("velocity": 90, "channel": 1 })",
              .code = structure,
              .where = "/tracks/0/slots/0/clip/content/notes/1/channel",
              .says = "unknown key",
          },
          {
              .from = R"({ "enabled": false, "start": 0, "length": 241920 })",
              .to = R"({ "enabled": false, "start": 0 })",
              .code = structure,
              .where = "/loop",
              .says = "missing key \"length\"",
          },
          {
              .from = R"("type": "midi",)",
              .to = R"("sort": "midi",)",
              .code = structure,
              .where = "/tracks/0/slots/0/clip/content/sort",
              .says = "unknown key \"sort\"",
          },
          {
              .from = R"({ "type": "all_midi" })",
              .to = R"({ "type": "all_midi", "x": 1 })",
              .code = structure,
              .where = "/tracks/0/mixer/input/x",
              .says = "unknown key",
          },
          {
              .from = R"({ "type": "all_midi" })",
              .to = "{}",
              .code = structure,
              .where = "/tracks/0/mixer/input",
              .says = "missing key \"type\"",
          },
      });
}

TEST_CASE("format/strict: a value of the wrong type is an error") {
  checkDamage(
      minimalText(),
      {
          {
              .from = R"("name": "Keys")",
              .to = "\"name\": 5",
              .code = structure,
              .where = "/tracks/0/name",
              .says = "expected a string, found integer",
          },
          {
              .from = "\"next_id\": 8",
              .to = R"("next_id": "8")",
              .code = structure,
              .where = "/next_id",
              .says = "expected an integer, found string",
          },
          {
              .from = "\"next_id\": 8",
              .to = "\"next_id\": 8.0",
              .code = structure,
              .where = "/next_id",
              .says = "expected an integer, found number",
          },
          {
              .from = "\"tempo\": 120.0",
              .to = R"("tempo": "fast")",
              .code = structure,
              .where = "/tempo",
              .says = "expected a number, found string",
          },
          {
              .from = "\"tempo\": 120.0",
              .to = "\"tempo\": null",
              .code = structure,
              .where = "/tempo",
              .says = "expected a number, found null",
          },
          {
              .from = "[4, 4]",
              .to = "\"4/4\"",
              .code = structure,
              .where = "/time_signature",
              .says = "expected an array, found string",
          },
          {
              .from = "[4, 4]",
              .to = "[4]",
              .code = structure,
              .where = "/time_signature",
              .says = "expected two",
          },
          {
              .from = "[4, 4]",
              .to = "[4, 4, 4]",
              .code = structure,
              .where = "/time_signature",
              .says = "expected two",
          },
          {
              .from = "[4, 4]",
              .to = "[4.0, 4]",
              .code = structure,
              .where = "/time_signature/0",
              .says = "expected an integer",
          },
          {
              .from = "[4, 4]",
              .to = "[4, true]",
              .code = structure,
              .where = "/time_signature/1",
              .says = "expected an integer, found boolean",
          },
          {
              .from = "\"enabled\": false",
              .to = "\"enabled\": 0",
              .code = structure,
              .where = "/loop/enabled",
              .says = "expected true or false, found integer",
          },
          {
              .from = "\"media\": []",
              .to = "\"media\": {}",
              .code = structure,
              .where = "/media",
              .says = "expected an array, found object",
          },
          {
              .from = "\"scenes\": [",
              .to = "\"scenes\": [\n    7,",
              .code = structure,
              .where = "/scenes/0",
              .says = "expected an object, found integer",
          },
          {
              .from = R"("launch_quantisation": "1_bar")",
              .to = "\"launch_quantisation\": 4",
              .code = structure,
              .where = "/launch_quantisation",
              .says = "expected a string",
          },
          {
              .from = "\"mute\": false",
              .to = R"("mute": "false")",
              .code = structure,
              .where = "/tracks/0/mixer/mute",
              .says = "expected true or false, found string",
          },
          {
              .from = "\"volume_db\": 0.0",
              .to = "\"volume_db\": []",
              .code = structure,
              .where = "/tracks/0/mixer/volume_db",
              .says = "expected a number, found array",
          },
          {
              .from = "\"velocity\": 90",
              .to = "\"velocity\": 90.5",
              .code = structure,
              .where = "/tracks/0/slots/0/clip/content/notes/1/velocity",
              .says = "expected an integer, found number",
          },
          {
              .from = R"({ "type": "all_midi" })",
              .to = "\"all_midi\"",
              .code = structure,
              .where = "/tracks/0/mixer/input",
              .says = "expected an object, found string",
          },
          {
              .from = R"({ "type": "all_midi" })",
              .to = "{ \"type\": 3 }",
              .code = structure,
              .where = "/tracks/0/mixer/input/type",
              .says = "expected a string",
          },
          {
              .from = "\"master\": {",
              .to = R"("master": [], "m": {)",
              .code = structure,
              .where = "/m",
              .says = "unknown key",
          },
      });
  // The whole document must be an object with a version.
  for (const std::string_view text : {"[]", "7", "\"project\"", "null"}) {
    const auto loaded = readDocument(text);
    REQUIRE(!loaded.has_value());
    REQUIRE(loaded.error().code == structure);
    REQUIRE(loaded.error().where.empty());
    REQUIRE(loaded.error().message.find("expected an object") !=
            std::string::npos);
  }
}

TEST_CASE("format/strict: numbers outside the model's types are errors") {
  checkDamage(
      minimalText(),
      {
          {
              .from = "\"pitch\": 60",
              .to = "\"pitch\": 256",
              .code = value,
              .where = "/tracks/0/slots/0/clip/content/notes/0/pitch",
              .says = "256 is above",
          },
          {
              .from = "\"pitch\": 60",
              .to = "\"pitch\": -1",
              .code = value,
              .where = "/tracks/0/slots/0/clip/content/notes/0/pitch",
              .says = "must not be negative",
          },
          {
              .from = "\"velocity\": 90",
              .to = "\"velocity\": 1000",
              .code = value,
              .where = "/tracks/0/slots/0/clip/content/notes/1/velocity",
              .says = "above",
          },
          {
              .from = "\"start_marker\": 0",
              .to = "\"start_marker\": 9223372036854775808",
              .code = value,
              .where = "/start_marker",
              .says = "above the largest value allowed",
          },
          {
              .from = "\"next_id\": 8",
              .to = "\"next_id\": -8",
              .code = value,
              .where = "/next_id",
              .says = "must not be negative",
          },
          {
              .from = "[4, 4]",
              .to = "[4, 2147483648]",
              .code = value,
              .where = "/time_signature/1",
              .says = "above",
          },
          {
              .from = "[4, 4]",
              .to = "[-2147483649, 4]",
              .code = value,
              .where = "/time_signature/0",
              .says = "below the smallest value allowed",
          },
          {
              .from = "[4, 4]",
              .to = "[4, 3]",
              .code = value,
              .where = "/time_signature",
              .says = "1 to 99",
          },
          {
              .from = "[4, 4]",
              .to = "[0, 4]",
              .code = value,
              .where = "/time_signature",
              .says = "1 to 99",
          },
          {
              .from = "[4, 4]",
              .to = "[100, 4]",
              .code = value,
              .where = "/time_signature",
              .says = "1 to 99",
          },
          {
              .from = "\"tempo\": 120.0",
              .to = "\"tempo\": 19.99",
              .code = value,
              .where = "/tempo",
              .says = "20 to 300",
          },
          {
              .from = "\"tempo\": 120.0",
              .to = "\"tempo\": 300.01",
              .code = value,
              .where = "/tempo",
              .says = "20 to 300",
          },
          {
              .from = "\"tempo\": 120.0",
              .to = "\"tempo\": 120.005",
              .code = value,
              .where = "/tempo",
              .says = "steps of 0.01",
          },
          {
              .from = "\"tempo\": 120.0",
              .to = "\"tempo\": 1e300",
              .code = value,
              .where = "/tempo",
              .says = "20 to 300",
          },
          {
              .from = "\"tempo\": 120.0",
              .to = "\"tempo\": -120.0",
              .code = value,
              .where = "/tempo",
              .says = "20 to 300",
          },
      });
}

TEST_CASE("format/strict: names that are not known are errors") {
  checkDamage(minimalText(),
              {
                  {
                      .from = R"("launch_quantisation": "1_bar")",
                      .to = R"("launch_quantisation": "bar")",
                      .code = value,
                      .where = "/launch_quantisation",
                      .says = "\"bar\" is not a known name",
                  },
                  {
                      .from = "\"launch_quantisation\": null",
                      .to = R"("launch_quantisation": "1/3")",
                      .code = value,
                      .where = "/tracks/0/slots/0/clip/launch_quantisation",
                      .says = "\"1/3\"",
                  },
                  {
                      .from = R"("kind": "midi")",
                      .to = R"("kind": "MIDI")",
                      .code = value,
                      .where = "/tracks/0/kind",
                      .says = "not a known name",
                  },
                  {
                      .from = R"("monitoring": "auto")",
                      .to = R"("monitoring": "")",
                      .code = value,
                      .where = "/tracks/0/mixer/monitoring",
                      .says = "not a known name",
                  },
                  {
                      .from = R"("type": "midi")",
                      .to = R"("type": "video")",
                      .code = value,
                      .where = "/tracks/0/slots/0/clip/content/type",
                      .says = R"("midi" or "audio")",
                  },
                  {
                      .from = R"("type": "all_midi")",
                      .to = R"("type": "usb")",
                      .code = value,
                      .where = "/tracks/0/mixer/input/type",
                      .says = "\"midi_device\"",
                  },
                  {
                      .from = R"("colour": "#808080")",
                      .to = R"("colour": "#80808")",
                      .code = value,
                      .where = "/tracks/0/colour",
                      .says = "six lower-case hexadecimal digits",
                  },
                  {
                      .from = R"("colour": "#808080")",
                      .to = R"("colour": "8080800")",
                      .code = value,
                      .where = "/tracks/0/colour",
                      .says = "six lower-case hexadecimal digits",
                  },
                  {
                      .from = R"("colour": "#808080")",
                      .to = R"("colour": "#80808g")",
                      .code = value,
                      .where = "/tracks/0/colour",
                      .says = "six lower-case hexadecimal digits",
                  },
                  {
                      .from = R"("colour": "#808080")",
                      .to = R"("colour": "#8080800")",
                      .code = value,
                      .where = "/tracks/0/colour",
                      .says = "six lower-case hexadecimal digits",
                  },
              });
  checkDamage(
      everythingText(),
      {
          {
              .from = R"("role": "instrument")",
              .to = R"("role": "midi_effect")",
              .code = value,
              .where = "/tracks/1/devices/0/role",
              .says = "not a known name",
          },
          {
              .from = R"("kind": "pitch_bend")",
              .to = R"("kind": "aftertouch")",
              .code = value,
              .where = "/tracks/1/slots/1/clip/content/controllers/0/kind",
              .says = "not a known name",
          },
          {
              .from = R"("origin": "detected")",
              .to = R"("origin": "dreamt")",
              .code = value,
              .where = "/tracks/0/slots/0/clip/content/warp/origin",
              .says = "not a known name",
          },
          {
              .from = R"("stretch_mode": "repitch")",
              .to = R"("stretch_mode": "granular")",
              .code = value,
              .where = "/tracks/0/slots/0/clip/content/stretch_mode",
              .says = "not a known name",
          },
          {
              .from = R"({ "type": "mixer", "parameter": "volume" })",
              .to = R"({ "type": "mixer", "parameter": "width" })",
              .code = value,
              .where = "/tracks/0/automation/0/target/parameter",
              .says = "not a known name",
          },
          {
              .from = R"({ "type": "mixer", "parameter": "volume" })",
              .to = R"({ "type": "macro", "parameter": "volume" })",
              .code = value,
              .where = "/tracks/0/automation/0/target/type",
              .says = "\"device_parameter\"",
          },
          {
              .from = R"("type": "send", "target")",
              .to = R"("type": "send", "track")",
              .code = structure,
              .where = "/tracks/0/automation/2/target/track",
              .says = "unknown key",
          },
          {
              .from = R"("type": "device_parameter", "device": )",
              .to = R"("type": "device_parameter", "device": "synth", "d": )",
              .code = structure,
              .where = "/tracks/1/automation/0/target/d",
              .says = "unknown key",
          },
          {
              .from = R"({ "type": "audio", "channel": 2, "stereo": true })",
              .to =
                  R"({ "type": "audio", "channel": 4294967296, "stereo": true })",
              .code = value,
              .where = "/tracks/0/mixer/input/channel",
              .says = "above",
          },
          {
              .from = R"({ "type": "midi_device", "device": "Keystation 61" })",
              .to = R"({ "type": "midi_device", "device": 61 })",
              .code = structure,
              .where = "/tracks/1/mixer/input/device",
              .says = "expected a string",
          },
          {
              .from = R"({ "type": "audio", "channel": 2, "stereo": true })",
              .to = R"({ "type": "audio", "channel": 2 })",
              .code = structure,
              .where = "/tracks/0/mixer/input",
              .says = "missing key \"stereo\"",
          },
          {
              .from = R"({ "type": "midi_device", "device": "Keystation 61" })",
              .to = R"({ "type": "midi_device" })",
              .code = structure,
              .where = "/tracks/1/mixer/input",
              .says = "missing key \"device\"",
          },
          {
              .from = R"({ "id": "cutoff", "value": 0.1 })",
              .to = R"({ "id": "cutoff" })",
              .code = structure,
              .where = "/tracks/1/devices/0/parameters/0",
              .says = "missing key \"value\"",
          },
          {
              .from = R"({ "type": "mixer", "parameter": "volume" })",
              .to = R"("volume")",
              .code = structure,
              .where = "/tracks/0/automation/0/target",
              .says = "expected an object, found string",
          },
          {
              .from = R"("path": "media/kick-3f9a1c2e.wav")",
              .to = R"("path": 5)",
              .code = structure,
              .where = "/media/0/path",
              .says = "expected a string, found integer",
          },
      });
}

TEST_CASE("format/strict: identities must be handed out and used once") {
  checkDamage(minimalText(),
              {
                  {
                      .from = R"({ "id": 2, "name": "Intro" })",
                      .to = R"({ "id": 0, "name": "Intro" })",
                      .code = value,
                      .where = "/scenes/0/id",
                      .says = "the identity 0 is not one",
                  },
                  {
                      .from = R"({ "id": 2, "name": "Intro" })",
                      .to = R"({ "id": 8, "name": "Intro" })",
                      .code = value,
                      .where = "/scenes/0/id",
                      .says = "\"next_id\" 8",
                  },
                  {
                      .from = R"({ "id": 2, "name": "Intro" })",
                      .to = R"({ "id": 1, "name": "Intro" })",
                      .code = value,
                      .where = "/master/id",
                      .says = "the identity 1 is used twice",
                  },
                  {
                      .from = R"({ "id": 7, "start")",
                      .to = R"({ "id": 6, "start")",
                      .code = value,
                      .where = "/tracks/0/slots/0/clip/content/notes/1/id",
                      .says = "the identity 6 is used twice",
                  },
                  {
                      .from = R"({ "id": 2, "name": "Intro" })",
                      .to = R"({ "id": -2, "name": "Intro" })",
                      .code = value,
                      .where = "/scenes/0/id",
                      .says = "must not be negative",
                  },
                  {
                      .from = "\"next_id\": 8",
                      .to = "\"next_id\": 0",
                      .code = value,
                      .where = "/scenes/0/id",
                      .says = "not one that \"next_id\" 0 has handed out",
                  },
              });
}

TEST_CASE("format/strict: paths and state references must stay inside") {
  checkDamage(everythingText(),
              {
                  {
                      .from = R"("path": "media/kick-3f9a1c2e.wav")",
                      .to = R"("path": "../kick.wav")",
                      .code = value,
                      .where = "/media/0/path",
                      .says = "a media path is either",
                  },
                  {
                      .from = R"("path": "media/kick-3f9a1c2e.wav")",
                      .to = R"("path": "media/../../kick.wav")",
                      .code = value,
                      .where = "/media/0/path",
                      .says = "a media path is either",
                  },
                  {
                      .from = R"("path": "media/kick-3f9a1c2e.wav")",
                      .to = R"("path": "")",
                      .code = value,
                      .where = "/media/0/path",
                      .says = "a media path is either",
                  },
                  {
                      .from = R"("path": "media/kick-3f9a1c2e.wav")",
                      .to = R"("path": "a\u0000b")",
                      .code = value,
                      .where = "/media/0/path",
                      .says = "a media path is either",
                  },
                  {
                      .from = R"("state": ")",
                      .to = R"("state": "../)",
                      .code = value,
                      .where = "/tracks/1/devices/0/state",
                      .says = "the name of a state file",
                  },
                  {
                      .from = "\"state\": null",
                      .to = R"("state": "")",
                      .code = value,
                      .where = "/tracks/0/devices/0/state",
                      .says = "the name of a state file",
                  },
                  {
                      .from = "\"state\": null",
                      .to = R"("state": "..")",
                      .code = value,
                      .where = "/tracks/0/devices/0/state",
                      .says = "the name of a state file",
                  },
                  {
                      .from = "\"state\": null",
                      .to = "\"state\": false",
                      .code = structure,
                      .where = "/tracks/0/devices/0/state",
                      .says = "expected a string, found boolean",
                  },
                  {
                      .from = "\"sample_rate\": 44100",
                      .to = "\"sample_rate\": 4294967296",
                      .code = value,
                      .where = "/media/0/sample_rate",
                      .says = "above",
                  },
              });
}

TEST_CASE("format/strict: the model's rules have the last word") {
  checkDamage(
      minimalText(),
      {
          // Notes out of order.
          {
              .from = R"({ "id": 6, "start": 0,)",
              .to = R"({ "id": 6, "start": 30240,)",
              .code = rule,
              .where = "",
              .says = "order-violation at note 7",
          },
          // A velocity of zero fits the type and breaks the model's range.
          {
              .from = "\"velocity\": 90",
              .to = "\"velocity\": 0",
              .code = rule,
              .where = "",
              .says = "value-out-of-range at note 7",
          },
          // A slot for a scene that is not there.
          {
              .from = "\"scene\": 2",
              .to = "\"scene\": 3",
              .code = rule,
              .where = "",
              .says = "slots-mismatch at track 3",
          },
          {
              .from = R"("kind": "master")",
              .to = R"("kind": "return")",
              .code = rule,
              .where = "",
              .says = "wrong-track-kind at track 1",
          },
          {
              .from = R"("kind": "midi")",
              .to = R"("kind": "audio")",
              .code = rule,
              .where = "",
              .says = "wrong-input-kind at track 3",
          },
          {
              .from = "\"start_marker\": 0",
              .to = "\"start_marker\": 9007199254740993",
              .code = rule,
              .where = "",
              .says = "value-out-of-range at project 0",
          },
          {
              .from = "\"start_marker\": 0",
              .to = "\"start_marker\": -1",
              .code = rule,
              .where = "",
              .says = "value-out-of-range at project 0",
          },
          // Negative zero is not a value the model stores.
          {
              .from = "\"pan\": 0.0",
              .to = "\"pan\": -0.0",
              .code = rule,
              .where = "",
              .says = "value-out-of-range at track 3",
          },
          {
              .from = "\"volume_db\": 0.0",
              .to = "\"volume_db\": 6.000001",
              .code = rule,
              .where = "",
              .says = "value-out-of-range at track 3",
          },
      });

  // Many broken rules are listed up to a point.
  std::string manyNotes = minimalText();
  std::string notes;
  for (int index = 0; index < 12; ++index) {
    notes += ",\n            { \"id\": " + std::to_string(8 + index) +
             R"(, "start": 60480, "length": 1, "pitch": )" +
             std::to_string(index) + ", \"velocity\": 0 }";
  }
  manyNotes =
      replaced(manyNotes, "\"velocity\": 90 }", "\"velocity\": 90 }" + notes);
  manyNotes = replaced(manyNotes, "\"next_id\": 8", "\"next_id\": 20");
  const auto loaded = readDocument(manyNotes);
  REQUIRE(!loaded.has_value());
  REQUIRE(loaded.error().code == rule);
  REQUIRE(loaded.error().message.find("note 15, and 4 more") !=
          std::string::npos);
  REQUIRE(loaded.error().message.find("note 16") == std::string::npos);
}

TEST_CASE("format/strict: what a careful hand edit may do is accepted") {
  const std::string text = minimalText();
  const model::Project expected = minimalDocument().project();

  // Keys in another order, other white space, an integer where a decimal is
  // usual: the same document, written back canonically.
  std::string edited =
      replaced(text, "  \"next_id\": 8,\n  \"tempo\": 120.0,\n",
               "  \"tempo\":120,\r\n\t\"next_id\" : 8 ,\n");
  edited = replaced(edited, "\"volume_db\": 0.0", "\"volume_db\": 0");
  edited = replaced(edited, "\"pan\": 0.0", "\"pan\": 0e0");
  // A negative integer where a decimal is usual.
  const auto quieter =
      readDocument(replaced(text, "\"volume_db\": 0.0", "\"volume_db\": -6"));
  REQUIRE(quieter.has_value());
  REQUIRE(quieter->document.project().tracks.front().mixer.volumeDb == -6.0);
  const auto loaded = readDocument(edited);
  REQUIRE(loaded.has_value());
  REQUIRE(loaded->document.project() == expected);
  const auto rewritten = writeDocument(loaded->document, testWriter);
  REQUIRE(rewritten.has_value());
  REQUIRE(rewritten->bytes() == text);

  // A tempo of exactly so many hundredths, however it is spelt.
  for (const auto &[spelling, hundredths] :
       std::vector<std::pair<std::string_view, int>>{
           {"93.47", 9'347},
           {"20", 2'000},
           {"300.00", 30'000},
           {"1.2005e2", 12'005},
           {"299.99", 29'999},
       }) {
    const auto withTempo = readDocument(replaced(
        text, "\"tempo\": 120.0", "\"tempo\": " + std::string{spelling}));
    REQUIRE(withTempo.has_value());
    REQUIRE(withTempo->document.project().tempo.hundredths() == hundredths);
  }
}

TEST_CASE("format/strict: a failed read leaves nothing behind to use") {
  // The result is an error value and holds no document at all; reading a
  // good document right after is unaffected.
  const std::string text = minimalText();
  const auto bad =
      readDocument(replaced(text, "\"velocity\": 90", "\"velocity\": 0"));
  REQUIRE(!bad.has_value());
  const auto good = readDocument(text);
  REQUIRE(good.has_value());
  REQUIRE(good->document.project() == minimalDocument().project());
  REQUIRE(!good->document.canUndo());
  REQUIRE(good->formatRead == formatVersion);
}

TEST_CASE("format/strict: one spelling for what has one meaning") {
  const std::string minimal = minimalText();
  const std::string everything = everythingText();
  const auto refusal = [](const std::string &text) {
    const auto loaded = readDocument(text);
    REQUIRE(!loaded.has_value());
    return loaded.error();
  };

  // Zero is 0, never -0, wherever a number stands: where an integer is
  // expected, and where a decimal is.
  for (const auto &[from, to] :
       std::vector<std::pair<std::string_view, std::string_view>>{
           {"\"start_marker\": 0", "\"start_marker\": -0"},
           {"\"position\": 0", "\"position\": -0"},
           {"\"content_start\": 0", "\"content_start\": -0"},
           {"\"pan\": 0.0", "\"pan\": -0"},
           {"\"volume_db\": 0.0", "\"volume_db\": -0"},
           {"\"next_id\": 8", "\"next_id\": -0"},
           {"\"pitch\": 60", "\"pitch\": -0"},
       }) {
    const Error error = refusal(replaced(minimal, from, to));
    CAPTURE(to, describe(error));
    REQUIRE(error.code == ErrorCode::Value);
    REQUIRE(error.message == "zero is written 0, not -0");
  }
  REQUIRE(refusal(replaced(minimal, "[4, 4]", "[4, -0]")).where ==
          "/time_signature/1");
  REQUIRE(refusal(replaced(minimal, "\"format\": 1", "\"format\": -0")).code ==
          ErrorCode::Structure);

  // A colour's digits are lower-case.
  Error error = refusal(replaced(minimal, "\"#808080\"", "\"#ABCDEF\""));
  REQUIRE(error.code == ErrorCode::Value);
  REQUIRE(error.where == "/tracks/0/colour");
  REQUIRE(refusal(replaced(minimal, "\"#808080\"", "\"#80808F\"")).code ==
          ErrorCode::Value);
  REQUIRE(readDocument(replaced(minimal, "\"#808080\"", "\"#abcdef\""))
              .has_value());

  // A content hash is 64 lower-case hexadecimal digits.
  const std::string hash =
      "3f9a1c2e5d0b4a6f8e7c9d1b2a3f4e5d6c7b8a9f0e1d2c3b4a5f6e7d8c9b0a1f";
  for (const std::string &wrong : {
           std::string{},
           std::string{"00"},
           hash.substr(1),
           hash + "0",
           "3F" + hash.substr(2),
           "zz" + hash.substr(2),
       }) {
    error = refusal(replaced(everything, R"("hash": ")" + hash + "\"",
                             R"("hash": ")" + wrong + "\""));
    CAPTURE(wrong);
    REQUIRE(error.code == ErrorCode::Value);
    REQUIRE(error.where == "/media/0/hash");
    REQUIRE(error.message == "a content hash is 64 lower-case hexadecimal "
                             "digits");
  }
  REQUIRE(
      refusal(replaced(everything, "\"hash\": \"" + hash + "\"", "\"hash\": 7"))
          .code == ErrorCode::Structure);

  // A state reference is null or the name of a state file.
  for (const std::string &wrong : {
           std::string{"state.bin"},
           std::string(64, 'a'),
           std::string(64, 'A') + ".bin",
           std::string(63, 'a') + ".bin",
           std::string(64, 'a') + ".BIN",
           "x/" + std::string(64, 'a') + ".bin",
       }) {
    error = refusal(replaced(everything, "\"state\": null",
                             R"("state": ")" + wrong + "\""));
    CAPTURE(wrong);
    REQUIRE(error.code == ErrorCode::Value);
    REQUIRE(error.where == "/tracks/0/devices/0/state");
  }
  REQUIRE(
      readDocument(replaced(everything, "\"state\": null",
                            "\"state\": \"" + std::string(64, 'a') + ".bin\""))
          .has_value());

  // A media path inside the project is relative and normalised; an absolute
  // one is a reference to a file elsewhere.
  for (const std::string_view wrong : {
           "./media/kick.wav",
           "media//kick.wav",
           "media/./kick.wav",
           "media/",
           "media/sub/../kick.wav",
       }) {
    error = refusal(replaced(everything, R"("path": "media/kick-3f9a1c2e.wav")",
                             R"("path": ")" + std::string{wrong} + "\""));
    CAPTURE(wrong);
    REQUIRE(error.code == ErrorCode::Value);
    REQUIRE(error.where == "/media/0/path");
  }
  for (const std::string_view fine : {
           "kick.wav",
           "media/drums/kick 1.wav",
           "/elsewhere/kick.wav",
           "/elsewhere/../kick.wav",
       }) {
    REQUIRE(readDocument(replaced(everything,
                                  "\"path\": \"media/kick-3f9a1c2e.wav\"",
                                  "\"path\": \"" + std::string{fine} + "\""))
                .has_value());
  }
}

TEST_CASE("format/strict: the name of the writing application is required") {
  const std::string text = minimalText();
  const std::string line = "  \"written_by\": \"LINA tests 1\",\n";
  auto loaded = readDocument(replaced(text, line, ""));
  REQUIRE(!loaded.has_value());
  REQUIRE(loaded.error().code == ErrorCode::Structure);
  REQUIRE(loaded.error().message == "missing key \"written_by\"");
  for (const std::string_view wrong : {"7", "null", "[\"LINA\"]", "true"}) {
    loaded = readDocument(replaced(text, R"("written_by": "LINA tests 1")",
                                   "\"written_by\": " + std::string{wrong}));
    REQUIRE(!loaded.has_value());
    REQUIRE(loaded.error().code == ErrorCode::Structure);
    REQUIRE(loaded.error().where == "/written_by");
  }
  // What it says is passed on and otherwise ignored.
  loaded = readDocument(replaced(text, "\"LINA tests 1\"",
                                 R"("something else entirely \u2713")"));
  REQUIRE(loaded.has_value());
  REQUIRE(loaded->writtenBy == "something else entirely \xE2\x9C\x93");
  REQUIRE(loaded->document.project() == minimalDocument().project());
}

} // namespace lina::format::testing
