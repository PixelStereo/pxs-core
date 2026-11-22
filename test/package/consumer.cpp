#include <pxs/core/dmx/frame.h>
#include <pxs/core/osc/codec.h>
#include <pxs/core/websocket/frame.h>

int main()
{
    pxs::core::osc::Message m{"/hello", "i", {std::int32_t(1)}};
    std::vector<pxs::core::osc::Message> out;
    const bool osc = pxs::core::osc::decode(pxs::core::osc::encode(m), out) && out.size() == 1;
    const bool ws = pxs::core::websocket::acceptKey("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";
    pxs::core::dmx::Frame f;
    const bool dmx = !pxs::core::dmx::encode(f).empty();
    return osc && ws && dmx ? 0 : 1;
}
