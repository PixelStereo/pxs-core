#include <pxs/core/tree/oscquery.h>
#include <pxs/core/tree/remote.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

namespace pxs::core::tree {

namespace {

using Json = nlohmann::json;

// The arguments of a type tag string (the brackets of an array not counted)
std::size_t argumentCount(const std::string &type)
{
    std::size_t n = 0;
    for (char c : type)
        if (c != '[' && c != ']') ++n;
    return n;
}

// VALUE, RANGE, UNIT, EXTENDED_TYPE and CLIPMODE hold one entry per argument; the proposal lets a device write a single value instead of an
// array when every argument shares it (a null stands for "none" in an array)
std::vector<Json> perArgument(const Json &o, const char *key, std::size_t arguments)
{
    std::vector<Json> out;
    const auto it = o.find(key);
    if (it == o.end() || it->is_null()) return out;
    if (it->is_array()) {
        for (const Json &e : *it) out.push_back(e);
        return out;
    }
    for (std::size_t k = 0; k < std::max<std::size_t>(1, arguments); ++k) out.push_back(*it);
    return out;
}

std::vector<std::string> strings(const Json &o, const char *key, std::size_t arguments)
{
    std::vector<std::string> out;
    for (const Json &e : perArgument(o, key, arguments)) out.push_back(e.is_string() ? e.get<std::string>() : std::string());
    return out;
}

// A JSON value as the value it describes: a number, a string, a switch; nested arrays flattened as the type tags have them; nothing for the rest
void flatten(const Json &v, ValueList &out)
{
    if (v.is_array()) {
        for (const Json &e : v) flatten(e, out);
    } else if (v.is_boolean()) {
        out.emplace_back(v.get<bool>());
    } else if (v.is_number()) {
        out.emplace_back(v.get<double>());
    } else if (v.is_string()) {
        out.emplace_back(v.get<std::string>());
    } else {
        out.emplace_back();
    }
}

ValueList valuesOf(const std::vector<Json> &entries)
{
    ValueList out;
    for (const Json &e : entries) flatten(e, out);
    return out;
}

ArgRange rangeOf(const Json &e)
{
    ArgRange r;
    if (e.is_array()) { // an array's: a nested array of ranges
        for (const Json &item : e) r.items.push_back(rangeOf(item));
        return r;
    }
    if (!e.is_object()) return r;
    if (e.contains("MIN") && e["MIN"].is_number()) r.min = Value(e["MIN"].get<double>());
    if (e.contains("MAX") && e["MAX"].is_number()) r.max = Value(e["MAX"].get<double>());
    if (e.contains("VALS") && e["VALS"].is_array()) {
        for (const Json &v : e["VALS"]) {
            if (v.is_string()) r.vals.emplace_back(v.get<std::string>());
            else if (v.is_number()) r.vals.emplace_back(v.get<double>());
            else if (v.is_boolean()) r.vals.emplace_back(v.get<bool>());
        }
    }
    return r;
}

bool parse(const Json &o, const std::string &name, RemoteNode *out, int depth)
{
    if (depth > 64 || !o.is_object()) return false;
    const auto text = [&o](const char *key) {
        const auto it = o.find(key);
        return it != o.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    out->name = name;
    out->path = text("FULL_PATH");
    out->type = text("TYPE");
    out->description = text("DESCRIPTION");
    const auto acc = o.find("ACCESS");
    out->access = acc != o.end() && acc->is_number() ? std::clamp(acc->get<int>(), 0, 3) : 0;
    // A node with no value is not a parameter: one that is written is a message, one that is read is a signal
    if (!o.contains("CONTENTS") && !o.contains("VALUE") && !out->type.empty())
        out->kind = out->access == 2 ? "action" : out->access == 1 ? "signal" : "";
    const std::size_t arguments = argumentCount(out->type);
    for (const Json &e : perArgument(o, "RANGE", arguments)) out->range.push_back(rangeOf(e));
    out->value = valuesOf(perArgument(o, "VALUE", arguments));
    out->defaultValue = valuesOf(perArgument(o, "DEFAULT_VALUE", arguments));
    out->extendedType = strings(o, "EXTENDED_TYPE", arguments);
    out->unit = strings(o, "UNIT", arguments);
    const auto tags = o.find("TAGS");
    if (tags != o.end() && tags->is_array())
        for (const Json &t : *tags) out->tags.push_back(t.is_string() ? t.get<std::string>() : std::string());
    const auto contents = o.find("CONTENTS");
    if (contents != o.end() && contents->is_object()) {
        for (auto it = contents->begin(); it != contents->end(); ++it) {
            if (!it.value().is_object()) continue;
            RemoteNode child;
            if (!parse(it.value(), it.key(), &child, depth + 1)) return false;
            if (child.path.empty()) child.path = (out->path == "/" ? std::string() : out->path) + '/' + it.key();
            out->children.emplace(it.key(), std::move(child));
        }
    }
    return true;
}

void collect(const RemoteNode &n, std::vector<std::string> &out)
{
    if (n.hasValue()) out.push_back(n.path);
    for (const auto &c : n.children) collect(c.second, out);
}

std::string rangeText(const ArgRange &r)
{
    Json o = Json::object();
    if (r.min) o["MIN"] = r.min->number().value_or(0.0);
    if (r.max) o["MAX"] = r.max->number().value_or(0.0);
    if (!r.vals.empty()) {
        Json v = Json::array();
        for (const Value &x : r.vals) v.push_back(x.text());
        o["VALS"] = v;
    }
    return o.dump();
}

void sign(const RemoteNode &n, std::string &out)
{
    out += n.path + '|' + n.type + '|' + std::to_string(n.access) + '|' + n.kind + '|';
    for (const ArgRange &r : n.range) out += rangeText(r) + ',';
    out += '\n';
    for (const auto &c : n.children) sign(c.second, out);
}

std::string capitalised(std::string s)
{
    if (!s.empty()) s[0] = char(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

const char *typeName(ParamInfo::Type t)
{
    switch (t) {
    case ParamInfo::Type::Float: return "float";
    case ParamInfo::Type::Int: return "int";
    case ParamInfo::Type::Bool: return "bool";
    case ParamInfo::Type::Choice: return "choice";
    case ParamInfo::Type::Text: return "text";
    case ParamInfo::Type::Trigger: return "trigger";
    }
    return "float";
}

} // namespace

bool RemoteNode::toParamInfo(ParamInfo *out) const
{
    if (type.size() != 1) return false;
    if (kind == "signal" && type == "N") return false; // an event with nothing to show
    ParamInfo info;
    info.signal = kind == "signal";
    info.path = path;
    info.label = capitalised(name); // "level" -> "Level"
    info.access = info.signal ? int(ParamInfo::Read) : access ? access : int(ParamInfo::ReadWrite);
    info.stored = false;
    const char t = type[0];
    const ArgRange r = range.empty() ? ArgRange() : range[0];
    if (t == 'f' || t == 'd' || t == 'i' || t == 'h') {
        info.type = t == 'f' || t == 'd' ? ParamInfo::Type::Float : ParamInfo::Type::Int;
        const bool bounded = r.min && r.max && r.max->number().value_or(0.0) > r.min->number().value_or(0.0);
        info.min = bounded ? r.min->number().value_or(0.0) : 0;
        info.max = bounded ? r.max->number().value_or(1.0) : 1;
        info.lo = info.min;
        info.hi = info.max;
        info.unbounded = !bounded;
        info.step = info.type == ParamInfo::Type::Int ? 1 : (info.max - info.min) / 100.0;
        info.animatable = true;
    } else if (t == 'T' || t == 'F') {
        info.type = ParamInfo::Type::Bool;
    } else if (t == 's') {
        if (r.vals.empty()) {
            info.type = ParamInfo::Type::Text;
        } else {
            info.type = ParamInfo::Type::Choice;
            for (const Value &v : r.vals) info.choices.push_back(v.text());
        }
    } else if (t == 'N') {
        info.type = ParamInfo::Type::Trigger;
        info.action = true;
    } else {
        return false;
    }
    // A reset is writing the DEFAULT_VALUE the device announces; with none announced there is nothing to reset to
    if (!defaultValue.empty()) info.defaultValue = defaultValue[0];
    if (!unit.empty()) info.unit = unit[0];
    if (!extendedType.empty()) info.extendedType = extendedType[0];
    *out = info;
    return true;
}

bool RemoteTree::load(const std::string &json)
{
    const Json root = Json::parse(json, nullptr, false);
    if (!root.is_object() || (!root.contains("FULL_PATH") && !root.contains("CONTENTS"))) return false;
    RemoteNode node;
    if (!parse(root, std::string(), &node, 0)) return false;
    if (node.path.empty()) node.path = "/";
    m_root = std::move(node);
    m_loaded = true;
    return true;
}

const RemoteNode *RemoteTree::find(const std::string &path) const
{
    const RemoteNode *n = &m_root;
    std::size_t i = 0;
    while (i < path.size()) {
        const std::size_t j = path.find('/', i);
        const std::string part = path.substr(i, j == std::string::npos ? std::string::npos : j - i);
        i = j == std::string::npos ? path.size() : j + 1;
        if (part.empty()) continue;
        const auto it = n->children.find(part);
        if (it == n->children.end()) return nullptr;
        n = &it->second;
    }
    return n;
}

bool RemoteTree::setValue(const std::string &path, ValueList value)
{
    RemoteNode *n = const_cast<RemoteNode *>(find(path));
    if (!n) return false;
    n->value = std::move(value);
    return true;
}

bool RemoteTree::setValueJson(const std::string &path, const std::string &valueJson)
{
    const Json v = Json::parse(valueJson, nullptr, false);
    if (!v.is_array()) return false;
    ValueList list;
    flatten(v, list);
    return setValue(path, std::move(list));
}

std::string RemoteTree::signature() const
{
    std::string out;
    sign(m_root, out);
    return out;
}

std::vector<std::string> RemoteTree::leaves() const
{
    std::vector<std::string> out;
    collect(m_root, out);
    return out;
}

std::string remoteNodeJson(const RemoteTree &tree, const std::string &path)
{
    const RemoteNode *n = tree.find(path);
    if (!n) return "{}";
    Json o = Json::object();
    o["path"] = n->path;
    o["name"] = n->name;
    o["type"] = n->type;
    o["access"] = n->access;
    o["kind"] = n->kind;
    o["description"] = n->description;
    o["hasValue"] = n->hasValue();
    o["container"] = n->isContainer();
    Json kids = Json::array();
    for (const auto &c : n->children) kids.push_back(c.first);
    o["children"] = kids;
    if (!n->value.empty()) o["value"] = Json::parse(valuesJson(n->type, n->value), nullptr, false);
    if (!n->defaultValue.empty()) o["default"] = Json::parse(valuesJson(n->type, n->defaultValue), nullptr, false);
    ParamInfo info;
    if (n->toParamInfo(&info)) {
        Json p = Json::object();
        p["type"] = typeName(info.type);
        p["label"] = info.label;
        p["access"] = info.access;
        p["min"] = info.min;
        p["max"] = info.max;
        p["lo"] = info.lo;
        p["hi"] = info.hi;
        p["step"] = info.step;
        p["unbounded"] = info.unbounded;
        p["signal"] = info.signal;
        p["action"] = info.action;
        p["unit"] = info.unit;
        p["extendedType"] = info.extendedType;
        Json choices = Json::array();
        for (const std::string &c : info.choices) choices.push_back(c);
        p["choices"] = choices;
        if (!info.defaultValue.isNil()) p["default"] = Json::parse(valueJson(n->type[0], info.defaultValue), nullptr, false);
        o["param"] = p;
    }
    return o.dump(-1, ' ', false, Json::error_handler_t::replace);
}

} // namespace pxs::core::tree
