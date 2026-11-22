// The SHA-1 and base64 the WebSocket handshake needs, against the standard vectors (FIPS 180-4, RFC 4648).
#include "check.h"
#include "detail/base64.h"
#include "detail/sha1.h"

#include <string>

using namespace pxs::core::detail;

namespace {
std::string sha1Hex(const std::string &s)
{
    const auto d = sha1(reinterpret_cast<const std::uint8_t *>(s.data()), s.size());
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for (const auto b : d) out += {digits[b >> 4], digits[b & 15]};
    return out;
}
std::string b64(const std::string &s) { return base64(reinterpret_cast<const std::uint8_t *>(s.data()), s.size()); }
} // namespace

int main()
{
    CHECK_EQ(sha1Hex(""), std::string("da39a3ee5e6b4b0d3255bfef95601890afd80709"));
    CHECK_EQ(sha1Hex("abc"), std::string("a9993e364706816aba3e25717850c26c9cd0d89d"));
    CHECK_EQ(sha1Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
             std::string("84983e441c3bd26ebaae4aa1f95129e5e54670f1"));
    CHECK_EQ(sha1Hex(std::string(1000000, 'a')), std::string("34aa973cd4c4daa4f61eeb2bdbad27316534016f"));
    // the padding boundaries: 55, 56, 63, 64 bytes
    CHECK_EQ(sha1Hex(std::string(55, 'a')), std::string("c1c8bbdc22796e28c0e15163d20899b65621d65a"));
    CHECK_EQ(sha1Hex(std::string(56, 'a')), std::string("c2db330f6083854c99d4b5bfb6e8f29f201be699"));
    CHECK_EQ(sha1Hex(std::string(63, 'a')), std::string("03f09f5b158a7a8cdad920bddc29b81c18a551f5"));
    CHECK_EQ(sha1Hex(std::string(64, 'a')), std::string("0098ba824b5c16427bd7a1122a5a442a25ec644d"));

    CHECK_EQ(b64(""), std::string(""));
    CHECK_EQ(b64("f"), std::string("Zg=="));
    CHECK_EQ(b64("fo"), std::string("Zm8="));
    CHECK_EQ(b64("foo"), std::string("Zm9v"));
    CHECK_EQ(b64("foob"), std::string("Zm9vYg=="));
    CHECK_EQ(b64("fooba"), std::string("Zm9vYmE="));
    CHECK_EQ(b64("foobar"), std::string("Zm9vYmFy"));
    CHECK_EQ(b64(std::string("\xFB\xFF\xBF", 3)), std::string("+/+/"));
    return pxs_test::finish("detail");
}
