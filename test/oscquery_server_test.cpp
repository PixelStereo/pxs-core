#include <pxs/core/tree/oscquery_server.h>

#include "check.h"

#include <memory>

using namespace pxs::core;
using namespace pxs::core::tree;

namespace {

struct Device {
    Tree tree;
    double level = 0.5;
    ServerInfo info;
    Device()
    {
        info.name = "TestApp";
        info.oscPort = 1234;
        tree.addAction("/show/go", "i", [](const ValueList &) { return true; }, "Go to a cue");
        tree.addSignal("/detect/peak", "f", "A peak was heard");
        auto p = std::make_unique<Parameter>("level", "Level", ParamInfo::Type::Float);
        p->range(0, 1).bind([this] { return Value(level); }, [this](const Value &v) { level = v.number().value_or(0.0); });
        tree.add("/level", std::move(p));
    }
    HttpAnswer get(std::string_view target) const { return answerRequest(tree, info, "GET", target); }
};

void theProposalsRequests()
{
    Device d;
    CHECK(d.get("/?HOST_INFO").body.find("\"NAME\":\"TestApp\"") != std::string::npos);
    CHECK(d.get("/?HOST_INFO").body.find("\"OSC_PORT\":1234") != std::string::npos);
    CHECK_EQ(d.get("/level?VALUE").body, std::string("{\"VALUE\":[0.5]}"));
    CHECK_EQ(d.get("/level?value").body, std::string("{\"VALUE\":[0.5]}")); // the attribute is matched in upper case
    CHECK(d.get("/level").body.find("\"FULL_PATH\":\"/level\"") != std::string::npos);
    CHECK(d.get("/").body.find("\"CONTENTS\"") != std::string::npos);
    CHECK_EQ(d.get("").status, 200); // an empty target is the root
    CHECK_EQ(d.get("/level?VALUE#nose").body, std::string("{\"VALUE\":[0.5]}")); // a fragment is disregarded
    CHECK_EQ(d.get("/foo?HOST_INFO").body, d.get("/?HOST_INFO").body);             // the path of HOST_INFO is irrelevant
}

void statuses()
{
    Device d;
    CHECK_EQ(d.get("/nothing").status, 404);
    CHECK(d.get("/nothing").body.empty());
    CHECK_EQ(d.get("/nothing?TYPE").status, 404);
    // an attribute that is absent: an empty object, and 204 only for a value that cannot be read or an attribute this server lacks
    CHECK(d.get("/detect/peak?VALUE").status == 200 && d.get("/detect/peak?VALUE").body == "{}");
    CHECK(d.get("/show?TYPE").status == 200 && d.get("/show?TYPE").body == "{}");
    CHECK_EQ(d.get("/show/go?VALUE").status, 204);
    CHECK_EQ(d.get("/level?NOPE").status, 204);
    CHECK_EQ(answerRequest(d.tree, d.info, "POST", "/").status, 405);
    CHECK_EQ(answerRequest(d.tree, d.info, "DELETE", "/level").status, 405);
    CHECK_EQ(answerRequest(d.tree, d.info, "OPTIONS", "/level").status, 204);
    CHECK_EQ(answerRequest(d.tree, d.info, "HEAD", "/level").status, 200);
    CHECK_EQ(d.get("/level?VALUE%00").status, 400); // a NUL after decoding
    // an extension the server has declared is supported: its absence on a node is an empty object, not 204
    CHECK(d.get("/level?CLIPMODE").status == 200 && d.get("/level?CLIPMODE").body.find("both") != std::string::npos);
    CHECK_EQ(d.get("/detect/peak?CLIPMODE").body, std::string("{}"));
    d.info.extensionsJson = "{\"VALUE\":true}";
    CHECK_EQ(d.get("/detect/peak?CLIPMODE").status, 204); // not declared by this server
}

void ownAttributesAreKnown()
{
    Device d;
    Node &n = d.tree.add("/x");
    n.type = "f";
    n.access = 3;
    n.get = [] { return ValueList{Value(1.0)}; };
    n.attributes["PXS_NONE"] = [] { return std::optional<std::string>(); };
    CHECK_EQ(d.get("/x?PXS_NONE").status, 200); // the node's own attribute, with no value: an empty object
    CHECK_EQ(d.get("/x?PXS_OTHER").status, 204);
}

void targetsAreSplit()
{
    CHECK_EQ(splitTarget("/a/b?value#frag").path, std::string("/a/b"));
    CHECK_EQ(splitTarget("/a/b?value#frag").query, std::string("VALUE"));
    CHECK_EQ(splitTarget("/a%20b/c%2Fd").path, std::string("/a b/c/d"));
    CHECK_EQ(splitTarget("/100%").path, std::string("/100%"));      // a % that is no escape is kept
    CHECK_EQ(splitTarget("/%zz").path, std::string("/%zz"));
    CHECK_EQ(splitTarget("/a%4").path, std::string("/a%4"));
    CHECK_EQ(splitTarget("").path, std::string("/"));
    CHECK_EQ(splitTarget("?VALUE").path, std::string("/"));
    CHECK_EQ(splitTarget("/a?b?c").query, std::string("B?C"));       // only the first ? starts the query
    CHECK(!splitTarget("/a%00").ok && splitTarget("/a").ok);
    CHECK_EQ(splitTarget("/caf%C3%A9").path, std::string("/caf\xC3\xA9"));
}

void originsAreChecked()
{
    CHECK(originAllowed(""));
    CHECK(originAllowed("http://localhost:8080") && originAllowed("http://LOCALHOST") && originAllowed("https://127.0.0.1:3000"));
    CHECK(originAllowed("http://[::1]:80") && originAllowed("http://127.1.2.3"));
    CHECK(!originAllowed("null") && !originAllowed("http://example.com") && !originAllowed("http://") && !originAllowed("not a url"));
    CHECK(!originAllowed("http://192.168.1.20")); // another machine, unless it is this one
    CHECK(originAllowed("http://192.168.1.20:8000", {"192.168.1.20"}) && originAllowed("http://FE80::1", {}) == false);
    CHECK(originAllowed("http://[fe80::1]", {"FE80::1"}));
    CHECK(!originAllowed("http://[::1", {}));
}

void commandsAreRead()
{
    const Command c = parseCommand("{\"COMMAND\":\"LISTEN\",\"DATA\":\"/level\"}");
    CHECK(c.command == "LISTEN" && c.data == "/level");
    for (const char *bad : {"", "nope", "[1]", "{\"COMMAND\":3,\"DATA\":null}", "{}"}) {
        const Command b = parseCommand(bad);
        CHECK(b.command.empty() && b.data.empty());
    }
    CHECK_EQ(parseCommand("{\"COMMAND\":\"IGNORE\"}").data, std::string());
}

} // namespace

int main()
{
    theProposalsRequests();
    statuses();
    ownAttributesAreKnown();
    targetsAreSplit();
    originsAreChecked();
    commandsAreRead();
    return pxs_test::finish("oscquery_server");
}
