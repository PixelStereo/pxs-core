#include <pxs/core/value.h>

#include <cerrno>
#include <charconv>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <locale>
#include <sstream>

#if defined(_WIN32)
#include <locale.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <locale.h>
#if defined(__APPLE__)
#include <xlocale.h>
#endif
#endif

namespace pxs::core {
namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }
char lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }
bool equalsNoCase(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != b[i]) return false;
    return true;
}

// A double from text already checked to be [sign] digits [. digits] [e [sign] digits], read in the "C" locale so that the decimal
// point is '.' whatever the program's locale is (plain strtod follows it). `overflow` and `underflow` say what the
// conversion reported; a denormal is a result, not an underflow.
bool readDouble(const std::string &token, double &out, bool &overflow, bool &underflow)
{
    overflow = underflow = false;
    errno = 0;
    char *end = nullptr;
#if defined(_WIN32)
    static _locale_t classic = _create_locale(LC_NUMERIC, "C");
    out = _strtod_l(token.c_str(), &end, classic);
#elif defined(__unix__) || defined(__APPLE__)
    static const locale_t classic = newlocale(LC_NUMERIC_MASK, "C", locale_t(0));
    out = strtod_l(token.c_str(), &end, classic);
#else
    std::istringstream in(token);
    in.imbue(std::locale::classic());
    in >> out;
    if (in.fail()) return false;
    end = const_cast<char *>(token.c_str()) + token.size();
#endif
    if (end != token.c_str() + token.size()) return false;
    if (errno == ERANGE) {
        if (std::isinf(out)) overflow = true;
        else if (out == 0.0) underflow = true;
    }
    return true;
}

} // namespace

std::optional<double> parseNumber(std::string_view text)
{
    while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
    while (!text.empty() && isSpace(text.back())) text.remove_suffix(1);
    if (text.empty()) return std::nullopt;

    std::size_t i = 0;
    bool negative = false;
    if (text[i] == '+' || text[i] == '-') {
        negative = text[i] == '-';
        ++i;
    }
    const std::string_view rest = text.substr(i);
    if (equalsNoCase(rest, "inf")) return negative ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
    if (equalsNoCase(rest, "nan")) return negative ? std::nullopt : std::optional<double>(std::numeric_limits<double>::quiet_NaN());

    const std::size_t digitsStart = i;
    std::size_t intDigits = 0, fracDigits = 0;
    while (i < text.size() && isDigit(text[i])) ++i, ++intDigits;
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && isDigit(text[i])) ++i, ++fracDigits;
    }
    if (intDigits + fracDigits == 0) return std::nullopt;
    bool nonZeroMantissa = false;
    for (std::size_t k = digitsStart; k < i; ++k)
        if (text[k] >= '1' && text[k] <= '9') nonZeroMantissa = true;
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) ++i;
        std::size_t expDigits = 0;
        while (i < text.size() && isDigit(text[i])) ++i, ++expDigits;
        if (expDigits == 0) return std::nullopt;
    }
    if (i != text.size()) return std::nullopt;

    double value = 0;
    bool overflow = false, underflow = false;
    if (!readDouble(std::string(text), value, overflow, underflow)) return std::nullopt;
    if (overflow || std::isinf(value)) return std::nullopt;                // "1e400"
    if (underflow || (value == 0.0 && nonZeroMantissa)) return std::nullopt; // "1e-400"
    return value;
}

std::optional<double> Value::number() const
{
    switch (kind()) {
    case Kind::Bool: return *get<bool>() ? 1.0 : 0.0;
    case Kind::Int32: return double(*get<std::int32_t>());
    case Kind::Int64: return double(*get<std::int64_t>());
    case Kind::Float: return double(*get<float>());
    case Kind::Double: return *get<double>();
    case Kind::String: return parseNumber(*get<std::string>());
    default: return std::nullopt;
    }
}

bool Value::truthy() const
{
    switch (kind()) {
    case Kind::Bool: return *get<bool>();
    case Kind::Int32: return *get<std::int32_t>() != 0;
    case Kind::Int64: return *get<std::int64_t>() != 0;
    case Kind::Float: return std::isnan(*get<float>()) ? false : *get<float>() != 0.0f;
    case Kind::Double: return std::isnan(*get<double>()) ? false : *get<double>() != 0.0;
    case Kind::String: {
        const std::string &s = *get<std::string>();
        return !(s.empty() || s == "0" || equalsNoCase(s, "false"));
    }
    default: return false;
    }
}

std::string formatDouble(double v)
{
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v < 0 ? "-inf" : "inf";
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, v);
    if (r.ec == std::errc()) return std::string(buf, r.ptr);
#endif
    // The shortest of 15, 16, 17 significant digits that reads back the same; snprintf follows the locale for the decimal
    // point, so it is put back to '.'.
    char buf[64];
    const std::lconv *lc = std::localeconv();
    const std::string point = (lc && lc->decimal_point && *lc->decimal_point) ? lc->decimal_point : ".";
    for (int digits : {15, 16, 17}) {
        std::snprintf(buf, sizeof buf, "%.*g", digits, v);
        if (std::strtod(buf, nullptr) == v || digits == 17) break;
    }
    std::string s = buf;
    if (point != ".") {
        const auto at = s.find(point);
        if (at != std::string::npos) s.replace(at, point.size(), ".");
    }
    return s;
}

std::string formatFloat(float v)
{
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v < 0 ? "-inf" : "inf";
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, v);
    if (r.ec == std::errc()) return std::string(buf, r.ptr);
#endif
    char buf2[64];
    const std::lconv *lc = std::localeconv();
    const std::string point = (lc && lc->decimal_point && *lc->decimal_point) ? lc->decimal_point : ".";
    for (int digits : {6, 7, 8, 9}) {
        std::snprintf(buf2, sizeof buf2, "%.*g", digits, double(v));
        if (std::strtof(buf2, nullptr) == v || digits == 9) break;
    }
    std::string s = buf2;
    if (point != ".") {
        const auto at = s.find(point);
        if (at != std::string::npos) s.replace(at, point.size(), ".");
    }
    return s;
}

std::string Value::text() const
{
    switch (kind()) {
    case Kind::Bool: return *get<bool>() ? "true" : "false";
    case Kind::Int32: return std::to_string(*get<std::int32_t>());
    case Kind::Int64: return std::to_string(*get<std::int64_t>());
    case Kind::Float: return formatFloat(*get<float>());
    case Kind::Double: return formatDouble(*get<double>());
    case Kind::String: return *get<std::string>();
    default: return {};
    }
}

} // namespace pxs::core
