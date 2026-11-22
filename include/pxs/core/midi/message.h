#pragma once
// The channel messages of MIDI that a control surface needs (control change, notes, pitch bend, program change): read from
// the bytes a port delivers, and built for the bytes a port sends. No dependency beyond the C++17 standard library; the
// ports themselves (CoreMIDI, WinMM, ALSA, …) are the platform's.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pxs::core::midi {

using Bytes = std::vector<std::uint8_t>;

enum class Kind { ControlChange = 0, Note, PitchBend, ProgramChange };

// A channel message as a control sees it.
struct Message {
    Kind kind = Kind::ControlChange;
    int channel = 1;   // 1..16
    int number = 0;    // the controller, the note, the program (0..127); 0 for a pitch bend
    double value = 0;  // 0..1: a controller, a velocity (a note off is 0), a pitch bend (0.5 at rest); a program change is 1
};

// Reads one message: control change, note on and off (a note on at velocity 0 is an off), pitch bend, program change
// (value 1: a program was chosen; the caller sends 0 after it if it wants a pulse). Returns false for anything else (clock,
// SysEx, aftertouch, system messages, a data byte first) or for a message cut short; `message` is then not touched.
bool parse(const std::uint8_t *data, std::size_t size, Message &message);

// The bytes of a message. The channel (1..16), the number (0..127) and the value (0..1) are held within their ranges; a
// value that is not a number is 0.
Bytes controlChange(int channel, int controller, double value);
Bytes noteOn(int channel, int note, double velocity);
Bytes noteOff(int channel, int note);
Bytes pitchBend(int channel, double value);
Bytes programChange(int channel, int program);

} // namespace pxs::core::midi
