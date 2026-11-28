#pragma once
// The devices an application drives, the same in every application: what a device is (an OSCQuery device, a plain OSC receiver, a DMX
// fixture, a MIDI instrument), the addresses it takes with their range, its id kept across launches, its JSON, and what a value becomes
// on its wire. The logic only: the sockets, the DMX outputs, the MIDI ports and the discovery of OSCQuery devices are the platform's
// (pxs-qt has the Qt face: the transport and the Devices window).
//
// An address is a number in [min, max] at a path of the device. What it is on the wire:
//   OSC     a message at the path, of the address's tag: f, d, i, h (a whole number, rounded), T (on / off: T from the middle of the range,
//           else F). A plain OSC device may declare no address: it takes any path, sent as the sender says (MpOsc's destinations).
//   DMX     a channel of the device's universe, counted from its start address: 8 bits (0..255), or 16 bits on two channels, the coarse
//           byte then the fine one (0..65535), as fixtures take them.
//   MIDI    a control change, a note (a velocity; 0 is a note off), a pitch bend, or a program change (sent when the value reaches the
//           middle of its range), on the device's channel.
//   OSCQuery  its addresses are read from it (pxs-core::tree's RemoteTree) and written over OSC: they are not kept with the device.
#include <pxs/core/midi/message.h>
#include <pxs/core/osc/codec.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pxs::core::device {

enum class Kind { OscQuery = 0, Osc, Dmx, Midi };
inline constexpr int kKindCount = 4;
std::string namespaceSegment(std::string_view name);
const char *key(Kind k);  // as saved: "oscquery", "osc", "dmx", "midi"
const char *name(Kind k); // "OSCQuery", "OSC", "DMX", "MIDI"
std::optional<Kind> kindFromKey(std::string_view key);

// A branch may also be a parameter. Its index refers to the input path list.
struct ParameterNode {
    std::optional<std::size_t> parameter;
    std::map<std::string, ParameterNode> children;
};

ParameterNode parameterTree(const std::vector<std::string> &paths);

// One number a device takes
struct Address {
    std::string path;        // what it is called in the device: "/mixer/fader/1" (OSC), "/dimmer", "/pan" (DMX, MIDI)
    double min = 0, max = 1; // declared OSC range
    char type = 'f';         // OSC: f, d, i, h or T
    int channel = 1;         // DMX: 1..512, counted from the device's start address (1: its first channel)
    bool fine = false;       // DMX: 16 bits, on this channel (coarse) and the next (fine)
    midi::Kind message = midi::Kind::ControlChange; // MIDI
    int number = 0;          // MIDI: the controller, the note, the program (0..127)
    bool operator==(const Address &o) const;
    bool operator!=(const Address &o) const { return !(*this == o); }
};

struct Device {
    std::uint64_t id = 0; // 0: none yet (the registry gives it), and the application itself as a destination
    Kind kind = Kind::Osc;
    std::string name;
    bool manual = true;   // made by hand; an OSCQuery device found on the network is not, and is not saved
    // OSC and OSCQuery: where it is. A name or an address; OSC: its UDP port, OSCQuery: its HTTP port
    std::string host;
    std::uint16_t port = 0;
    // DMX: the application's universe it is patched in (its DMX outputs route that universe to the network), its start address 1..512
    int universe = 1;
    int start = 1;
    // MIDI: the output port, by its name (empty: the first one), and the channel 1..16
    std::string midiPort;
    int midiChannel = 1;
    std::vector<Address> addresses; // OSCQuery: none (they are read from it)

    const Address *address(std::string_view path) const;
    // What it is, for its id across launches: its kind and its name (an OSCQuery device found on the network: its name; added by hand:
    // its host and port)
    std::string key() const;
    // Why it cannot send, or empty: a host and a port (OSC, OSCQuery), a universe and channels within 1..512 (DMX), a MIDI channel and
    // numbers within their ranges, addresses that start with '/' and are not twice the same
    std::string validate() const;
    // The DMX channels it takes, from its start address: its highest channel (1: one channel), 0 for another kind
    int footprint() const;
    bool operator==(const Device &o) const;
    bool operator!=(const Device &o) const { return !(*this == o); }
};

// ---- what a value is on the wire

// The value as 0..1 of the address's range (clamped; an empty range: 0)
double unit(const Address &a, double value);
// The maximum native value for a control. Program Change is a 0/1 trigger.
int nativeMaximum(Kind kind, const Address &address);

// OSC: the message of a value at a path, of a tag (f, d, i, h, T; another tag is sent as f)
osc::Message oscMessage(const std::string &path, char type, double value);
// At a declared address: its path and its tag
osc::Message oscMessage(const Address &a, double value);

// DMX: the channel the address starts on in its universe (1..512) and how many it takes (1 or 2); {0, 0} when it falls outside
std::pair<int, int> dmxChannels(const Device &d, const Address &a);
// The bytes of a value in native DMX units: one (8 bits) or two, coarse then fine (16 bits)
std::vector<std::uint8_t> dmxBytes(const Address &a, double value);
// Writes the value in a universe (`size` bytes, channel 1 at index 0); false when it does not fit
bool writeDmx(const Device &d, const Address &a, double value, std::uint8_t *universe, std::size_t size);

// MIDI: the message of a value in native MIDI units on the device's channel (empty: nothing to send, a program change below the middle of its range)
midi::Bytes midiBytes(const Device &d, const Address &a, double value);

// ---- saved

std::string toJson(const Device &d);
// A text that is not a device gives none; what is missing takes its default
std::optional<Device> fromJson(const std::string &json);

// The devices of an application by their id, an id kept for each one by what it is (a device that comes back, or is read from a show
// file, gets the id it had). The id 0 is the application itself.
class Registry
{
public:
    // The id kept for this key (a new one the first time)
    std::uint64_t idFor(const std::string &key);
    // Adds a device, or replaces the one of its id. Without an id it takes the one of its key. Its id
    std::uint64_t put(Device d);
    bool remove(std::uint64_t id); // its id stays kept for its key
    Device *find(std::uint64_t id);
    const Device *find(std::uint64_t id) const;
    // OSCQuery, OSC, DMX, MIDI, each by name
    std::vector<const Device *> all() const;
    std::size_t size() const { return m_devices.size(); }

    // A value about to be sent: false when it is the one sent there last (OSC and MIDI need not send it again; DMX streams its universe
    // anyway). The value is then the last one sent
    bool changed(std::uint64_t id, const std::string &path, double value);
    // The last value sent there (none: nothing yet)
    std::optional<double> last(std::uint64_t id, const std::string &path) const;
    void forgetSent(std::uint64_t id); // a device that came back, or was edited: the next values are sent

    // The devices made by hand and the ids of every key, as JSON; and back (replaces them; false: not a registry, and nothing changes)
    std::string toJson() const;
    bool fromJson(const std::string &json);

private:
    std::map<std::uint64_t, Device> m_devices;
    std::map<std::string, std::uint64_t> m_ids; // key -> id
    std::uint64_t m_next = 1;
    std::map<std::pair<std::uint64_t, std::string>, double> m_last;
};

} // namespace pxs::core::device
