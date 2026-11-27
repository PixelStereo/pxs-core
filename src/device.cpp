#include <pxs/core/device/device.h>
#include <pxs/core/dmx/frame.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <tuple>

namespace pxs::core::device {

namespace {

using Json = nlohmann::json;

struct KindInfo {
    const char *key, *name;
};
const KindInfo kKinds[kKindCount] = {{"oscquery", "OSCQuery"}, {"osc", "OSC"}, {"dmx", "DMX"}, {"midi", "MIDI"}};
const KindInfo &info(Kind k) { return kKinds[std::clamp(int(k), 0, kKindCount - 1)]; }

const char *messageKey(midi::Kind k)
{
    switch (k) {
    case midi::Kind::ControlChange: return "cc";
    case midi::Kind::Note: return "note";
    case midi::Kind::PitchBend: return "pitch_bend";
    case midi::Kind::ProgramChange: return "program";
    }
    return "cc";
}

midi::Kind messageFromKey(const std::string &key)
{
    if (key == "note") return midi::Kind::Note;
    if (key == "pitch_bend") return midi::Kind::PitchBend;
    if (key == "program") return midi::Kind::ProgramChange;
    return midi::Kind::ControlChange;
}

bool isOscType(char t) { return t == 'f' || t == 'd' || t == 'i' || t == 'h' || t == 'T'; }

double finite(double v, double def) { return std::isfinite(v) ? v : def; }

Json addressJson(const Address &a, Kind kind)
{
    Json j{{"path", a.path}};
    if (kind == Kind::Osc) {
        j["min"] = a.min;
        j["max"] = a.max;
        j["type"] = std::string(1, a.type);
    }
    if (kind == Kind::Dmx) {
        j["channel"] = a.channel;
        j["fine"] = a.fine;
    }
    if (kind == Kind::Midi) {
        j["message"] = messageKey(a.message);
        j["number"] = a.number;
    }
    return j;
}

Address addressFrom(const Json &j, Kind kind)
{
    Address a;
    if (!j.is_object()) return a;
    a.path = j.value("path", std::string());
    if (kind == Kind::Osc) {
        a.min = finite(j.value("min", 0.0), 0.0);
        a.max = finite(j.value("max", 1.0), 1.0);
    }
    const std::string type = j.value("type", std::string("f"));
    a.type = type.size() == 1 && isOscType(type[0]) ? type[0] : 'f';
    a.channel = j.value("channel", 1);
    a.fine = j.value("fine", false);
    a.message = messageFromKey(j.value("message", std::string("cc")));
    a.number = j.value("number", 0);
    return a;
}

Json deviceJson(const Device &d)
{
    Json j{{"id", d.id}, {"kind", key(d.kind)}, {"name", d.name}};
    if (d.kind == Kind::Osc || d.kind == Kind::OscQuery) {
        j["host"] = d.host;
        j["port"] = d.port;
    }
    if (d.kind == Kind::Dmx) {
        j["universe"] = d.universe;
        j["start"] = d.start;
    }
    if (d.kind == Kind::Midi) {
        j["midi_port"] = d.midiPort;
        j["channel"] = d.midiChannel;
    }
    if (d.kind != Kind::OscQuery) {
        Json list = Json::array();
        for (const Address &a : d.addresses) list.push_back(addressJson(a, d.kind));
        j["addresses"] = list;
    }
    return j;
}

std::optional<Device> deviceFrom(const Json &j)
{
    if (!j.is_object()) return std::nullopt;
    const auto kind = kindFromKey(j.value("kind", std::string()));
    if (!kind) return std::nullopt;
    Device d;
    d.kind = *kind;
    d.id = j.value("id", std::uint64_t(0));
    d.name = j.value("name", std::string());
    d.host = j.value("host", std::string());
    const int port = j.value("port", 0);
    d.port = port > 0 && port <= 65535 ? std::uint16_t(port) : 0;
    d.universe = j.value("universe", 1);
    d.start = j.value("start", 1);
    d.midiPort = j.value("midi_port", std::string());
    d.midiChannel = j.value("channel", 1);
    if (d.kind != Kind::OscQuery) {
        const Json list = j.value("addresses", Json::array());
        if (list.is_array())
            for (const Json &a : list) d.addresses.push_back(addressFrom(a, d.kind));
    }
    return d;
}

// A JSON text, read without an exception (a text that is not JSON: null)
Json parse(const std::string &text)
{
    Json j = Json::parse(text, nullptr, false);
    return j.is_discarded() ? Json() : j;
}

} // namespace

const char *key(Kind k) { return info(k).key; }
const char *name(Kind k) { return info(k).name; }

std::optional<Kind> kindFromKey(std::string_view key)
{
    for (int i = 0; i < kKindCount; ++i)
        if (key == kKinds[i].key) return Kind(i);
    return std::nullopt;
}

bool Address::operator==(const Address &o) const
{
    return std::tie(path, min, max, type, channel, fine, message, number) == std::tie(o.path, o.min, o.max, o.type, o.channel, o.fine, o.message, o.number);
}

const Address *Device::address(std::string_view path) const
{
    for (const Address &a : addresses)
        if (a.path == path) return &a;
    return nullptr;
}

std::string namespaceSegment(std::string_view name)
{
    std::string segment;
    bool separator = false;
    for (char c : name) {
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || static_cast<unsigned char>(c) >= 128) {
            if (separator && !segment.empty()) segment += '-';
            segment += c;
            separator = false;
        } else separator = true;
    }
    if (segment.empty()) return "device";
    if (segment.find_first_not_of("0123456789") == std::string::npos) return "device-" + segment;
    return segment;
}

