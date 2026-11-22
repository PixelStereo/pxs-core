#include <pxs/core/dmx/frame.h>

#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    using namespace pxs::core::dmx;
    for (const bool sacn : {false, true}) {
        Frame f;
        if (!decode(data, size, sacn, f)) continue;
        Frame again;
        if (!decode(encode(f), sacn, again) || again.universe != f.universe || again.values != f.values) std::abort();
    }
    return 0;
}
