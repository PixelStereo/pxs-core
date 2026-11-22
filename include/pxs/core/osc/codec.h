#pragma once
// OSC 1.0 packets: encoding, decoding and address-pattern matching, with no dependency beyond the C++17 standard library.
//
// Packets come from the network, so decoding treats every length as hostile: it checks each one against the room left,
// bounds the work and the memory (see the limits below) and refuses what is malformed. Nothing here throws, allocates
// without a bound set by the input size and the limits, or reads outside the buffer it is given.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace pxs::core::osc {

using Bytes = std::vector<std::uint8_t>;
using Blob = Bytes;

// An argument. Which alternative a tag decodes to:
//   i -> int32      h, t -> int64 (t: the time tag's 64 bits, as they come)      f -> float      d -> double
//   s, S -> string  b -> Blob            c, r, m -> uint32 (a character, a color, a MIDI message)
//   T -> true       F -> false           N, I, '[' , ']' and any unknown tag -> monostate (no data is read for them)
using Value = std::variant<std::monostate, bool, std::int32_t, std::int64_t, float, double, std::string, Blob,
                           std::uint32_t>;

struct Message {
    std::string address;
    // The type tags, without the leading ','. One byte is one tag, whatever its value: a byte that is not a tag this
    // library knows is an unknown tag (a nil argument that carries no data), so `types.size() == args.size()` always
    // holds for a decoded message. The tags are ASCII in OSC 1.0.
    std::string types;
    std::vector<Value> args;
};

// The limits of a packet from the network. A packet beyond one of them is refused, never truncated.
inline constexpr std::size_t kMaxPacketBytes = std::size_t(1) << 20;
inline constexpr std::size_t kMaxArguments = 1024;        // type tags in one message
inline constexpr int kMaxBundleDepth = 8;                 // bundles inside bundles
inline constexpr std::size_t kMaxMessagesPerPacket = 1024; // messages decoded from one packet, bundles flattened
inline constexpr std::size_t kMaxPatternChars = 8192;     // characters of a pattern or of an address to match
inline constexpr std::size_t kMaxSegmentChars = 4096;     // characters of one '/'-separated part of those

// The bytes of one message (not a bundle). `message.types` is the authority: one argument is written for each tag, in
// order. An argument that is missing, or whose alternative is not the one the tag decodes to (see `Value`), is written
// as that tag's zero (0, 0.0, an empty string or blob). Strings are written as they are: a string that holds a NUL
// byte reads back cut at it. A blob is cut at 2^31-1 bytes, the most its length field holds.
Bytes encode(const Message &message);
// The same, appended to `out` (to build a packet without a copy).
void encode(const Message &message, Bytes &out);

// Decodes a message or a bundle (nested bundles flattened, in order), appending to `out`.
// Returns false for a malformed packet, an empty one, or one beyond a limit above; `out` is then left as it was
// (all or nothing). The limit on messages counts the ones this call decodes, not what `out` already held.
bool decode(const std::uint8_t *data, std::size_t size, std::vector<Message> &out);
bool decode(const Bytes &packet, std::vector<Message> &out);

// OSC 1.0 address-pattern matching, one '/'-separated part at a time (both must have as many): `*` any run of
// characters, `?` one character, `[abc]` `[a-z]` `[!a-z]` a set, `{foo,bar}` one of the alternatives. A pattern that is
// malformed (a '[' or '{' never closed) matches nothing, and so does an input beyond kMaxPatternChars or a part beyond
// kMaxSegmentChars.
//
// Both strings are UTF-8 and a character is a Unicode code point (so `?` matches an emoji whole). A byte that is not
// valid UTF-8 counts as one character of its own that no other byte, and no valid character, is equal to.
// The time is O(pattern x address) whatever the pattern: it cannot be made exponential by a run of `*`.
bool match(std::string_view pattern, std::string_view address);

} // namespace pxs::core::osc