std::string Device::key() const
{
    std::string k = std::string(device::key(kind)) + ":";
    if (kind == Kind::OscQuery && manual) return k + host + ":" + std::to_string(port);
    return k + name;
}

std::string Device::validate() const
{
    if (kind == Kind::Osc || kind == Kind::OscQuery) {
        if (host.empty()) return "No host";
        if (port == 0) return "No port";
    }
    if (kind == Kind::Dmx) {
        if (universe < 0 || universe > 63999) return "The universe is out of range";
        if (start < 1 || start > dmx::kChannelCount) return "The start address is not within 1 to 512";
    }
    if (kind == Kind::Midi && (midiChannel < 1 || midiChannel > 16)) return "The MIDI channel is not within 1 to 16";
    std::set<std::string> seen;
    for (const Address &a : addresses) {
        if (a.path.empty() || a.path[0] != '/') return "An address does not start with /: " + a.path;
        if (!seen.insert(a.path).second) return "The address " + a.path + " is there twice";
        if (kind == Kind::Dmx && dmxChannels(*this, a).first == 0) return "The address " + a.path + " is past channel 512";
        if (kind == Kind::Midi && (a.number < 0 || a.number > 127)) return "The MIDI number of " + a.path + " is not within 0 to 127";
    }
    return {};
}

int Device::footprint() const
{
    if (kind != Kind::Dmx) return 0;
    int highest = 0;
    for (const Address &a : addresses) highest = std::max(highest, a.channel + (a.fine ? 1 : 0));
    return highest;
}

bool Device::operator==(const Device &o) const
{
    return std::tie(id, kind, name, manual, host, port, universe, start, midiPort, midiChannel, addresses)
        == std::tie(o.id, o.kind, o.name, o.manual, o.host, o.port, o.universe, o.start, o.midiPort, o.midiChannel, o.addresses);
}

// ---- the wire

int nativeMaximum(Kind kind, const Address &address)
{
    if (kind == Kind::Dmx) return address.fine ? 65535 : 255;
    if (kind == Kind::Midi) {
        if (address.message == midi::Kind::PitchBend) return 16383;
        if (address.message != midi::Kind::ProgramChange) return 127;
    }
    return 1;
}

