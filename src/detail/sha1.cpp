#include "detail/sha1.h"

#include <cstring>

namespace pxs::core::detail {
namespace {

constexpr std::uint32_t rotl(std::uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

void processBlock(std::uint32_t h[5], const std::uint8_t *p)
{
    std::uint32_t w[80];
    for (int t = 0; t < 16; ++t, p += 4)
        w[t] = (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
    for (int t = 16; t < 80; ++t) w[t] = rotl(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);
    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int t = 0; t < 80; ++t) {
        std::uint32_t f, k;
        if (t < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
        else if (t < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
        else if (t < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
        const std::uint32_t tmp = rotl(a, 5) + f + e + k + w[t];
        e = d; d = c; c = rotl(b, 30); b = a; a = tmp;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

} // namespace

std::array<std::uint8_t, 20> sha1(const std::uint8_t *data, std::size_t size)
{
    std::uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    // The whole blocks straight from the input, then the rest, a 1 bit, zeros up to 56 mod 64 and the length in bits
    // (64 bits, big endian) in a buffer of one or two blocks: nothing is allocated.
    const std::size_t whole = size / 64 * 64;
    for (std::size_t at = 0; at < whole; at += 64) processBlock(h, data + at);

    std::uint8_t tail[128] = {};
    const std::size_t rest = size - whole;
    if (rest) std::memcpy(tail, data + whole, rest);
    tail[rest] = 0x80;
    const std::size_t tailSize = rest < 56 ? 64 : 128;
    const std::uint64_t bits = std::uint64_t(size) * 8;
    for (int i = 0; i < 8; ++i) tail[tailSize - 1 - std::size_t(i)] = static_cast<std::uint8_t>(bits >> (8 * i));
    for (std::size_t at = 0; at < tailSize; at += 64) processBlock(h, tail + at);

    std::array<std::uint8_t, 20> out{};
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 4; ++j) out[std::size_t(i * 4 + j)] = static_cast<std::uint8_t>(h[i] >> (24 - 8 * j));
    return out;
}

} // namespace pxs::core::detail
