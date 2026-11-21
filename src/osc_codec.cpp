#include <pxs/core/osc/codec.h>

#include <algorithm>
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

} // namespace pxs::core::osc
