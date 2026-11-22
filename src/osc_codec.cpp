#include <pxs/core/osc/codec.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <type_traits>

namespace pxs::core::osc {
namespace {

// ---------------------------------------------------------------------------------------------------------------------
// Encoding
// ---------------------------------------------------------------------------------------------------------------------

// Zero bytes up to a multiple of 4 counted from `base`, where the message being written starts (an `out` that already
// holds something is appended to, and the alignment is the message's, not the buffer's).
void pad(Bytes &bytes, std::size_t base)
{
    while ((bytes.size() - base) % 4 != 0) bytes.push_back(0);
}

void putString(Bytes &bytes, std::size_t base, std::string_view value)
{
    bytes.insert(bytes.end(), value.begin(), value.end());
    bytes.push_back(0);
    pad(bytes, base);
}

template <typename T> void putBE(Bytes &bytes, T value)
{
    using U = std::make_unsigned_t<T>;
    const U bits = static_cast<U>(value);
    for (int shift = int(sizeof(T) * 8) - 8; shift >= 0; shift -= 8)
        bytes.push_back(static_cast<std::uint8_t>((bits >> shift) & U(0xff)));
}

template <typename Bits, typename Real> Bits bitsOf(Real value)
{
    static_assert(sizeof(Bits) == sizeof(Real));
    Bits bits{};
    std::memcpy(&bits, &value, sizeof(Real));
    return bits;
}

template <typename Real, typename Bits> Real realOf(Bits bits)
{
    static_assert(sizeof(Bits) == sizeof(Real));
    Real value{};
    std::memcpy(&value, &bits, sizeof(Real));
    return value;
}

// ---------------------------------------------------------------------------------------------------------------------
// Decoding
// ---------------------------------------------------------------------------------------------------------------------

// A bounded cursor over a packet from the network: no check may overflow, so the room left is compared, never pos + n.
struct Reader {
    const std::uint8_t *data;
    std::size_t size;
    std::size_t pos = 0;
    bool ok = true;

