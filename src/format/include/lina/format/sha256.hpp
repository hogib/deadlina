// SPDX-License-Identifier: GPL-3.0-or-later
//
// SHA-256 (FIPS 180-4), written from the standard. Names plugin state files
// and fingerprints media
// (docs/design/16-project-file-format-encoding.md).
#ifndef LINA_FORMAT_SHA256_HPP
#define LINA_FORMAT_SHA256_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace lina::format {

// Hashes bytes handed over in any number of pieces.
class Sha256 {
public:
  void update(std::string_view bytes);
  // The digest as 64 lower-case hexadecimal digits. The object must not be
  // used afterwards.
  std::string finishHex();

private:
  void compress();

  std::array<std::uint32_t, 8> state_{
      0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
      0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
  };
  std::array<std::uint8_t, 64> block_{};
  std::size_t filled_{0};
  std::uint64_t length_{0};
};

std::string sha256Hex(std::string_view bytes);

} // namespace lina::format

#endif // LINA_FORMAT_SHA256_HPP
