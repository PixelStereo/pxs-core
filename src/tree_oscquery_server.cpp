#include <pxs/core/tree/oscquery_server.h>

#include <nlohmann/json.hpp>

namespace pxs::core::tree {
namespace {

using Json = nlohmann::json;

int hexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string percentDecode(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && hexValue(s[i + 1]) >= 0 && hexValue(s[i + 2]) >= 0) {
            out.push_back(static_cast<char>(hexValue(s[i + 1]) * 16 + hexValue(s[i + 2])));
            i += 2;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

std::string upperAscii(std::string s)
{
    for (char &c : s)
        if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    return s;
}

std::string lowerAscii(std::string s)
{
    for (char &c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

} // namespace

Target splitTarget(std::string_view target)
{
    Target t;
    if (const auto hash = target.find('#'); hash != std::string_view::npos) target = target.substr(0, hash); // a fragment is disregarded
    std::string_view path = target, query;
    if (const auto q = target.find('?'); q != std::string_view::npos) {
        path = target.substr(0, q);
        query = target.substr(q + 1);
    }
    t.path = percentDecode(path);
    t.query = upperAscii(percentDecode(query));
    if (t.path.find('\0') != std::string::npos || t.query.find('\0') != std::string::npos) t.ok = false;
    if (t.path.empty()) t.path = "/";
    return t;
}

HttpAnswer answerRequest(const Tree &tree, const ServerInfo &info, std::string_view method, std::string_view target)
{
    HttpAnswer a;
    if (method == "OPTIONS") {
        a.status = 204;
        return a;
    }
    if (method != "GET" && method != "HEAD") {
        a.status = 405;
        return a;
    }
    const Target t = splitTarget(target);
    if (!t.ok) {
        a.status = 400;
        return a;
    }
    if (t.query == "HOST_INFO") {
        a.body = hostInfoJson(info.name, info.oscPort, info.extensionsJson, info.moreJson);
        return a;
    }
    if (!t.query.empty()) {
        std::string json;
        if (!attributeJson(tree, t.path, t.query, json)) {
            a.status = 404;
            return a;
        }
        if (json == "{}") {
            const Node *node = tree.find(t.path);
            const Json extensions = Json::parse(info.extensionsJson, nullptr, false);
            const bool supported = extensions.is_object() && extensions.contains(t.query) && extensions[t.query] == true;
            const bool known = t.query == "FULL_PATH" || t.query == "CONTENTS" || t.query == "TYPE" || supported || node->attributes.count(t.query) > 0;
            // 204: a value asked of what cannot be read, or an attribute this server does not have; otherwise the node has no such
            // attribute (a container's TYPE, a signal's VALUE): an empty object
            if ((t.query == "VALUE" && !(node->access & 1)) || !known) {
                a.status = 204;
                return a;
            }
        }
        a.body = std::move(json);
        return a;
    }
    a.body = describeJson(tree, t.path);
    if (a.body == "{}") {
        a.status = 404;
        a.body.clear();
    }
    return a;
}

bool originAllowed(std::string_view origin, const std::vector<std::string> &ownAddresses)
{
    if (origin.empty()) return true;
    // scheme://host[:port][/…]
    std::string_view rest = origin;
    if (const auto scheme = rest.find("://"); scheme != std::string_view::npos) rest = rest.substr(scheme + 3);
    else return false;
    if (const auto slash = rest.find('/'); slash != std::string_view::npos) rest = rest.substr(0, slash);
    std::string host;
    if (!rest.empty() && rest.front() == '[') { // an IPv6 literal
        const auto end = rest.find(']');
        if (end == std::string_view::npos) return false;
        host = std::string(rest.substr(1, end - 1));
    } else {
        host = std::string(rest.substr(0, rest.find(':')));
    }
    if (host.empty()) return false; // "null": a sandboxed frame of any page sends it
    host = lowerAscii(host);
    if (host == "localhost") return true;
    if (host == "::1" || host.rfind("127.", 0) == 0) return true; // a loopback address
    for (const std::string &own : ownAddresses)
        if (lowerAscii(own) == host) return true;
    return false; // a name too: a rebound DNS name reaches this machine as well
}

Command parseCommand(std::string_view json)
{
    Command c;
    const Json o = Json::parse(json, nullptr, false);
    if (!o.is_object()) return c;
    if (const auto it = o.find("COMMAND"); it != o.end() && it->is_string()) c.command = it->get<std::string>();
    if (const auto it = o.find("DATA"); it != o.end() && it->is_string()) c.data = it->get<std::string>();
    return c;
}

} // namespace pxs::core::tree
