#include <pxs/c/pxs.h>

#include <pxs/core/dmx/frame.h>
#include <pxs/core/filter/filter.h>
#include <pxs/core/physics/physics.h>
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

struct pxs_filter_chain {
    pxs::core::filter::Chain chain;
    std::vector<pxs::core::filter::StageView> views;
    std::vector<std::vector<pxs::core::timeline::Key>> curves; // the stages' keys, as pxs-core has them
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

int pxs_tree_add_node(pxs_tree *t, const pxs_node *spec)
{
    if (!t || !spec || !spec->path || !spec->types) return PXS_ERR_ARGUMENT;
    PXS_TRY(PXS_ERR_INTERNAL, {
        const std::string type = spec->types;
        std::size_t arguments = 0;
        for (char c : type)
            if (c != '[' && c != ']') ++arguments;
        tree::Node &n = t->tree.add(spec->path);
        n.type = type;
        n.access = spec->access;
        n.description = text(spec->description);
        if (spec->unit) n.unit.assign(std::max<std::size_t>(1, arguments), text(spec->unit));
        for (std::size_t i = 0; spec->min && spec->max && i < spec->range_count; ++i) {
            tree::ArgRange r;
            r.min = core::Value(spec->min[i]);
            r.max = core::Value(spec->max[i]);
            n.range.push_back(r);
        }
        if (spec->vals && spec->vals_count) {
            tree::ArgRange r;
            for (std::size_t i = 0; i < spec->vals_count; ++i) r.vals.emplace_back(text(spec->vals[i]));
            n.range.push_back(r);
        }
        if (!n.range.empty() && spec->min) n.clip = "both";
        if (spec->defaults && spec->defaults_count) {
            core::ValueList d;
            for (std::size_t i = 0; i < spec->defaults_count; ++i) d.push_back(fromC(spec->defaults[i]));
            n.defaultValue = [d] { return d; };
        }
        const pxs_values_get_fn get = spec->get;
        const pxs_values_set_fn set = spec->set;
        void *const user = spec->user;
        const std::size_t capacity = std::max<std::size_t>(1, arguments);
        if (get && (spec->access & 1)) {
            n.get = [get, user, capacity]() -> core::ValueList {
                std::vector<pxs_value> buf(capacity);
                for (pxs_value &v : buf) std::memset(&v, 0, sizeof v);
                std::size_t count = 0;
                if (get(user, buf.data(), capacity, &count) != 0) return {};
                core::ValueList out;
                for (std::size_t i = 0; i < count && i < capacity; ++i) out.push_back(fromC(buf[i]));
                return out;
            };
        }
        if (set && (spec->access & 2)) {
            n.set = [set, user](const core::ValueList &values) {
                std::vector<pxs_value> buf(values.size());
                for (std::size_t i = 0; i < values.size(); ++i) toC(values[i], buf[i]);
                return set(user, buf.empty() ? nullptr : buf.data(), buf.size()) == 0;
            };
        }
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

// ---- filters ----

namespace {
namespace filter = pxs::core::filter;
bool validKind(int kind) { return kind >= 0 && kind < filter::kKindCount; }
bool validOp(int op) { return op >= 0 && op < filter::kMathOpCount; }
} // namespace

int pxs_filter_count(void) { return filter::kKindCount; }
const char *pxs_filter_key(int kind) { return validKind(kind) ? filter::key(filter::Kind(kind)) : nullptr; }
const char *pxs_filter_name(int kind) { return validKind(kind) ? filter::name(filter::Kind(kind)) : nullptr; }
const char *pxs_filter_group(int kind) { return validKind(kind) ? filter::group(filter::Kind(kind)) : nullptr; }
const char *pxs_filter_about(int kind) { return validKind(kind) ? filter::about(filter::Kind(kind)) : nullptr; }

int pxs_filter_from_key(const char *key)
{
    if (!key) return PXS_ERR_ARGUMENT;
    const auto k = filter::kindFromKey(key);
    return k ? int(*k) : PXS_ERR_NOT_FOUND;
}

int pxs_filter_setting_of(int kind, int which, pxs_filter_setting *out)
{
    if (!out || !validKind(kind)) return PXS_ERR_ARGUMENT;
    const filter::Setting s = filter::setting(filter::Kind(kind), which);
    if (!s.name) return PXS_ERR_NOT_FOUND;
    *out = pxs_filter_setting{s.name, s.suffix, s.min, s.max, s.def};
    return PXS_OK;
}

int pxs_math_op_count(void) { return filter::kMathOpCount; }
const char *pxs_math_op_key(int op) { return validOp(op) ? filter::key(filter::MathOp(op)) : nullptr; }
const char *pxs_math_op_name(int op) { return validOp(op) ? filter::name(filter::MathOp(op)) : nullptr; }

int pxs_math_op_from_key(const char *key)
{
    if (!key) return PXS_ERR_ARGUMENT;
    const auto op = filter::mathOpFromKey(key);
    return op ? int(*op) : PXS_ERR_NOT_FOUND;
}

int pxs_math_op_uses_value(int op) { return validOp(op) && filter::usesValue(filter::MathOp(op)) ? 1 : 0; }
double pxs_math_op_apply(int op, double x, double a) { return validOp(op) ? filter::apply(filter::MathOp(op), x, a) : x; }

pxs_filter_chain *pxs_filter_chain_new(void) { PXS_TRY(nullptr, return new pxs_filter_chain();) }
void pxs_filter_chain_free(pxs_filter_chain *chain) { delete chain; }

void pxs_filter_chain_reset(pxs_filter_chain *chain)
{
    if (chain) chain->chain.reset();
}

void pxs_filter_chain_input(pxs_filter_chain *chain, double value)
{
    if (chain) chain->chain.input(value);
}

int pxs_filter_chain_step(pxs_filter_chain *chain, const pxs_filter_stage *stages, size_t count, double dt, double *out)
{
    if (!chain || !out || (count && !stages)) return PXS_ERR_ARGUMENT;
    PXS_TRY(PXS_ERR_INTERNAL, {
        chain->views.resize(count);
        chain->curves.resize(count);
        for (size_t i = 0; i < count; ++i) {
            const pxs_filter_stage &s = stages[i];
            if (!validKind(s.kind) || !validOp(s.op) || (s.curve_count && !s.curve)) return PXS_ERR_ARGUMENT;
            std::vector<pxs::core::timeline::Key> &keys = chain->curves[i];
            keys.resize(s.curve ? s.curve_count : 0);
            for (size_t k = 0; k < keys.size(); ++k) {
                const pxs_curve_key &c = s.curve[k];
                keys[k] = pxs::core::timeline::Key{};
                keys[k].t = c.t;
                keys[k].v = c.v;
                keys[k].curve = c.curve;
                keys[k].inDt = c.in_dt;
                keys[k].inDv = c.in_dv;
                keys[k].outDt = c.out_dt;
                keys[k].outDv = c.out_dv;
            }
            chain->views[i] = {filter::Kind(s.kind), s.enabled != 0, s.a, s.b, s.c, s.d, filter::MathOp(s.op), &keys};
        }
        const std::optional<double> y = chain->chain.step(chain->views, dt);
        if (!y) return 0;
        *out = *y;
        return 1;
    })
}

const double *pxs_filter_chain_stage_outputs(const pxs_filter_chain *chain, size_t *count)
{
    if (count) *count = chain ? chain->chain.stageOutputs().size() : 0;
    return chain && !chain->chain.stageOutputs().empty() ? chain->chain.stageOutputs().data() : nullptr;
}

int pxs_filter_chain_level(const pxs_filter_chain *chain, double *out)
{
    if (!chain || !out) return PXS_ERR_ARGUMENT;
    const auto v = chain->chain.level();
    if (v) *out = *v;
    return v ? 1 : 0;
}

int pxs_filter_chain_curve_input(const pxs_filter_chain *chain, double *out)
{
    if (!chain || !out) return PXS_ERR_ARGUMENT;
    const auto v = chain->chain.curveInput();
    if (v) *out = *v;
    return v ? 1 : 0;
}

// ---- physics ----

namespace {
pxs::core::physics::Settings fromC(const pxs_physics &p)
{
    pxs::core::physics::Settings s;
    s.mode = p.mode;
    s.friction = p.friction;
    s.attraction = p.attraction;
    s.tension = p.tension;
    s.wall = p.wall;
    s.rate = p.rate;
    return s;
}
pxs_physics toC(const pxs::core::physics::Settings &s) { return pxs_physics{s.mode, s.friction, s.attraction, s.tension, s.wall, s.rate}; }
} // namespace

void pxs_physics_defaults(pxs_physics *out)
{
    if (out) *out = toC(pxs::core::physics::Settings{});
}

void pxs_physics_clamp(pxs_physics *settings)
{
    if (settings) *settings = toC(fromC(*settings).clamped());
}

int pxs_physics_step(double *pos, double *vel, const double *target, size_t count, int held, const uint8_t *hand, const pxs_physics *each,
                     size_t each_count, double dt)
{
    if (count && (!pos || !vel || !target)) return PXS_ERR_ARGUMENT;
    if (!each || each_count == 0) return PXS_ERR_ARGUMENT;
    PXS_TRY(PXS_ERR_INTERNAL, {
        pxs::core::physics::Body b;
        b.pos.assign(pos, pos + count);
        b.vel.assign(vel, vel + count);
        b.target.assign(target, target + count);
        b.held = held != 0;
        if (hand) b.hand.assign(hand, hand + count);
        std::vector<pxs::core::physics::Settings> settings(each_count);
        for (size_t i = 0; i < each_count; ++i) settings[i] = fromC(each[i]);
        const bool moving = pxs::core::physics::step(b, settings.data(), settings.size(), dt);
        std::copy(b.pos.begin(), b.pos.end(), pos);
        std::copy(b.vel.begin(), b.vel.end(), vel);
        return moving ? 1 : 0;
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
