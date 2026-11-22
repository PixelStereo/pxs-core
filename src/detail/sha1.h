#pragma once
// SHA-1 (FIPS 180-4), for the WebSocket handshake only: it is not a security primitive here (RFC 6455 uses it to prove a
// server read the request, nothing more).
#include <array>
#include <cstddef>
#include <cstdint>

namespace pxs::core::detail {
std::array<std::uint8_t, 20> sha1(const std::uint8_t *data, std::size_t size);
}
