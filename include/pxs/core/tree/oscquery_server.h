#pragma once
// What an OSCQuery server answers to an HTTP request, without the HTTP: the request line's method and target in, a status and a JSON
// body out. The server (Qt's QTcpServer, Dart's HttpServer, a socket of its own) reads the request and writes the answer; what the
// answer is, is decided here, the same on every platform.
//
//   GET <osc path>                the description of the node and of everything below it (404: no such node)
//   GET <osc path>?VALUE|?TYPE|…  one attribute, alone ("{}" when the node has none; 204: a value asked of what cannot be read, or an
//                                 attribute this server does not support; 404: no such node)
//   GET /?HOST_INFO               the server's name, its OSC port, what it supports
//   OPTIONS                       204 (a browser's preflight); anything but GET, HEAD and OPTIONS is 405; a target that does not parse is 400
//
// Reading is open to a web page: setting the CORS headers is the server's.
#include <pxs/core/tree/oscquery.h>

#include <string>
#include <string_view>
#include <vector>

namespace pxs::core::tree {

struct ServerInfo {
    std::string name = "pxs";
    int oscPort = 0;
    std::string extensionsJson = defaultExtensionsJson(); // what the server supports: an object, attribute -> true
    std::string moreJson = "{}";                          // other HOST_INFO members (WS_PORT), an object
};

struct HttpAnswer {
    int status = 200;
    std::string body; // JSON text; empty for 204 and the errors
};

HttpAnswer answerRequest(const Tree &tree, const ServerInfo &info, std::string_view method, std::string_view target);

// A request target split the way an OSCQuery server reads it: the fragment cut off, the path and the query percent-decoded (a `%`
// that is no escape is kept as it is), the query in upper case (ASCII). `ok` is false for a target with a NUL after decoding.
struct Target {
    std::string path; // "/" when empty
    std::string query;
    bool ok = true;
};
Target splitTarget(std::string_view target);

// Whether a browser page of this Origin may open a WebSocket on this machine: no Origin (a native client), or localhost, a loopback
// address, or one of `ownAddresses` (the addresses of this machine's interfaces, as text). A name, "null" and every other host are refused:
// a rebound DNS name reaches this machine too.
bool originAllowed(std::string_view origin, const std::vector<std::string> &ownAddresses = {});

// The COMMAND and DATA of a WebSocket text message ({"COMMAND": "LISTEN", "DATA": "/path"}); empty strings when it is not one.
struct Command {
    std::string command, data;
};
Command parseCommand(std::string_view json);

} // namespace pxs::core::tree