double unit(const Address &a, double value)
{
    if (!std::isfinite(value) || !(a.max > a.min)) return 0.0;
    return std::clamp((value - a.min) / (a.max - a.min), 0.0, 1.0);
}

osc::Message oscMessage(const std::string &path, char type, double value)
{
    osc::Message m;
    m.address = path;
    if (!std::isfinite(value)) value = 0;
    switch (type) {
    case 'T': m.types = value >= 0.5 ? "T" : "F"; m.args = {value >= 0.5}; break;
    case 'i': {
        const double r = std::clamp(std::round(value), double(std::numeric_limits<std::int32_t>::min()), double(std::numeric_limits<std::int32_t>::max()));
        m.types = "i";
        m.args = {std::int32_t(r)};
        break;
    }
    case 'h': {
        const double r = std::clamp(std::round(value), -9.2e18, 9.2e18);
        m.types = "h";
        m.args = {std::int64_t(r)};
        break;
    }
    case 'd': m.types = "d"; m.args = {value}; break;
    default: m.types = "f"; m.args = {float(value)}; break;
    }
    return m;
}

osc::Message oscMessage(const Address &a, double value)
{
    // A switch is on from the middle of its range
    if (a.type == 'T') return oscMessage(a.path, 'T', unit(a, value) >= 0.5 ? 1.0 : 0.0);
    return oscMessage(a.path, a.type, value);
}

std::pair<int, int> dmxChannels(const Device &d, const Address &a)
{
    const int width = a.fine ? 2 : 1;
    const long first = long(d.start) + long(a.channel) - 1;
    if (a.channel < 1 || d.start < 1 || first + width - 1 > dmx::kChannelCount) return {0, 0};
    return {int(first), width};
}

std::vector<std::uint8_t> dmxBytes(const Address &a, double value)
{
    const double level = std::clamp(finite(value, 0), 0.0, double(nativeMaximum(Kind::Dmx, a)));
    if (!a.fine) return {std::uint8_t(std::lround(level))};
    const auto v = std::uint16_t(std::lround(level));
    return {std::uint8_t(v >> 8), std::uint8_t(v & 0xff)};
}

bool writeDmx(const Device &d, const Address &a, double value, std::uint8_t *universe, std::size_t size)
{
    const auto [first, width] = dmxChannels(d, a);
    if (first == 0 || !universe || std::size_t(first - 1 + width) > size) return false;
    const auto bytes = dmxBytes(a, value);
    std::copy(bytes.begin(), bytes.end(), universe + (first - 1));
    return true;
}

midi::Bytes midiBytes(const Device &d, const Address &a, double value)
{
    const double u = std::clamp(finite(value, 0), 0.0, double(nativeMaximum(Kind::Midi, a))) / nativeMaximum(Kind::Midi, a);
    switch (a.message) {
    case midi::Kind::ControlChange: return midi::controlChange(d.midiChannel, a.number, u);
    case midi::Kind::Note: return u > 0 ? midi::noteOn(d.midiChannel, a.number, u) : midi::noteOff(d.midiChannel, a.number);
    case midi::Kind::PitchBend: return midi::pitchBend(d.midiChannel, u);
    case midi::Kind::ProgramChange: return u >= 0.5 ? midi::programChange(d.midiChannel, a.number) : midi::Bytes{};
    }
    return {};
}

// ---- saved

std::string toJson(const Device &d) { return deviceJson(d).dump(); }

std::optional<Device> fromJson(const std::string &json)
{
    try {
        return deviceFrom(parse(json));
    } catch (const Json::exception &) { // a member of another type than the one expected
        return std::nullopt;
    }
}

std::uint64_t Registry::idFor(const std::string &key)
{
    const auto it = m_ids.find(key);
    if (it != m_ids.end()) {
        const auto held = m_devices.find(it->second);
        if (held == m_devices.end() || held->second.key() == key) return it->second;
        m_ids.erase(it); // its id is another device's now: a new one
    }
    for (bool used = true; used;) {
        used = m_devices.count(m_next) > 0;
        for (const auto &[k, id] : m_ids) used = used || id == m_next;
        if (used) ++m_next;
    }
    m_ids[key] = m_next;
    return m_next++;
}

