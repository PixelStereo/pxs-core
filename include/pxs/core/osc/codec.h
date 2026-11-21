#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace pxs::core::osc {

using Bytes = std::vector<std::uint8_t>;
using Blob = Bytes;
using Value = std::variant<std::monostate, bool, std::int32_t, std::int64_t, float, double,
                           std::string, Blob, std::uint32_t>;

struct Message {
    std::string address;
    std::string types;
    std::vector<Value> args;
};

// Encode one OSC message. `types` omits the leading comma.
Bytes encode(const Message &message);

// Decode a message or bundle, flattening nested bundles. Malformed or oversized packets are refused.
bool decode(const Bytes &packet, std::vector<Message> &out);

} // namespace pxs::core::osc
