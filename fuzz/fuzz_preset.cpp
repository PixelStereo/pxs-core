#include <pxs/core/tree/oscquery.h>
#include <pxs/core/tree/preset.h>

#include <cstdlib>

// Whatever text arrives as a preset, an attribute or a HOST_INFO member: nothing may crash, and a preset read from text writes
// out as text that reads back as the same preset.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    using namespace pxs::core;
    using namespace pxs::core::tree;
    const std::string text(reinterpret_cast<const char *>(data), size);

    const Preset p = Preset::fromJson(text);
    const Preset q = Preset::fromJson(p.toJson());
    if (q.name != p.name || q.values.size() != p.values.size() || q.actions.size() != p.actions.size()) std::abort();

    Tree tree;
    struct Cell { Value value; } cell;
    auto param = std::make_unique<Parameter>("x", "x", ParamInfo::Type::Float);
    param->range(-1, 1).bind([&cell] { return cell.value; }, [&cell](const Value &v) { cell.value = v; });
    Node &node = tree.add("/x", std::move(param));
    node.attributes["PXS_A"] = [text] { return std::optional<std::string>(text); };
    tree.addAction("/act", "[ff]i", [](const ValueList &) { return true; });
    Preset recalled = p;
    recalled.values["/x"] = text;
    recalled.actions.push_back({"/act", text});
    recall(tree, recalled);
    if (const auto *d = cell.value.get<double>(); d && !(*d >= -1.0 && *d <= 1.0)) std::abort();

    std::string out;
    attributeJson(tree, "/x", "PXS_A", out);
    (void)describeJson(tree, "/");
    (void)hostInfoJson(text, 1, text, text);
    return 0;
}
