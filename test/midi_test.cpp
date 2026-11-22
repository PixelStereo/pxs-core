#include <pxs/core/midi/message.h>

#include "check.h"
#include "golden.h"

#include <cmath>
#include <limits>

using namespace pxs::core::midi;

namespace {

void goldenBuilders()
{
    for (const auto &g : golden::lines("midibuild")) {
        // midibuild  kind(0 cc, 1 note on, 2 note off, 3 pitch bend, 4 program)  channel  number  value  bytes
        const int channel = std::stoi(g[2]), number = std::stoi(g[3]);
        const double value = std::stod(g[4]);
        Bytes b;
        switch (std::stoi(g[1])) {
        case 0: b = controlChange(channel, number, value); break;
        case 1: b = noteOn(channel, number, value); break;
        case 2: b = noteOff(channel, number); break;
        case 3: b = pitchBend(channel, value); break;
        default: b = programChange(channel, number); break;
        }
        CHECK_EQ(golden::hex(b), g[5]);
    }
}

void goldenParser()
{
    for (const auto &g : golden::lines("midiparse")) {
        // midiparse  bytes  ok  kind  channel  number  value
        const Bytes in = golden::unhex(g[1]);
        Message m{Kind::PitchBend, 9, 99, 0.123}; // a sentinel: a refused message must leave it alone
        const Message before = m;
        const bool ok = parse(in.empty() ? nullptr : in.data(), in.size(), m);
        CHECK_EQ(ok, g[2] == "1");
        if (ok) {
            CHECK_EQ(int(m.kind), std::stoi(g[3]));
            CHECK_EQ(m.channel, std::stoi(g[4]));
            CHECK_EQ(m.number, std::stoi(g[5]));
            CHECK_EQ(m.value, std::stod(g[6])); // the same arithmetic: equal, not nearly
        } else {
            CHECK(m.kind == before.kind && m.channel == before.channel && m.number == before.number && m.value == before.value);
        }
    }
}

void builtIsReadBack()
{
    for (int channel = 1; channel <= 16; ++channel) {
        for (int number : {0, 1, 60, 127}) {
            Message m;
            const Bytes cc = controlChange(channel, number, 0.5);
            CHECK(parse(cc.data(), cc.size(), m) && m.kind == Kind::ControlChange && m.channel == channel && m.number == number);
            const Bytes on = noteOn(channel, number, 1.0);
            CHECK(parse(on.data(), on.size(), m) && m.kind == Kind::Note && m.value == 1.0);
            const Bytes off = noteOff(channel, number);
            CHECK(parse(off.data(), off.size(), m) && m.kind == Kind::Note && m.value == 0.0);
            const Bytes pc = programChange(channel, number);
            CHECK(parse(pc.data(), pc.size(), m) && m.kind == Kind::ProgramChange && m.number == number && m.value == 1.0);
        }
        Message m;
        const Bytes bend = pitchBend(channel, 0.5);
        CHECK(parse(bend.data(), bend.size(), m) && m.kind == Kind::PitchBend && m.number == 0 && std::abs(m.value - 0.5) < 1e-4);
    }
}

void notesAndRanges()
{
    const std::uint8_t quiet[] = {0x90, 60, 0}; // a note on at velocity 0 is an off
    Message m;
    CHECK(parse(quiet, 3, m) && m.kind == Kind::Note && m.value == 0.0);
    CHECK_EQ(golden::hex(controlChange(99, 500, 5.0)), std::string("bf7f7f")); // held within MIDI's ranges
    CHECK_EQ(golden::hex(controlChange(-4, -9, -1.0)), std::string("b00000"));
}

void notANumberIsZero()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    CHECK_EQ(golden::hex(controlChange(1, 7, nan)), std::string("b00700"));
    CHECK_EQ(golden::hex(noteOn(1, 60, nan)), std::string("903c00"));
    CHECK_EQ(golden::hex(pitchBend(1, nan)), std::string("e00000"));
    CHECK_EQ(golden::hex(controlChange(1, 7, inf)), std::string("b0077f"));
    CHECK_EQ(golden::hex(controlChange(1, 7, -inf)), std::string("b00700"));
    CHECK_EQ(golden::hex(pitchBend(1, inf)), std::string("e07f7f"));
}

void everyStatusByteIsHandled()
{
    // Whatever comes from a port, nothing but the four messages is read, and nothing is read out of bounds.
    for (int first = 0; first < 256; ++first) {
        for (int a = 0; a < 256; a += 51) {
            for (int b = 0; b < 256; b += 51) {
                const std::uint8_t bytes[3] = {static_cast<std::uint8_t>(first), static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(b)};
                for (std::size_t n = 0; n <= 3; ++n) {
                    Message m;
                    const bool ok = parse(bytes, n, m);
                    const int status = first & 0xF0;
                    const bool known = status == 0x80 || status == 0x90 || status == 0xB0 || status == 0xE0 || status == 0xC0;
                    const std::size_t need = status == 0xC0 ? 2 : 3;
                    CHECK_EQ(ok, known && n >= need);
                    if (ok) CHECK(m.channel >= 1 && m.channel <= 16 && m.number >= 0 && m.number <= 127 && m.value >= 0.0 && m.value <= 1.0);
                }
            }
        }
    }
}

} // namespace

int main()
{
    goldenBuilders();
    goldenParser();
    builtIsReadBack();
    notesAndRanges();
    notANumberIsZero();
    everyStatusByteIsHandled();
    return pxs_test::finish("midi");
}
