#include <pxs/core/dmx/frame.h>

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace pxs::core::dmx {
namespace {

int u8(const std::uint8_t *d, std::size_t i) { return d[i]; }
int be(const std::uint8_t *d, std::size_t i) { return (d[i] << 8) | d[i + 1]; }
void put(Bytes &d, std::size_t i, int v)
{
    d[i] = static_cast<std::uint8_t>(v >> 8);
    d[i + 1] = static_cast<std::uint8_t>(v);
}
// An E1.31 PDU: flags 0x7 and a length that runs to the end of the packet
bool pdu(const std::uint8_t *d, std::size_t size, std::size_t i)
{
    return (u8(d, i) & 0xf0) == 0x70 && std::size_t(be(d, i) & 0xfff) == size - i;
}
void place(Bytes &d, std::size_t at, const void *src, std::size_t n)
{
    if (n) std::memcpy(d.data() + at, src, n);
}

} // namespace

Bytes encode(const Frame &f)
{
    if (f.universe < (f.sacn ? 1 : 0) || f.universe > (f.sacn ? 63999 : 32767) ||
        (f.sacn && (f.priority < 0 || f.priority > 200)))
        return {};
    int n = std::clamp(f.length, 1, kChannelCount);
    if (!f.sacn) n = std::max(2, n + (n & 1)); // Art-Net carries an even number of channels
    const std::size_t count = std::size_t(n);
    Bytes values(count, 0);
    place(values, 0, f.values.data(), std::min(f.values.size(), count));

    if (!f.sacn) {
        Bytes d(18 + count, 0);
        place(d, 0, "Art-Net\0", 8);
        d[9] = 0x50;
        put(d, 10, 14);
        d[12] = f.sequence;
        d[14] = static_cast<std::uint8_t>(f.universe);
        d[15] = static_cast<std::uint8_t>(f.universe >> 8);
        put(d, 16, n);
        place(d, 18, values.data(), count);
        return d;
    }
    const int total = 126 + n; // the three PDUs' lengths follow from it
    Bytes d(std::size_t(total), 0);
    put(d, 0, 16);
    place(d, 4, "ASC-E1.17\0\0\0", 12);
    put(d, 16, 0x7000 | (total - 16));
    d[21] = 4;
    place(d, 22, f.cid.data(), std::min<std::size_t>(f.cid.size(), 16));
    put(d, 38, 0x7000 | (total - 38));
    d[43] = 2;
    place(d, 44, f.name.data(), std::min<std::size_t>(f.name.size(), 63));
    d[108] = static_cast<std::uint8_t>(f.priority);
    d[111] = f.sequence;
    d[112] = f.terminated ? 0x40 : 0;
    put(d, 113, f.universe);
    put(d, 115, 0x7000 | (total - 115));
    d[117] = 2;
    d[118] = 0xa1;
    put(d, 121, 1);
    put(d, 123, n + 1);
    d[125] = f.startCode;
    place(d, 126, values.data(), count);
    return d;
}

bool decode(const std::uint8_t *d, std::size_t size, bool sacn, Frame &f)
{
    f = Frame{};
    f.sacn = sacn;
    if (!d) return false;
    if (!sacn) {
        if (size < 18 || std::memcmp(d, "Art-Net\0", 8) != 0 || u8(d, 8) != 0 || u8(d, 9) != 0x50 || be(d, 10) < 14)
            return false;
        const int n = be(d, 16);
        if (n < 2 || n > 512 || n % 2 || size < 18 + std::size_t(n) || u8(d, 15) > 127) return false;
        f.universe = u8(d, 14) | (u8(d, 15) << 8);
        f.sequence = static_cast<std::uint8_t>(u8(d, 12));
        f.values.assign(d + 18, d + 18 + n);
        f.length = n;
    } else {
        if (size < 126 || be(d, 0) != 16 || be(d, 2) != 0 || std::memcmp(d + 4, "ASC-E1.17\0\0\0", 12) != 0 ||
            !pdu(d, size, 16) || be(d, 18) != 0 || be(d, 20) != 4 || !pdu(d, size, 38) || be(d, 40) != 0 ||
            be(d, 42) != 2 || !pdu(d, size, 115) || u8(d, 117) != 2 || u8(d, 118) != 0xa1 || be(d, 119) != 0 ||
            be(d, 121) != 1 || u8(d, 125) != 0)
            return false;
        const int n = be(d, 123);
        if (n < 1 || n > 513 || size != 125 + std::size_t(n) || be(d, 113) < 1 || be(d, 113) > 63999 || u8(d, 108) > 200 ||
            be(d, 109) != 0 || (u8(d, 112) & 0x80))
            return false;
        f.universe = be(d, 113);
        f.priority = u8(d, 108);
        f.sequence = static_cast<std::uint8_t>(u8(d, 111));
        f.terminated = (u8(d, 112) & 0x40) != 0;
        f.cid.assign(d + 22, d + 38);
        const auto *name = d + 44;
        const auto *nameEnd = static_cast<const std::uint8_t *>(std::memchr(name, 0, 64)); // up to the first NUL, 64 bytes at most
        f.name.assign(reinterpret_cast<const char *>(name), nameEnd ? std::size_t(nameEnd - name) : 64);
        f.values.assign(d + 126, d + 126 + (n - 1));
        f.length = n - 1;
    }
    f.values.resize(kChannelCount, 0);
    return true;
}

bool decode(const Bytes &packet, bool sacn, Frame &frame) { return decode(packet.data(), packet.size(), sacn, frame); }

std::string sacnGroup(int universe)
{
    return "239.255." + std::to_string(universe >> 8) + "." + std::to_string(universe & 255);
}

} // namespace pxs::core::dmx
