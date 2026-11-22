#include <pxs/core/midi/message.h>

#include <algorithm>
#include <cmath>

namespace pxs::core::midi {
namespace {

std::uint8_t data7(double v)
{
    if (!(v > 0.0)) return 0; // negative, zero or not a number
    if (v >= 1.0) return 127;
    return static_cast<std::uint8_t>(std::lround(v * 127.0));
}
int data14(double v)
{
    if (!(v > 0.0)) return 0;
    if (v >= 1.0) return 16383;
    return static_cast<int>(std::lround(v * 16383.0));
}
std::uint8_t channelByte(int channel) { return static_cast<std::uint8_t>(std::clamp(channel, 1, 16) - 1); }
std::uint8_t number7(int n) { return static_cast<std::uint8_t>(std::clamp(n, 0, 127)); }

} // namespace

bool parse(const std::uint8_t *m, std::size_t n, Message &out)
{
    if (!m || n < 2) return false;
    const int status = m[0] & 0xF0;
    Message e;
    switch (status) {
    case 0xB0:
        if (n < 3) return false;
        e.kind = Kind::ControlChange;
        e.number = m[1] & 0x7F;
        e.value = (m[2] & 0x7F) / 127.0;
        break;
    case 0x90:
    case 0x80:
        if (n < 3) return false;
        e.kind = Kind::Note;
        e.number = m[1] & 0x7F;
        e.value = status == 0x90 ? (m[2] & 0x7F) / 127.0 : 0.0; // a note on at velocity 0 is an off
        break;
    case 0xE0:
        if (n < 3) return false;
        e.kind = Kind::PitchBend;
        e.number = 0;
        e.value = (((m[2] & 0x7F) << 7) | (m[1] & 0x7F)) / 16383.0;
        break;
    case 0xC0:
        e.kind = Kind::ProgramChange;
        e.number = m[1] & 0x7F;
        e.value = 1.0;
        break;
    default: return false;
    }
    e.channel = (m[0] & 0x0F) + 1;
    out = e;
    return true;
}

Bytes controlChange(int channel, int controller, double value)
{
    return {static_cast<std::uint8_t>(0xB0 | channelByte(channel)), number7(controller), data7(value)};
}
Bytes noteOn(int channel, int note, double velocity)
{
    return {static_cast<std::uint8_t>(0x90 | channelByte(channel)), number7(note), data7(velocity)};
}
Bytes noteOff(int channel, int note) { return {static_cast<std::uint8_t>(0x80 | channelByte(channel)), number7(note), 0}; }
Bytes pitchBend(int channel, double value)
{
    const int v = data14(value);
    return {static_cast<std::uint8_t>(0xE0 | channelByte(channel)), static_cast<std::uint8_t>(v & 0x7F),
            static_cast<std::uint8_t>(v >> 7)};
}
Bytes programChange(int channel, int program) { return {static_cast<std::uint8_t>(0xC0 | channelByte(channel)), number7(program)}; }

} // namespace pxs::core::midi
