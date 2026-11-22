#include <pxs/core/osc/names.h>

#include <cstdint>

namespace pxs::core::osc {
namespace {

bool isWhiteSpace(char32_t c)
{
    return (c >= 0x09 && c <= 0x0D) || c == 0x20 || c == 0x85 || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
           c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

bool isReserved(char32_t c)
{
    switch (c) {
    case U'#': case U'*': case U',': case U'/': case U'?': case U'[': case U']': case U'{': case U'}': case U'!': return true;
    default: return false;
    }
}

// One unit of a UTF-8 string: a valid character (code point, byte length) or a stray byte (not valid UTF-8, kept as is).
struct Unit {
    char32_t value = 0;
    std::size_t length = 1;
    bool valid = false;
};

Unit unitAt(std::string_view s, std::size_t i)
{
    const auto byte = [&](std::size_t k) { return static_cast<std::uint8_t>(s[k]); };
    const auto cont = [&](std::size_t k) { return k < s.size() && (byte(k) & 0xC0) == 0x80; };
    const std::uint8_t lead = byte(i);
    if (lead < 0x80) return {lead, 1, true};
    if (lead >= 0xC2 && lead <= 0xDF && cont(i + 1))
        return {(char32_t(lead & 0x1F) << 6) | (byte(i + 1) & 0x3F), 2, true};
    if (lead >= 0xE0 && lead <= 0xEF && cont(i + 1) && cont(i + 2)) {
        const char32_t v = (char32_t(lead & 0x0F) << 12) | (char32_t(byte(i + 1) & 0x3F) << 6) | (byte(i + 2) & 0x3F);
        if (v >= 0x800 && !(v >= 0xD800 && v <= 0xDFFF)) return {v, 3, true};
    } else if (lead >= 0xF0 && lead <= 0xF4 && cont(i + 1) && cont(i + 2) && cont(i + 3)) {
        const char32_t v = (char32_t(lead & 0x07) << 18) | (char32_t(byte(i + 1) & 0x3F) << 12) |
                           (char32_t(byte(i + 2) & 0x3F) << 6) | (byte(i + 3) & 0x3F);
        if (v >= 0x10000 && v <= 0x10FFFF) return {v, 4, true};
    }
    return {0, 1, false};
}

// Where the text without its white space starts and ends.
void trimmedRange(std::string_view s, std::size_t &begin, std::size_t &end)
{
    begin = 0;
    while (begin < s.size()) {
        const Unit u = unitAt(s, begin);
        if (!u.valid || !isWhiteSpace(u.value)) break;
        begin += u.length;
    }
    end = s.size();
    while (end > begin) { // walk back to the start of the last unit
        std::size_t start = end - 1;
        while (start > begin && (static_cast<std::uint8_t>(s[start]) & 0xC0) == 0x80 && end - start < 4) --start;
        Unit u = unitAt(s, start);
        if (!u.valid || start + u.length != end) { // not a unit ending here: the last byte is a stray one
            start = end - 1;
            u = {0, 1, false};
        }
        if (!u.valid || !isWhiteSpace(u.value)) break;
        end = start;
    }
}

} // namespace

std::string safeName(std::string_view name)
{
    std::size_t begin = 0, end = 0;
    trimmedRange(name, begin, end);
    std::string out;
    out.reserve(end - begin);
    for (std::size_t i = begin; i < end;) {
        const Unit u = unitAt(name, i);
        if (u.valid && (isWhiteSpace(u.value) || isReserved(u.value))) out.push_back('_');
        else out.append(name.substr(i, u.length));
        i += u.length;
    }
    return out.empty() ? std::string("_") : out;
}

std::string SegmentNamer::take(std::string_view scope, std::string_view name)
{
    const std::string base = safeName(name);
    const std::string prefix = std::string(scope) + '/';
    const std::string key = prefix + base;
    int k = 0;
    if (const auto it = m_used.find(key); it != m_used.end()) k = it->second;
    std::string segment = k ? base + "_" + std::to_string(k + 1) : base;
    while (m_taken.count(prefix + segment)) segment = base + "_" + std::to_string(++k + 1);
    m_used[key] = k + 1;
    m_taken.insert(prefix + segment);
    return segment;
}

std::vector<std::string> uniqueSegments(const std::vector<std::string> &names)
{
    std::vector<std::string> out;
    out.reserve(names.size());
    SegmentNamer namer;
    for (const auto &name : names) out.push_back(namer.take({}, name));
    return out;
}

} // namespace pxs::core::osc
