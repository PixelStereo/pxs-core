#include <pxs/core/websocket/frame.h>

#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    using namespace pxs::core::websocket;
    Frame frame;
    std::size_t consumed = 0;
    const DecodeStatus s = decode(data, size, 1 << 20, frame, consumed);
    if (consumed > size || (s != DecodeStatus::Frame && consumed != 0)) std::abort();
    (void)acceptKey(std::string_view(reinterpret_cast<const char *>(data), size));
    return 0;
}