    bool need(std::size_t n)
    {
        if (pos > size || n > size - pos) ok = false;
        return ok;
    }
    void align4() { pos = std::min(size, (pos + 3) & ~std::size_t(3)); }
    std::string string()
    {
        if (pos >= size) {
            ok = false;
            return {};
        }
        const auto *begin = data + pos;
        const auto *end = static_cast<const std::uint8_t *>(std::memchr(begin, 0, size - pos));
        if (!end) {
            ok = false;
            return {};
        }
        std::string value(reinterpret_cast<const char *>(begin), std::size_t(end - begin));
        pos += std::size_t(end - begin) + 1;
        align4();
        return value;
    }
    template <typename T> T be()
    {
        if (!need(sizeof(T))) return T{};
        using U = std::make_unsigned_t<T>;
        U bits = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i) bits = U((bits << 8) | data[pos++]);
        return static_cast<T>(bits);
    }
};

bool decodeMessage(const std::uint8_t *data, std::size_t size, std::vector<Message> &out)
{
    Reader reader{data, size};
    Message message;
    message.address = reader.string();
    if (!reader.ok) return false;
    if (reader.pos < size && data[reader.pos] == ',') {
        const std::string tags = reader.string();
        if (!reader.ok || tags.size() - 1 > kMaxArguments) return false;
        message.types = tags.substr(1);
        message.args.reserve(message.types.size());
        for (const char tag : message.types) {
            switch (tag) {
            case 'i': message.args.emplace_back(reader.be<std::int32_t>()); break;
            case 'h':
            case 't': message.args.emplace_back(reader.be<std::int64_t>()); break;
            case 'f': message.args.emplace_back(realOf<float>(reader.be<std::uint32_t>())); break;
            case 'd': message.args.emplace_back(realOf<double>(reader.be<std::uint64_t>())); break;
            case 's':
            case 'S': message.args.emplace_back(reader.string()); break;
            case 'b': {
                const auto length = reader.be<std::int32_t>();
                if (!reader.ok || length < 0 || !reader.need(std::size_t(length))) return false;
                const auto *begin = data + reader.pos;
                message.args.emplace_back(Blob(begin, begin + length));
                reader.pos += std::size_t(length);
                reader.align4();
                break;
            }
            case 'c':
            case 'r':
            case 'm': message.args.emplace_back(reader.be<std::uint32_t>()); break;
            case 'T': message.args.emplace_back(true); break;
            case 'F': message.args.emplace_back(false); break;
            default: message.args.emplace_back(std::monostate{}); break; // N, I, arrays, unknown: no data
            }
            if (!reader.ok) return false;
        }
    }
    out.push_back(std::move(message));
    return true;
}

// `limit`: the size `out` may not grow beyond (the messages of this packet are counted, not those already in `out`).
bool decodeInto(const std::uint8_t *data, std::size_t size, std::vector<Message> &out, int depth, std::size_t limit)
{
    if (size == 0 || depth > kMaxBundleDepth) return false;
    if (size >= 8 && std::memcmp(data, "#bundle", 8) == 0) { // "#bundle" and its NUL
        if (size < 16) return false;
        Reader reader{data, size};
        reader.pos = 16; // after the time tag
        while (reader.ok && reader.pos < size) {
            const auto length = reader.be<std::int32_t>();
            if (!reader.ok || length <= 0 || !reader.need(std::size_t(length))) return false;
            if (!decodeInto(data + reader.pos, std::size_t(length), out, depth + 1, limit)) return false;
            reader.pos += std::size_t(length);
        }
        return reader.ok;
    }
    if (data[0] != '/') return false;
    if (out.size() >= limit) return false;
    return decodeMessage(data, size, out);
}

// ---------------------------------------------------------------------------------------------------------------------
// Address patterns
// ---------------------------------------------------------------------------------------------------------------------

// A byte that is not valid UTF-8 becomes U+DC00 + byte (the "surrogate escape"): a code point valid UTF-8 can never
// produce, so two different invalid bytes stay different, and neither equals a valid character.
constexpr char32_t kEscapeBase = 0xDC00;

bool isCont(std::uint8_t byte) { return (byte & 0xC0) == 0x80; }

std::u32string codePoints(std::string_view text)
{
    std::u32string out;
    out.reserve(text.size());
    const auto at = [&](std::size_t i) { return static_cast<std::uint8_t>(text[i]); };
    for (std::size_t i = 0; i < text.size();) {
        const std::uint8_t lead = at(i);
        std::size_t length = 0;
        char32_t value = 0;
        if (lead < 0x80) {
            length = 1;
            value = lead;
        } else if (lead >= 0xC2 && lead <= 0xDF && i + 1 < text.size() && isCont(at(i + 1))) {
            length = 2;
            value = (char32_t(lead & 0x1F) << 6) | (at(i + 1) & 0x3F);
        } else if (lead >= 0xE0 && lead <= 0xEF && i + 2 < text.size() && isCont(at(i + 1)) && isCont(at(i + 2))) {
            value = (char32_t(lead & 0x0F) << 12) | (char32_t(at(i + 1) & 0x3F) << 6) | (at(i + 2) & 0x3F);
            if (value >= 0x800 && !(value >= 0xD800 && value <= 0xDFFF)) length = 3; // no overlong, no surrogate
        } else if (lead >= 0xF0 && lead <= 0xF4 && i + 3 < text.size() && isCont(at(i + 1)) && isCont(at(i + 2)) &&
                   isCont(at(i + 3))) {
            value = (char32_t(lead & 0x07) << 18) | (char32_t(at(i + 1) & 0x3F) << 12) |
                    (char32_t(at(i + 2) & 0x3F) << 6) | (at(i + 3) & 0x3F);
            if (value >= 0x10000 && value <= 0x10FFFF) length = 4; // no overlong, nothing past Unicode
        }
        if (length == 0) { // invalid: this byte alone is escaped, the next one is looked at afresh
            out.push_back(kEscapeBase + lead);
            ++i;
        } else {
            out.push_back(value);
            i += length;
        }
    }
    return out;
}

std::vector<std::u32string> splitPath(const std::u32string &path)
{
    std::vector<std::u32string> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == U'/') {
            parts.push_back(path.substr(start, i - start));
            start = i + 1;
        }
    }
    return parts;
}

