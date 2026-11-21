// SPDX-License-Identifier: GPL-3.0-or-later
//
// SHA-256 against the published test vectors (FIPS 180-4 examples and the
// NIST long message).
#include "lina/format/sha256.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace lina::format {

TEST_CASE("format/sha256: published vectors") {
  REQUIRE(sha256Hex("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  REQUIRE(sha256Hex("abc") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  REQUIRE(
      sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
      "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  REQUIRE(sha256Hex(std::string(64, 'a')) ==
          "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
  REQUIRE(sha256Hex(std::string(1'000'000, 'a')) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

namespace {

struct Vector {
  std::size_t length;
  std::string_view digest;
};

} // namespace

TEST_CASE("format/sha256: digests at every padding boundary") {
  // The message of length n is the bytes (7 i + 1) mod 256 for i below n.
  // The digests were computed with another implementation (Python's hashlib,
  // which agrees with sha256sum) and are written out here.
  constexpr std::array vectors{
      Vector{
          .length = 0,
          .digest = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991"
                    "b7852b855",
      },
      Vector{
          .length = 1,
          .digest = "4bf5122f344554c53bde2ebb8cd2b7e3d1600ad631c385a5d7cce23"
                    "c7785459a",
      },
      Vector{
          .length = 55,
          .digest = "16fa57a0a3423a715d594516339f36189d6b5f93754a9714fef2026"
                    "16a9fabfe",
      },
      Vector{
          .length = 56,
          .digest = "c37b44e5f1b18554b36966f4f8e08bfbf3164c4b6c10374d12d8985"
                    "0892073c5",
      },
      Vector{
          .length = 57,
          .digest = "12b234922502022f755ab8550a3d4e202ad39c81d961a4f59ec39d5"
                    "fd83d15a7",
      },
      Vector{
          .length = 63,
          .digest = "bbba992d2c85af960fb2987a1fd05e0aa82a3db3c740dd8982a9e27"
                    "3b75e36a3",
      },
      Vector{
          .length = 64,
          .digest = "66bd4633ed6f71c4ecfa4763bf7ba1c8ec7612de9aa6c0578a7b675"
                    "207c71e0b",
      },
      Vector{
          .length = 65,
          .digest = "9f7dc47107b750a1f3d35db5d9547f24ef40da5b731b9540d4f4371"
                    "0a154f6c9",
      },
      Vector{
          .length = 119,
          .digest = "a3ed307b730fa77c07531300c6e4a282330011d4d4caf6bb7b63ae0"
                    "5950f4b66",
      },
      Vector{
          .length = 120,
          .digest = "8e3b15d9fea7472655aa069620b7f8c2e55ee1499f763200a7515fe"
                    "826e99d20",
      },
      Vector{
          .length = 121,
          .digest = "a8320d9736c8a5a7445c87c4d18c0da2fa8fb3b323ed8318c724a5b"
                    "3e230f87b",
      },
      Vector{
          .length = 127,
          .digest = "44480fb9672845177f5368a08b69ea263275f2a5ec42e06a933370f"
                    "e0d2968a4",
      },
      Vector{
          .length = 128,
          .digest = "e462c130fef8c97e34f7dc3ff3ad2f8b3533ab849af21c10531552a"
                    "2852387a4",
      },
      Vector{
          .length = 1000,
          .digest = "095ecb62e30793ab4b954cd6a0586d0cc91f7ea5b1332694d8da780"
                    "e98676d78",
      },
  };
  REQUIRE(vectors.size() == 14);
  std::string message;
  for (std::size_t index = 0; index < 1'000; ++index) {
    message += static_cast<char>(((7 * index) + 1) % 256);
  }
  for (const Vector &vector : vectors) {
    CAPTURE(vector.length);
    const std::string_view whole =
        std::string_view{message}.substr(0, vector.length);
    REQUIRE(sha256Hex(whole) == vector.digest);
    // Handed over in two pieces, cut at every place.
    for (std::size_t cut = 0; cut <= vector.length; ++cut) {
      Sha256 hash;
      hash.update(whole.substr(0, cut));
      hash.update(whole.substr(cut));
      REQUIRE(hash.finishHex() == vector.digest);
    }
    // And a byte at a time.
    Sha256 slow;
    for (const char byte : whole) {
      slow.update(std::string_view{&byte, 1});
    }
    REQUIRE(slow.finishHex() == vector.digest);
  }
}

} // namespace lina::format
