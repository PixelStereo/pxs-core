#pragma once
// The address space of an application: a tree of named nodes. A node is a container, or a method that is read, written or
// both, described the way OSCQuery describes it (type tags, access, range). The OSCQuery answers are derived from the tree and OSC
// messages are applied to it, so nothing is written twice.
//
// A node can be declared through a Parameter, or filled in by hand (type, access, get, set) when the application already has its
// own way to read and write the value.
//
// Three kinds of leaf, the same in every application (a container holds them):
//   parameter  has a state: it is read, written, saved, and tells when it changes (a Parameter, or filled by hand)
//   action     has no state: it is received and does something (an action, a trigger, with or without arguments)
//   signal     has no state: the application emits it (a detection, a button pressed, a meter), nobody writes it
// OSCQuery says them with ACCESS and VALUE only: a parameter always has a VALUE that can be read (ACCESS 1 or 3); an action has
// ACCESS 2 and no VALUE; a signal has ACCESS 1 and no VALUE. So what nobody can read back is never a parameter: a setter without
// getter is declared as an action.
//
// The idea comes from libossia's device tree. Left out on purpose: bounding modes, repetition filters, a callback list on every
// node. A write already tells the others through ChangeFeed.
//
// No Qt, no global state, nothing thrown. Not thread safe: the writes go through one thread at a time (the application's), as in
// every pxs application; a signal fired from another thread is the application's to serialise.
#include <pxs/core/osc/codec.h>
#include <pxs/core/tree/parameter.h>
#include <pxs/core/value.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pxs::core::tree {

class Tree;

enum class Kind { Container, Parameter, Action, Signal };

// The range of one OSC argument: MIN and MAX, or the VALS it can take. For an array in the type tags ("[ff]") `items` holds the range
// of each of its elements (and the other members are not used): OSCQuery writes it as a nested array.
struct ArgRange {
    std::optional<Value> min, max;
    std::vector<Value> vals;
    std::vector<ArgRange> items;
    bool empty() const { return !min && !max && vals.empty() && items.empty(); }
};

class Node
{
public:
    const std::string &name() const { return m_name; }
    std::string path() const; // "/layer/1/opacity"; the root is "/"
    Node *parent() const { return m_parent; }
    Node *child(const std::string &name) const;
    const std::map<std::string, std::unique_ptr<Node>> &children() const { return m_children; }
    Parameter *parameter() const { return m_parameter.get(); } // when it was declared through a Parameter

    // What it is, as OSCQuery says it
    std::string type;        // OSC type tags, one per argument ("f", "ff", "T"); empty for a container or an action without argument
    int access = 0;          // 0 none (a container), 1 read, 2 write, 3 both
    std::string description;
    std::string label;       // what an interface shows for it ("Opacity"); empty: its name
    bool locked = false;     // refuses every write (OSC, a mapping, the panel) until unlocked; the application's own code is not stopped
    // A method of this node (its reset, the setter of a range…): reached by its address like any node, but not listed in the
    // CONTENTS of its node, and it does not make a parameter a container: the parameter stays a value, with its VALUE
    bool attached = false;
    std::function<bool()> reset;            // puts the default back (a parameter with a default; empty: nothing to reset)
    std::vector<ArgRange> range;            // one entry per argument
    // What a client may be told besides: one entry per argument ("" for none), and the node's tags
    std::vector<std::string> extendedType;  // EXTENDED_TYPE: what each argument is ("color.rgb.r")
    std::vector<std::string> unit;          // UNIT: what each argument measures ("distance.m")
    std::vector<std::string> tags;          // TAGS: for a client to search and filter by
    std::function<ValueList()> defaultValue; // DEFAULT_VALUE, what a reset puts back; empty: none
    std::string clip;                       // "both" when the range is enforced (CLIPMODE)
    std::function<ValueList()> get;         // the value, one entry per argument
    std::function<bool(const ValueList &)> set; // false: refused (a locked layer, a value that does not fit)
    // The application's own attributes (an extension of OSCQuery), asked with ?NAME: each gives a JSON value as text, or nothing
    // (no value for it)
    std::map<std::string, std::function<std::optional<std::string>()>> attributes;

