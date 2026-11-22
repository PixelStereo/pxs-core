#include <pxs/core/tree/tree.h>

#include <pxs/core/tree/osc_bridge.h>

#include <algorithm>
#include <cmath>

namespace pxs::core::tree {
namespace {

std::vector<std::string> segments(const std::string &path)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            if (i > start) out.push_back(path.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

// OSC does not allow a space or any of  # * , ? [ ] { }  in the name of a container or a method: they become '_'
std::string cleanName(std::string name)
{
    for (char &c : name)
        if (c == ' ' || c == '#' || c == '*' || c == ',' || c == '?' || c == '[' || c == ']' || c == '{' || c == '}') c = '_';
    return name;
}
std::vector<std::string> names(const std::string &path)
{
    std::vector<std::string> out = segments(path);
    for (std::string &name : out) name = cleanName(std::move(name));
    return out;
}

void collect(Node &node, const std::vector<std::string> &pattern, std::size_t at, std::vector<Node *> &out)
{
    if (at == pattern.size()) {
        out.push_back(&node);
        return;
    }
    for (const auto &entry : node.children())
        if (osc::match(pattern[at], entry.first)) collect(*entry.second, pattern, at + 1, out);
}

std::string lowerAscii(std::string s)
{
    for (char &c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

// An OSC argument as the value of a parameter; Nil (with `fits` false) when it does not fit
Value fromArgument(const ParamInfo &info, const Value &arg, bool &fits)
{
    fits = true;
    switch (info.type) {
    case ParamInfo::Type::Float:
        if (const auto n = arg.number()) return Value(*n);
        break;
    case ParamInfo::Type::Int:
        if (const auto n = arg.number(); n && std::isfinite(*n)) return Value(static_cast<std::int64_t>(std::llround(std::clamp(*n, -1e18, 1e18))));
        break;
    case ParamInfo::Type::Bool:
        if (arg.is<std::string>()) break; // a text is no switch
        return Value(arg.number().value_or(0.0) != 0 || arg.truthy());
    case ParamInfo::Type::Choice:
        if (const auto *text = arg.get<std::string>()) {
            const std::string key = lowerAscii(*text);
            if (std::find(info.choices.begin(), info.choices.end(), key) != info.choices.end()) return Value(key);
            break;
        }
        if (const auto n = arg.number(); n && std::isfinite(*n)) { // by its place among the keys
            const double k = std::round(*n);
            if (k >= 0 && k < double(info.choices.size()) && !info.choices[std::size_t(k)].empty()) return Value(info.choices[std::size_t(k)]);
        }
        break;
    case ParamInfo::Type::Text: return Value(arg.text());
    case ParamInfo::Type::Trigger: return Value(true);
    }
    fits = false;
    return {};
}

} // namespace

std::string oscType(const ParamInfo &info)
{
    switch (info.type) {
    case ParamInfo::Type::Float: return "f";
    case ParamInfo::Type::Int: return "i";
    case ParamInfo::Type::Bool: return "T";
    case ParamInfo::Type::Choice:
    case ParamInfo::Type::Text: return "s";
    case ParamInfo::Type::Trigger: return "N";
    }
    return {};
}

std::string Node::path() const
{
    if (!m_parent) return "/";
    const std::string up = m_parent->path();
    return (up == "/" ? std::string() : up) + '/' + m_name;
}

Node *Node::child(const std::string &name) const
{
    const auto it = m_children.find(name);
    return it == m_children.end() ? nullptr : it->second.get();
}

Node &Tree::add(const std::string &path)
{
    Node *node = &m_root;
    std::string made; // the highest node this call makes
    for (const std::string &name : names(path)) {
        auto &slot = node->m_children[name];
        if (!slot) {
            slot.reset(new Node);
            slot->m_name = name;
            slot->m_parent = node;
            if (made.empty()) made = slot->path();
            if (!m_pendingLocks.empty()) {
                const std::string at = slot->path();
                const auto pending = std::find(m_pendingLocks.begin(), m_pendingLocks.end(), at);
                if (pending != m_pendingLocks.end()) {
                    slot->locked = true;
                    m_pendingLocks.erase(std::remove(m_pendingLocks.begin(), m_pendingLocks.end(), at), m_pendingLocks.end());
                }
            }
        }
        node = slot.get();
    }
    if (!made.empty() && m_structure) m_structure(made, Change::Added);
    return *node;
}

Node &Tree::add(const std::string &path, std::unique_ptr<Parameter> parameter)
{
    Node &node = add(path);
    Parameter *p = parameter.get();
    node.m_parameter = std::move(parameter);
    const ParamInfo info = p->info();
    // A parameter has a value that can be read. What nobody can read back (a trigger, a setter without getter) is a message.
    const bool readable = info.type != ParamInfo::Type::Trigger && (info.access & ParamInfo::Read);
    node.m_kind = readable ? Kind::Parameter : Kind::Action;
    node.label = info.name();
    if (info.type != ParamInfo::Type::Trigger && !info.defaultValue.isNil()) node.reset = [p] { return p->reset(); };
    node.type = oscType(info);
    node.access = info.access;
    if (info.isNumber() && !info.unbounded) {
        node.range = {ArgRange{Value(info.min), Value(info.max), {}}};
        if (info.access & ParamInfo::Write) node.clip = "both"; // a write is kept within the limits
    }
    // What it is: said, or its place among the components of a color or a point
    std::string extended = info.extendedType;
    if (extended.empty() && info.colorComponent >= 0 && info.colorComponent < 4) extended = std::string("color.rgb.") + "rgba"[info.colorComponent];
    if (extended.empty() && info.pointComponent >= 0 && info.pointComponent < 2) extended = std::string("position.cartesian.") + "xy"[info.pointComponent];
    if (!extended.empty()) node.extendedType = {extended};
    if (!info.unit.empty()) node.unit = {info.unit};
    if (readable && !info.defaultValue.isNil()) node.defaultValue = [p] { return ValueList{p->info().defaultValue}; };
    if (info.type == ParamInfo::Type::Choice) {
        ArgRange keys;
        for (const std::string &key : info.choices) keys.vals.emplace_back(key);
        node.range = {keys};
    }
    if (!readable) node.get = nullptr;
    else node.get = [p] { return ValueList{p->value()}; };
    node.set = [p](const ValueList &args) {
        const ParamInfo now = p->info();
        bool fits = false;
        const Value v = fromArgument(now, args.empty() ? Value() : args.front(), fits);
        if (!fits) return false;
        return p->setValue(v);
    };
    return node;
}

Node &Tree::addAction(const std::string &path, const std::string &type, std::function<bool(const ValueList &)> handler,
                      const std::string &description)
{
    Node &node = add(path);
    node.m_kind = Kind::Action;
    if (node.label.empty()) node.label = node.name();
    node.type = type.empty() ? "N" : type;
    node.access = 2;
    node.description = description;
    node.get = nullptr;
    node.set = std::move(handler);
    return node;
}

Node &Tree::addSignal(const std::string &path, const std::string &type, const std::string &description)
{
    Node &node = add(path);
    node.m_kind = Kind::Signal;
    if (node.label.empty()) node.label = node.name();
    node.type = type.empty() ? "N" : type;
    node.access = 1;
    node.description = description;
    node.get = nullptr;
    node.set = nullptr;
    return node;
}

bool Tree::fire(const std::string &path, const ValueList &args) const
{
    const Node *node = find(path);
    if (!node || node->m_kind != Kind::Signal || node->isContainer()) return false;
    if (m_output) {
        osc::Message m;
        m.address = node->path();
        if (node->type != "N") {
            m.types = node->type;
            m.args = toOscArguments(node->type, args);
        }
        m_output(m);
    }
    notify(*node, args);
    return true;
}

bool Tree::changed(const std::string &path) const
{
    const Node *node = find(path);
    if (!node || node->kind() != Kind::Parameter) return false;
    if (m_output) m_output(message(*node));
    notify(*node);
    return true;
}

Node *Tree::find(const std::string &path) const
{
    const Node *node = &m_root;
    for (const std::string &name : names(path)) {
        node = node->child(name);
        if (!node) return nullptr;
    }
    return const_cast<Node *>(node);
}

bool Tree::remove(const std::string &path)
{
    Node *node = find(path);
    if (!node || !node->m_parent) return false;
    const std::string removedPath = node->path();
    const bool removed = node->m_parent->m_children.erase(node->m_name) > 0;
    if (removed && m_structure) m_structure(removedPath, Change::Removed);
    return removed;
}

void Tree::clear()
{
    m_root.m_children.clear();
    m_root.description.clear();
    if (m_structure) m_structure("/", Change::Cleared);
}

std::vector<Node *> Tree::match(const std::string &pattern) const
{
    std::vector<Node *> out;
    collect(const_cast<Node &>(m_root), segments(pattern), 0, out);
    return out;
}

int Tree::apply(const osc::Message &message) const
{
    ValueList args;
    args.reserve(message.args.size());
    for (const osc::Value &a : message.args) args.push_back(fromOsc(a));
    int accepted = 0;
    for (Node *node : match(message.address))
        if ((node->access & 2) && !node->locked && node->set) {
            // A write may cause another one (a mapping that drives a second node): each keeps its own state
            const Node *outer = m_applying;
            const bool outerNotified = m_applyNotified;
            m_applying = node;
            m_applyNotified = false;
            const bool ok = node->set(args);
            const bool told = m_applyNotified;
            m_applying = outer;
            m_applyNotified = outerNotified;
            if (!ok) continue;
            ++accepted;
            if (!told) notify(*node);
        }
    return accepted;
}

int Tree::addObserver(Observer observer)
{
    auto next = std::make_shared<std::vector<std::pair<int, Observer>>>(m_observers ? *m_observers : std::vector<std::pair<int, Observer>>());
    next->emplace_back(m_nextObserver, std::move(observer));
    m_observers = next;
    return m_nextObserver++;
}

void Tree::removeObserver(int id)
{
    if (!m_observers) return;
    auto next = std::make_shared<std::vector<std::pair<int, Observer>>>();
    for (const auto &entry : *m_observers)
        if (entry.first != id) next->push_back(entry);
    m_observers = next;
}

void Tree::notify(const Node &node, const ValueList &args) const
{
    if (&node == m_applying) m_applyNotified = true;
    if (!m_observers || m_observers->empty()) return;
    const auto observers = m_observers; // an observer may add or remove observers: it sees the list as it was
    const ValueList value = args.empty() && node.get && node.kind() == Kind::Parameter ? node.get() : args;
    for (const auto &entry : *observers) entry.second(node, value);
}

bool Tree::setLocked(const std::string &path, bool locked)
{
    Node *node = find(path);
    if (!node) return false;
    node->locked = locked;
    notify(*node);
    return true;
}

namespace {
void collectLocked(const Node &node, std::vector<std::string> &out)
{
    if (node.locked) out.push_back(node.path());
    for (const auto &entry : node.children()) collectLocked(*entry.second, out);
}
} // namespace

std::vector<std::string> Tree::lockedPaths() const
{
    std::vector<std::string> out;
    collectLocked(m_root, out);
    return out;
}

void Tree::setLockedPaths(const std::vector<std::string> &paths)
{
    m_pendingLocks.clear();
    for (const std::string &path : paths) {
        if (Node *node = find(path)) node->locked = true;
        else m_pendingLocks.push_back(path);
    }
}

bool Tree::reset(const std::string &path) const
{
    Node *node = find(path);
    if (!node || node->locked || !node->reset || !node->reset()) return false;
    notify(*node);
    return true;
}

osc::Message Tree::message(const Node &node)
{
    osc::Message m;
    m.address = node.path();
    if (node.type == "T" || node.type == "F") {
        const ValueList v = node.get ? node.get() : ValueList();
        m.types = !v.empty() && v[0].truthy() ? "T" : "F";
    } else if (node.get && node.type != "N") {
        m.types = node.type;
        m.args = toOscArguments(node.type, node.get());
    }
    return m;
}

} // namespace pxs::core::tree
