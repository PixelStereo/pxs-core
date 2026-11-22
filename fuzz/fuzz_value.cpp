#include <pxs/core/value.h>

#include <cmath>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    using namespace pxs::core;
    const std::string_view text(reinterpret_cast<const char *>(data), size);
    const auto n = parseNumber(text);
    if (!n) return 0;
    if (std::isnan(*n)) return 0;
    // what was read prints, and reads back as the same number
    const auto back = parseNumber(formatDouble(*n));
    if (!back || *back != *n) std::abort();
    return 0;
}
