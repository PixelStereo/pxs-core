#include <pxs/core/osc/codec.h>
#include <pxs/core/osc/names.h>

#include <cstdlib>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    using namespace pxs::core::osc;
    std::vector<Message> out;
    if (decode(data, size, out)) {
        for (const auto &m : out) { // what was decoded is sent again and must read back
            std::vector<Message> again;
            if (!decode(encode(m), again) || again.size() != 1 || again[0].address != m.address || again[0].types != m.types) std::abort();
        }
    }
    // the same bytes as two patterns: the matcher must stay bounded whatever they are
    const std::size_t half = size / 2;
    (void)match(std::string_view(reinterpret_cast<const char *>(data), half), std::string_view(reinterpret_cast<const char *>(data) + half, size - half));
    (void)safeName(std::string_view(reinterpret_cast<const char *>(data), size));
    return 0;
}
