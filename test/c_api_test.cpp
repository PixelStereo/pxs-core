// The C interface, called as a C program would: nothing but pxs/c/pxs.h.
#include <pxs/c/pxs.h>

#include "check.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace {

pxs_value number(double d)
{
    pxs_value v{};
    v.kind = PXS_DOUBLE;
    v.number = d;
    return v;
}
pxs_value text(const char *s)
{
    pxs_value v{};
    v.kind = PXS_STRING;
    v.data = reinterpret_cast<const uint8_t *>(s);
    v.size = std::strlen(s);
    return v;
}
pxs_value whole(int64_t n)
{
    pxs_value v{};
    v.kind = PXS_INT32;
    v.integer = n;
    return v;
}

void general()
{
    CHECK_EQ(pxs_api_version(), PXS_C_API_VERSION);
    CHECK(std::strlen(pxs_version()) >= 5);
    pxs_free(nullptr); // fine
}

void oscRoundTrip()
{
    uint8_t *bytes = nullptr;
    size_t size = 0;
    const pxs_value args[] = {whole(7), number(0.5), text("h\xC3\xA9")};
    CHECK_EQ(pxs_osc_encode("/mix/level", "ifs", args, 3, &bytes, &size), PXS_OK);
    CHECK(bytes && size > 0 && size % 4 == 0);

    int status = 99;
    pxs_osc_packet *packet = pxs_osc_decode(bytes, size, &status);
    CHECK(packet && status == PXS_OK);
    CHECK_EQ(pxs_osc_packet_count(packet), size_t(1));
    CHECK_EQ(std::string(pxs_osc_message_address(packet, 0)), std::string("/mix/level"));
    CHECK_EQ(std::string(pxs_osc_message_types(packet, 0)), std::string("ifs"));
    CHECK_EQ(pxs_osc_message_argc(packet, 0), size_t(3));
    pxs_value v;
    CHECK(pxs_osc_message_arg(packet, 0, 0, &v) == PXS_OK && v.kind == PXS_INT32 && v.integer == 7);
    CHECK(pxs_osc_message_arg(packet, 0, 1, &v) == PXS_OK && v.kind == PXS_FLOAT && v.number == 0.5);
    CHECK(pxs_osc_message_arg(packet, 0, 2, &v) == PXS_OK && v.kind == PXS_STRING && std::string(reinterpret_cast<const char *>(v.data), v.size) == "h\xC3\xA9");
    CHECK(pxs_osc_message_arg(packet, 0, 3, &v) == PXS_ERR_ARGUMENT && pxs_osc_message_arg(packet, 1, 0, &v) == PXS_ERR_ARGUMENT);
    CHECK(pxs_osc_message_address(packet, 1) == nullptr && pxs_osc_message_types(packet, 1) == nullptr && pxs_osc_message_argc(packet, 1) == 0);
    pxs_osc_packet_free(packet);

    // refused packets: a status, never a crash
    CHECK(pxs_osc_decode(bytes, 3, &status) == nullptr && status == PXS_ERR_MALFORMED);
    CHECK(pxs_osc_decode(nullptr, 5, &status) == nullptr && status == PXS_ERR_ARGUMENT);
    CHECK(pxs_osc_decode(bytes, 0, nullptr) == nullptr); // a NULL status is fine
    pxs_free(bytes);

    uint8_t *none = nullptr;
    size_t n = 0;
    CHECK_EQ(pxs_osc_encode(nullptr, "", nullptr, 0, &none, &n), PXS_ERR_ARGUMENT);
    CHECK_EQ(pxs_osc_encode("/a", "i", nullptr, 1, &none, &n), PXS_ERR_ARGUMENT);
    CHECK_EQ(pxs_osc_encode("/a", "", nullptr, 0, &none, &n), PXS_OK); // no argument, no value
    pxs_free(none);
}

struct Device {
    double level = 0.25;
    bool on = false;
    std::string mode = "a";
    std::string modeText;
    int goes = 0;
    int lastCue = -1;
    int resets = 0;
    std::vector<std::vector<uint8_t>> told;
};

