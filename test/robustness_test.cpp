// Hostile input: every decoder is fed a deterministic corpus of random, mutated and truncated packets. Whatever comes in,
// nothing may crash, read out of bounds (run under the sanitizers: PXS_CORE_SANITIZE) or break the invariants below.
#include <pxs/core/dmx/frame.h>
#include <pxs/core/osc/codec.h>
#include <pxs/core/osc/names.h>
#include <pxs/core/websocket/frame.h>

#include "check.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {

struct Rng {
    std::uint64_t s;
    std::uint64_t next()
    {
        std::uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    std::size_t below(std::size_t n) { return std::size_t(next() % n); }
    std::uint8_t byte() { return static_cast<std::uint8_t>(next()); }
};

using Bytes = std::vector<std::uint8_t>;

Bytes randomBytes(Rng &r, std::size_t maxLen)
{
    Bytes b(r.below(maxLen + 1));
    for (auto &x : b) x = r.byte();
    return b;
}

void mutate(Rng &r, Bytes &b)
{
    if (b.empty()) return;
    switch (r.below(5)) {
    case 0: b[r.below(b.size())] = r.byte(); break;
    case 1: b.resize(r.below(b.size() + 1)); break;
    case 2: b.insert(b.begin() + std::ptrdiff_t(r.below(b.size() + 1)), r.byte()); break;
    case 3: b.erase(b.begin() + std::ptrdiff_t(r.below(b.size()))); break;
    default: for (int i = 0; i < 4; ++i) b[r.below(b.size())] ^= std::uint8_t(1u << r.below(8)); break;
    }
}

// ---- OSC

bool sameValue(const pxs::core::osc::Value &a, const pxs::core::osc::Value &b)
{
    if (a.index() != b.index()) return false;
    if (const auto *x = std::get_if<float>(&a)) {
        const float y = std::get<float>(b);
        return (std::isnan(*x) && std::isnan(y)) || std::memcmp(x, &y, sizeof y) == 0;
    }
    if (const auto *x = std::get_if<double>(&a)) {
        const double y = std::get<double>(b);
        return (std::isnan(*x) && std::isnan(y)) || std::memcmp(x, &y, sizeof y) == 0;
    }
    return a == b;
}

bool sameMessage(const pxs::core::osc::Message &a, const pxs::core::osc::Message &b)
{
    if (a.address != b.address || a.types != b.types || a.args.size() != b.args.size()) return false;
    for (std::size_t i = 0; i < a.args.size(); ++i)
        if (!sameValue(a.args[i], b.args[i])) return false;
    return true;
}

pxs::core::osc::Bytes validMessage(Rng &r)
{
    using namespace pxs::core::osc;
    Message m;
    m.address = "/";
    for (std::size_t i = r.below(6); i > 0; --i) m.address += char('a' + r.below(26)), m.address += r.below(3) ? "" : "/";
    static const char tags[] = "ihfdsSbcrmtTFNI";
    for (std::size_t i = r.below(8); i > 0; --i) {
        const char t = tags[r.below(sizeof tags - 1)];
        m.types += t;
        switch (t) {
        case 'i': m.args.emplace_back(std::int32_t(r.next())); break;
        case 'h':
        case 't': m.args.emplace_back(std::int64_t(r.next())); break;
        case 'f': m.args.emplace_back(float(r.below(1000)) / 7.0f); break;
        case 'd': m.args.emplace_back(double(r.below(100000)) / 3.0); break;
        case 's':
        case 'S': m.args.emplace_back(std::string(r.below(9), 'x')); break;
        case 'b': m.args.emplace_back(Blob(r.below(9), r.byte())); break;
        case 'c':
        case 'r':
        case 'm': m.args.emplace_back(std::uint32_t(r.next())); break;
        case 'T': m.args.emplace_back(true); break;
        case 'F': m.args.emplace_back(false); break;
        default: m.args.emplace_back(std::monostate{}); break;
        }
    }
    return encode(m);
}

void oscCorpus(Rng &r)
{
    using namespace pxs::core::osc;
    Bytes packet;
    switch (r.below(4)) {
    case 0: packet = randomBytes(r, 64); if (!packet.empty() && r.below(2)) packet[0] = '/'; break;
    case 1: packet = validMessage(r); break;
    case 2: {
        packet = {'#', 'b', 'u', 'n', 'd', 'l', 'e', 0, 0, 0, 0, 0, 0, 0, 0, 1};
        for (std::size_t i = r.below(4); i > 0; --i) {
            const Bytes e = validMessage(r);
            std::size_t n = e.size();
            if (r.below(8) == 0) n = r.below(100); // a lying size
            for (int s = 24; s >= 0; s -= 8) packet.push_back(static_cast<std::uint8_t>(n >> s));
            packet.insert(packet.end(), e.begin(), e.end());
        }
        break;
    }
    default: packet = validMessage(r); break;
    }
    for (std::size_t i = r.below(4); i > 0; --i) mutate(r, packet);

    std::vector<Message> out;
    out.push_back(Message{"/before", "", {}});
    if (!decode(packet, out)) {
        CHECK_EQ(out.size(), std::size_t(1)); // all or nothing
        return;
    }
    CHECK(out.size() >= 1 && out.size() <= 1 + kMaxMessagesPerPacket); // an empty bundle is valid
    for (std::size_t i = 1; i < out.size(); ++i) {
        CHECK_EQ(out[i].args.size(), out[i].types.size());
        // what was decoded encodes to a packet that decodes to the same message
        std::vector<Message> again;
        CHECK(decode(encode(out[i]), again));
        CHECK_EQ(again.size(), std::size_t(1));
        if (again.size() == 1) CHECK(sameMessage(again[0], out[i]));
    }
}

void matchCorpus(Rng &r)
{
    static const char alphabet[] = "ab/*?[]{}!-,\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\xFF\x80";
    std::string p = "/", a = "/";
    for (std::size_t i = r.below(14); i > 0; --i) p += alphabet[r.below(sizeof alphabet - 1)];
    for (std::size_t i = r.below(14); i > 0; --i) a += alphabet[r.below(sizeof alphabet - 1)];
    (void)pxs::core::osc::match(p, a);
    // a pattern with no special character matches exactly itself
    if (p.find_first_of("*?[{") == std::string::npos) CHECK(pxs::core::osc::match(p, p) && pxs::core::osc::match(p, p) == (p == p));
    (void)pxs::core::osc::safeName(a);
}

// ---- DMX

void dmxCorpus(Rng &r)
{
    using namespace pxs::core::dmx;
    const bool sacn = r.below(2) != 0;
    Frame f;
    f.sacn = sacn;
    f.universe = int(r.below(sacn ? 64000 : 32768)) + (sacn ? 1 : 0);
    f.priority = int(r.below(201));
    f.length = int(r.below(520));
    f.values = Bytes(r.below(600), r.byte());
    f.cid = Bytes(r.below(20), r.byte());
    f.name = std::string(r.below(80), 'n');
    Bytes packet = r.below(4) ? encode(f) : randomBytes(r, 200);
    for (std::size_t i = r.below(3); i > 0; --i) mutate(r, packet);
    Frame out;
    if (!decode(packet, sacn, out)) return;
    CHECK_EQ(out.values.size(), std::size_t(kChannelCount));
    CHECK(out.length >= 1 && out.length <= kChannelCount);
    CHECK(out.universe >= (sacn ? 1 : 0) && out.universe <= (sacn ? 63999 : 32767));
    // a decoded frame can be sent again, and reads back the same
    Frame again;
    CHECK(decode(encode(out), sacn, again));
    CHECK_EQ(again.universe, out.universe);
    CHECK(again.values == out.values);
}

// ---- WebSocket

void websocketCorpus(Rng &r)
{
    using namespace pxs::core::websocket;
    Bytes buffer = randomBytes(r, 40);
    if (!buffer.empty() && r.below(2)) buffer[0] = static_cast<std::uint8_t>(0x80 | (r.below(3) + 1)); // look like a text frame
    if (buffer.size() > 1 && r.below(2)) buffer[1] |= 0x80;
    const Bytes before = buffer;
    Frame frame;
    std::size_t consumed = 0;
    const DecodeStatus s = decode(buffer.data(), buffer.size(), 1 << 16, frame, consumed);
    CHECK(consumed <= before.size());
    if (s == DecodeStatus::Frame) {
        CHECK(consumed >= 6 && consumed >= frame.payload.size());
    } else {
        CHECK_EQ(consumed, std::size_t(0));
    }
    (void)acceptKey(std::string(before.begin(), before.end()));
}

} // namespace

int main()
{
    const char *env = std::getenv("PXS_ROBUSTNESS_ITERATIONS");
    const long iterations = env ? std::atol(env) : 60000;
    Rng r{0x5eedF00Dull};
    for (long i = 0; i < iterations; ++i) {
        oscCorpus(r);
        matchCorpus(r);
        dmxCorpus(r);
        websocketCorpus(r);
    }
    return pxs_test::finish("robustness");
}
