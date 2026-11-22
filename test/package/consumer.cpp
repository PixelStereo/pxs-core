#include <pxs/c/pxs.h>
#include <pxs/core/dmx/frame.h>
#include <pxs/core/feed/change_feed.h>
#include <pxs/core/midi/message.h>
#include <pxs/core/tree/oscquery.h>
#include <pxs/core/tree/oscquery_server.h>
#include <pxs/core/tree/preset.h>
#include <pxs/core/tree/tree.h>
#include <pxs/core/value.h>
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
    const bool value = pxs::core::Value("0.5").number() == 0.5;
    pxs::core::tree::Tree tree;
    tree.addSignal("/beat", "f");
    const bool treeOk = tree.find("/beat") != nullptr && tree.fire("/beat", {pxs::core::Value(1.0f)});
    const bool json = pxs::core::tree::describeJson(tree, "/beat").find("\"FULL_PATH\":\"/beat\"") != std::string::npos &&
                      pxs::core::tree::Preset::fromJson(pxs::core::tree::capture(tree).toJson()).isEmpty();
    const bool http = pxs::core::tree::answerRequest(tree, pxs::core::tree::ServerInfo{}, "GET", "/beat").status == 200;
    const bool c = pxs_api_version() == PXS_C_API_VERSION;
    return osc && ws && dmx && midi && quiet && value && treeOk && json && http && c ? 0 : 1;
}
