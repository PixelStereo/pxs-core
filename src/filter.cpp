#include <pxs/core/filter/filter.h>

#include <algorithm>
#include <cmath>

namespace pxs::core::filter {

namespace {

struct KindInfo {
    const char *key, *name, *group, *about;
};
// In the order of Kind
const KindInfo kKinds[kKindCount] = {
    {"average", "Average", "Smoothing", "The mean of what was set over a window of time: a steadier value."},
    {"weighted_average", "Weighted Average", "Smoothing", "Follows what is set with a soft lag: the smoothing is the time to get there."},
    {"pulse_detection", "Pulse Detection", "Trigger", "A sudden rise gives a pulse that fades."},
    {"attack_release", "Attack Release", "Smoothing", "Rises in the attack time and falls in the release time."},
    {"damper", "Damper", "Smoothing", "A spring: follows with a little life; damping 1 does not overshoot."},
    {"curve", "Curve", "Shape", "Reshapes the value with a curve (the presets are the usual ones)."},
    {"math", "Math", "Shape", "An operation on the value: add, multiply, power, round, invert…"},
    {"clamp", "Clamp", "Shape", "Keeps the value between two limits."},
    {"steps", "Steps", "Shape", "Rounds the value to a number of steps."},
    {"threshold", "Threshold", "Shape", "On above the threshold, off below, with a margin so that it does not flicker."},
    {"delay", "Delay", "Timing", "What is set arrives later."},
    {"speed", "Speed", "Timing", "The speed of the value (how fast it moves) instead of the value."},
    {"remap", "Remap", "Shape", "Takes a part of the range (In) and spreads it over another (Out)."},
    {"toggle", "Toggle", "Trigger", "Each time the value goes over the threshold, it flips between on and off."},
    {"envelope", "Envelope", "Trigger", "A trigger gives an ADSR: attack, decay, sustain, release."},
    {"hold", "Hold", "Timing", "A fall is held for a time before it follows."},
    {"debounce", "Debounce", "Timing", "Follows once the value has stopped changing for a time: a flicker is ignored."},
    {"return", "Auto Return", "Timing", "Leaves its rest value for a time, then goes back to it."},
    {"physics", "Physics", "Smoothing",
     "A mass on a spring, in 0 to 1: pulled to the value while it comes; when the source goes quiet it goes on by itself and bounces on 0 and 1."},
};

struct OpInfo {
    const char *key, *name;
};
const OpInfo kOps[kMathOpCount] = {
    {"add", "x + a"},
    {"subtract", "x − a"},
    {"multiply", "x × a"},
    {"divide", "x ÷ a"},
    {"modulo", "x modulo a"},
    {"power", "x to the power a"},
    {"min", "smaller of x and a"},
    {"max", "larger of x and a"},
    {"abs", "absolute x"},
    {"floor", "x down to a step of a"},
    {"ceil", "x up to a step of a"},
    {"round", "x rounded to a step of a"},
    {"one_minus", "1 − x (invert)"},
};

const KindInfo &info(Kind k) { return kKinds[std::clamp(int(k), 0, kKindCount - 1)]; }
const OpInfo &info(MathOp op) { return kOps[std::clamp(int(op), 0, kMathOpCount - 1)]; }

constexpr double kPi = 3.14159265358979323846;

} // namespace

const char *key(Kind k) { return info(k).key; }
const char *name(Kind k) { return info(k).name; }
const char *group(Kind k) { return info(k).group; }
const char *about(Kind k) { return info(k).about; }

std::optional<Kind> kindFromKey(std::string_view key)
{
    for (int i = 0; i < kKindCount; ++i)
        if (key == kKinds[i].key) return Kind(i);
    return std::nullopt;
}

const char *key(MathOp op) { return info(op).key; }
const char *name(MathOp op) { return info(op).name; }

std::optional<MathOp> mathOpFromKey(std::string_view key)
{
    for (int i = 0; i < kMathOpCount; ++i)
        if (key == kOps[i].key) return MathOp(i);
    return std::nullopt;
}

bool usesValue(MathOp op) { return op != MathOp::Abs && op != MathOp::OneMinus; }

double apply(MathOp op, double x, double a)
{
    switch (op) {
    case MathOp::Add: return x + a;
    case MathOp::Subtract: return x - a;
    case MathOp::Multiply: return x * a;
    case MathOp::Divide: return std::abs(a) < 1e-12 ? x : x / a; // ÷ 0: left as it is
    case MathOp::Modulo: return std::abs(a) < 1e-12 ? x : x - a * std::floor(x / a); // always ≥ 0 for a > 0
    case MathOp::Power: {
        const double p = std::pow(std::abs(x), a);
        return std::isfinite(p) ? std::copysign(p, x) : x;
    }
    case MathOp::Min: return std::min(x, a);
    case MathOp::Max: return std::max(x, a);
    case MathOp::Abs: return std::abs(x);
    case MathOp::Floor: return a > 1e-12 ? std::floor(x / a) * a : std::floor(x);
    case MathOp::Ceil: return a > 1e-12 ? std::ceil(x / a) * a : std::ceil(x);
    case MathOp::Round: return a > 1e-12 ? std::round(x / a) * a : std::round(x);
    case MathOp::OneMinus: return 1.0 - x;
    }
    return x;
}

Setting setting(Kind k, int which)
{
    const char *s = " s";
    auto two = [which](Setting a, Setting b = Setting{}) { return which == 0 ? a : which == 1 ? b : Setting{}; };
    switch (k) {
    case Kind::Average: return two({"Window", s, 0.01, 5, 0.2});
    case Kind::WeightedAverage: return two({"Smoothing", s, 0.01, 5, 0.2});
    case Kind::PulseDetection: return two({"Threshold", "", 0.01, 1, 0.2}, {"Decay", s, 0.01, 5, 0.3});
    case Kind::AttackRelease: return two({"Attack", s, 0.001, 5, 0.05}, {"Release", s, 0.001, 5, 0.5});
    case Kind::Damper: return two({"Response", s, 0.02, 5, 0.4}, {"Damping", "", 0.05, 2, 0.4});
    case Kind::Curve: return Setting{};
    case Kind::Math: return two({"Value", "", -100, 100, 1});
    case Kind::Clamp: return two({"Min", "", 0, 1, 0}, {"Max", "", 0, 1, 1});
    case Kind::Steps: return two({"Steps", "", 2, 64, 4});
    case Kind::Threshold: return two({"Threshold", "", 0, 1, 0.5}, {"Margin", "", 0, 0.5, 0.05});
    case Kind::Delay: return two({"Delay", s, 0, 10, 0.5});
    case Kind::Speed: return two({"Full scale", " /s", 0.01, 100, 1}, {"Smoothing", s, 0.001, 5, 0.1});
    case Kind::Toggle: return two({"Threshold", "", 0, 1, 0.5});
    case Kind::Hold: return two({"Hold", s, 0, 10, 0.5});
    case Kind::Debounce: return two({"Settle", s, 0, 10, 0.1});
    case Kind::AutoReturn: return two({"After", s, 0.01, 60, 1}, {"Back to", "", -1000, 1000, 0});
    case Kind::Physics: {
        const Setting settings[4] = {{"Friction", " /s", 0, physics::kFrictionMax, 5}, {"Attraction", "", 0, physics::kAttractionMax, 120},
                                     {"Tension", "", 0, physics::kTensionMax, 0}, {"Bounce", "", 0, physics::kWallMax, 0.6}};
        return which >= 0 && which < 4 ? settings[which] : Setting{};
    }
    case Kind::Envelope: {
        // The defaults are an impact: a hit at once, gone in a third of a second. Sustain above 0 holds while the trigger is held
        const Setting e[] = {{"Attack", s, 0.001, 5, 0.005}, {"Decay", s, 0.001, 10, 0.3}, {"Sustain", "", 0, 1, 0}, {"Release", s, 0.001, 10, 0.3}};
        return which >= 0 && which < 4 ? e[which] : Setting{};
    }
    case Kind::Remap: {
        const Setting r[] = {{"In Min", "", 0, 1, 0}, {"In Max", "", 0, 1, 1}, {"Out Min", "", 0, 1, 0}, {"Out Max", "", 0, 1, 1}};
        return which >= 0 && which < 4 ? r[which] : Setting{};
    }
    }
    return Setting{};
}

std::vector<timeline::Key> straightCurve()
{
    timeline::Key a, b;
    a.t = 0;
    a.v = 0;
    b.t = 1;
    b.v = 1;
    return {a, b};
}

Stage Stage::made(Kind kind)
{
    Stage s;
    s.kind = kind;
    for (int w = 0; w < kSettingCount; ++w) s.value(w) = setting(kind, w).def;
    if (kind == Kind::Curve) s.curve = straightCurve();
    return s;
}

Stage Stage::remap(double inMin, double inMax, double outMin, double outMax)
{
    Stage s = made(Kind::Remap);
    s.a = inMin;
    s.b = inMax;
    s.c = outMin;
    s.d = outMax;
    return s;
}

// ---- the chain

void Chain::input(double raw)
{
    if (!std::isfinite(raw)) return;
    m_level = raw;
    m_fresh = true;
}

std::optional<double> Chain::step(const std::vector<Stage> &stages, double dt)
{
    m_views.resize(stages.size());
    for (std::size_t i = 0; i < stages.size(); ++i) m_views[i] = StageView::of(stages[i]);
    return step(m_views.data(), m_views.size(), dt);
}

std::optional<double> Chain::step(const StageView *stages, std::size_t count, double dt)
{
    if (!m_level) return std::nullopt;
    dt = std::isfinite(dt) ? std::clamp(dt, 0.0, 0.25) : 0.0; // a long stall is not a long smoothing
    m_time += dt;
    double y = *m_level;
    m_curveIn.reset();
    if (m_states.size() != count) m_states.resize(count);
    m_stageOut.resize(count);
    auto toward = [dt](double from, double to, double tau) { return from + (to - from) * (1.0 - std::exp(-dt / tau)); };
    for (std::size_t i = 0; i < count; ++i) {
        const StageView &f = stages[i];
        State &st = m_states[i];
        if (st.kind != f.kind) st = State{f.kind}; // another kind of filter: from scratch
        if (!f.enabled) {
            m_stageOut[i] = y; // through
            continue;
        }
        const double x = y;
        if (!st.started) { // starts where its input is (not from 0)
            st.y = f.kind == Kind::PulseDetection || f.kind == Kind::Speed || f.kind == Kind::Envelope ? 0.0 : x;
            st.slow = st.prev = x;
            st.v = 0;
            if (f.kind == Kind::Physics) {
                st.body = physics::Body{};
                st.body.pos = st.body.target = {std::clamp(x, 0.0, 1.0)};
                st.body.vel = {0.0};
            }
            // a Toggle: off, its first press turns it on; an Envelope: a trigger held at the start fires it
            st.high = f.kind != Kind::Toggle && f.kind != Kind::Envelope && f.kind != Kind::AutoReturn && x >= f.a;
            st.started = true;
        }
        const double a = std::max(1e-3, f.a), b = std::max(1e-3, f.b);
        switch (f.kind) {
        case Kind::Average: {
            st.window.push_back({m_time, x});
            while (!st.window.empty() && st.window.front().first < m_time - a) st.window.pop_front();
            double sum = 0;
            for (const auto &[t, v] : st.window) sum += v;
            y = st.window.empty() ? x : sum / double(st.window.size());
            break;
        }
        case Kind::WeightedAverage: y = st.y = toward(st.y, x, a / 3.0); break; // ~95% of the way in `a`
        case Kind::PulseDetection:
            // A rise above the slow average by more than the threshold: a pulse, decaying in `b`
            if (m_fresh && x - st.slow > a) st.y = 1.0;
            else st.y = std::max(0.0, st.y - dt / b);
            st.slow = toward(st.slow, x, 0.25);
            y = st.y;
            break;
        case Kind::AttackRelease: y = st.y = toward(st.y, x, (x > st.y ? a : b) / 3.0); break;
        case Kind::Damper: {
            // A spring of period `a`, damped by `b` (1: no overshoot), in small steps for stability
            const double w = 2.0 * kPi / a;
            const int n = std::max(1, int(std::ceil(dt / 0.002)));
            const double h = dt / n;
            for (int k = 0; k < n; ++k) {
                st.v += (w * w * (x - st.y) - 2.0 * b * w * st.v) * h;
                st.y += st.v * h;
            }
            y = st.y;
            break;
        }
        case Kind::Curve:
            if (!m_curveIn) m_curveIn = x; // the first curve's (shown on its lane)
            if (f.curve && !f.curve->empty()) {
                const double c = timeline::curveAt(*f.curve, std::clamp(x, 0.0, 1.0));
                if (std::isfinite(c)) y = c;
            }
            break;
        case Kind::Math: {
            const double r = apply(f.op, x, f.a);
            if (std::isfinite(r)) y = r;
            break;
        }
        case Kind::Clamp: y = std::clamp(x, std::min(f.a, f.b), std::max(f.a, f.b)); break;
        case Kind::Steps: {
            const int n = std::clamp(int(std::lround(f.a)), 2, 1024); // levels, 0 and 1 included
            y = std::round(std::clamp(x, 0.0, 1.0) * (n - 1)) / (n - 1);
            break;
        }
        case Kind::Threshold: {
            // On above the threshold plus half the margin, off below it minus half: no flicker around it
            const double half = std::max(0.0, f.b) / 2;
            if (x >= f.a + half) st.high = true;
            else if (x < f.a - half) st.high = false;
            y = st.high ? 1.0 : 0.0;
            break;
        }
        case Kind::Delay: {
            // The value of `a` seconds ago (the first one until then)
            st.window.push_back({m_time, x});
            const double then = m_time - std::max(0.0, f.a);
            while (st.window.size() > 1 && st.window[1].first <= then) st.window.pop_front();
            y = st.window.front().second;
            break;
        }
        case Kind::Remap: {
            // In Min..In Max to Out Min..Out Max, held within Out at its ends (either way round)
            const double span = f.b - f.a;
            const double n = std::abs(span) < 1e-12 ? (x >= f.b ? 1.0 : 0.0) : std::clamp((x - f.a) / span, 0.0, 1.0);
            y = f.c + n * (f.d - f.c);
            break;
        }
        case Kind::Envelope: {
            // A rise from 0 is a trigger, at the level it comes in (its velocity); falling back to 0 releases. Attack is a ramp from
            // where the envelope is (a retrigger does not jump), decay and release fall exponentially (the shape of a hit) and end
            // exactly on their target. A sustain of 0 makes a pulse that ends by itself, held or not.
            const double attack = std::max(1e-3, f.a), decay = std::max(1e-3, f.b), release = std::max(1e-3, f.d);
            const double sustain = std::clamp(f.c, 0.0, 1.0);
            const bool gate = x > 1e-3;
            if (gate && !st.high) {
                st.phase = 1;
                st.peak = std::clamp(x, 0.0, 1.0);
            } else if (!gate && st.high && st.phase != 0) {
                st.phase = 4;
            }
            st.high = gate;
            auto fall = [&](double to, double time) {
                st.y += (to - st.y) * (1.0 - std::exp(-dt * 4.6 / time)); // within 1% in `time`
                if (std::abs(st.y - to) < 1e-3) st.y = to;
            };
            switch (st.phase) {
            case 1:
                st.y += dt / attack;
                if (st.y >= 1.0) {
                    st.y = 1.0;
                    st.phase = 2;
                }
                break;
            case 2:
                fall(sustain, decay);
                if (st.y == sustain) st.phase = sustain > 0 ? 3 : 0;
                break;
            case 3: st.y = sustain; break;
            case 4:
                fall(0.0, release);
                if (st.y == 0.0) st.phase = 0;
                break;
            default: break;
            }
            y = st.y * st.peak;
            break;
        }
        case Kind::Hold: {
            // A rise goes through; a fall waits `a` seconds at the level it fell from, then follows
            if (x >= st.y) {
                st.y = x;
                st.v = 0;
            } else {
                st.v += dt;
                if (st.v >= f.a) {
                    st.y = x;
                    st.v = 0;
                }
            }
            y = st.y;
            break;
        }
        case Kind::Debounce: {
            // The value it had when it last changed, once it stayed `a` seconds: until then, the one it settled on before
            if (std::abs(x - st.prev) > 1e-9) {
                st.prev = x;
                st.v = 0;
            } else {
                st.v += dt;
            }
            if (st.v >= f.a) st.y = x;
            y = st.y;
            break;
        }
        case Kind::AutoReturn: {
            // Away from its rest (`b`), it follows for `a` seconds, then goes back to rest and stays there until the value is at rest again
            if (std::abs(x - f.b) <= 1e-9) {
                st.high = false; // at rest: ready
                st.v = 0;
                y = x;
            } else {
                if (!st.high) st.v += dt;
                if (st.v >= f.a) st.high = true;
                y = st.high ? f.b : x;
            }
            break;
        }
        case Kind::Toggle: {
            // Each time it goes up past its threshold: the other state (0, 1, 0…)
            const bool above = x > f.a;
            if (above && !st.high) st.v = st.v > 0.5 ? 0.0 : 1.0;
            st.high = above;
            y = st.v;
            break;
        }
        case Kind::Physics: {
            // Held to the value while values come (the spring of Attraction), then let go: pulled back to where it last was by Tension (0:
            // thrown, free), slowed by Friction, bouncing on 0 and 1 with what Bounce keeps of its speed
            st.quiet = m_fresh ? 0.0 : st.quiet + dt;
            st.body.held = st.quiet < kPhysicsQuiet;
            if (st.body.held) st.body.target.assign(1, std::clamp(x, 0.0, 1.0));
            physics::Settings p;
            p.mode = physics::Settings::MassSpring;
            p.friction = f.a;
            p.attraction = f.b;
            p.tension = f.c;
            p.wall = f.d;
            physics::step(st.body, p.clamped(), dt);
            y = st.body.pos.empty() ? x : st.body.pos[0];
            break;
        }
        case Kind::Speed: {
            // How fast it moves (either way), `a` per second being 1, smoothed over `b`
            const double rate = dt > 0 ? std::abs(x - st.prev) / dt : 0.0;
            st.prev = x;
            st.y = toward(st.y, std::clamp(rate / a, 0.0, 1.0), b);
            y = st.y;
            break;
        }
        }
        if (!std::isfinite(y)) y = x;
        m_stageOut[i] = y;
    }
    m_fresh = false;
    m_out = y;
    return y;
}

} // namespace pxs::core::filter
