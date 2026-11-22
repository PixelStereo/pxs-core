#pragma once
// Reads test/golden/vectors.tsv: one vector per line, tab-separated, first column the kind.
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace golden {

using Fields = std::vector<std::string>;

inline std::vector<Fields> lines(const std::string &kind)
{
    std::ifstream in(PXS_GOLDEN_FILE);
    if (!in) throw std::runtime_error(std::string("cannot open ") + PXS_GOLDEN_FILE);
    std::vector<Fields> out;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        Fields f;
        std::size_t start = 0;
        for (std::size_t i = 0; i <= line.size(); ++i) {
            if (i == line.size() || line[i] == '\t') {
                f.push_back(line.substr(start, i - start));
                start = i + 1;
            }
        }
        if (f[0] == kind) out.push_back(std::move(f));
    }
    if (out.empty()) throw std::runtime_error("no golden vector of kind " + kind);
    return out;
}

inline std::vector<std::uint8_t> unhex(const std::string &s)
{
    std::vector<std::uint8_t> out;
    const auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (std::size_t i = 0; i + 1 < s.size(); i += 2) out.push_back(static_cast<std::uint8_t>(nibble(s[i]) * 16 + nibble(s[i + 1])));
    return out;
}

inline std::string hex(const std::uint8_t *d, std::size_t n)
{
    static const char digits[] = "0123456789abcdef";
    std::string s;
    for (std::size_t i = 0; i < n; ++i) {
        s.push_back(digits[d[i] >> 4]);
        s.push_back(digits[d[i] & 15]);
    }
    return s;
}
inline std::string hex(const std::vector<std::uint8_t> &v) { return hex(v.data(), v.size()); }

inline std::string fromHex(const std::string &s)
{
    const auto b = unhex(s);
    return std::string(b.begin(), b.end());
}

inline std::vector<std::string> split(const std::string &s, char sep)
{
    std::vector<std::string> out;
    if (s.empty()) return out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i)
        if (i == s.size() || s[i] == sep) {
            out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    return out;
}

} // namespace golden
