#include <pxs/core/dmx/frame.h>
#include <pxs/core/feed/change_feed.h>
#include <pxs/core/midi/message.h>
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
    pxs::core::midi::Message note;
    const auto cc = pxs::core::midi::controlChange(1, 7, 1.0);
    const bool midi = pxs::core::midi::parse(cc.data(), cc.size(), note) && note.number == 7;
    pxs::core::ChangeFeed feed;
    const bool quiet = !feed.listening();
    return osc && ws && dmx && midi && quiet ? 0 : 1;
}
