#pragma once
// The frames of a WebSocket (RFC 6455) as a small server needs them: text, binary and control frames, no fragmentation,
// no extensions. A client's frames are masked, a server's are not. No dependency beyond the C++17 standard library.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pxs::core::websocket {

using Bytes = std::vector<std::uint8_t>;

enum Opcode : int { Continuation = 0, Text = 1, Binary = 2, Close = 8, Ping = 9, Pong = 10 };

// The Sec-WebSocket-Accept of a handshake, from the client's Sec-WebSocket-Key (the value of the header, as received).
std::string acceptKey(std::string_view key);

// A server frame to send: FIN set, not masked. `opcode` is one of Opcode (its low four bits are used).
Bytes encode(int opcode, const std::uint8_t *payload, std::size_t size);
Bytes encode(int opcode, const Bytes &payload);

enum class DecodeStatus {
    Frame,    // a frame was read: `consumed` bytes are its
    NeedMore, // the bytes so far are the start of a frame: wait for more
    Drop      // the peer broke the protocol: close the connection
};

struct Frame {
    int opcode = Text;
    Bytes payload; // unmasked
};

// Reads the first frame of `data` (a client's frame). Drop for: a frame whose payload is above `maxPayload`, a fragmented
// message (FIN clear, or a continuation frame), an unmasked frame, reserved bits set (no extension is negotiated), an
// opcode RFC 6455 does not define, and a control frame (Close, Ping, Pong) of more than 125 bytes. Where the header alone
// is enough to tell a frame is refused, it is, without waiting for the payload.
DecodeStatus decode(const std::uint8_t *data, std::size_t size, std::uint64_t maxPayload, Frame &frame,
                    std::size_t &consumed);

// The same on a receive buffer: the frame read is taken out of its front.
DecodeStatus decode(Bytes &buffer, std::uint64_t maxPayload, Frame &frame);

} // namespace pxs::core::websocket