struct PatternToken {
    enum class Kind { Char, Any, Star, Set, Alt } kind = Kind::Char;
    char32_t character = 0;
    std::u32string set; // Set: its characters and ranges (a-z), without the '!'
    bool negate = false;
    std::vector<std::u32string> alternatives; // Alt: literal alternatives
};

bool inSet(const PatternToken &token, char32_t character)
{
    bool found = false;
    for (std::size_t i = 0; i < token.set.size(); ++i) {
        if (i + 2 < token.set.size() && token.set[i + 1] == U'-') {
            found = found || (character >= token.set[i] && character <= token.set[i + 2]);
            i += 2;
        } else {
            found = found || character == token.set[i];
        }
    }
    return found != token.negate;
}

// false: malformed ('[' or '{' never closed), which matches nothing
bool tokenize(const std::u32string &pattern, std::vector<PatternToken> &out)
{
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        const char32_t c = pattern[i];
        PatternToken token;
        if (c == U'*') {
            if (!out.empty() && out.back().kind == PatternToken::Kind::Star) continue; // "**" is "*"
            token.kind = PatternToken::Kind::Star;
        } else if (c == U'?') {
            token.kind = PatternToken::Kind::Any;
        } else if (c == U'[') {
            const auto end = pattern.find(U']', i);
            if (end == std::u32string::npos) return false;
            token.kind = PatternToken::Kind::Set;
            token.set = pattern.substr(i + 1, end - i - 1);
            token.negate = !token.set.empty() && token.set.front() == U'!';
            if (token.negate) token.set.erase(token.set.begin());
            i = end;
        } else if (c == U'{') {
            const auto end = pattern.find(U'}', i);
            if (end == std::u32string::npos) return false;
            token.kind = PatternToken::Kind::Alt;
            const auto body = pattern.substr(i + 1, end - i - 1);
            std::size_t start = 0;
            for (std::size_t j = 0; j <= body.size(); ++j) {
                if (j == body.size() || body[j] == U',') {
                    token.alternatives.push_back(body.substr(start, j - start));
                    start = j + 1;
                }
            }
            i = end;
        } else {
            token.character = c;
        }
        out.push_back(std::move(token));
    }
    return true;
}

// One part: the positions of the address that the pattern can have reached are carried from one token to the next (two
// rows), so the time is O(pattern x address) whatever the pattern; a backtracking matcher is exponential on "/**…*a".
bool matchSegment(const std::u32string &pattern, const std::u32string &address)
{
    if (pattern.find_first_of(U"*?[{") == std::u32string::npos) return pattern == address; // the usual case
    std::vector<PatternToken> tokens;
    if (!tokenize(pattern, tokens)) return false;
    const std::size_t n = address.size();
    std::vector<unsigned char> at(n + 1, 0), next(n + 1, 0);
    at[0] = 1;
    for (const auto &token : tokens) {
        std::fill(next.begin(), next.end(), static_cast<unsigned char>(0));
        bool any = false;
        if (token.kind == PatternToken::Kind::Star) { // from the first position reached, every one after it
            bool on = false;
            for (std::size_t i = 0; i <= n; ++i) {
                on = on || at[i] != 0;
                next[i] = on ? 1 : 0;
                any = any || on;
            }
        } else {
            for (std::size_t i = 0; i <= n; ++i) {
                if (!at[i]) continue;
                switch (token.kind) {
                case PatternToken::Kind::Char:
                    if (i < n && address[i] == token.character) next[i + 1] = 1, any = true;
                    break;
                case PatternToken::Kind::Any:
                    if (i < n) next[i + 1] = 1, any = true;
                    break;
                case PatternToken::Kind::Set:
                    if (i < n && inSet(token, address[i])) next[i + 1] = 1, any = true;
                    break;
                case PatternToken::Kind::Alt:
                    for (const auto &alternative : token.alternatives) {
                        if (alternative.size() <= n - i &&
                            std::equal(alternative.begin(), alternative.end(), address.begin() + std::ptrdiff_t(i)))
                            next[i + alternative.size()] = 1, any = true;
                    }
                    break;
                case PatternToken::Kind::Star: break;
                }
            }
        }
        if (!any) return false;
        at.swap(next);
    }
    return at[n] != 0;
}

} // namespace