int getLevel(void *u, pxs_value *out)
{
    *out = number(static_cast<Device *>(u)->level);
    return 0;
}
int setLevel(void *u, const pxs_value *in)
{
    static_cast<Device *>(u)->level = in->number;
    return 0;
}
int getOn(void *u, pxs_value *out)
{
    out->kind = PXS_BOOL;
    out->boolean = static_cast<Device *>(u)->on ? 1 : 0;
    return 0;
}
int setOn(void *u, const pxs_value *in)
{
    static_cast<Device *>(u)->on = in->boolean != 0;
    return 0;
}
int getMode(void *u, pxs_value *out)
{
    Device *d = static_cast<Device *>(u);
    out->kind = PXS_STRING;
    out->data = reinterpret_cast<const uint8_t *>(d->mode.c_str());
    out->size = d->mode.size();
    return 0;
}
int setMode(void *u, const pxs_value *in)
{
    static_cast<Device *>(u)->mode.assign(reinterpret_cast<const char *>(in->data), in->size);
    return 0;
}
int goTo(void *u, const pxs_value *args, size_t argc)
{
    Device *d = static_cast<Device *>(u);
    if (argc != 1) return 1;
    d->lastCue = int(args[0].kind == PXS_INT32 || args[0].kind == PXS_INT64 ? args[0].integer : int64_t(args[0].number));
    ++d->goes;
    return 0;
}
int reset(void *u, const pxs_value *)
{
    ++static_cast<Device *>(u)->resets;
    return 0;
}
void hear(void *u, const uint8_t *packet, size_t size) { static_cast<Device *>(u)->told.emplace_back(packet, packet + size); }

std::vector<uint8_t> message(const char *address, const char *types, const std::vector<pxs_value> &args)
{
    uint8_t *bytes = nullptr;
    size_t size = 0;
    CHECK_EQ(pxs_osc_encode(address, types, args.data(), args.size(), &bytes, &size), PXS_OK);
    std::vector<uint8_t> out(bytes, bytes + size);
    pxs_free(bytes);
    return out;
}

