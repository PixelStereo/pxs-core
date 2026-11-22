#include <pxs/core/tree/osc_bridge.h>

#include <cmath>
#include <limits>

namespace pxs::core::tree {
namespace {

// A number, as a whole number held within the limits of a type (what is not a number is 0; not-a-number is 0 too).
std::int64_t wholeNumber(const Value &v, double lo, double hi)
{
    const auto n = v.number();
    if (!n || std::isnan(*n)) return 0;
    const double r = std::round(*n);
    if (r <= lo) return static_cast<std::int64_t>(lo);
    if (r >= hi) return static_cast<std::int64_t>(hi);
    return static_cast<std::int64_t>(r);
}

std::uint32_t firstCodePoint(const std::string &s)
{
    if (s.empty()) return 0;
    const auto b = [&](std::size_t i) { return static_cast<std::uint8_t>(s[i]); };
    const std::uint8_t lead = b(0);
    if (lead < 0x80) return lead;
    std::size_t n = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 0;
    if (n == 0 || s.size() < n) return 0xFFFD;
    std::uint32_t cp = lead & (0xFF >> (n + 1));
    for (std::size_t i = 1; i < n; ++i) cp = (cp << 6) | (b(i) & 0x3F);
    return cp;
}

} // namespace

Value fromOsc(const osc::Value &argument)
{
    struct Visitor {
        Value operator()(const std::monostate &) const { return Value(); }
        Value operator()(bool v) const { return Value(v); }
        Value operator()(std::int32_t v) const { return Value(v); }
        Value operator()(std::int64_t v) const { return Value(v); }
        Value operator()(float v) const { return Value(v); }
        Value operator()(double v) const { return Value(v); }
        Value operator()(const std::string &v) const { return Value(v); }
        Value operator()(const osc::Blob &v) const { return Value(Blob(v)); }
        Value operator()(std::uint32_t v) const { return Value(static_cast<std::int64_t>(v)); }
    };
    return std::visit(Visitor{}, argument);
}

osc::Value toOsc(char tag, const Value &value)
{
    constexpr double i32lo = double(std::numeric_limits<std::int32_t>::min()), i32hi = double(std::numeric_limits<std::int32_t>::max());
    constexpr double i64lo = -9223372036854775808.0, i64hi = 9223372036854774784.0; // the last double below 2^63
    switch (tag) {
    case 'i': return osc::Value(static_cast<std::int32_t>(wholeNumber(value, i32lo, i32hi)));
    case 'h':
    case 't': return osc::Value(wholeNumber(value, i64lo, i64hi));
    case 'f': return osc::Value(static_cast<float>(value.number().value_or(0.0)));
    case 'd': return osc::Value(value.number().value_or(0.0));
    case 's':
    case 'S': return osc::Value(value.text());
    case 'b':
        if (const auto *blob = value.get<Blob>()) return osc::Value(osc::Blob(blob->begin(), blob->end()));
        {
            const std::string text = value.text();
            return osc::Value(osc::Blob(text.begin(), text.end()));
        }
    case 'c': return osc::Value(firstCodePoint(value.text()));
    case 'r':
    case 'm': return osc::Value(static_cast<std::uint32_t>(wholeNumber(value, i64lo, i64hi)));
    case 'T':
    case 'F': return osc::Value(value.truthy());
    default: return osc::Value(); // N, I and the unknown tags carry no data
    }
}

std::vector<osc::Value> toOscArguments(const std::string &type, const ValueList &values)
{
    ValueList flat; // a vector is as many floats
    for (const Value &v : values) {
        if (const auto *a = v.get<Vec2>()) for (float f : *a) flat.emplace_back(f);
        else if (const auto *b = v.get<Vec3>()) for (float f : *b) flat.emplace_back(f);
        else if (const auto *c = v.get<Vec4>()) for (float f : *c) flat.emplace_back(f);
        else flat.push_back(v);
    }
    std::vector<osc::Value> out;
    std::size_t used = 0;
    for (char tag : type) {
        if (tag == '[' || tag == ']') continue;
        out.push_back(toOsc(tag, used < flat.size() ? flat[used] : Value()));
        ++used;
    }
    return out;
}

} // namespace pxs::core::tree
