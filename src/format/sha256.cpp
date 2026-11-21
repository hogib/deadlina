// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/format/sha256.hpp"

#include <algorithm>
#include <bit>
#include <functional>
#include <iterator>

namespace lina::format {

namespace {

// The first 32 bits of the fractional parts of the cube roots of the first 64
// primes (FIPS 180-4, 4.2.2).
constexpr std::array<std::uint32_t, 64> roundConstants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
    0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
    0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
    0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
    0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
    0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
    0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
    0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

constexpr std::size_t blockSize = 64;
// The message length goes into the last eight bytes of the final block.
constexpr std::size_t lengthOffset = 56;

} // namespace

void Sha256::compress() {
  std::array<std::uint32_t, 64> schedule{};
  for (std::size_t word = 0; word < 16; ++word) {
    const std::size_t byte = 4 * word;
    schedule.at(word) = (std::uint32_t{block_.at(byte)} << 24U) |
                        (std::uint32_t{block_.at(byte + 1)} << 16U) |
                        (std::uint32_t{block_.at(byte + 2)} << 8U) |
                        std::uint32_t{block_.at(byte + 3)};
  }
  for (std::size_t word = 16; word < schedule.size(); ++word) {
    const std::uint32_t w15 = schedule.at(word - 15);
    const std::uint32_t w2 = schedule.at(word - 2);
    const std::uint32_t s0 =
        std::rotr(w15, 7) ^ std::rotr(w15, 18) ^ (w15 >> 3U);
    const std::uint32_t s1 =
        std::rotr(w2, 17) ^ std::rotr(w2, 19) ^ (w2 >> 10U);
    schedule.at(word) =
        schedule.at(word - 16) + s0 + schedule.at(word - 7) + s1;
  }

  std::array<std::uint32_t, 8> v = state_;
  auto &[a, b, c, d, e, f, g, h] = v;
  for (std::size_t round = 0; round < schedule.size(); ++round) {
    const std::uint32_t s1 =
        std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
    const std::uint32_t choice = (e & f) ^ (~e & g);
    const std::uint32_t t1 =
        h + s1 + choice + roundConstants.at(round) + schedule.at(round);
    const std::uint32_t s0 =
        std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
    const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t t2 = s0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  std::ranges::transform(state_, v, state_.begin(), std::plus<>{});
}

void Sha256::update(std::string_view bytes) {
  length_ += bytes.size();
  while (!bytes.empty()) {
    // As much as fits into the block, in one go.
    const std::size_t taken = std::min(bytes.size(), blockSize - filled_);
    std::ranges::transform(
        bytes.substr(0, taken),
        std::next(block_.begin(), static_cast<std::ptrdiff_t>(filled_)),
        [](char character) { return static_cast<std::uint8_t>(character); });
    filled_ += taken;
    bytes.remove_prefix(taken);
    if (filled_ == blockSize) {
      compress();
      filled_ = 0;
    }
  }
}

std::string Sha256::finishHex() {
  const std::uint64_t bits = length_ * 8;
  // One 1 bit, then zeros up to the place of the length.
  std::string padding(1, '\x80');
  const std::size_t used = (filled_ + 1) % blockSize;
  padding.append((lengthOffset + blockSize - used) % blockSize, '\0');
  for (int shift = 56; shift >= 0; shift -= 8) {
    padding +=
        static_cast<char>((bits >> static_cast<unsigned>(shift)) & 0xFFU);
  }
  update(padding);

  constexpr std::string_view digits = "0123456789abcdef";
  std::string hex;
  hex.reserve(64);
  for (const std::uint32_t word : state_) {
    for (int shift = 28; shift >= 0; shift -= 4) {
      hex += digits.at((word >> static_cast<unsigned>(shift)) & 0xFU);
    }
  }
  return hex;
}

std::string sha256Hex(std::string_view bytes) {
  Sha256 hash;
  hash.update(bytes);
  return hash.finishHex();
}

} // namespace lina::format
