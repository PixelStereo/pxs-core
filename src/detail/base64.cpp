#include "detail/base64.h"

namespace pxs::core::detail {

std::string base64(const std::uint8_t *data, std::size_t size)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    for (std::size_t i = 0; i < size; i += 3) {
        const std::size_t left = size - i;
        const std::uint32_t v = (std::uint32_t(data[i]) << 16) | (left > 1 ? std::uint32_t(data[i + 1]) << 8 : 0u) |
                                (left > 2 ? std::uint32_t(data[i + 2]) : 0u);
        out.push_back(alphabet[(v >> 18) & 63]);
        out.push_back(alphabet[(v >> 12) & 63]);
        out.push_back(left > 1 ? alphabet[(v >> 6) & 63] : '=');
        out.push_back(left > 2 ? alphabet[v & 63] : '=');
    }
    return out;
}

} // namespace pxs::core::detail