void theAddressSpace()
{
    Device d;
    pxs_tree *tree = pxs_tree_new();
    CHECK(tree != nullptr);
    pxs_tree_set_output(tree, hear, &d);
    const char *keys[] = {"a", "b", "c"};
    CHECK_EQ(pxs_tree_add_parameter(tree, "/mix/level", "Level", PXS_PARAM_FLOAT, 0, 1, nullptr, 0, getLevel, setLevel, &d), PXS_OK);
    CHECK_EQ(pxs_tree_add_parameter(tree, "/mix/on", "On", PXS_PARAM_BOOL, 0, 1, nullptr, 0, getOn, setOn, &d), PXS_OK);
    CHECK_EQ(pxs_tree_add_parameter(tree, "/osc/mode", "Mode", PXS_PARAM_CHOICE, 0, 0, keys, 3, getMode, setMode, &d), PXS_OK);
    CHECK_EQ(pxs_tree_add_parameter(tree, "/osc/reset", "Reset", PXS_PARAM_TRIGGER, 0, 0, nullptr, 0, nullptr, reset, &d), PXS_OK);
    CHECK_EQ(pxs_tree_add_action(tree, "/show/go", "i", goTo, &d), PXS_OK);
    CHECK_EQ(pxs_tree_add_signal(tree, "/detect/peak", "f"), PXS_OK);
    CHECK_EQ(pxs_tree_add_parameter(tree, "/x", "X", 99, 0, 1, nullptr, 0, nullptr, nullptr, nullptr), PXS_ERR_ARGUMENT);

    // OSC in: applied, held within the limits
    auto in = message("/mix/level", "f", {number(9.0)});
    CHECK_EQ(pxs_tree_apply_packet(tree, in.data(), in.size()), 1);
    CHECK_EQ(d.level, 1.0);
    in = message("/mix/level", "s", {text("loud")});
    CHECK_EQ(pxs_tree_apply_packet(tree, in.data(), in.size()), 0); // refused: not a number
    in = message("/mix/on", "T", {});
    CHECK_EQ(pxs_tree_apply_packet(tree, in.data(), in.size()), 1);
    in = message("/osc/mode", "s", {text("B")});
    CHECK_EQ(pxs_tree_apply_packet(tree, in.data(), in.size()), 1);
    CHECK_EQ(d.mode, std::string("b"));
    in = message("/osc/reset", "", {});
    CHECK_EQ(pxs_tree_apply_packet(tree, in.data(), in.size()), 1);
    CHECK_EQ(d.resets, 1);
    in = message("/show/go", "i", {whole(3)});
    CHECK_EQ(pxs_tree_apply_packet(tree, in.data(), in.size()), 1);
    CHECK(d.goes == 1 && d.lastCue == 3);
    in = message("/mix/*", "f", {number(0.5)});
    CHECK(pxs_tree_apply_packet(tree, in.data(), in.size()) >= 1); // a pattern
    const uint8_t garbage[] = {1, 2, 3};
    CHECK_EQ(pxs_tree_apply_packet(tree, garbage, 3), PXS_ERR_MALFORMED);
    CHECK_EQ(pxs_tree_apply_packet(nullptr, garbage, 3), PXS_ERR_ARGUMENT);

    // OSC out: the feedback of a parameter, a signal
    d.told.clear();
    d.level = 0.75;
    CHECK_EQ(pxs_tree_changed(tree, "/mix/level"), PXS_OK);
    CHECK_EQ(d.told.size(), size_t(1));
    pxs_osc_packet *heard = pxs_osc_decode(d.told[0].data(), d.told[0].size(), nullptr);
    CHECK(heard != nullptr);
    pxs_value v;
    CHECK(heard && pxs_osc_message_arg(heard, 0, 0, &v) == PXS_OK && v.kind == PXS_FLOAT && v.number == 0.75);
    pxs_osc_packet_free(heard);
    const pxs_value peak[] = {number(0.9)};
    CHECK_EQ(pxs_tree_fire(tree, "/detect/peak", peak, 1), PXS_OK);
    CHECK_EQ(d.told.size(), size_t(2));
    CHECK_EQ(pxs_tree_fire(tree, "/mix/level", peak, 1), PXS_ERR_NOT_FOUND); // not a signal
    CHECK_EQ(pxs_tree_changed(tree, "/nowhere"), PXS_ERR_NOT_FOUND);

    // what an OSCQuery client is told
    int status = 0;
    char *body = pxs_oscquery_answer(tree, "App", 9000, "GET", "/mix/level?VALUE", &status);
    CHECK(status == 200 && body && std::string(body) == "{\"VALUE\":[0.75]}");
    pxs_free(body);
    body = pxs_oscquery_answer(tree, "App", 9000, "GET", "/?HOST_INFO", &status);
    CHECK(status == 200 && body && std::string(body).find("\"OSC_PORT\":9000") != std::string::npos);
    pxs_free(body);
    CHECK(pxs_oscquery_answer(tree, "App", 9000, "GET", "/nowhere", &status) == nullptr && status == 404);
    CHECK(pxs_oscquery_answer(tree, "App", 9000, "POST", "/", &status) == nullptr && status == 405);
    CHECK(pxs_oscquery_answer(tree, nullptr, 0, nullptr, "/", &status) == nullptr && status == 400);

    // presets and locks
    char *preset = pxs_preset_capture(tree, "Intro");
    CHECK(preset && std::string(preset).find("\"name\":\"Intro\"") != std::string::npos);
    d.level = 0.1;
    d.on = false;
    CHECK_EQ(pxs_tree_lock(tree, "/mix/on", 1), PXS_OK);
    const int applied = pxs_preset_recall(tree, preset);
    CHECK(applied >= 2); // level and mode (the locked switch keeps its value)
    CHECK(d.level == 0.75 && !d.on);
    pxs_free(preset);
    CHECK_EQ(pxs_tree_lock(tree, "/nowhere", 1), PXS_ERR_NOT_FOUND);
    CHECK_EQ(pxs_preset_recall(tree, "not json"), 0);

    CHECK_EQ(pxs_tree_remove(tree, "/mix/on"), PXS_OK);
    CHECK_EQ(pxs_tree_remove(tree, "/mix/on"), PXS_ERR_NOT_FOUND);
    pxs_tree_set_output(tree, nullptr, nullptr);
    pxs_tree_free(tree);
    pxs_tree_free(nullptr);
}