std::uint64_t Registry::put(Device d)
{
    if (d.id == 0) d.id = idFor(d.key());
    else {
        // One key for each id: a device renamed (or moved, an OSCQuery device added by hand) leaves its old key, which a new device may take
        for (auto it = m_ids.begin(); it != m_ids.end();) it = it->second == d.id && it->first != d.key() ? m_ids.erase(it) : std::next(it);
        m_ids[d.key()] = d.id;
        m_next = std::max(m_next, d.id + 1);
    }
    const std::uint64_t id = d.id;
    m_devices[id] = std::move(d);
    return id;
}

bool Registry::remove(std::uint64_t id)
{
    forgetSent(id);
    return m_devices.erase(id) > 0;
}

Device *Registry::find(std::uint64_t id)
{
    const auto it = m_devices.find(id);
    return it == m_devices.end() ? nullptr : &it->second;
}

const Device *Registry::find(std::uint64_t id) const
{
    const auto it = m_devices.find(id);
    return it == m_devices.end() ? nullptr : &it->second;
}

std::vector<const Device *> Registry::all() const
{
    std::vector<const Device *> out;
    for (const auto &[id, d] : m_devices) out.push_back(&d);
    std::stable_sort(out.begin(), out.end(), [](const Device *a, const Device *b) { return std::tie(a->kind, a->name) < std::tie(b->kind, b->name); });
    return out;
}

bool Registry::changed(std::uint64_t id, const std::string &path, double value)
{
    const auto k = std::make_pair(id, path);
    const auto it = m_last.find(k);
    if (it != m_last.end() && it->second == value) return false;
    m_last[k] = value;
    return true;
}

std::optional<double> Registry::last(std::uint64_t id, const std::string &path) const
{
    const auto it = m_last.find({id, path});
    if (it == m_last.end()) return std::nullopt;
    return it->second;
}

void Registry::forgetSent(std::uint64_t id)
{
    for (auto it = m_last.begin(); it != m_last.end();) it = it->first.first == id ? m_last.erase(it) : std::next(it);
}

std::string Registry::toJson() const
{
    Json devices = Json::array();
    for (const Device *d : all())
        if (d->manual) devices.push_back(deviceJson(*d));
    Json ids = Json::object();
    for (const auto &[k, id] : m_ids) ids[k] = id;
    return Json{{"devices", devices}, {"ids", ids}}.dump();
}

bool Registry::fromJson(const std::string &json)
{
    try {
        const Json j = parse(json);
        if (!j.is_object()) return false;
        std::map<std::string, std::uint64_t> ids;
        const Json idList = j.value("ids", Json::object());
        if (idList.is_object())
            for (const auto &[k, v] : idList.items())
                if (v.is_number_unsigned() && v.get<std::uint64_t>() > 0) ids[k] = v.get<std::uint64_t>();
        std::vector<Device> devices;
        const Json list = j.value("devices", Json::array());
        if (list.is_array())
            for (const Json &e : list)
                if (auto d = deviceFrom(e)) devices.push_back(std::move(*d));
        // Replaces the devices made by hand; the ones found on the network stay, with the id the text keeps for them (or a new one)
        std::vector<Device> found;
        for (auto &[id, d] : m_devices)
            if (!d.manual) found.push_back(std::move(d));
        m_devices.clear();
        m_ids = std::move(ids);
        m_last.clear();
        m_next = 1;
        for (const auto &[k, id] : m_ids) m_next = std::max(m_next, id + 1);
        for (Device &d : devices) {
            d.manual = true;
            if (d.id && m_devices.count(d.id)) d.id = 0; // twice the same id in the text: the second takes its key's
            put(std::move(d));
        }
        for (Device &d : found) {
            d.id = 0;
            put(std::move(d));
        }
        return true;
    } catch (const Json::exception &) {
        return false;
    }
}

} // namespace pxs::core::device
