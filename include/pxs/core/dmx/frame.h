#pragma once
// DMX over the network: the frames of Art-Net (DMX data, OpDmx) and sACN (ANSI E1.31), encoded and decoded, with no
// dependency beyond the C++17 standard library. The sockets, the merging of sources and what a channel drives are the
// application's.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pxs::core::dmx {

inline constexpr int kChannelCount = 512;
inline constexpr std::uint16_t kArtNetPort = 6454;
inline constexpr std::uint16_t kSacnPort = 5568;

using Bytes = std::vector<std::uint8_t>;

struct Frame {
    bool sacn = false;                 // sACN, else Art-Net
    int universe = 0;                  // Art-Net: 0..32767 (zero based); sACN: 1..63999
    int priority = 100;                // sACN: 0..200
    std::uint8_t sequence = 0;
    Bytes cid;                         // sACN: the sender's 16-byte id (cut or zero-padded to 16 when encoded)
    Bytes values;                      // the levels (512 after a decode)
    int length = kChannelCount;        // channels in the packet, 1..512 (Art-Net: even, at least 2): fewer is a partial frame
    std::uint8_t startCode = 0;        // sACN: 0 the levels, 0xDD a priority for each channel (encode only; decode takes levels)
    std::string name;                  // sACN: the source's name, UTF-8 (at most 63 bytes are sent, read back up to the first NUL)
    bool terminated = false;           // sACN: the source stops sending this universe
};

// The packet of a frame; empty when the universe or the priority is out of range. `length` channels are sent (the values
// are padded with zeros, or cut); a decoded frame says in `length` how many the packet carried.
Bytes encode(const Frame &frame);

// What a packet carries, checked in full (lengths, flags, vectors, ranges); false for anything that is not DMX of that
// protocol, and `frame` is then a default frame of that protocol (nothing of the packet is kept). `sacn` says which protocol to expect: the port it arrived on tells it.
bool decode(const std::uint8_t *data, std::size_t size, bool sacn, Frame &frame);
bool decode(const Bytes &packet, bool sacn, Frame &frame);

// Where an sACN universe is sent: the multicast group 239.255.<high>.<low>
std::string sacnGroup(int universe);

} // namespace pxs::core::dmx
