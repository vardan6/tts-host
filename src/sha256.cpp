#include "tts_host/sha256.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace tts_host {

namespace {

// A minimal, self-contained SHA-256 (FIPS 180-4), processed incrementally so
// verifying a large weight file never holds the whole thing in memory.
class Sha256 {
 public:
  void update(const unsigned char *data, std::size_t length) {
    total_bytes_ += length;
    std::size_t offset = 0;
    if (!buffer_.empty()) {
      const std::size_t needed = 64 - buffer_.size();
      const std::size_t take = std::min(needed, length);
      buffer_.insert(buffer_.end(), data, data + take);
      offset = take;
      if (buffer_.size() < 64) {
        return;
      }
      process_block(buffer_.data());
      buffer_.clear();
    }
    while (offset + 64 <= length) {
      process_block(data + offset);
      offset += 64;
    }
    buffer_.assign(data + offset, data + length);
  }

  std::array<unsigned char, 32> finalize() {
    std::vector<unsigned char> tail;
    tail.push_back(0x80);
    const std::size_t used = buffer_.size() + 1;
    const std::size_t remainder = used % 64;
    const std::size_t zero_count = remainder <= 56 ? (56 - remainder) : (120 - remainder);
    tail.insert(tail.end(), zero_count, 0);
    const std::uint64_t total_bits = total_bytes_ * 8;
    for (int i = 7; i >= 0; --i) {
      tail.push_back(static_cast<unsigned char>((total_bits >> (i * 8)) & 0xff));
    }

    std::vector<unsigned char> final_bytes(buffer_);
    final_bytes.insert(final_bytes.end(), tail.begin(), tail.end());
    for (std::size_t offset = 0; offset < final_bytes.size(); offset += 64) {
      process_block(final_bytes.data() + offset);
    }

    std::array<unsigned char, 32> digest{};
    for (int i = 0; i < 8; ++i) {
      digest[i * 4 + 0] = static_cast<unsigned char>((state_[i] >> 24) & 0xff);
      digest[i * 4 + 1] = static_cast<unsigned char>((state_[i] >> 16) & 0xff);
      digest[i * 4 + 2] = static_cast<unsigned char>((state_[i] >> 8) & 0xff);
      digest[i * 4 + 3] = static_cast<unsigned char>(state_[i] & 0xff);
    }
    return digest;
  }

 private:
  static std::uint32_t rotr(std::uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
  }

  void process_block(const unsigned char *block) {
    static constexpr std::uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    };

    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
             (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
             (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
             static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
      const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (int i = 0; i < 64; ++i) {
      const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const std::uint32_t ch = (e & f) ^ (~e & g);
      const std::uint32_t temp1 = h + s1 + ch + k[i] + w[i];
      const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t temp2 = s0 + maj;
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::uint32_t state_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  std::vector<unsigned char> buffer_;
  std::uint64_t total_bytes_ = 0;
};

std::string to_lowercase_hex(const std::array<unsigned char, 32> &digest) {
  static constexpr char kHexDigits[] = "0123456789abcdef";
  std::string hex(64, '0');
  for (std::size_t i = 0; i < digest.size(); ++i) {
    hex[i * 2] = kHexDigits[(digest[i] >> 4) & 0x0f];
    hex[i * 2 + 1] = kHexDigits[digest[i] & 0x0f];
  }
  return hex;
}

}  // namespace

std::string sha256_hex_of_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("unable to read " + path.string() + " to verify its checksum");
  }

  Sha256 hasher;
  std::vector<char> buffer(64 * 1024);
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto read = input.gcount();
    if (read > 0) {
      hasher.update(reinterpret_cast<const unsigned char *>(buffer.data()),
                   static_cast<std::size_t>(read));
    }
  }
  return to_lowercase_hex(hasher.finalize());
}

std::string sha256_hex(std::string_view bytes) {
  Sha256 hasher;
  hasher.update(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size());
  return to_lowercase_hex(hasher.finalize());
}

}  // namespace tts_host