    std::string displayLabel() const { return label.empty() ? m_name : label; }
    bool animatable() const { return m_parameter && m_parameter->info().animatable; } // a timeline can drive it
    // A node has a value of its own (a type or an access), whether it holds other nodes or not: "/layer/1/color/tint" is a value and
    // holds "/layer/1/color/tint/enable". A node with neither is only a container. Being a parameter and holding children are two
    // separate questions, and these are the answers for every application and every client.
    bool hasValue() const { return !type.empty() || access != 0; }
    bool isContainer() const { return !hasValue(); }
    Kind kind() const { return hasValue() ? m_kind : Kind::Container; }
    // The children that are listed (the attached methods are not)
    bool hasListedChildren() const
    {
        for (const auto &entry : m_children)
            if (!entry.second->attached) return true;
        return false;
    }

private:
    friend class Tree;
    Kind m_kind = Kind::Parameter;
    std::string m_name;
    Node *m_parent = nullptr;
    std::map<std::string, std::unique_ptr<Node>> m_children;
    std::unique_ptr<Parameter> m_parameter;
};

class Tree
{
public:
    Tree() = default;
    Tree(const Tree &) = delete;
    Tree &operator=(const Tree &) = delete;

    Node &root() { return m_root; }
    const Node &root() const { return m_root; }

    // The node at this address, with its containers made on the way. A name cannot hold a space or any of  # * , ? [ ] { }  in OSC:
    // they become '_'.
    Node &add(const std::string &path);
    // The same, declared through a Parameter, which the tree owns: type, access, range, choices and the way to read and write the
    // value come from it
    Node &add(const std::string &path, std::unique_ptr<Parameter> parameter);
    // An action: received, no state. `type` is the OSC type tags of its arguments ("N" when it takes none); the handler gets the
    // arguments and returns false to refuse them.
    Node &addAction(const std::string &path, const std::string &type, std::function<bool(const ValueList &)> handler,
                    const std::string &description = {});
    // A signal: emitted by the application, no state. Say it with fire().
    Node &addSignal(const std::string &path, const std::string &type = "N", const std::string &description = {});
    Node *find(const std::string &path) const;
    bool remove(const std::string &path);
    void clear();

    // Every node whose address matches an OSC address pattern (* ? [abc] {a,b}), containers included
    std::vector<Node *> match(const std::string &pattern) const;

    // Hands an OSC message to every method its address names that can be written; returns how many accepted it
    int apply(const osc::Message &message) const;
    // The same, with the arguments already as values (a preset recalled, a mapping)
    int apply(const std::string &address, const ValueList &args) const;
    // The OSC message that says what a method holds now (the feedback to send)
    static osc::Message message(const Node &node);

    // Where everything the application says goes (the OSC output, the listeners): a signal emitted, a parameter that changed. Set by
    // whoever sends; one at a time.
    void setOutput(std::function<void(const osc::Message &)> output) { m_output = std::move(output); }
    // Told when the structure changes: a node was made (the path of the highest one made), a node was removed (its own path), the tree
    // was cleared ("/"). One at a time, set by whoever tells the clients (OSCQuery's PATH_ADDED, PATH_REMOVED).
    enum class Change { Added, Removed, Cleared };
    void setStructureObserver(std::function<void(const std::string &, Change)> observer) { m_structure = std::move(observer); }
    // Who else wants to know, inside the application (a panel that shows the values, the mappings): called when a parameter was written
    // (OSC, a mapping, the panel) or told changed, a signal was fired, a node was locked. Returns an id for removeObserver(). Called on
    // the thread that wrote. An observer may add or remove observers: it sees the list as it was.
    using Observer = std::function<void(const Node &, const ValueList &)>; // the value fired, or the one now held
    int addObserver(Observer observer);
    void removeObserver(int id);
    bool setLocked(const std::string &path, bool locked); // false: no such node
    // The locks are saved with what the application saves: the addresses of the locked nodes. A lock set before its node exists (the
    // node is made later) is kept and applied when it is added.
    std::vector<std::string> lockedPaths() const;
    void setLockedPaths(const std::vector<std::string> &paths);
    bool reset(const std::string &path) const; // false: no such node, locked, or nothing to reset
    // Fires a signal (nothing is sent when the path is not a signal), with the arguments its type tags name
    bool fire(const std::string &path, const ValueList &args = {}) const;
    // Says that a parameter now holds another value (the feedback: its current message goes to the output)
    bool changed(const std::string &path) const;

private:
    void notify(const Node &node, const ValueList &args = {}) const;

    Node m_root;
    std::vector<std::string> m_pendingLocks;
    std::function<void(const osc::Message &)> m_output;
    std::function<void(const std::string &, Change)> m_structure;
    // Copy on write: a fired signal (thousands a second) takes one reference, not a copy of the list
    std::shared_ptr<const std::vector<std::pair<int, Observer>>> m_observers;
    int m_nextObserver = 1;
    mutable const Node *m_applying = nullptr; // the node apply() is writing
    mutable bool m_applyNotified = false;
};

// The OSC type tag of a parameter ("f", "i", "T" for a switch, "s", "N" for an action)
std::string oscType(const ParamInfo &info);

} // namespace pxs::core::tree
