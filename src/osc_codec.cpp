#include <pxs/core/osc/codec.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <type_traits>

namespace pxs::core::osc {
namespace {

constexpr std::size_t kMaxPacket = 1u << 20;
constexpr std::size_t kMaxArgs = 1024;
constexpr int kMaxDepth = 8;
constexpr std::size_t kMaxMessages = 1024;

void pad(Bytes &bytes)
{
    while (bytes.size() % 4) bytes.push_back(0);
}

void putString(Bytes &bytes, const std::string &value)
{
    bytes.insert(bytes.end(), value.begin(), value.end());
    bytes.push_back(0);
    pad(bytes);
}

template <typename T> void putBE(Bytes &bytes, T value)
{
    using U = std::make_unsigned_t<T>;
    U bits{};
    std::memcpy(&bits, &value, sizeof(T));
    for (int shift = int(sizeof(T) * 8) - 8; shift >= 0; shift -= 8)
        bytes.push_back(std::uint8_t((bits >> shift) & U(0xff)));
}

struct Reader {
    const Bytes &bytes;
    std::size_t pos = 0;
    bool ok = true;

    std::size_t left() const { return pos <= bytes.size() ? bytes.size() - pos : 0; }
    bool need(std::size_t n)
    {
        if (pos > bytes.size() || n > bytes.size() - pos) ok = false;
        return ok;
    }
    void align4() { pos = std::min<std::size_t>((pos + 3) & ~std::size_t(3), bytes.size()); }
    std::string string()
    {
        if (pos >= bytes.size()) {
            ok = false;
            return {};
        }
        const auto begin = bytes.begin() + std::ptrdiff_t(pos);
        const auto end = std::find(begin, bytes.end(), std::uint8_t(0));
        if (end == bytes.end()) {
            ok = false;
            return {};
        }
        std::string value(begin, end);
        pos = std::size_t(end - bytes.begin()) + 1;
        align4();
        return value;
    }
    template <typename T> T be()
    {
        if (!need(sizeof(T))) return T{};
        using U = std::make_unsigned_t<T>;
        U bits = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i) bits = U((bits << 8) | bytes[pos++]);
        T value{};
        std::memcpy(&value, &bits, sizeof(T));
        return value;
    }
};

bool decodeInto(const Bytes &packet, std::vector<Message> &out, int depth)
{
    if (packet.empty() || packet.size() > kMaxPacket || depth > kMaxDepth) return false;
    if (packet.size() >= 8 && std::equal(packet.begin(), packet.begin() + 8, "#bundle\0")) {
        if (packet.size() < 16) return false;
        Reader reader{packet};
        reader.pos = 16;
        while (reader.ok && reader.pos < packet.size()) {
            const auto size = reader.be<std::int32_t>();
            if (!reader.ok || size <= 0 || !reader.need(std::size_t(size))) return false;
            const auto first = packet.begin() + std::ptrdiff_t(reader.pos);
            Bytes child(first, first + size);
            if (!decodeInto(child, out, depth + 1)) return false;
            reader.pos += std::size_t(size);
        }
        return reader.ok;
    }
    if (packet.front() != '/') return false;

    Reader reader{packet};
    Message message;
    message.address = reader.string();
    if (!reader.ok) return false;
    if (reader.pos < packet.size() && packet[reader.pos] == ',') {
        const std::string tags = reader.string();
        if (!reader.ok || tags.empty() || tags.size() - 1 > kMaxArgs) return false;
        message.types = tags.substr(1);
        message.args.reserve(message.types.size());
        for (const char tag : message.types) {
            switch (tag) {
            case 'i': message.args.emplace_back(reader.be<std::int32_t>()); break;
            case 'h': message.args.emplace_back(reader.be<std::int64_t>()); break;
            case 'f': {
                const auto bits = reader.be<std::uint32_t>();
                float value{};
                std::memcpy(&value, &bits, sizeof(value));
                message.args.emplace_back(value);
                break;
            }
            case 'd': {
                const auto bits = reader.be<std::uint64_t>();
                double value{};
                std::memcpy(&value, &bits, sizeof(value));
                message.args.emplace_back(value);
                break;
            }
            case 's':
            case 'S': message.args.emplace_back(reader.string()); break;
            case 'b': {
                const auto size = reader.be<std::int32_t>();
                if (!reader.ok || size < 0 || !reader.need(std::size_t(size))) return false;
                const auto begin = packet.begin() + std::ptrdiff_t(reader.pos);
                message.args.emplace_back(Blob(begin, begin + size));
                reader.pos += std::size_t(size);
                reader.align4();
                break;
            }
            case 'c': message.args.emplace_back(reader.be<std::uint32_t>()); break;
            case 'r':
            case 'm': message.args.emplace_back(reader.be<std::uint32_t>()); break;
            case 't': message.args.emplace_back(reader.be<std::int64_t>()); break;
            case 'T': message.args.emplace_back(true); break;
            case 'F': message.args.emplace_back(false); break;
            default: message.args.emplace_back(std::monostate{}); break;
            }
            if (!reader.ok) return false;
        }
    }
    if (out.size() >= kMaxMessages) return false;
    out.push_back(std::move(message));
    return true;
}

constexpr std::size_t kMaxPatternChars = 8192;
constexpr std::size_t kMaxSegmentChars = 4096;

std::u32string codepoints(const std::string &text)
{
    std::u32string out;
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        if (lead < 0x80) {
            out.push_back(char32_t(lead));
            ++i;
            continue;
        }
        int count = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc2 ? 2 : 0;
        char32_t value = count == 2 ? char32_t(lead & 0x1f) : count == 3 ? char32_t(lead & 0x0f) : char32_t(lead & 0x07);
        bool valid = count != 0 && i + std::size_t(count) <= text.size();
        for (int j = 1; valid && j < count; ++j) {
            const auto next = static_cast<unsigned char>(text[i + std::size_t(j)]);
            if ((next & 0xc0) != 0x80) valid = false;
            else value = (value << 6) | char32_t(next & 0x3f);
        }
        if (valid && ((count == 2 && value < 0x80) || (count == 3 && value < 0x800) ||
                      (count == 4 && value < 0x10000) || value > 0x10ffff ||
                      (value >= 0xd800 && value <= 0xdfff))) valid = false;
        if (valid) {
            if (value > 0xffff) {
                value -= 0x10000;
                out.push_back(0xd800 + (value >> 10));
                out.push_back(0xdc00 + (value & 0x3ff));
            } else {
                out.push_back(value);
            }
            i += std::size_t(count);
        } else {
            out.push_back(0xfffd);
            ++i;
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
    std::u32string set;
    bool negate = false;
    std::vector<std::u32string> alternatives;
};

bool inSet(const PatternToken &token, char32_t character)
{
    bool found = false;
    for (std::size_t i = 0; i < token.set.size(); ++i) {
        if (i + 2 < token.set.size() && token.set[i + 1] == U'-') {
            found |= character >= token.set[i] && character <= token.set[i + 2];
            i += 2;
        } else {
            found |= character == token.set[i];
        }
    }
    return found != token.negate;
}

bool tokenize(const std::u32string &pattern, std::vector<PatternToken> &out)
{
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        const char32_t c = pattern[i];
        PatternToken token;
        if (c == U'*') {
            if (!out.empty() && out.back().kind == PatternToken::Kind::Star) continue;
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

bool matchSegment(const std::u32string &pattern, const std::u32string &address)
{
    if (pattern.find_first_of(U"*?[{") == std::u32string::npos) return pattern == address;
    std::vector<PatternToken> tokens;
    if (!tokenize(pattern, tokens)) return false;
    const std::size_t n = address.size();
    std::vector<unsigned char> at(n + 1, 0), next(n + 1, 0);
    at[0] = 1;
    for (const auto &token : tokens) {
        std::fill(next.begin(), next.end(), 0);
        bool any = false;
        if (token.kind == PatternToken::Kind::Star) {
            bool on = false;
            for (std::size_t i = 0; i <= n; ++i) {
                on = on || at[i];
                next[i] = on;
                any = any || on;
            }
        } else {
            for (std::size_t i = 0; i <= n; ++i) {
                if (!at[i]) continue;
                switch (token.kind) {
                case PatternToken::Kind::Char:
                    if (i < n && address[i] == token.character) next[i + 1] = any = true;
                    break;
                case PatternToken::Kind::Any:
                    if (i < n) next[i + 1] = any = true;
                    break;
                case PatternToken::Kind::Set:
                    if (i < n && inSet(token, address[i])) next[i + 1] = any = true;
                    break;
                case PatternToken::Kind::Alt:
                    for (const auto &alternative : token.alternatives) {
                        if (alternative.size() <= n - i &&
                            std::equal(alternative.begin(), alternative.end(), address.begin() + std::ptrdiff_t(i)))
                            next[i + alternative.size()] = any = true;
                    }
                    break;
                case PatternToken::Kind::Star:
                    break;
                }
            }
        }
        if (!any) return false;
        at.swap(next);
    }
    return at[n] != 0;
}

} // namespace

Bytes encode(const Message &message)
{
    Bytes bytes;
    putString(bytes, message.address);
    putString(bytes, "," + message.types);
    for (std::size_t i = 0; i < message.types.size(); ++i) {
        const Value value = i < message.args.size() ? message.args[i] : Value{};
        switch (message.types[i]) {
        case 'i': {
            const auto v = std::get_if<std::int32_t>(&value);
            putBE(bytes, v ? *v : std::int32_t(0));
            break;
        }
        case 'h':
        case 't': {
            const auto v = std::get_if<std::int64_t>(&value);
            putBE(bytes, v ? *v : std::int64_t(0));
            break;
        }
        case 'f': {
            const auto v = std::get_if<float>(&value);
            const float f = v ? *v : 0.f;
            std::uint32_t bits{};
            std::memcpy(&bits, &f, sizeof(f));
            putBE(bytes, bits);
            break;
        }
        case 'd': {
            const auto v = std::get_if<double>(&value);
            const double d = v ? *v : 0.0;
            std::uint64_t bits{};
            std::memcpy(&bits, &d, sizeof(d));
            putBE(bytes, bits);
            break;
        }
        case 's':
        case 'S': {
            const auto v = std::get_if<std::string>(&value);
            putString(bytes, v ? *v : std::string{});
            break;
        }
        case 'b': {
            const auto v = std::get_if<Blob>(&value);
            const Blob empty;
            const Blob &blob = v ? *v : empty;
            putBE(bytes, std::int32_t(blob.size()));
            bytes.insert(bytes.end(), blob.begin(), blob.end());
            pad(bytes);
            break;
        }
        case 'c':
        case 'r':
        case 'm': {
            const auto v = std::get_if<std::uint32_t>(&value);
            putBE(bytes, v ? *v : std::uint32_t(0));
            break;
        }
        case 'T':
        case 'F':
        case 'N':
        case 'I':
            break;
        default:
            break;
        }
    }
    return bytes;
}

bool decode(const Bytes &packet, std::vector<Message> &out) { return decodeInto(packet, out, 0); }

bool match(const std::string &patternUtf8, const std::string &addressUtf8)
{
    const auto pattern = codepoints(patternUtf8);
    const auto address = codepoints(addressUtf8);
    if (pattern.size() > kMaxPatternChars || address.size() > kMaxPatternChars) return false;
    const auto patternParts = splitPath(pattern);
    const auto addressParts = splitPath(address);
    if (patternParts.size() != addressParts.size()) return false;
    for (std::size_t i = 0; i < patternParts.size(); ++i) {
        if (patternParts[i].size() > kMaxSegmentChars || addressParts[i].size() > kMaxSegmentChars ||
            !matchSegment(patternParts[i], addressParts[i])) return false;
    }
    return true;
}

} // namespace pxs::core::osc
