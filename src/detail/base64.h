#pragma once
// Base64 (RFC 4648, with padding), for the WebSocket handshake.
#include <cstddef>
#include <cstdint>
#include <string>

namespace pxs::core::detail {
std::string base64(const std::uint8_t *data, std::size_t size);
}
