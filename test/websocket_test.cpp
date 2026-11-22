#include <pxs/core/websocket/frame.h>

#include "check.h"
#include "golden.h"

#include <algorithm>
#include <limits>

using namespace pxs::core::websocket;

namespace {

Bytes clientFrame(int opcode, const Bytes &payload, bool fin = true, bool masked = true, int rsv = 0)
{
    Bytes f;
    f.push_back(static_cast<std::uint8_t>((fin ? 0x80 : 0) | rsv | opcode));
    const std::uint8_t maskBit = masked ? 0x80 : 0;
    const std::size_t n = payload.size();
    if (n < 126) {
        f.push_back(static_cast<std::uint8_t>(maskBit | n));
    } else if (n < 65536) {
        f.push_back(static_cast<std::uint8_t>(maskBit | 126));
        f.push_back(static_cast<std::uint8_t>(n >> 8));
        f.push_back(static_cast<std::uint8_t>(n));
    } else {
        f.push_back(static_cast<std::uint8_t>(maskBit | 127));
        for (int s = 56; s >= 0; s -= 8) f.push_back(static_cast<std::uint8_t>(std::uint64_t(n) >> s));
    }
    const std::uint8_t mask[4] = {0x37, 0xfa, 0x21, 0x3d};
    if (masked) f.insert(f.end(), mask, mask + 4);
    for (std::size_t i = 0; i < n; ++i) f.push_back(masked ? static_cast<std::uint8_t>(payload[i] ^ mask[i & 3]) : payload[i]);
    return f;
}

Bytes bytes(const std::string &s) { return Bytes(s.begin(), s.end()); }

void goldenAcceptKeys()
{
    for (const auto &g : golden::lines("wskey")) CHECK_EQ(acceptKey(g[1]), g[2]);
    // RFC 6455 section 1.3
    CHECK_EQ(acceptKey("dGhlIHNhbXBsZSBub25jZQ=="), std::string("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
}

void goldenEncode()
{
    for (const auto &g : golden::lines("wsenc")) {
        const int opcode = std::stoi(g[1]);
        const std::size_t n = std::stoul(g[2]);
        Bytes payload(n);
        for (std::size_t i = 0; i < n; ++i) payload[i] = static_cast<std::uint8_t>(i * 7 + 3);
        const Bytes f = encode(opcode, payload);
        CHECK_EQ(golden::hex(f.data(), std::min<std::size_t>(10, f.size())), g[3]);
        CHECK(f.size() >= n);
        CHECK(n == 0 || std::equal(payload.begin(), payload.end(), f.end() - std::ptrdiff_t(n)));
    }
}

void goldenDecode()
{
    for (const auto &g : golden::lines("wsdec")) {
        // wsdec  frame(hex)  maxPayload  status(1 frame, 0 more, -1 drop)  opcode  payload(hex)  consumed
        const Bytes input = golden::unhex(g[1]);
        const std::uint64_t max = std::stoull(g[2]);
        const int status = std::stoi(g[3]);
        Frame frame;
        std::size_t consumed = 0;
        const DecodeStatus got = decode(input.data(), input.size(), max, frame, consumed);
        CHECK_EQ(int(got == DecodeStatus::Frame ? 1 : got == DecodeStatus::NeedMore ? 0 : -1), status);
        if (status == 1) {
            CHECK_EQ(frame.opcode, std::stoi(g[4]));
            CHECK_EQ(golden::hex(frame.payload), g[5]);
            CHECK_EQ(consumed, std::stoul(g[6]));
        } else {
            CHECK_EQ(consumed, std::size_t(0));
        }
        // the receive-buffer form takes the frame out of the buffer
        Bytes buffer = input;
        Frame again;
        const DecodeStatus status2 = decode(buffer, max, again);
        CHECK(status2 == got);
        CHECK_EQ(input.size() - buffer.size(), consumed);
    }
}

void roundTripThroughAClient()
{
    for (const std::size_t n : {std::size_t(0), std::size_t(1), std::size_t(125), std::size_t(126), std::size_t(65535), std::size_t(65536), std::size_t(70000)}) {
        Bytes payload(n);
        for (std::size_t i = 0; i < n; ++i) payload[i] = static_cast<std::uint8_t>(i * 13 + 1);
        Bytes buffer = clientFrame(Binary, payload);
        buffer.push_back(0x81); // the start of the next frame stays in the buffer
        Frame frame;
        CHECK(decode(buffer, 1 << 20, frame) == DecodeStatus::Frame);
        CHECK_EQ(frame.opcode, int(Binary));
        CHECK(frame.payload == payload);
        CHECK_EQ(buffer.size(), std::size_t(1));
    }
}

void incrementalDelivery()
{
    // Fed one byte at a time, the frame is read exactly when its last byte arrives.
    const Bytes whole = clientFrame(Text, bytes(std::string(300, 'q')));
    Bytes buffer;
    for (std::size_t i = 0; i < whole.size(); ++i) {
        buffer.push_back(whole[i]);
        Frame frame;
        const DecodeStatus s = decode(buffer, 1000, frame);
        CHECK(s == (i + 1 == whole.size() ? DecodeStatus::Frame : DecodeStatus::NeedMore));
    }
    CHECK(buffer.empty());
}

void protocolViolationsDropThePeer()
{
    Frame frame;
    std::size_t consumed = 0;
    const auto status = [&](const Bytes &b, std::uint64_t max = 1000) { return decode(b.data(), b.size(), max, frame, consumed); };

    CHECK(status(clientFrame(Text, bytes("x"), /*fin*/ false)) == DecodeStatus::Drop);          // fragmented
    CHECK(status(clientFrame(Continuation, bytes("x"))) == DecodeStatus::Drop);                  // continuation
    CHECK(status(clientFrame(Text, bytes("x"), true, /*masked*/ false)) == DecodeStatus::Drop);  // unmasked
    CHECK(status(clientFrame(Text, bytes("x"), true, true, 0x40)) == DecodeStatus::Drop);        // RSV1 (no extension negotiated)
    CHECK(status(clientFrame(Text, bytes("x"), true, true, 0x20)) == DecodeStatus::Drop);
    CHECK(status(clientFrame(Text, bytes("x"), true, true, 0x10)) == DecodeStatus::Drop);
    for (const int op : {3, 4, 5, 6, 7, 11, 12, 13, 14, 15}) CHECK(status(clientFrame(op, bytes("x"))) == DecodeStatus::Drop);
    CHECK(status(clientFrame(Ping, Bytes(125, 1))) == DecodeStatus::Frame);                      // a control frame: 125 bytes at most
    CHECK(status(clientFrame(Ping, Bytes(126, 1))) == DecodeStatus::Drop);
    CHECK(status(clientFrame(Close, Bytes(200, 1))) == DecodeStatus::Drop);
    CHECK(status(clientFrame(Text, Bytes(1001, 1))) == DecodeStatus::Drop);                      // above the limit
    CHECK(status(clientFrame(Text, Bytes(1000, 1))) == DecodeStatus::Frame);
}

void refusedFromTheHeaderAlone()
{
    // The peer must not be able to keep a connection open with a prefix that can never become a valid frame.
    Frame frame;
    std::size_t consumed = 0;
    const Bytes unmasked{0x81, 0x05};
    CHECK(decode(unmasked.data(), unmasked.size(), 1000, frame, consumed) == DecodeStatus::Drop);
    const Bytes rsv{0xC1, 0x85};
    CHECK(decode(rsv.data(), rsv.size(), 1000, frame, consumed) == DecodeStatus::Drop);
    const Bytes hugeUnread{0x81, 0xFF, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // a 64-bit length, before the mask
    CHECK(decode(hugeUnread.data(), hugeUnread.size(), 1000, frame, consumed) == DecodeStatus::Drop);
}

void hugeLengthsDoNotOverflow()
{
    Frame frame;
    std::size_t consumed = 0;
    Bytes huge{0x81, 0xFF};
    for (int i = 0; i < 8; ++i) huge.push_back(0xFF); // length 2^64-1
    for (int i = 0; i < 4; ++i) huge.push_back(0);    // the mask
    const auto max = std::numeric_limits<std::uint64_t>::max();
    // with a limit that allows it, the frame is simply not there yet: no wrap-around, no allocation
    CHECK(decode(huge.data(), huge.size(), max, frame, consumed) == DecodeStatus::NeedMore);
    CHECK(decode(huge.data(), huge.size(), 1 << 20, frame, consumed) == DecodeStatus::Drop);
}

void twoFramesInOneBuffer()
{
    Bytes buffer = clientFrame(Text, bytes("one"));
    const Bytes second = clientFrame(Text, bytes("two"));
    buffer.insert(buffer.end(), second.begin(), second.end());
    Frame frame;
    CHECK(decode(buffer, 1000, frame) == DecodeStatus::Frame);
    CHECK(frame.payload == bytes("one"));
    CHECK(decode(buffer, 1000, frame) == DecodeStatus::Frame);
    CHECK(frame.payload == bytes("two"));
    CHECK(buffer.empty());
    CHECK(decode(buffer, 1000, frame) == DecodeStatus::NeedMore);
}

} // namespace

int main()
{
    goldenAcceptKeys();
    goldenEncode();
    goldenDecode();
    roundTripThroughAClient();
    incrementalDelivery();
    protocolViolationsDropThePeer();
    refusedFromTheHeaderAlone();
    hugeLengthsDoNotOverflow();
    twoFramesInOneBuffer();
    return pxs_test::finish("websocket");
}
