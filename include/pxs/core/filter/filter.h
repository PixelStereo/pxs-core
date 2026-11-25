#pragma once
// Filters: a value taken through a chain of stages, one after the other, each on or off, stepped frame by frame. The logic only, with no
// toolkit: what each filter does, its settings (names, ranges, defaults), the keys it is saved under and the state it keeps between
// frames. Where the value comes from (a key, MIDI, OSC, a control on a page, another value) and where the result goes is the
// application's, and so are the JSON and the editor (pxs-qt has the Qt face). From NyxMap's interactions, where it was written first.
//
// The filters, by group:
//     smoothing — Average (the mean over a window of time), Weighted Average (follows with a soft lag), Attack Release (rises and
//     falls at speeds of their own), Damper (a spring that follows, overshooting more or less);
//     shape — Curve (0..1 to 0..1, keys as a timeline's), Math (+, −, ×, ÷, modulo, power, min, max, absolute, floor / ceil / round
//     to a step, 1 − x), Clamp (kept within two values), Steps (a few levels in 0..1), Remap (a range to another, held within the
//     second at its ends), Threshold (0 or 1, with a margin against flicker);
//     timing — Delay (the value of a moment ago), Hold (a fall waits), Debounce (a change is followed once it lasts), Auto Return (back
//     to its rest after a time), Speed (how fast it moves);
//     trigger — Toggle (flips between 0 and 1 each time it goes up past its threshold), Pulse Detection (a pulse on each sudden rise,
//     decaying), Envelope (a trigger in, an ADSR out).
#include <pxs/core/timeline/timeline.h>

#include <cstddef>
#include <deque>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace pxs::core::filter {

// The order is the one they are numbered by (saved by their keys, never by their number)
enum class Kind {
    Average = 0, WeightedAverage, PulseDetection, AttackRelease, Damper,
    Curve, Math, Clamp, Steps, Threshold, Delay, Speed, Remap, Toggle,
    Envelope,  // a trigger in, an ADSR out: Attack, Decay, Sustain, Release (the impact of a hit, a flash, a pulse)
    Hold,      // a fall is held for a time before it follows (a switch stays on a moment after it went off)
    Debounce,  // a change is followed once the value has not changed for a time (a flicker is ignored)
    AutoReturn // leaves its rest value for a time, then goes back to it until the value is at rest again
};
inline constexpr int kKindCount = 18;

enum class MathOp { Add = 0, Subtract, Multiply, Divide, Modulo, Power, Min, Max, Abs, Floor, Ceil, Round, OneMinus };
inline constexpr int kMathOpCount = 13;

inline constexpr int kSettingCount = 4; // a, b, c, d

// ---- names

const char *key(Kind k);   // as saved: "average", "weighted_average"… ("return" for Auto Return)
const char *name(Kind k);  // "Average", "Weighted Average"…
const char *group(Kind k); // "Smoothing", "Shape", "Timing" or "Trigger"
const char *about(Kind k); // what it does, in a sentence
std::optional<Kind> kindFromKey(std::string_view key);

const char *key(MathOp op);  // "add", "one_minus"…
const char *name(MathOp op); // "x + a", "1 − x (invert)"…
std::optional<MathOp> mathOpFromKey(std::string_view key);
bool usesValue(MathOp op); // all but Abs and 1 − x take `a`
// The operation on x with a (÷ 0 and modulo 0 leave x as it is; a power keeps the sign of x)
double apply(MathOp op, double x, double a);

// One setting of a filter: its name (null: the filter does not use it), its suffix (" s"…), its range and default
struct Setting {
    const char *name = nullptr, *suffix = "";
    double min = 0, max = 1, def = 0;
};
Setting setting(Kind k, int which); // which: 0..3 (a, b, c, d)

// ---- a stage

// A curve from 0 to 1, straight (a new Curve filter's)
std::vector<timeline::Key> straightCurve();

// One filter of a chain, as the application keeps it
struct Stage {
    Kind kind = Kind::Average;
    bool enabled = true;
    double a = 0, b = 0, c = 0, d = 0; // its settings (setting())
    MathOp op = MathOp::Multiply;       // Math
    std::vector<timeline::Key> curve;   // Curve: keys over 0..1 (the input) to 0..1 (the value)
    double &value(int which) { return which == 0 ? a : which == 1 ? b : which == 2 ? c : d; }
    double value(int which) const { return which == 0 ? a : which == 1 ? b : which == 2 ? c : d; }
    static Stage made(Kind kind); // with its defaults (a Curve: straight)
    static Stage remap(double inMin, double inMax, double outMin, double outMax);
};

// What the chain reads of a stage: the application can keep its stages in its own types and hand these over each frame, with no copy
// of a curve
struct StageView {
    Kind kind = Kind::Average;
    bool enabled = true;
    double a = 0, b = 0, c = 0, d = 0;
    MathOp op = MathOp::Multiply;
    const std::vector<timeline::Key> *curve = nullptr; // Curve (null: none)
    static StageView of(const Stage &s) { return {s.kind, s.enabled, s.a, s.b, s.c, s.d, s.op, &s.curve}; }
};

// ---- the chain

// A value through its stages. Pure: no clock (the frames' durations are given), no thread (one caller at a time).
class Chain
{
public:
    // A value from the source; not a number: ignored
    void input(double raw);
    // One frame of `dt` seconds (a stall is taken as a quarter of a second at most): the filters move on. The result (none: nothing
    // came in yet). The state of a stage is made again when it is another kind of filter than in the last frame.
    std::optional<double> step(const StageView *stages, std::size_t count, double dt);
    std::optional<double> step(const std::vector<StageView> &stages, double dt) { return step(stages.data(), stages.size(), dt); }
    std::optional<double> step(const std::vector<Stage> &stages, double dt);
    std::optional<double> level() const { return m_level; }        // the source's last value
    std::optional<double> output() const { return m_out; }         // the last result of step
    std::optional<double> curveInput() const { return m_curveIn; } // what entered the first curve on
    const std::vector<double> &stageOutputs() const { return m_stageOut; } // the value after each filter
    void reset() { *this = Chain(); }

private:
    struct State {
        Kind kind = Kind::Average;
        bool started = false, high = false;
        double y = 0, v = 0, slow = 0, prev = 0; // its value, its speed (Damper), its slow average (Pulse), the last input
        int phase = 0;                           // Envelope: 0 idle, 1 attack, 2 decay, 3 sustain, 4 release
        double peak = 1;                         // Envelope: the level the trigger came in at (its velocity)
        std::deque<std::pair<double, double>> window{}; // Average, Delay: (time, value)
    };
    std::optional<double> m_level;
    bool m_fresh = false; // a value came in since the last step
    double m_time = 0;
    std::vector<State> m_states;
    std::vector<double> m_stageOut;
    std::vector<StageView> m_views; // step(vector<Stage>)
    std::optional<double> m_out, m_curveIn;
};

} // namespace pxs::core::filter
