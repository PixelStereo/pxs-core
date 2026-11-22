#include <pxs/core/tree/oscquery_server.h>

#include <cstdlib>

// Whatever request target arrives, the answer is one of the statuses OSCQuery uses, with a body only when it is a 200.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    using namespace pxs::core;
    using namespace pxs::core::tree;
    static Tree tree;
    static const bool built = [] {
        tree.addAction("/show/go", "i", [](const ValueList &) { return true; });
        tree.addSignal("/detect/peak", "f");
        tree.add("/a b/c");
        return true;
    }();
    (void)built;
    static const ServerInfo info;
    const std::string_view target(reinterpret_cast<const char *>(data), size);
    for (const char *method : {"GET", "HEAD", "OPTIONS", "POST"}) {
        const HttpAnswer a = answerRequest(tree, info, method, target);
        if (a.status != 200 && a.status != 204 && a.status != 400 && a.status != 404 && a.status != 405) std::abort();
        if (a.status != 200 && !a.body.empty()) std::abort();
    }
    (void)splitTarget(target);
    (void)originAllowed(target, {"10.0.0.1"});
    (void)parseCommand(target);
    return 0;
}