void encode(const Message &message, Bytes &out)
{
    const std::size_t base = out.size();
    putString(out, base, message.address);
    out.push_back(',');
    out.insert(out.end(), message.types.begin(), message.types.end());
    out.push_back(0);
    pad(out, base);
    for (std::size_t i = 0; i < message.types.size(); ++i) {
        static const Value none;
        const Value &value = i < message.args.size() ? message.args[i] : none;
        switch (message.types[i]) {
        case 'i': {
            const auto *v = std::get_if<std::int32_t>(&value);
            putBE(out, v ? *v : std::int32_t(0));
            break;
        }
        case 'h':
        case 't': {
            const auto *v = std::get_if<std::int64_t>(&value);
            putBE(out, v ? *v : std::int64_t(0));
            break;
        }
        case 'f': {
            const auto *v = std::get_if<float>(&value);
            putBE(out, bitsOf<std::uint32_t>(v ? *v : 0.0f));
            break;
        }
        case 'd': {
            const auto *v = std::get_if<double>(&value);
            putBE(out, bitsOf<std::uint64_t>(v ? *v : 0.0));
            break;
        }
        case 's':
        case 'S': {
            const auto *v = std::get_if<std::string>(&value);
            putString(out, base, v ? std::string_view(*v) : std::string_view());
            break;
        }
        case 'b': {
            const auto *v = std::get_if<Blob>(&value);
            const std::size_t length =
                v ? std::min<std::size_t>(v->size(), std::numeric_limits<std::int32_t>::max()) : 0;
            putBE(out, static_cast<std::int32_t>(length));
            if (v) out.insert(out.end(), v->begin(), v->begin() + std::ptrdiff_t(length));
            pad(out, base);
            break;
        }
        case 'c':
        case 'r':
        case 'm': {
            const auto *v = std::get_if<std::uint32_t>(&value);
            putBE(out, v ? *v : std::uint32_t(0));
            break;
        }
        default: break; // T F N I and the tags this library does not know: no data
        }
    }
}

Bytes encode(const Message &message)
{
    Bytes bytes;
    encode(message, bytes);
    return bytes;
}

bool decode(const std::uint8_t *data, std::size_t size, std::vector<Message> &out)
{
    if (!data || size == 0 || size > kMaxPacketBytes) return false;
    const std::size_t before = out.size();
    const bool ok = decodeInto(data, size, out, 0, before + kMaxMessagesPerPacket);
    if (!ok) out.resize(before); // all or nothing
    return ok;
}

bool decode(const Bytes &packet, std::vector<Message> &out) { return decode(packet.data(), packet.size(), out); }

bool match(std::string_view patternUtf8, std::string_view addressUtf8)
{
    const auto pattern = codePoints(patternUtf8);
    const auto address = codePoints(addressUtf8);
    if (pattern.size() > kMaxPatternChars || address.size() > kMaxPatternChars) return false;
    const auto patternParts = splitPath(pattern);
    const auto addressParts = splitPath(address);
    if (patternParts.size() != addressParts.size()) return false;
    for (std::size_t i = 0; i < patternParts.size(); ++i) {
        if (patternParts[i].size() > kMaxSegmentChars || addressParts[i].size() > kMaxSegmentChars ||
            !matchSegment(patternParts[i], addressParts[i]))
            return false;
    }
    return true;
}

} // namespace pxs::core::osc
