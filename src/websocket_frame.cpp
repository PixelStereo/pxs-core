#include <pxs/core/websocket/frame.h>

#include "detail/base64.h"
#include "detail/sha1.h"

#include <algorithm>

namespace pxs::core::websocket {

std::string acceptKey(std::string_view key)
{
    static constexpr std::string_view guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string joined(key);
    joined += guid;
    const auto digest = detail::sha1(reinterpret_cast<const std::uint8_t *>(joined.data()), joined.size());
    return detail::base64(digest.data(), digest.size());
}

Bytes encode(int opcode, const std::uint8_t *payload, std::size_t size)
{
    Bytes f;
    f.reserve(size + 10);
    f.push_back(static_cast<std::uint8_t>(0x80 | (opcode & 0x0F)));
    if (size < 126) {
        f.push_back(static_cast<std::uint8_t>(size));
    } else if (size < 65536) {
        f.push_back(126);
        f.push_back(static_cast<std::uint8_t>(size >> 8));
        f.push_back(static_cast<std::uint8_t>(size));
    } else {
        f.push_back(127);
        for (int shift = 56; shift >= 0; shift -= 8) f.push_back(static_cast<std::uint8_t>(std::uint64_t(size) >> shift));
    }
    if (size) f.insert(f.end(), payload, payload + size);
    return f;
}

Bytes encode(int opcode, const Bytes &payload) { return encode(opcode, payload.data(), payload.size()); }

DecodeStatus decode(const std::uint8_t *d, std::size_t size, std::uint64_t maxPayload, Frame &frame, std::size_t &consumed)
{
    consumed = 0;
    if (size < 2) return DecodeStatus::NeedMore;
    const bool fin = (d[0] & 0x80) != 0;
    const int rsv = d[0] & 0x70;
    const int opcode = d[0] & 0x0F;
    const bool masked = (d[1] & 0x80) != 0;
    std::uint64_t length = d[1] & 0x7F;
    std::size_t pos = 2;
    // Everything the first two bytes decide is checked before waiting for more: a peer must not be able to keep a
    // connection open with a prefix that can never become a valid frame.
    const bool control = opcode >= Close;
    const bool defined = opcode == Continuation || opcode == Text || opcode == Binary || opcode == Close || opcode == Ping ||
                         opcode == Pong;
    if (rsv != 0 || !defined || opcode == Continuation || !fin || !masked) return DecodeStatus::Drop;
    if (control && length > 125) return DecodeStatus::Drop;
    if (length == 126) {
        if (size < 4) return DecodeStatus::NeedMore;
        length = (std::uint64_t(d[2]) << 8) | d[3];
        pos = 4;
    } else if (length == 127) {
        if (size < 10) return DecodeStatus::NeedMore;
        length = 0;
        for (std::size_t i = 0; i < 8; ++i) length = (length << 8) | d[2 + i];
        pos = 10;
    }
    if (length > maxPayload) return DecodeStatus::Drop;
    const std::size_t maskAt = pos;
    pos += 4;
    if (size < pos || std::uint64_t(size - pos) < length) return DecodeStatus::NeedMore; // no pos + length that could overflow
    const std::size_t n = static_cast<std::size_t>(length);
    frame.opcode = opcode;
    frame.payload.resize(n);
    for (std::size_t k = 0; k < n; ++k) frame.payload[k] = static_cast<std::uint8_t>(d[pos + k] ^ d[maskAt + (k & 3)]);
    consumed = pos + n;
    return DecodeStatus::Frame;
}

DecodeStatus decode(Bytes &buffer, std::uint64_t maxPayload, Frame &frame)
{
    std::size_t consumed = 0;
    const DecodeStatus status = decode(buffer.data(), buffer.size(), maxPayload, frame, consumed);
    if (status == DecodeStatus::Frame) buffer.erase(buffer.begin(), buffer.begin() + std::ptrdiff_t(consumed));
    return status;
}

} // namespace pxs::core::websocket
