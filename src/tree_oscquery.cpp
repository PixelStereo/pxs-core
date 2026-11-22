#include <pxs/core/tree/oscquery.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <limits>

namespace pxs::core::tree {
namespace {

// std::map objects: the keys come out in alphabetical order. Text is dumped with "replace": a string that is not valid UTF-8 (an OSC
// string from the network, a name) comes out with U+FFFD in its place, where the default would throw.
using Json = nlohmann::json;

std::int64_t wholeNumber(const Value &v)
{
    const auto n = v.number();
    if (!n || std::isnan(*n)) return 0;
    const double r = std::round(*n);
    if (r <= -9223372036854775808.0) return std::numeric_limits<std::int64_t>::min();
    if (r >= 9223372036854774784.0) return std::numeric_limits<std::int64_t>::max();
    return static_cast<std::int64_t>(r);
}

// A value as the number it is, JSON having no infinity or not-a-number
Json numberJson(double d) { return std::isfinite(d) ? Json(d) : Json(0.0); }

// One OSC argument as OSCQuery writes it (the table of the proposal): numbers, strings, a colour as #RRGGBBAA, a switch as a
// boolean, nothing for what has no value (N I) or no description (blob, MIDI)
Json scalar(char t, const Value &v)
{
    if (t == 'T' || t == 'F') return Json(v.truthy());
    if (t == 'i' || t == 'h') return Json(wholeNumber(v));
    if (t == 's' || t == 'S' || t == 'c') return Json(v.text());
    if (t == 'r') {
        char buf[16];
        std::snprintf(buf, sizeof buf, "#%08X", static_cast<unsigned>(static_cast<std::uint32_t>(wholeNumber(v))));
        return Json(std::string(buf));
    }
    if (t == 'N' || t == 'I' || t == 'b' || t == 'm') return Json(nullptr);
    return numberJson(v.number().value_or(0.0));
}

Json valuesOf(const std::string &type, const ValueList &args, std::size_t &at, std::size_t &used, bool nested)
{
    Json out = Json::array();
    while (at < type.size()) {
        const char t = type[at++];
        if (t == ']') {
            if (nested) break;
            continue;
        }
        if (t == '[') {
            out.push_back(valuesOf(type, args, at, used, true));
        } else {
            out.push_back(scalar(t, used < args.size() ? args[used] : Value()));
            ++used;
        }
    }
    return out;
}

Json valuesJsonOf(const std::string &type, const ValueList &args)
{
    std::size_t at = 0, used = 0;
    return valuesOf(type, args, at, used, false);
}

std::size_t scalarCount(const std::string &type)
{
    std::size_t n = 0;
    for (char t : type)
        if (t != '[' && t != ']') ++n;
    return n;
}

Json rangeEntry(const ArgRange &r)
{
    Json o = Json::object();
    if (r.min) o["MIN"] = numberJson(r.min->number().value_or(0.0));
    if (r.max) o["MAX"] = numberJson(r.max->number().value_or(0.0));
    if (!r.vals.empty()) {
        Json vals = Json::array();
        for (const Value &v : r.vals) vals.push_back(v.text());
        o["VALS"] = vals;
    }
    return o;
}

Json describe(const Node &n, bool contents)
{
    Json o = Json::object();
    o["FULL_PATH"] = n.path();
    o["ACCESS"] = n.access;
    if (!n.description.empty()) o["DESCRIPTION"] = n.description;
    if (n.locked) o["PXS_LOCKED"] = true;
    if (!n.type.empty()) {
        o["TYPE"] = n.type;
        if ((n.access & 1) && n.get) {
            const ValueList v = n.get();
            if (!v.empty()) o["VALUE"] = valuesJsonOf(n.type, v);
        }
        if (!n.range.empty()) {
            Json r = Json::array();
            for (const ArgRange &entry : n.range) r.push_back(rangeEntry(entry));
            o["RANGE"] = r;
        }
        if (!n.clip.empty()) {
            Json c = Json::array();
            for (std::size_t k = 0; k < scalarCount(n.type); ++k) c.push_back(n.clip);
            o["CLIPMODE"] = c;
        }
        const std::size_t arguments = scalarCount(n.type);
        const auto perArgument = [&](const std::vector<std::string> &names) {
            Json a = Json::array();
            for (std::size_t k = 0; k < arguments; ++k) a.push_back(k < names.size() && !names[k].empty() ? Json(names[k]) : Json(nullptr));
            return a;
        };
        if (!n.extendedType.empty()) o["EXTENDED_TYPE"] = perArgument(n.extendedType);
        if (!n.unit.empty()) o["UNIT"] = perArgument(n.unit);
        if (n.defaultValue) {
            const ValueList d = n.defaultValue();
            if (!d.empty()) o["DEFAULT_VALUE"] = valuesJsonOf(n.type, d);
        }
    }
    if (!n.tags.empty()) o["TAGS"] = n.tags;
    for (const auto &entry : n.attributes) {
        const std::optional<std::string> text = entry.second();
        if (!text) continue;
        Json v = Json::parse(*text, nullptr, false);
        if (v.is_discarded() || v.is_null()) continue; // a value that is not JSON, or null: no value
        o[entry.first] = std::move(v);
    }
    if (n.isContainer() && contents) { // a container always has CONTENTS (even empty)
        Json children = Json::object();
        for (const auto &entry : n.children()) children[entry.first] = describe(*entry.second, true);
        o["CONTENTS"] = children;
    }
    return o;
}

std::string upperAscii(std::string s)
{
    for (char &c : s)
        if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    return s;
}

} // namespace

// Used by the presets: the VALUE of a node as a JSON array ("" when it has none)
std::string nodeValueJson(const Node &n)
{
    if (n.type.empty() || !(n.access & 1) || !n.get) return {};
    const ValueList v = n.get();
    if (v.empty()) return {};
    return valuesJsonOf(n.type, v).dump(-1, ' ', false, Json::error_handler_t::replace);
}

std::string describeJson(const Tree &tree, const std::string &path)
{
    const Node *n = tree.find(path);
    return n ? describe(*n, true).dump(-1, ' ', false, Json::error_handler_t::replace) : "{}";
}

bool attributeJson(const Tree &tree, const std::string &path, const std::string &name, std::string &out)
{
    const Node *n = tree.find(path);
    if (!n) return false;
    const std::string key = upperAscii(name);
    const Json node = describe(*n, key == "CONTENTS"); // the subtree only when it is the one asked for
    Json result = Json::object();
    if (node.contains(key)) result[key] = node[key];
    out = result.dump(-1, ' ', false, Json::error_handler_t::replace);
    return true;
}

std::string valueJson(char tag, const Value &value) { return scalar(tag, value).dump(-1, ' ', false, Json::error_handler_t::replace); }

std::string valuesJson(const std::string &type, const ValueList &values) { return valuesJsonOf(type, values).dump(-1, ' ', false, Json::error_handler_t::replace); }

std::string defaultExtensionsJson()
{
    Json o = Json::object();
    for (const char *name : {"ACCESS", "VALUE", "RANGE", "DESCRIPTION", "CLIPMODE", "PXS_LOCKED", "TAGS", "EXTENDED_TYPE", "UNIT", "DEFAULT_VALUE"})
        o[name] = true;
    return o.dump(-1, ' ', false, Json::error_handler_t::replace);
}

std::string hostInfoJson(const std::string &name, int oscPort, const std::string &extensionsJson, const std::string &moreJson)
{
    Json o = Json::object();
    o["NAME"] = name;
    o["OSC_PORT"] = oscPort;
    o["OSC_TRANSPORT"] = "UDP";
    Json ext = Json::parse(extensionsJson, nullptr, false);
    o["EXTENSIONS"] = ext.is_object() ? ext : Json::object();
    const Json more = Json::parse(moreJson, nullptr, false);
    if (more.is_object())
        for (auto it = more.begin(); it != more.end(); ++it) o[it.key()] = it.value();
    return o.dump(-1, ' ', false, Json::error_handler_t::replace);
}

} // namespace pxs::core::tree
