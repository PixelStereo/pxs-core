#include <pxs/core/dmx/frame.h>

#include "check.h"
#include "golden.h"

using namespace pxs::core::dmx;

namespace {

Frame frameOf(const golden::Fields &g)
{
    // dmx  sacn universe priority sequence length startCode terminated cid(hex) name(hex) values(hex)  packet(hex)
    Frame f;
    f.sacn = g[1] == "1";
    f.universe = std::stoi(g[2]);
    f.priority = std::stoi(g[3]);
    f.sequence = static_cast<std::uint8_t>(std::stoi(g[4]));
    f.length = std::stoi(g[5]);
    f.startCode = static_cast<std::uint8_t>(std::stoi(g[6]));
    f.terminated = g[7] == "1";
    f.cid = golden::unhex(g[8]);
    f.name = golden::fromHex(g[9]);
    f.values = golden::unhex(g[10]);
    return f;
}

void goldenFrames()
{
    int valid = 0;
    for (const auto &g : golden::lines("dmx")) {
        const Frame f = frameOf(g);
        const Bytes packet = encode(f);
        CHECK_EQ(golden::hex(packet), g[11]);
        if (packet.empty()) continue; // out of range: nothing is sent
        ++valid;
        Frame back;
        if (f.sacn && f.startCode != 0) { // a start code other than 0 is for encoding only: decode takes levels
            CHECK(!decode(packet, true, back));
            continue;
        }
        CHECK(decode(packet, f.sacn, back));
        if (back.values.size() != std::size_t(kChannelCount)) continue;
        CHECK_EQ(back.universe, f.universe);
        CHECK_EQ(back.sequence, f.sequence);
        CHECK_EQ(back.values.size(), std::size_t(kChannelCount));
        CHECK(back.length >= 1 && back.length <= kChannelCount);
        CHECK(std::equal(back.values.begin(), back.values.begin() + back.length, [&] {
            Bytes expected(std::size_t(back.length), 0);
            std::copy_n(f.values.begin(), std::min<std::size_t>(f.values.size(), expected.size()), expected.begin());
            return expected;
        }().begin()));
        if (f.sacn) {
            CHECK_EQ(back.priority, f.priority);
            CHECK_EQ(back.terminated, f.terminated);
            CHECK_EQ(back.cid.size(), std::size_t(16));
            CHECK_EQ(back.name, f.name.substr(0, std::min<std::size_t>(f.name.size(), 63)));
        }
        // a decoded frame encodes to the same bytes (when nothing in it was cut or padded)
        if (!f.sacn || (f.cid.size() == 16 && f.startCode == 0)) {
            Frame again = back;
            again.values.resize(std::size_t(again.length));
            CHECK_EQ(golden::hex(encode(again)), g[11]);
        }
    }
    CHECK(valid >= 30);
}

void goldenGroups()
{
    for (const auto &g : golden::lines("group")) CHECK_EQ(sacnGroup(std::stoi(g[1])), g[2]);
}

void refusedPackets()
{
    Frame f;
    f.universe = 3;
    f.values = Bytes(512, 9);
    const Bytes art = encode(f);
    f.sacn = true;
    f.universe = 3;
    const Bytes sacn = encode(f);
    Frame out;

    CHECK(decode(art, false, out));
    CHECK(decode(sacn, true, out));
    CHECK(!decode(art, true, out)); // the other protocol
    CHECK(!decode(sacn, false, out));
    CHECK(!decode(nullptr, 0, false, out));
    CHECK(!decode(Bytes{}, false, out));
    CHECK(!decode(Bytes{}, true, out));

    // every truncation of a valid packet is refused (sACN: its PDU lengths no longer match), never read out of range
    for (std::size_t n = 0; n < art.size(); ++n) CHECK(!decode(art.data(), n, false, out));
    for (std::size_t n = 0; n < sacn.size(); ++n) CHECK(!decode(sacn.data(), n, true, out));
    CHECK(out.sacn && out.universe == 0 && out.values.empty()); // a refused packet leaves a default frame of the protocol asked

    // flipping any single byte of the fixed parts either keeps a valid frame or is refused, and never crashes
    for (std::size_t i = 0; i < sacn.size(); ++i) {
        Bytes bad = sacn;
        bad[i] ^= 0xFF;
        Frame x;
        (void)decode(bad, true, x);
    }
    // trailing bytes: sACN must match its length exactly, Art-Net may carry more
    Bytes longer = sacn;
    longer.push_back(0);
    CHECK(!decode(longer, true, out));
    Bytes artLonger = art;
    artLonger.push_back(0);
    CHECK(decode(artLonger, false, out));
}

void artNetRanges()
{
    Frame f;
    f.universe = 32767;
    CHECK(!encode(f).empty());
    f.universe = 32768;
    CHECK(encode(f).empty());
    f.universe = -1;
    CHECK(encode(f).empty());
    f.sacn = true;
    f.universe = 0;
    CHECK(encode(f).empty());
    f.universe = 63999;
    CHECK(!encode(f).empty());
    f.universe = 64000;
    CHECK(encode(f).empty());
    f.universe = 1;
    f.priority = 201;
    CHECK(encode(f).empty());
    f.priority = -1;
    CHECK(encode(f).empty());
}

void nameIsCutAtFirstNul()
{
    Frame f;
    f.sacn = true;
    f.universe = 1;
    f.name = std::string("abc\0def", 7);
    Frame back;
    CHECK(decode(encode(f), true, back));
    CHECK_EQ(back.name, std::string("abc"));
}

} // namespace

int main()
{
    goldenFrames();
    goldenGroups();
    refusedPackets();
    artNetRanges();
    nameIsCutAtFirstNul();
    return pxs_test::finish("dmx");
}
