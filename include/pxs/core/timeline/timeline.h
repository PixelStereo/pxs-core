#pragma once
// Timelines: numbers drawn over time, played on their own clock. The logic only, with no toolkit: the keys of a curve and the
// way it goes from one to the next, the waves of an oscillator, and the transport (one pass of a duration, once, in a loop or
// back and forth, at a speed). What a timeline drives and how it is saved and edited is the application's (pxs-qt has the Qt
// face and the editor). From NyxMap's timelines, where it was written first.
//
// A curve is keys over the duration of a pass, each eased towards the next one (the pattern repeats when the timeline loops).
// An oscillator is a wave of its own period, on the time played, so that it goes on without a jump across the loops. Values
// are absolute.
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace pxs::core::timeline {

// Random: a new value each period, held (sample and hold); SmoothRandom: from one random value to the next over a period. Both
// are drawn from a seed: the same at the same time, so they can be shown and sought.
enum class Wave { Sine = 0, Triangle = 1, Saw = 2, Square = 3, Random = 4, SmoothRandom = 5 };
inline constexpr int kWaveCount = 6;
enum class Loop { Once = 0, Loop = 1, PingPong = 2 };
enum class State { Stopped = 0, Playing = 1, Paused = 2 };
// What a transport is told: by its buttons, by a sequence, by OSC
enum class Action { Play = 0, Pause = 1, Stop = 2, Rewind = 3, Seek = 4, LoopMode = 5, Speed = 6 };
inline constexpr int kHold = 100;  // a key's curve: holds its value until the next key
inline constexpr int kBezier = 6;  // a key's curve: a cubic Bézier, shaped by its out handle and the next key's in handle
inline constexpr int kEaseCount = 6; // the easings: 0 linear, 1 in, 2 out, 3 in-out, 4 in cubic, 5 out cubic

struct Key {
    double t = 0, v = 0; // seconds into the pass, value
    int curve = 0;       // towards the next key: an easing (0 to 5), kBezier or kHold
    bool isCurrentValue = false; // the first key only: starts from the value the number has when the timeline starts (v is
                                 // then only a placeholder), so that nothing jumps
    // Bézier handles, from the key (seconds, value): `out` shapes the curve towards the next key (when this key's curve is
    // kBezier), `in` the one from the previous key (when that key's curve is). (0, 0): automatic, flat, a third of the way.
    double inDt = 0, inDv = 0, outDt = 0, outDv = 0;
    bool hasIn() const { return inDt != 0 || inDv != 0; }
    bool hasOut() const { return outDt != 0 || outDv != 0; }
    std::array<double, 3> rgb{0, 0, 0}; // colour curves: one RGB value at this time, sharing the outgoing easing
};

inline constexpr double kNoValue = std::numeric_limits<double>::quiet_NaN();
inline const std::array<double, 3> kNoColor{kNoValue, kNoValue, kNoValue};

// The easing of normalized time 0..1 (curve 0 to 5; out of that range: the nearest)
double ease(double t, int curve);

// ---- an oscillator

struct Oscillator {
    Wave wave = Wave::Sine;
    double period = 1, center = 0.5, amplitude = 0.5, phase = 0; // phase: 0..1 of a period
    std::uint32_t seed = 1;                                         // the random waves
    // Its value after `played` seconds (the time played since the timeline started, across its loops)
    double valueAt(double played) const;
};

// ---- a curve: keys sorted by time. `captured` is the number's value when the timeline started, for a first key "current value"

double keyValue(const std::vector<Key> &keys, std::size_t k, double captured = kNoValue);
// The two handles of the segment from key k to key k + 1, as absolute points (time, value), the automatic ones resolved and both
// kept within the segment's time (the curve stays a function of time)
void bezierHandles(const std::vector<Key> &keys, std::size_t k, double captured, double *t1, double *v1, double *t2, double *v2);
// The value at `position` seconds into the pass; nan for a curve without keys
double curveAt(const std::vector<Key> &keys, double position, double captured = kNoValue);
std::array<double, 3> keyColor(const std::vector<Key> &keys, std::size_t k, const std::array<double, 3> &captured = kNoColor);
std::array<double, 3> colorAt(const std::vector<Key> &keys, double position, const std::array<double, 3> &captured = kNoColor);

// ---- the transport

struct Transport {
    static constexpr double kMinDuration = 0.05; // a pass is never shorter

    double duration = 4; // seconds of one pass
    Loop loop = Loop::Loop;
    int repeat = 0;      // passes of Loop / PingPong (0: endless)
    double speed = 1.0;  // playback speed (0: frozen where it is, still playing)
    // Where it is (not saved)
    State state = State::Stopped;
    double clock = 0;            // seconds played since its start (or since its loop mode last changed)
    double oscillatorOffset = 0; // keeps the time played continuous when the clock is rebased
    bool reversed = false;       // the first pass goes backwards (a loop mode changed during a ping-pong's way back)

    double passDuration() const; // the duration, never shorter than kMinDuration
    double length() const;       // the time it plays from the start (infinity: endless)
    double position(double clock) const; // within the pass: 0..duration
    double position() const { return position(clock); }
    int pass() const;            // the pass it is in: 1, 2… (the last one once its passes are done)
    double progress() const;     // where it is within its pass: 0..1 (back down on a ping-pong's way back)
    double played() const;       // the time played, continuous across the loops (what an oscillator reads)
    bool backwardsAt(double clock) const; // the pass at that clock goes from the end to the start
    double clockAt(double position) const; // the clock giving `position` within the pass it is in now
    // A new loop mode / repeat while it runs: it goes on from where it is, the way it goes, and the passes count from the one it is
    // in (Once: it ends this pass, then stops; it does not jump to the end)
    void setLoop(Loop loop, int repeat);
    // One step of its clock while it plays (dt seconds of real time, times the speed); false once it is over (Once, or its passes
    // done): its last values hold
    bool advance(double dt);
};

} // namespace pxs::core::timeline
