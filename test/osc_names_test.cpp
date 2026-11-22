#include <pxs/core/osc/names.h>

#include "check.h"
#include "golden.h"

#include <algorithm>

using namespace pxs::core::osc;

namespace {

void goldenSafeNames()
{
    for (const auto &g : golden::lines("name")) CHECK_EQ(golden::hex(reinterpret_cast<const std::uint8_t *>(safeName(golden::fromHex(g[1])).data()), safeName(golden::fromHex(g[1])).size()), g[2]);
}

void goldenSegments()
{
    for (const auto &g : golden::lines("segments")) {
        std::vector<std::string> names;
        for (const auto &h : golden::split(g[1], ',')) names.push_back(golden::fromHex(h));
        // an empty name is an empty hex string, which split() drops: restore it
        const auto count = std::count(g[1].begin(), g[1].end(), ',') + 1;
        names.resize(std::size_t(count));
        std::string joined;
        for (const auto &s : uniqueSegments(names)) joined += (joined.empty() ? "" : ",") + golden::hex(reinterpret_cast<const std::uint8_t *>(s.data()), s.size());
        CHECK_EQ(joined, g[2]);
    }
}

void emptyAndReserved()
{
    CHECK_EQ(safeName(""), std::string("_"));
    CHECK_EQ(safeName("   "), std::string("_"));
    CHECK_EQ(safeName("a/b"), std::string("a_b"));
    CHECK_EQ(safeName("#*,/?[]{}!"), std::string("__________"));
    CHECK_EQ(safeName("  Layer 1  "), std::string("Layer_1"));
    CHECK_EQ(safeName("\xC2\xA0x\xE3\x80\x80"), std::string("x"));        // no-break and ideographic space at the ends
    CHECK_EQ(safeName("a\xE2\x80\xA8" "b"), std::string("a_b"));          // line separator inside
}

void invalidBytesAreKept()
{
    CHECK_EQ(safeName("a\xFF" "b"), std::string("a\xFF" "b"));
    CHECK_EQ(safeName("\xFF"), std::string("\xFF"));
    CHECK_EQ(safeName(" \xC3 "), std::string("\xC3"));                    // a cut lead byte is not white space
}

void segmentNamer()
{
    SegmentNamer n;
    CHECK_EQ(n.take("/l", "Video"), std::string("Video"));
    CHECK_EQ(n.take("/l", "Video"), std::string("Video_2"));
    CHECK_EQ(n.take("/l", "Video 2"), std::string("Video_2_2")); // the suffix another name gave itself is never shared
    CHECK_EQ(n.take("/other", "Video"), std::string("Video"));   // another scope starts again
    CHECK_EQ(n.take("/l", "Video"), std::string("Video_3"));
}

} // namespace

int main()
{
    goldenSafeNames();
    goldenSegments();
    emptyAndReserved();
    invalidBytesAreKept();
    segmentNamer();
    return pxs_test::finish("osc_names");
}
