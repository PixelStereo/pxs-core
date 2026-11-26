#include <pxs/core/tree/preset.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

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
    if (!id.empty()) o["id"] = id;
    if (fade > 0) o["fade"] = fade;
    if (!times.empty()) o["times"] = times;
    if (!delays.empty()) o["delays"] = delays;
    if (!name.empty()) o["name"] = name;
    if (!list.empty()) o["actions"] = list;
    return o.dump(-1, ' ', false, Json::error_handler_t::replace);
}

Preset Preset::fromJson(const std::string &text)
{
    Preset p;
    const Json o = Json::parse(text, nullptr, false);
    if (!o.is_object()) return p;
    if (o.contains("id") && o["id"].is_string()) p.id = o["id"].get<std::string>();
    const auto seconds = [](const Json &v) { return v.is_number() ? std::clamp(v.get<double>(), 0.0, 3600.0) : 0.0; };
    if (o.contains("fade")) p.fade = seconds(o["fade"]);
    for (auto entry : {std::pair{"times", &p.times}, std::pair{"delays", &p.delays}}) {
        const auto fields = o.find(entry.first);
        if (fields != o.end() && fields->is_object())
            for (auto field = fields->begin(); field != fields->end(); ++field)
                if (field.value().is_number()) (*entry.second)[field.key()] = seconds(field.value());
    }
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

namespace {
double timing(const std::map<std::string, double> &fields, std::string path, double fallback)
{
    for (;;) {
        if (auto it = fields.find(path); it != fields.end()) return it->second;
        if (path.empty()) return fallback;
        path.resize(path.rfind('/') == std::string::npos ? 0 : path.rfind('/'));
    }
}
}

Preset Preset::resolved(const Preset &overrides, double fadeOverride) const
{
    Preset result = *this;
    if (fadeOverride >= 0) result.fade = fadeOverride;
    for (auto &[path, value] : result.values) {
        if (auto it = overrides.values.find(path); it != overrides.values.end()) value = it->second;
        result.times[path] = timing(overrides.times, path, timing(times, path, result.fade));
        result.delays[path] = timing(overrides.delays, path, timing(delays, path, 0));
    }
    return result;
}

double Preset::duration() const
{
    double result = 0;
    for (const auto &[path, value] : values)
        result = std::max(result, timing(times, path, fade) + timing(delays, path, 0));
    return result;
}

void PresetPlayback::start(Tree &tree, const Preset &preset)
{
    Preset immediate = preset;
    immediate.values.clear();
    for (const auto &[path, value] : preset.values) {
        m_jobs.erase(path);
        const Node *node = tree.find(path);
        if (!node || node->locked || !holdable(*node)) continue;
        const double delay = timing(preset.delays, path, 0), time = timing(preset.times, path, preset.fade);
        if (delay + time <= 0) immediate.values[path] = value;
        else m_jobs[path] = {delay == 0 ? nodeValueJson(*node) : std::string{}, value, delay, time};
    }
    recall(tree, immediate);
}

void PresetPlayback::advance(Tree &tree, double seconds)
{
    for (auto it = m_jobs.begin(); it != m_jobs.end();) {
        auto &[path, job] = *it;
        job.elapsed += seconds;
        const Node *node = tree.find(path);
        if (!node || node->locked || !holdable(*node)) {
            it = m_jobs.erase(it);
            continue;
        }
        if (job.elapsed >= job.delay + job.time) {
            tree.apply(path, arguments(job.to));
            it = m_jobs.erase(it);
            continue;
        }
        if (job.elapsed >= job.delay) {
            if (job.from.empty()) job.from = nodeValueJson(*node);
            auto from = arguments(job.from);
            const auto to = arguments(job.to);
            const double fraction = (job.elapsed - job.delay) / job.time;
            const auto a = Json::parse(job.from, nullptr, false), b = Json::parse(job.to, nullptr, false);
            bool numeric = !from.empty() && from.size() == to.size();
            for (size_t i = 0; numeric && i < from.size(); ++i) {
                numeric = a.is_array() && b.is_array() && a.size() == from.size() && b.size() == to.size()
                    && a[i].is_number() && b[i].is_number();
                if (numeric) from[i] = Value(a[i].get<double>() + (b[i].get<double>() - a[i].get<double>()) * fraction);
            }
            if (numeric) tree.apply(path, from);
        }
        ++it;
    }
}

void PresetPlayback::finish(Tree &tree)
{
    advance(tree, 7200);
}

} // namespace pxs::core::tree
