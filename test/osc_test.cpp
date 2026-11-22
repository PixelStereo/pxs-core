#include <pxs/core/osc/codec.h>

#include "check.h"
#include "golden.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

using namespace pxs::core::osc;

namespace {

Message buildMessage(const std::string &address, const std::string &types, const std::string &argText)
{
    Message m;
    m.address = address;
    m.types = types;
    // "tag:value" separated by ',' (a 's' value has no comma)
    for (const auto &item : golden::split(argText, ',')) {
        const char tag = item[0];
        const std::string text = item.substr(2);
        switch (tag) {
        case 'i': m.args.emplace_back(std::int32_t(std::stol(text))); break;
        case 'h':
        case 't': m.args.emplace_back(std::int64_t(std::stoll(text))); break;
        case 'f': m.args.emplace_back(std::stof(text)); break;
        case 'd': m.args.emplace_back(std::stod(text)); break;
        case 's':
        case 'S': m.args.emplace_back(text); break;
        case 'b': m.args.emplace_back(golden::unhex(text)); break;
        case 'c': m.args.emplace_back(std::uint32_t(static_cast<unsigned char>(text[0]))); break;
        case 'r':
        case 'm': m.args.emplace_back(std::uint32_t(std::stoul(text))); break;
        case 'T': m.args.emplace_back(true); break;
        case 'F': m.args.emplace_back(false); break;
        default: m.args.emplace_back(std::monostate{}); break;
        }
    }
    return m;
}

void goldenMessages()
{
    for (const auto &g : golden::lines("osc")) {
        // The golden line says what the message is (g[1] address, g[2] tags, g[3] arguments); g[4] is its packet.
        const Message m = buildMessage(g[1], g[2], g[3]);
        const auto packet = golden::unhex(g[4]);
        CHECK_EQ(golden::hex(encode(m)), g[4]);

        std::vector<Message> out;
        CHECK(decode(packet, out));
        CHECK_EQ(out.size(), std::size_t(1));
        if (out.size() != 1) continue;
        CHECK_EQ(out[0].address, m.address);
        CHECK_EQ(out[0].types, m.types);
        CHECK_EQ(out[0].args.size(), m.args.size());
        CHECK(out[0].args == m.args);
        // and what was decoded encodes to the same bytes
        CHECK_EQ(golden::hex(encode(out[0])), g[4]);
    }
}

void goldenBundles()
{
    for (const auto &g : golden::lines("bundle")) {
        std::vector<Message> out;
        CHECK(decode(golden::unhex(g[1]), out));
        std::string addresses;
        for (const auto &m : out) addresses += (addresses.empty() ? "" : ",") + m.address;
        CHECK_EQ(addresses, g[2]);
    }
}

void goldenRefused()
{
    for (const auto &g : golden::lines("oscbad")) {
        std::vector<Message> out;
        out.push_back(Message{"/keep", "", {}});
        CHECK(!decode(golden::unhex(g[1]), out));
        CHECK_EQ(out.size(), std::size_t(1)); // all or nothing: what was there is untouched
    }
}

void goldenMatch()
{
    for (const auto &g : golden::lines("match")) CHECK_EQ(match(g[1], g[2]), g[3] == "1");
}

// ---- what the library defines beyond the reference

void nonFiniteFloats()
{
    Message m{"/f", "fd", {std::numeric_limits<float>::infinity(), -std::numeric_limits<double>::infinity()}};
    std::vector<Message> out;
    CHECK(decode(encode(m), out));
    CHECK(std::isinf(std::get<float>(out[0].args[0])));
    CHECK(std::isinf(std::get<double>(out[0].args[1])) && std::get<double>(out[0].args[1]) < 0);
    Message n{"/n", "fd", {std::nanf(""), std::nan("")}};
    out.clear();
    CHECK(decode(encode(n), out));
    CHECK(std::isnan(std::get<float>(out[0].args[0])) && std::isnan(std::get<double>(out[0].args[1])));
}

void encodeIsAuthoritativeOnTypes()
{
    // A missing argument, or one of another alternative than the tag decodes to, is written as the tag's zero.
    Message m{"/z", "ifsb", {std::string("not an int"), 1.5}}; // wrong kinds, and two missing
    std::vector<Message> out;
    CHECK(decode(encode(m), out));
    CHECK_EQ(out.size(), std::size_t(1));
    CHECK_EQ(std::get<std::int32_t>(out[0].args[0]), 0);
    CHECK_EQ(std::get<float>(out[0].args[1]), 0.0f);
    CHECK_EQ(std::get<std::string>(out[0].args[2]), std::string());
    CHECK(std::get<Blob>(out[0].args[3]).empty());
}

void encodeAppends()
{
    Message m{"/a", "i", {std::int32_t(1)}};
    Bytes out{0xAA, 0xBB};
    encode(m, out);
    CHECK_EQ(out.size(), std::size_t(2 + encode(m).size()));
    CHECK_EQ(out[0], 0xAA);
    CHECK(std::equal(out.begin() + 2, out.end(), encode(m).begin()));
}

void stringWithNulReadsBackCut()
{
    Message m{"/s", "s", {std::string("ab\0cd", 5)}};
    std::vector<Message> out;
    CHECK(decode(encode(m), out));
    CHECK_EQ(std::get<std::string>(out[0].args[0]), std::string("ab"));
}

void typeTagsAreBytes()
{
    // A byte that is no tag is an unknown tag: a nil that carries no data, so tags and arguments stay equal in number.
    std::string packet("/a\0\0", 4);
    packet += std::string(",i\xC3\xA9i\0\0\0", 8); // the tags 'i', two bytes that are no tag, 'i'; padded to 8
    packet += std::string("\0\0\0\x01", 4);
    packet += std::string("\0\0\0\x02", 4);
    std::vector<Message> out;
    CHECK(decode(reinterpret_cast<const std::uint8_t *>(packet.data()), packet.size(), out));
    CHECK_EQ(out.size(), std::size_t(1));
    CHECK_EQ(out[0].types, std::string("i\xC3\xA9i"));
    CHECK_EQ(out[0].args.size(), out[0].types.size());
    CHECK_EQ(std::get<std::int32_t>(out[0].args[3]), 2);
}

void noPointerNoCrash()
{
    std::vector<Message> out;
    CHECK(!decode(nullptr, 0, out));
    CHECK(!decode(nullptr, 5, out));
    CHECK(!decode(Bytes{}, out));
}

void limits()
{
    // A packet beyond kMaxPacketBytes is refused.
    Bytes big(kMaxPacketBytes + 1, 0);
    big[0] = '/';
    std::vector<Message> out;
    CHECK(!decode(big, out));

    // kMaxArguments tags are accepted, one more is not.
    for (const std::size_t n : {kMaxArguments, kMaxArguments + 1}) {
        Message m{"/many", std::string(n, 'T'), {}};
        out.clear();
        CHECK_EQ(decode(encode(m), out), n <= kMaxArguments);
    }

    // kMaxMessagesPerPacket messages of one bundle are accepted, one more is not; the limit is per packet, not per `out`.
    const Bytes one = encode(Message{"/x", "", {}});
    for (const std::size_t n : {kMaxMessagesPerPacket, kMaxMessagesPerPacket + 1}) {
        Bytes bundle{'#', 'b', 'u', 'n', 'd', 'l', 'e', 0, 0, 0, 0, 0, 0, 0, 0, 1};
        for (std::size_t i = 0; i < n; ++i) {
            for (int s = 24; s >= 0; s -= 8) bundle.push_back(static_cast<std::uint8_t>(one.size() >> s));
            bundle.insert(bundle.end(), one.begin(), one.end());
        }
        out.clear();
        CHECK_EQ(decode(bundle, out), n <= kMaxMessagesPerPacket);
        CHECK_EQ(out.size(), n <= kMaxMessagesPerPacket ? n : std::size_t(0));
    }
    std::vector<Message> carried(kMaxMessagesPerPacket, Message{"/old", "", {}});
    CHECK(decode(one, carried)); // 1024 already in `out`: this packet's own count is what is limited
    CHECK_EQ(carried.size(), kMaxMessagesPerPacket + 1);

    // nesting: kMaxBundleDepth levels deep, and one more
    for (const int depth : {kMaxBundleDepth, kMaxBundleDepth + 1}) {
        Bytes packet = one;
        for (int d = 0; d < depth; ++d) {
            Bytes wrap{'#', 'b', 'u', 'n', 'd', 'l', 'e', 0, 0, 0, 0, 0, 0, 0, 0, 1};
            for (int s = 24; s >= 0; s -= 8) wrap.push_back(static_cast<std::uint8_t>(packet.size() >> s));
            wrap.insert(wrap.end(), packet.begin(), packet.end());
            packet = wrap;
        }
        out.clear();
        CHECK_EQ(decode(packet, out), depth <= kMaxBundleDepth);
    }
}

void patternsUseCodePoints()
{
    CHECK(match("/a/?", "/a/\xF0\x9F\x98\x80"));          // `?` is one whole emoji
    CHECK(!match("/a/??", "/a/\xF0\x9F\x98\x80"));       // not two halves of it
    CHECK(match("/a/[\xF0\x9F\x98\x80]", "/a/\xF0\x9F\x98\x80"));
    CHECK(match("/a/[\xF0\x9F\x98\x80-\xF0\x9F\x98\x8F]", "/a/\xF0\x9F\x98\x85"));
    CHECK(match("/a/{\xE2\x82\xAC,\xC3\xA9}x", "/a/\xC3\xA9x"));
    CHECK(match("/a/*\xF0\x9F\x98\x80", "/a/xyz\xF0\x9F\x98\x80"));
}

void invalidUtf8IsKeptApart()
{
    // Each invalid byte is a character of its own: not equal to another invalid byte, nor to U+FFFD.
    CHECK(match("/\xFF", "/\xFF"));
    CHECK(!match("/\xFF", "/\xFE"));
    CHECK(!match("/\xFF", "/\xEF\xBF\xBD"));
    CHECK(!match("/\xEF\xBF\xBD", "/\xFF"));
    CHECK(match("/?", "/\xFF"));
    CHECK(!match("/?", "/\xFF\xFE"));
    CHECK(match("/??", "/\xFF\xFE"));
    // overlong, surrogate and out-of-range encodings are invalid: every byte its own character
    CHECK(match("/???", "/\xE0\x80\x80"));          // overlong NUL
    CHECK(match("/???", "/\xED\xA0\x80"));          // a UTF-16 surrogate
    CHECK(match("/????", "/\xF4\x90\x80\x80"));     // above U+10FFFF
    CHECK(match("/?", "/\xC3"));                    // a lead byte cut short is one character of its own
    CHECK(!match("/??", "/\xC3"));
}

void malformedPatternsMatchNothing()
{
    CHECK(!match("/a/[abc", "/a/a"));
    CHECK(!match("/a/{foo,bar", "/a/foo"));
    CHECK(!match("/a/[", "/a/x"));
    CHECK(!match("/a/{", "/a/x"));
}

void patternLimits()
{
    const std::string seg(kMaxSegmentChars, 'a');
    CHECK(match("/" + seg, "/" + seg));                                        // a part of exactly the limit
    CHECK(!match("/" + seg + "a", "/" + seg + "a"));                           // one over
    const std::string many = [] {
        std::string s;
        while (s.size() <= kMaxPatternChars) s += "/ab";
        return s;
    }();
    CHECK(!match(many, many)); // beyond the pattern's limit as a whole
}

void starRunsAreLinear()
{
    // A backtracking matcher takes exponential time here; this must return at once.
    const std::string pattern = "/" + std::string(2000, '*') + "a";
    CHECK(!match(pattern, "/" + std::string(3000, 'b')));
    std::string alt = "/";
    for (int i = 0; i < 600; ++i) alt += "*{a,b}";
    CHECK(!match(alt, "/" + std::string(3000, 'c')));
}

} // namespace

int main()
{
    goldenMessages();
    goldenBundles();
    goldenRefused();
    goldenMatch();
    nonFiniteFloats();
    encodeIsAuthoritativeOnTypes();
    encodeAppends();
    stringWithNulReadsBackCut();
    typeTagsAreBytes();
    noPointerNoCrash();
    limits();
    patternsUseCodePoints();
    invalidUtf8IsKeptApart();
    malformedPatternsMatchNothing();
    patternLimits();
    starRunsAreLinear();
    return pxs_test::finish("osc");
}
