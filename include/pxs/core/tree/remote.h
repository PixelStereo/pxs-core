#pragma once
// What an OSCQuery device says of itself (its answer to GET /), kept as a tree an application can browse and read: each node's address,
// type tags, access, range, default, description and last value. The client side of the tree: this is where "a node with a value, a node
// that only holds others" is decided for what a client reads, with the same rule as the server's (Node::hasValue): a node with a TYPE or
// an ACCESS has a value, whether it holds other nodes or not; one with neither is only a container.
//
// No sockets here (the HTTP, the WebSocket LISTEN and the UDP writes are the application layer's: Qt, dart:io): this takes the JSON text a
// device answered and the values it told, and says what they mean. A node that holds one argument can be seen as a ParamInfo, which is how
// a control becomes a view of a remote parameter.
#include <pxs/core/tree/parameter.h>
#include <pxs/core/tree/tree.h>
#include <pxs/core/value.h>

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace pxs::core::tree {

// OSCQuery momentary controls use a single Nil or Infinitum tag on a writable node.
constexpr bool isMomentaryAction(std::string_view type, int access)
{
    return (access & ParamInfo::Write) && (type == "N" || type == "I");
}

struct RemoteNode {
    std::string name;        // its segment ("opacity"); the root's is empty
    std::string path;        // "/layer/1/opacity"; the root's is "/"
    std::string type;        // OSC type tags, one per argument; empty for a container
    std::string description;
    std::string kind;        // "action" (written, no value), "signal" (read, no value), or empty (a parameter, or a container)
    int access = 0;          // 0 none (a container), 1 read, 2 write, 3 both
    std::vector<ArgRange> range;     // one entry per argument
    ValueList value;                 // the last one known, one entry per argument (empty: none known)
    ValueList defaultValue;          // DEFAULT_VALUE: what a reset puts back, one entry per argument (empty: none)
    std::vector<std::string> extendedType; // EXTENDED_TYPE, one per argument ("" for none): "color.rgb.r", "filepath"…
    std::vector<std::string> unit;         // UNIT, one per argument ("" for none): "distance.m"
    std::vector<std::string> tags;         // TAGS
    std::map<std::string, RemoteNode> children;

    // The same rule as Node::hasValue / isContainer
    bool hasValue() const { return !type.empty() || access != 0; }
    bool isContainer() const { return !hasValue(); }
    // As one parameter: false when it has no type or several arguments (a node of one argument only)
    bool toParamInfo(ParamInfo *out) const;
};

class RemoteTree {
public:
    // Replaces everything with a device's answer to GET / (JSON text); false when it is not a node
    bool load(const std::string &json);
    void clear()
    {
        m_root = RemoteNode();
        m_loaded = false;
    }
    bool loaded() const { return m_loaded; }
    const RemoteNode &root() const { return m_root; }
    const RemoteNode *find(const std::string &path) const;
    // A value the device told (LISTEN, ?VALUE): its JSON array text, one entry per argument; false: no such node or not an array
    bool setValueJson(const std::string &path, const std::string &valueJson);
    bool setValue(const std::string &path, ValueList value);
    // What the tree is made of, not its values: two trees with the same signature show the same
    std::string signature() const;
    // The addresses of every node that has a value, in order
    std::vector<std::string> leaves() const;

private:
    RemoteNode m_root;
    bool m_loaded = false;
};

// One node as the JSON an application's own layer can read (what a Dart or a QML side takes through the C interface): its address, name,
// whether it has a value and whether it holds nodes, and as one parameter its ParamInfo members; "{}" when there is no such node.
std::string remoteNodeJson(const RemoteTree &tree, const std::string &path);

} // namespace pxs::core::tree
