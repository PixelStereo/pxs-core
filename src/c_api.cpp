#include <pxs/c/pxs.h>

#include <pxs/core/dmx/frame.h>
#include <pxs/core/midi/message.h>
#include <pxs/core/osc/codec.h>
#include <pxs/core/tree/oscquery_server.h>
#include <pxs/core/tree/osc_bridge.h>
#include <pxs/core/tree/preset.h>
#include <pxs/core/tree/remote.h>
#include <pxs/core/tree/tree.h>
#include <pxs/core/version.h>
#include <pxs/core/websocket/frame.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <vector>

namespace core = pxs::core;
namespace tree = pxs::core::tree;

// No exception crosses the boundary: every entry point runs its body here.
#define PXS_TRY(failure, ...)           \
    try {                               \
        __VA_ARGS__                     \
    } catch (...) {                     \
        return failure;                 \
    }

struct pxs_osc_packet {
    std::vector<core::osc::Message> messages;
};

struct pxs_remote {
    tree::RemoteTree tree;
};

struct pxs_tree {
    tree::Tree tree;
    pxs_output_fn output = nullptr;
    void *outputUser = nullptr;
};

namespace {

core::Value fromC(const pxs_value &v)
{
    switch (v.kind) {
    case PXS_BOOL: return core::Value(v.boolean != 0);
    case PXS_INT32: return core::Value(static_cast<std::int32_t>(v.integer));
    case PXS_INT64: return core::Value(static_cast<std::int64_t>(v.integer));
    case PXS_FLOAT: return core::Value(static_cast<float>(v.number));
    case PXS_DOUBLE: return core::Value(v.number);
    case PXS_STRING: return core::Value(std::string(reinterpret_cast<const char *>(v.data), v.data ? v.size : 0));
    case PXS_VEC2: return core::Value(core::Vec2{v.vec[0], v.vec[1]});
    case PXS_VEC3: return core::Value(core::Vec3{v.vec[0], v.vec[1], v.vec[2]});
    case PXS_VEC4: return core::Value(core::Vec4{v.vec[0], v.vec[1], v.vec[2], v.vec[3]});
    case PXS_BLOB: return core::Value(core::Blob(v.data, v.data ? v.data + v.size : v.data));
    default: return core::Value();
    }
}

// `v` points into `value`, which must outlive it
void toC(const core::Value &value, pxs_value &v)
{
    std::memset(&v, 0, sizeof v);
    using K = core::Value::Kind;
    switch (value.kind()) {
    case K::Nothing: v.kind = PXS_NIL; break;
    case K::Boolean: v.kind = PXS_BOOL; v.boolean = *value.get<bool>() ? 1 : 0; break;
    case K::Integer32: v.kind = PXS_INT32; v.integer = *value.get<std::int32_t>(); break;
    case K::Integer64: v.kind = PXS_INT64; v.integer = *value.get<std::int64_t>(); break;
    case K::Float: v.kind = PXS_FLOAT; v.number = double(*value.get<float>()); break;
    case K::Double: v.kind = PXS_DOUBLE; v.number = *value.get<double>(); break;
    case K::String: {
        const std::string &s = *value.get<std::string>();
        v.kind = PXS_STRING;
        v.data = reinterpret_cast<const std::uint8_t *>(s.c_str());
        v.size = s.size();
        break;
    }
    case K::Vector2: v.kind = PXS_VEC2; for (std::size_t i = 0; i < 2; ++i) v.vec[i] = (*value.get<core::Vec2>())[i]; break;
    case K::Vector3: v.kind = PXS_VEC3; for (std::size_t i = 0; i < 3; ++i) v.vec[i] = (*value.get<core::Vec3>())[i]; break;
    case K::Vector4: v.kind = PXS_VEC4; for (std::size_t i = 0; i < 4; ++i) v.vec[i] = (*value.get<core::Vec4>())[i]; break;
    case K::Bytes: {
        const core::Blob &b = *value.get<core::Blob>();
        v.kind = PXS_BLOB;
        v.data = b.data();
        v.size = b.size();
        break;
    }
    }
}

std::uint8_t *duplicate(const std::vector<std::uint8_t> &bytes)
{
    auto *p = static_cast<std::uint8_t *>(std::malloc(bytes.empty() ? 1 : bytes.size()));
    if (p && !bytes.empty()) std::memcpy(p, bytes.data(), bytes.size());
    return p;
}

char *duplicate(const std::string &s)
{
    char *p = static_cast<char *>(std::malloc(s.size() + 1));
    if (p) std::memcpy(p, s.c_str(), s.size() + 1);
    return p;
}

std::string text(const char *s) { return s ? std::string(s) : std::string(); }

} // namespace

