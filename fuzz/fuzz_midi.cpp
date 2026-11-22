#include <pxs/core/midi/message.h>

#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    using namespace pxs::core::midi;
    Message m;
    if (!parse(data, size, m)) return 0;
    if (m.channel < 1 || m.channel > 16 || m.number < 0 || m.number > 127 || !(m.value >= 0.0 && m.value <= 1.0)) std::abort();
    return 0;
}
