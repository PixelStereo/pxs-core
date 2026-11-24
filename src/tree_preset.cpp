#include <pxs/core/tree/preset.h>

#include <nlohmann/json.hpp>

namespace pxs::core::tree {

std::string nodeValueJson(const Node &n); // tree_oscquery.cpp

namespace {

using Json = nlohmann::json;

// An OSCQuery value as the arguments of an OSC message: nested arrays flattened, as the type tags have them. Every number is a
// double (JSON has one number type), as it was in the Qt tree.
void flatten(const Json &v, ValueList &out)
{
    if (v.is_array()) {
        for (const Json &e : v) flatten(e, out);
    } else if (v.is_boolean()) {
        out.emplace_back(v.get<bool>());
    } else if (v.is_string()) {
        out.emplace_back(v.get<std::string>());
    } else if (v.is_number()) {
        out.emplace_back(v.get<double>());
    } else {
        out.emplace_back(); // null, an object
    }
}

ValueList arguments(const std::string &arrayJson)
{
    ValueList out;
    if (arrayJson.empty()) return out;
    const Json a = Json::parse(arrayJson, nullptr, false);
    if (!a.is_array()) return out; // anything but an array is no arguments
    for (const Json &v : a) flatten(v, out);
    return out;
}

// A node a preset may hold: a parameter that is read and written (and, when it has a Parameter, one that is saved)
bool holdable(const Node &n)
{
    if (n.kind() != Kind::Parameter || n.access != 3 || !n.get || !n.set) return false;
    return !n.parameter() || n.parameter()->isStored();
}

void collect(const Node &n, std::vector<std::string> &out)
{
    if (holdable(n)) out.push_back(n.path());
    for (const auto &c : n.children()) collect(*c.second, out);
}

} // namespace

std::string Preset::toJson() const
{
    Json list = Json::array();
    for (const Action &a : actions) {
        Json o = Json::object();
        o["address"] = a.address;
        if (!a.args.empty()) {
            const Json args = Json::parse(a.args, nullptr, false);
            if (args.is_array() && !args.empty()) o["args"] = args;
        }
        list.push_back(o);
    }
    Json v = Json::object();
    for (const auto &entry : values) {
        const Json value = Json::parse(entry.second, nullptr, false);
        v[entry.first] = value.is_discarded() ? Json::array() : value;
    }
    Json o = Json::object();
    o["values"] = v;
    if (!name.empty()) o["name"] = name;
    if (!list.empty()) o["actions"] = list;
    return o.dump(-1, ' ', false, Json::error_handler_t::replace);
}

Preset Preset::fromJson(const std::string &text)
{
    Preset p;
    const Json o = Json::parse(text, nullptr, false);
    if (!o.is_object()) return p;
    if (const auto it = o.find("name"); it != o.end() && it->is_string()) p.name = it->get<std::string>();
    if (const auto it = o.find("values"); it != o.end() && it->is_object())
        for (auto e = it->begin(); e != it->end(); ++e) p.values[e.key()] = e.value().dump(-1, ' ', false, Json::error_handler_t::replace);
    if (const auto it = o.find("actions"); it != o.end() && it->is_array()) {
        for (const Json &a : *it) {
            if (!a.is_object()) continue;
            const auto address = a.find("address");
            if (address == a.end() || !address->is_string() || address->get<std::string>().empty()) continue;
            Action action;
            action.address = address->get<std::string>();
            if (const auto args = a.find("args"); args != a.end() && args->is_array()) action.args = args->dump();
            p.actions.push_back(std::move(action));
        }
    }
    return p;
}

Preset capture(const Tree &tree, const std::vector<std::string> &only, const std::vector<Preset::Action> &actions)
{
    Preset p;
    std::vector<std::string> paths = only;
    if (paths.empty()) collect(tree.root(), paths);
    for (const std::string &path : paths) {
        const Node *n = tree.find(path);
        if (!n || !holdable(*n)) continue;
        const std::string value = nodeValueJson(*n);
        if (!value.empty() && value != "[]") p.values[n->path()] = value;
    }
    p.actions = actions;
    return p;
}

RecallResult recall(Tree &tree, const Preset &preset)
{
    RecallResult r;
    for (const auto &entry : preset.values) {
        const Node *n = tree.find(entry.first);
        if (!n || !holdable(*n)) {
            ++r.missing;
            continue;
        }
        if (n->locked) {
            ++r.locked;
            continue;
        }
        if (tree.apply(n->path(), arguments(entry.second)) > 0) ++r.applied;
        else ++r.missing; // refused: a value that does not fit
    }
    for (const Preset::Action &a : preset.actions) {
        const Node *n = tree.find(a.address);
        if (!n || n->kind() != Kind::Action) {
            ++r.missing;
            continue;
        }
        if (n->locked) {
            ++r.locked;
            continue;
        }
        if (tree.apply(n->path(), arguments(a.args)) > 0) ++r.actions;
        else ++r.missing;
    }
    return r;
}

} // namespace pxs::core::tree
