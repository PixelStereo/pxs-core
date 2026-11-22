#include <pxs/core/tree/tree.h>

#include <cstdlib>

// Whatever bytes arrive as an OSC packet, they are applied to a tree that has a parameter of every type: nothing may crash, and a
// parameter never holds a value outside what it declared.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    using namespace pxs::core;
    using namespace pxs::core::tree;
    struct Cell { Value value; };
    static Cell cells[6];
    static Tree tree;
    static const bool built = [] {
        const ParamInfo::Type types[] = {ParamInfo::Type::Float, ParamInfo::Type::Int, ParamInfo::Type::Bool, ParamInfo::Type::Choice,
                                         ParamInfo::Type::Text, ParamInfo::Type::Trigger};
        const char *names[] = {"f", "i", "b", "c", "t", "g"};
        for (int k = 0; k < 6; ++k) {
            auto p = std::make_unique<Parameter>(names[k], names[k], types[k]);
            if (k == 0) p->range(-1, 1);
            if (k == 1) p->values({1, 3, 9});
            if (k == 3) p->choices({"a", "b", "c"});
            p->bind([k] { return cells[k].value; }, [k](const Value &v) { cells[k].value = v; });
            tree.add(std::string("/") + names[k], std::move(p));
        }
        return true;
    }();
    (void)built;
    std::vector<osc::Message> messages;
    if (!osc::decode(data, size, messages)) return 0;
    for (const auto &m : messages) tree.apply(m);
    if (const auto *f = cells[0].value.get<double>(); f && !(*f >= -1.0 && *f <= 1.0)) std::abort();
    if (const auto *i = cells[1].value.get<std::int32_t>(); i && *i != 1 && *i != 3 && *i != 9) std::abort();
    if (const auto *c = cells[3].value.get<std::string>(); c && *c != "a" && *c != "b" && *c != "c") std::abort();
    return 0;
}