void dmx()
{
    pxs_dmx_frame f;
    std::memset(&f, 0, sizeof f);
    f.sacn = 1;
    f.universe = 7;
    f.priority = 100;
    f.sequence = 9;
    f.length = 512;
    std::strcpy(f.name, "Pixel Stereo");
    for (int i = 0; i < 512; ++i) f.values[i] = uint8_t(i);
    uint8_t *bytes = nullptr;
    size_t size = 0;
    CHECK_EQ(pxs_dmx_encode(&f, &bytes, &size), PXS_OK);
    CHECK_EQ(size, size_t(638));
    pxs_dmx_frame back;
    CHECK_EQ(pxs_dmx_decode(bytes, size, 1, &back), PXS_OK);
    CHECK(back.universe == 7 && back.priority == 100 && back.sequence == 9 && std::string(back.name) == "Pixel Stereo" && back.values[300] == uint8_t(300));
    CHECK_EQ(pxs_dmx_decode(bytes, size, 0, &back), PXS_ERR_MALFORMED); // the other protocol
    pxs_free(bytes);
    f.universe = 0; // out of range for sACN
    CHECK_EQ(pxs_dmx_encode(&f, &bytes, &size), PXS_ERR_ARGUMENT);
    f.sacn = 0;
    f.universe = 5;
    f.length = 10;
    CHECK_EQ(pxs_dmx_encode(&f, &bytes, &size), PXS_OK);
    CHECK_EQ(size, size_t(28));
    CHECK(pxs_dmx_decode(bytes, size, 0, &back) == PXS_OK && back.universe == 5 && back.length == 10);
    pxs_free(bytes);
    CHECK_EQ(pxs_dmx_decode(nullptr, 0, 0, &back), PXS_ERR_ARGUMENT);
}

void midi()
{
    uint8_t *bytes = nullptr;
    size_t size = 0;
    CHECK_EQ(pxs_midi_build(0, 3, 7, 1.0, &bytes, &size), PXS_OK);
    CHECK(size == 3 && bytes[0] == 0xB2 && bytes[1] == 7 && bytes[2] == 127);
    pxs_midi_message m;
    CHECK(pxs_midi_parse(bytes, size, &m) == PXS_OK && m.kind == 0 && m.channel == 3 && m.number == 7 && m.value == 1.0);
    pxs_free(bytes);
    CHECK_EQ(pxs_midi_build(2, 1, 60, 0, &bytes, &size), PXS_OK); // note off
    CHECK(pxs_midi_parse(bytes, size, &m) == PXS_OK && m.kind == 1 && m.value == 0.0);
    pxs_free(bytes);
    CHECK_EQ(pxs_midi_build(9, 1, 0, 0, &bytes, &size), PXS_ERR_ARGUMENT);
    const uint8_t clock[] = {0xF8};
    CHECK_EQ(pxs_midi_parse(clock, 1, &m), PXS_ERR_MALFORMED);
}

void websocket()
{
    char *key = pxs_websocket_accept_key("dGhlIHNhbXBsZSBub25jZQ==");
    CHECK(key && std::string(key) == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    pxs_free(key);
    CHECK(pxs_websocket_accept_key(nullptr) == nullptr);
}

} // namespace

int main()
{
    general();
    oscRoundTrip();
    theAddressSpace();
    dmx();
    midi();
    websocket();
    return pxs_test::finish("c_api");
}