extern "C" {

int pxs_api_version(void) { return PXS_C_API_VERSION; }
const char *pxs_version(void) { return PXS_CORE_VERSION_STRING; }
void pxs_free(void *p) { std::free(p); }

// ---- OSC ----

pxs_osc_packet *pxs_osc_decode(const std::uint8_t *data, std::size_t size, int *status)
{
    if (status) *status = PXS_OK;
    try {
        if (!data || size == 0) {
            if (status) *status = PXS_ERR_ARGUMENT;
            return nullptr;
        }
        auto packet = std::make_unique<pxs_osc_packet>();
        if (!core::osc::decode(data, size, packet->messages)) {
            if (status) *status = PXS_ERR_MALFORMED;
            return nullptr;
        }
        return packet.release();
    } catch (...) {
        if (status) *status = PXS_ERR_INTERNAL;
        return nullptr;
    }
}

void pxs_osc_packet_free(pxs_osc_packet *packet) { delete packet; }

std::size_t pxs_osc_packet_count(const pxs_osc_packet *packet) { return packet ? packet->messages.size() : 0; }

const char *pxs_osc_message_address(const pxs_osc_packet *packet, std::size_t i)
{
    return packet && i < packet->messages.size() ? packet->messages[i].address.c_str() : nullptr;
}

const char *pxs_osc_message_types(const pxs_osc_packet *packet, std::size_t i)
{
    return packet && i < packet->messages.size() ? packet->messages[i].types.c_str() : nullptr;
}

std::size_t pxs_osc_message_argc(const pxs_osc_packet *packet, std::size_t i)
{
    return packet && i < packet->messages.size() ? packet->messages[i].args.size() : 0;
}

int pxs_osc_message_arg(const pxs_osc_packet *packet, std::size_t i, std::size_t k, pxs_value *out)
{
    if (!packet || !out || i >= packet->messages.size() || k >= packet->messages[i].args.size()) return PXS_ERR_ARGUMENT;
    try {
        const core::osc::Value &a = packet->messages[i].args[k];
        std::memset(out, 0, sizeof *out);
        struct Visitor {
            pxs_value &v;
            void operator()(const std::monostate &) const { v.kind = PXS_NIL; }
            void operator()(bool x) const { v.kind = PXS_BOOL; v.boolean = x ? 1 : 0; }
            void operator()(std::int32_t x) const { v.kind = PXS_INT32; v.integer = x; }
            void operator()(std::int64_t x) const { v.kind = PXS_INT64; v.integer = x; }
            void operator()(float x) const { v.kind = PXS_FLOAT; v.number = double(x); }
            void operator()(double x) const { v.kind = PXS_DOUBLE; v.number = x; }
            void operator()(const std::string &x) const { v.kind = PXS_STRING; v.data = reinterpret_cast<const std::uint8_t *>(x.c_str()); v.size = x.size(); }
            void operator()(const core::osc::Blob &x) const { v.kind = PXS_BLOB; v.data = x.data(); v.size = x.size(); }
            void operator()(std::uint32_t x) const { v.kind = PXS_INT64; v.integer = static_cast<std::int64_t>(x); }
        };
        std::visit(Visitor{*out}, a);
        return PXS_OK;
    } catch (...) {
        return PXS_ERR_INTERNAL;
    }
}

int pxs_osc_encode(const char *address, const char *types, const pxs_value *args, std::size_t argc, std::uint8_t **out, std::size_t *size)
{
    if (!address || !out || !size || (argc && !args)) return PXS_ERR_ARGUMENT;
    PXS_TRY(PXS_ERR_INTERNAL, {
        core::ValueList values;
        for (std::size_t i = 0; i < argc; ++i) values.push_back(fromC(args[i]));
        core::osc::Message m;
        m.address = address;
        m.types = text(types);
        m.args = tree::toOscArguments(m.types, values);
        const core::osc::Bytes bytes = core::osc::encode(m);
        *out = duplicate(bytes);
        if (!*out) return PXS_ERR_INTERNAL;
        *size = bytes.size();
        return PXS_OK;
    })
}

// ---- the address space ----

pxs_tree *pxs_tree_new(void)
{
    try {
        return new pxs_tree;
    } catch (...) {
        return nullptr;
    }
}

void pxs_tree_free(pxs_tree *t) { delete t; }

int pxs_tree_add_parameter(pxs_tree *t, const char *path, const char *label, int type, double min, double max, const char *const *choices,
                           std::size_t choice_count, pxs_get_fn get, pxs_set_fn set, void *user)
{
    if (!t || !path || type < PXS_PARAM_FLOAT || type > PXS_PARAM_TRIGGER) return PXS_ERR_ARGUMENT;
    PXS_TRY(PXS_ERR_INTERNAL, {
        const std::string where = path;
        const auto slash = where.find_last_of('/');
        const std::string leaf = slash == std::string::npos ? where : where.substr(slash + 1);
        auto p = std::make_unique<tree::Parameter>(leaf, label ? label : leaf, static_cast<tree::ParamInfo::Type>(type));
        if (type == PXS_PARAM_FLOAT || type == PXS_PARAM_INT) p->range(min, max);
        if (type == PXS_PARAM_CHOICE) {
            std::vector<std::string> keys;
            for (std::size_t i = 0; choices && i < choice_count; ++i) keys.push_back(text(choices[i]));
            p->choices(keys);
        }
        if (!get) p->writeOnly();
        else if (!set) p->readOnly();
        if (type != PXS_PARAM_TRIGGER) {
            p->bind(
                [get, user]() -> core::Value {
                    if (!get) return {};
                    pxs_value v;
                    std::memset(&v, 0, sizeof v);
                    if (get(user, &v) != 0) return {};
                    return fromC(v);
                },
                [set, user](const core::Value &value) {
                    if (!set) return;
                    pxs_value v;
                    toC(value, v);
                    set(user, &v);
                });
        } else {
            p->bind(nullptr, [set, user](const core::Value &) {
                if (!set) return;
                pxs_value v;
                std::memset(&v, 0, sizeof v);
                set(user, &v);
            });
        }
        t->tree.add(where, std::move(p));
        return PXS_OK;
    })
}

int pxs_tree_add_action(pxs_tree *t, const char *path, const char *types, pxs_action_fn action, void *user)
{
    if (!t || !path || !action) return PXS_ERR_ARGUMENT;
    PXS_TRY(PXS_ERR_INTERNAL, {
        t->tree.addAction(path, text(types), [action, user](const core::ValueList &args) {
            std::vector<pxs_value> values(args.size());
            for (std::size_t i = 0; i < args.size(); ++i) toC(args[i], values[i]);
            return action(user, values.empty() ? nullptr : values.data(), values.size()) == 0;
        });
        return PXS_OK;
    })
}

int pxs_tree_add_signal(pxs_tree *t, const char *path, const char *types)
{
    if (!t || !path) return PXS_ERR_ARGUMENT;
    PXS_TRY(PXS_ERR_INTERNAL, {
        t->tree.addSignal(path, text(types));
        return PXS_OK;
    })
}

int pxs_tree_remove(pxs_tree *t, const char *path)
{
    if (!t || !path) return PXS_ERR_ARGUMENT;
    PXS_TRY(PXS_ERR_INTERNAL, { return t->tree.remove(path) ? PXS_OK : PXS_ERR_NOT_FOUND; })
}

void pxs_tree_set_output(pxs_tree *t, pxs_output_fn output, void *user)
{
    if (!t) return;
    try {
        t->output = output;
        t->outputUser = user;
        if (!output) {
            t->tree.setOutput(nullptr);
            return;
        }
        pxs_tree *self = t;
        t->tree.setOutput([self](const core::osc::Message &m) {
            if (!self->output) return;
            const core::osc::Bytes bytes = core::osc::encode(m);
            self->output(self->outputUser, bytes.data(), bytes.size());
        });
    } catch (...) {
    }
}

int pxs_tree_apply_packet(pxs_tree *t, const std::uint8_t *data, std::size_t size)
{
    if (!t || !data || size == 0) return PXS_ERR_ARGUMENT;
    PXS_TRY(PXS_ERR_INTERNAL, {
        std::vector<core::osc::Message> messages;
        if (!core::osc::decode(data, size, messages)) return PXS_ERR_MALFORMED;
        int accepted = 0;
        for (const core::osc::Message &m : messages) accepted += t->tree.apply(m);
        return accepted;
    })
}

int pxs_tree_changed(pxs_tree *t, const char *path)
{
    if (!t || !path) return PXS_ERR_ARGUMENT;
    PXS_TRY(PXS_ERR_INTERNAL, { return t->tree.changed(path) ? PXS_OK : PXS_ERR_NOT_FOUND; })
}

int pxs_tree_fire(pxs_tree *t, const char *path, const pxs_value *args, std::size_t argc)
{
    if (!t || !path || (argc && !args)) return PXS_ERR_ARGUMENT;
    PXS_TRY(PXS_ERR_INTERNAL, {
        core::ValueList values;
        for (std::size_t i = 0; i < argc; ++i) values.push_back(fromC(args[i]));
        return t->tree.fire(path, values) ? PXS_OK : PXS_ERR_NOT_FOUND;
    })
}

int pxs_tree_lock(pxs_tree *t, const char *path, int locked)
{
    if (!t || !path) return PXS_ERR_ARGUMENT;
    PXS_TRY(PXS_ERR_INTERNAL, { return t->tree.setLocked(path, locked != 0) ? PXS_OK : PXS_ERR_NOT_FOUND; })
}

char *pxs_oscquery_answer(const pxs_tree *t, const char *name, int osc_port, const char *method, const char *target, int *status)
{
    if (status) *status = 0;
    if (!t || !method || !target) {
        if (status) *status = 400;
        return nullptr;
    }
    try {
        tree::ServerInfo info;
        info.name = name ? name : "pxs";
        info.oscPort = osc_port;
        const tree::HttpAnswer a = tree::answerRequest(t->tree, info, method, target);
        if (status) *status = a.status;
        return a.body.empty() ? nullptr : duplicate(a.body);
    } catch (...) {
        if (status) *status = 500;
        return nullptr;
    }
}

char *pxs_preset_capture(const pxs_tree *t, const char *name)
{
    if (!t) return nullptr;
    try {
        tree::Preset p = tree::capture(t->tree);
        p.name = text(name);
        return duplicate(p.toJson());
    } catch (...) {
        return nullptr;
    }
}

pxs_remote *pxs_remote_new(void)
{
    try {
        return new pxs_remote;
    } catch (...) {
        return nullptr;
    }
}

void pxs_remote_free(pxs_remote *r)
{
    delete r;
}

int pxs_remote_load(pxs_remote *r, const char *json)
{
    if (!r || !json) return PXS_ERR_ARGUMENT;
    PXS_TRY(PXS_ERR_INTERNAL, return r->tree.load(json) ? PXS_OK : PXS_ERR_MALFORMED;)
}

void pxs_remote_clear(pxs_remote *r)
{
    if (r) r->tree.clear();
}

int pxs_remote_set_value(pxs_remote *r, const char *path, const char *value_json)
{
    if (!r || !path || !value_json) return PXS_ERR_ARGUMENT;
    PXS_TRY(PXS_ERR_INTERNAL, {
        if (!r->tree.find(path)) return PXS_ERR_NOT_FOUND;
        return r->tree.setValueJson(path, value_json) ? PXS_OK : PXS_ERR_MALFORMED;
    })
}

char *pxs_remote_node(const pxs_remote *r, const char *path)
{
    if (!r || !path) return nullptr;
    try {
        if (!r->tree.find(path)) return nullptr;
        return duplicate(tree::remoteNodeJson(r->tree, path));
    } catch (...) {
        return nullptr;
    }
}

char *pxs_remote_signature(const pxs_remote *r)
{
    if (!r) return nullptr;
    try {
        return duplicate(r->tree.signature());
    } catch (...) {
        return nullptr;
    }
}

char *pxs_remote_leaves(const pxs_remote *r)
{
    if (!r) return nullptr;
    try {
        std::string out = "[";
        bool first = true;
        for (const std::string &p : r->tree.leaves()) {
            if (!first) out += ',';
            first = false;
            out += '"';
            for (char c : p) {
                if (c == '"' || c == '\\') out += '\\';
                out += c;
            }
            out += '"';
        }
        return duplicate(out + "]");
    } catch (...) {
        return nullptr;
    }
}

int pxs_preset_recall(pxs_tree *t, const char *json)
{
    if (!t || !json) return PXS_ERR_ARGUMENT;
    PXS_TRY(PXS_ERR_INTERNAL, { return tree::recall(t->tree, tree::Preset::fromJson(json)).applied; })
}

// ---- DMX ----

int pxs_dmx_encode(const pxs_dmx_frame *f, std::uint8_t **out, std::size_t *size)
{
    if (!f || !out || !size) return PXS_ERR_ARGUMENT;
    PXS_TRY(PXS_ERR_INTERNAL, {
        core::dmx::Frame frame;
        frame.sacn = f->sacn != 0;
        frame.universe = f->universe;
        frame.priority = f->priority;
        frame.sequence = static_cast<std::uint8_t>(f->sequence);
        frame.length = f->length;
        frame.terminated = f->terminated != 0;
        frame.cid.assign(f->cid, f->cid + 16);
        std::size_t nameLength = 0;
        while (nameLength < sizeof f->name && f->name[nameLength]) ++nameLength;
        frame.name.assign(f->name, nameLength);
        frame.values.assign(f->values, f->values + 512);
        const core::dmx::Bytes bytes = core::dmx::encode(frame);
        if (bytes.empty()) return PXS_ERR_ARGUMENT;
        *out = duplicate(bytes);
        if (!*out) return PXS_ERR_INTERNAL;
        *size = bytes.size();
        return PXS_OK;
    })
}

int pxs_dmx_decode(const std::uint8_t *data, std::size_t size, int sacn, pxs_dmx_frame *f)
{
    if (!data || !f) return PXS_ERR_ARGUMENT;
    PXS_TRY(PXS_ERR_INTERNAL, {
        core::dmx::Frame frame;
        if (!core::dmx::decode(data, size, sacn != 0, frame)) return PXS_ERR_MALFORMED;
        std::memset(f, 0, sizeof *f);
        f->sacn = frame.sacn ? 1 : 0;
        f->universe = frame.universe;
        f->priority = frame.priority;
        f->sequence = frame.sequence;
        f->length = frame.length;
        f->terminated = frame.terminated ? 1 : 0;
        // (memcpy takes no null pointer, even for no bytes: an empty vector's data() may be one)
        if (!frame.cid.empty()) std::memcpy(f->cid, frame.cid.data(), std::min<std::size_t>(frame.cid.size(), 16));
        if (!frame.name.empty()) std::memcpy(f->name, frame.name.data(), std::min<std::size_t>(frame.name.size(), sizeof f->name - 1));
        if (!frame.values.empty()) std::memcpy(f->values, frame.values.data(), std::min<std::size_t>(frame.values.size(), 512));
        return PXS_OK;
    })
}

// ---- MIDI ----

int pxs_midi_parse(const std::uint8_t *data, std::size_t size, pxs_midi_message *out)
{
    if (!out) return PXS_ERR_ARGUMENT;
    core::midi::Message m;
    if (!core::midi::parse(data, size, m)) return PXS_ERR_MALFORMED;
    out->kind = static_cast<std::int32_t>(m.kind);
    out->channel = m.channel;
    out->number = m.number;
    out->value = m.value;
    return PXS_OK;
}

int pxs_midi_build(int kind, int channel, int number, double value, std::uint8_t **out, std::size_t *size)
{
    if (!out || !size) return PXS_ERR_ARGUMENT;
    PXS_TRY(PXS_ERR_INTERNAL, {
        core::midi::Bytes bytes;
        switch (kind) {
        case 0: bytes = core::midi::controlChange(channel, number, value); break;
        case 1: bytes = core::midi::noteOn(channel, number, value); break;
        case 2: bytes = core::midi::noteOff(channel, number); break;
        case 3: bytes = core::midi::pitchBend(channel, value); break;
        case 4: bytes = core::midi::programChange(channel, number); break;
        default: return PXS_ERR_ARGUMENT;
        }
        *out = duplicate(bytes);
        if (!*out) return PXS_ERR_INTERNAL;
        *size = bytes.size();
        return PXS_OK;
    })
}

// ---- WebSocket ----

char *pxs_websocket_accept_key(const char *key)
{
    if (!key) return nullptr;
    try {
        return duplicate(core::websocket::acceptKey(key));
    } catch (...) {
        return nullptr;
    }
}

} // extern "C"
