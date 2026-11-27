// Devices: what a value is on the wire of each kind (OSC tags, DMX 8 and 16 bits, MIDI), the checks, the JSON, the ids kept by key
#include "check.h"

#include <pxs/core/device/device.h>

#include <array>
#include <cmath>
#include <variant>

using namespace pxs::core;
using namespace pxs::core::device;

namespace {

Address at(const std::string &path, double min = 0, double max = 1)
{
    Address a;
    a.path = path;
    a.min = min;
    a.max = max;
    return a;
}

void kinds()
{
    for (int i = 0; i < kKindCount; ++i) CHECK(kindFromKey(key(Kind(i))) == Kind(i));
    CHECK(std::string(name(Kind::OscQuery)) == "OSCQuery");
    CHECK(!kindFromKey("artnet"));
}

void oscOut()
{
    const auto f = oscMessage("/a", 'f', 0.25);
    CHECK(f.address == "/a" && f.types == "f" && std::get<float>(f.args[0]) == 0.25f);
    const auto i = oscMessage("/count", 'i', 1.6);
    CHECK(i.types == "i" && std::get<std::int32_t>(i.args[0]) == 2);
    CHECK(std::get<std::int32_t>(oscMessage("/big", 'i', 1e12).args[0]) == 2147483647); // held within an int32
    CHECK(oscMessage("/on", 'T', 0.7).types == "T" && oscMessage("/on", 'T', 0.2).types == "F");
    CHECK(oscMessage("/d", 'd', 0.1).types == "d" && oscMessage("/x", 's', 1).types == "f"); // another tag: f
    CHECK(std::get<float>(oscMessage("/nan", 'f', std::nan("")).args[0]) == 0.0f);

    Address sw = at("/switch", 0, 10);
    sw.type = 'T';
    CHECK(oscMessage(sw, 6).types == "T" && oscMessage(sw, 4).types == "F"); // on from the middle of its range
    Address n = at("/n", 0, 100);
    n.type = 'i';
    CHECK(std::get<std::int32_t>(oscMessage(n, 42.4).args[0]) == 42); // the value as it is, not 0..1
}

void dmxOut()
{
    Device d;
    d.kind = Kind::Dmx;
    d.start = 10;
    Address dimmer = at("/dimmer");
    dimmer.channel = 1;
    Address pan = at("/pan");
    pan.channel = 2;
    pan.fine = true;
    d.addresses = {dimmer, pan};

    CHECK(dmxBytes(dimmer, 1) == std::vector<std::uint8_t>{1});
    CHECK(dmxBytes(dimmer, 256) == std::vector<std::uint8_t>{255}); // clamped
    CHECK(dmxBytes(pan, 32768) == (std::vector<std::uint8_t>{128, 0})); // 32768: coarse then fine
    CHECK(dmxBytes(pan, 65535) == (std::vector<std::uint8_t>{255, 255}));
    CHECK(dmxBytes(pan, -270) == (std::vector<std::uint8_t>{0, 0}));
    const auto fine = dmxBytes(pan, 300); // 300 = 0x012C
    CHECK(fine == (std::vector<std::uint8_t>{0x01, 0x2C}));

    CHECK(dmxChannels(d, dimmer) == std::make_pair(10, 1));
    CHECK(dmxChannels(d, pan) == std::make_pair(11, 2));
    std::array<std::uint8_t, 512> universe{};
    CHECK(writeDmx(d, dimmer, 255, universe.data(), universe.size()));
    CHECK(writeDmx(d, pan, 65535, universe.data(), universe.size()));
    CHECK(universe[9] == 255 && universe[10] == 255 && universe[11] == 255 && universe[8] == 0 && universe[12] == 0);
    CHECK(d.footprint() == 3);

    d.start = 511; // pan would take 512 and 513
    CHECK(dmxChannels(d, pan) == std::make_pair(0, 0));
    CHECK(!writeDmx(d, pan, 0, universe.data(), universe.size()));
    CHECK(!d.validate().empty());
    d.start = 510;
    CHECK(d.validate().empty());
}

void midiOut()
{
    Device d;
    d.kind = Kind::Midi;
    d.midiChannel = 2;
    Address cc = at("/fader");
    cc.number = 7;
    CHECK(midiBytes(d, cc, 1) == (midi::Bytes{0xB1, 7, 1}));
    Address note = at("/hit");
    note.message = midi::Kind::Note;
    note.number = 60;
    CHECK(midiBytes(d, note, 127) == (midi::Bytes{0x91, 60, 127}));
    CHECK(midiBytes(d, note, 0) == midi::noteOff(2, 60));
    Address bend = at("/bend");
    bend.message = midi::Kind::PitchBend;
    CHECK(midiBytes(d, bend, 8191.5) == midi::pitchBend(2, 0.5));
    Address program = at("/scene");
    program.message = midi::Kind::ProgramChange;
    program.number = 5;
    CHECK(midiBytes(d, program, 1) == (midi::Bytes{0xC1, 5}));
    CHECK(midiBytes(d, program, 0.2).empty());
}

void checks()
{
    Device osc;
    osc.kind = Kind::Osc;
    CHECK(!osc.validate().empty()); // no host
    osc.host = "127.0.0.1";
    osc.port = 9000;
    CHECK(osc.validate().empty()); // no address: open, any path
    osc.addresses = {at("/a"), at("/a")};
    CHECK(!osc.validate().empty()); // twice
    osc.addresses = {at("a")};
    CHECK(!osc.validate().empty()); // no '/'
    Device midi;
    midi.kind = Kind::Midi;
    midi.midiChannel = 17;
    CHECK(!midi.validate().empty());
}

void json()
{
    Device d;
    d.id = 7;
    d.kind = Kind::Dmx;
    d.name = "Spot";
    d.universe = 2;
    d.start = 33;
    Address pan = at("/pan");
    pan.channel = 3;
    pan.fine = true;
    d.addresses = {pan};
    const auto back = fromJson(toJson(d));
    CHECK(back && *back == d);

    Device m;
    m.kind = Kind::Midi;
    m.name = "Synth";
    m.midiPort = "IAC";
    m.midiChannel = 3;
    Address note = at("/hit");
    note.message = midi::Kind::Note;
    note.number = 36;
    m.addresses = {note};
    CHECK(fromJson(toJson(m)) == m);

    CHECK(!fromJson("not json") && !fromJson("{\"kind\":\"laser\"}") && !fromJson("{\"kind\":\"osc\",\"name\":3}"));
    const auto partial = fromJson("{\"kind\":\"osc\",\"name\":\"A\"}");
    CHECK(partial && partial->port == 0 && partial->addresses.empty());
}

void registry()
{
    Registry r;
    Device a;
    a.kind = Kind::Osc;
    a.name = "Mixer";
    a.host = "10.0.0.2";
    a.port = 8000;
    const auto id = r.put(a);
    CHECK(id == 1 && r.find(id) && r.find(id)->name == "Mixer");

    Device found;
    found.kind = Kind::OscQuery;
    found.name = "Video";
    found.manual = false;
    const auto vid = r.put(found);
    CHECK(vid == 2);
    CHECK(r.all().front()->kind == Kind::OscQuery); // OSCQuery first

    // Gone and found again: the same id
    CHECK(r.remove(vid) && !r.find(vid));
    CHECK(r.put(found) == vid);

    // Sent again only when it changed
    CHECK(r.changed(id, "/a", 0.5) && !r.changed(id, "/a", 0.5) && r.changed(id, "/a", 0.6));
    CHECK(r.last(id, "/a") == 0.6 && !r.last(id, "/b"));
    r.forgetSent(id);
    CHECK(r.changed(id, "/a", 0.6));

    // Saved: the devices made by hand and every id; read back, the device found on the network stays, the ids are the same
    const std::string saved = r.toJson();
    Registry other;
    CHECK(other.fromJson(saved));
    CHECK(other.size() == 1 && other.find(id) && other.find(id)->host == "10.0.0.2");
    CHECK(other.put(found) == vid);
    Device next;
    next.kind = Kind::Midi;
    next.name = "New";
    CHECK(other.put(next) == 3); // past every id kept
    CHECK(!other.fromJson("[]") && other.size() == 3); // not a registry: nothing changes

    // Read into a registry that already found a device on the network: it takes the id the text keeps for it, not one of the text's
    Registry running;
    Device elsewhere;
    elsewhere.kind = Kind::OscQuery;
    elsewhere.name = "Lights";
    elsewhere.manual = false;
    CHECK(running.put(elsewhere) == 1); // 1 is the Mixer's in the text
    CHECK(running.put(found) == 2);     // 2 is Video's in the text too
    CHECK(running.fromJson(saved));
    CHECK(running.find(id) && running.find(id)->name == "Mixer");
    CHECK(running.find(vid) && running.find(vid)->name == "Video");
    const Device *lights = nullptr;
    for (const Device *d : running.all())
        if (d->name == "Lights") lights = d;
    CHECK(lights && lights->id != id && lights->id != vid && running.size() == 3);
}

void renamed()
{
    Registry r;
    Device a;
    a.kind = Kind::Dmx;
    a.name = "A";
    const auto first = r.put(a);
    Device b = *r.find(first);
    b.name = "B"; // renamed
    CHECK(r.put(b) == first);
    const auto second = r.put(a); // a new "A"
    CHECK(second != first && r.size() == 2 && r.find(first) && r.find(first)->name == "B");
    const std::string saved = r.toJson();
    CHECK(saved.find("\"dmx:A\":" + std::to_string(second)) != std::string::npos);
    CHECK(saved.find("\"dmx:B\":" + std::to_string(first)) != std::string::npos);
    CHECK(saved.find("\"dmx:A\":" + std::to_string(first)) == std::string::npos);

    // A text that kept an old key for an id now another device's: that key gets a new id
    Registry stale;
    CHECK(stale.fromJson("{\"devices\":[{\"id\":1,\"kind\":\"dmx\",\"name\":\"B\"}],\"ids\":{\"dmx:A\":1,\"dmx:B\":1}}"));
    const auto again = stale.put(a);
    CHECK(again != 1 && stale.find(1) && stale.find(1)->name == "B");
}

} // namespace

int main()
{
    kinds();
    oscOut();
    dmxOut();
    midiOut();
    checks();
    json();
    registry();
    renamed();
    return pxs_test::finish("device");
}
