#pragma once
// What an OSCQuery client is told about a tree, as JSON text. (The HTTP server, the announcement over mDNS and the push to
// listeners belong to the layer that has sockets; this is what they say.) The JSON is returned as text so that nothing of the
// library that writes it is in a header, and so that every binding (Qt, a C ABI) can read it with its own parser. Keys come in
// alphabetical order, numbers in their shortest form.
#include <pxs/core/tree/tree.h>

#include <string>
#include <vector>

namespace pxs::core::tree {

// The description of a node and of everything below it (FULL_PATH, CONTENTS, ACCESS, TYPE, VALUE, RANGE, CLIPMODE, DESCRIPTION,
// EXTENDED_TYPE, UNIT, DEFAULT_VALUE, TAGS, PXS_LOCKED and the node's own attributes); "{}" when there is no such node.
std::string describeJson(const Tree &tree, const std::string &path = "/");

// One attribute of a node (?VALUE, ?TYPE…): false when there is no such node. `out` is "{}" when the node has no value for it (the
// HTTP answer is then 204), else `{"NAME": value}`. The name is matched in upper case.
bool attributeJson(const Tree &tree, const std::string &path, const std::string &name, std::string &out);

// A value as OSCQuery writes it, by the type tag of its argument: numbers, strings, a colour ('r') as #RRGGBBAA, a switch as a
// boolean, null for what has no description (N, I, blob, MIDI).
std::string valueJson(char tag, const Value &value);
// The VALUE (or DEFAULT_VALUE) of a type tag string: one entry per argument, an array in the tags an array in the JSON.
std::string valuesJson(const std::string &type, const ValueList &values);

// Every attribute of the OSCQuery proposal, and whether pxs serves it. The ones marked false are declared so that the whole list is in
// one place: implement each when an application needs it (a field on Node, served by describeJson, read by the clients, tested),
// then mark it true. FULL_PATH, CONTENTS, TYPE and ACCESS are the ones a client cannot do without; the others are optional.
struct OscQueryAttribute {
    const char *name;
    bool served;
    const char *note;
};
const std::vector<OscQueryAttribute> &oscQueryAttributes();

// The attributes a pxs server supports, as the EXTENSIONS of HOST_INFO (a JSON object: name -> true).
std::string defaultExtensionsJson();
// The /?HOST_INFO answer; `extensionsJson` (which attributes are supported) and `moreJson` (an object whose members are added to
// the answer, and win) are JSON text; text that is not an object is ignored.
std::string hostInfoJson(const std::string &name, int oscPort, const std::string &extensionsJson = defaultExtensionsJson(),
                         const std::string &moreJson = "{}");

} // namespace pxs::core::tree
