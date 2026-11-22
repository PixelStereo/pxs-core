#pragma once
// Names that can be one segment of an OSC address, and the unique segments of a list of names. Strings are UTF-8.
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pxs::core::osc {

// A name usable as one segment of an OSC address: the whitespace at both ends is cut, every Unicode white space inside
// and every character of `#*,/?[]{}!` becomes '_', and an empty result is "_". A byte that is not valid UTF-8 is kept as
// it is. (The white space is what Unicode calls White_Space: U+0009..U+000D, U+0020, U+0085, U+00A0, U+1680,
// U+2000..U+200A, U+2028, U+2029, U+202F, U+205F, U+3000.) These addresses are saved in projects: this function must
// not change what it returns for a name.
std::string safeName(std::string_view name);

// Segments unique within a scope (a container's path), taken in order: a repeated name gets _2, _3…, and a suffix another
// name already gave itself ("Video", "Video", "Video 2") is skipped, never shared.
class SegmentNamer
{
public:
    std::string take(std::string_view scope, std::string_view name);

private:
    std::unordered_map<std::string, int> m_used; // scope + base -> names given from it
    std::unordered_set<std::string> m_taken;     // scope + segment
};

// The segment of each name of a list (names made safe; a repeated name gets _2, _3…): the same in OSC addresses, timeline
// parameters and snapshot times.
std::vector<std::string> uniqueSegments(const std::vector<std::string> &names);

} // namespace pxs::core::osc
