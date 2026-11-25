// Filters: the names and settings, the operations, and each filter of a chain at values worked out by hand
#include "check.h"

#include <pxs/core/filter/filter.h>

#include <cmath>
#include <cstring>
#include <string>

using namespace pxs::core::filter;

namespace {

bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) <= eps; }

// The value after one frame of `dt` with `x` coming in
double feed(Chain &c, const std::vector<Stage> &stages, double x, double dt = 0.01)
{
    c.input(x);
    return c.step(stages, dt).value_or(std::nan(""));
}

void names()
{
    for (int i = 0; i < kKindCount; ++i) {
        const Kind k = Kind(i);
        CHECK(kindFromKey(key(k)) == k);
        CHECK(std::strlen(name(k)) > 0);
        const std::string g = group(k);
        CHECK(g == "Smoothing" || g == "Shape" || g == "Timing" || g == "Trigger");
        CHECK(std::strlen(about(k)) > 0);
    }
    CHECK(std::string(key(Kind::AutoReturn)) == "return"); // as NyxMap saved it
    CHECK(std::string(key(Kind::WeightedAverage)) == "weighted_average");
    CHECK(!kindFromKey("nothing"));
    for (int i = 0; i < kMathOpCount; ++i) CHECK(mathOpFromKey(key(MathOp(i))) == MathOp(i));
    CHECK(!mathOpFromKey(""));
}

void settings()
{
    CHECK(std::string(setting(Kind::Average, 0).name) == "Window");
    CHECK(setting(Kind::Average, 1).name == nullptr);
    CHECK(setting(Kind::Curve, 0).name == nullptr);
    CHECK(near(setting(Kind::Envelope, 1).def, 0.3));
    CHECK(std::string(setting(Kind::Remap, 3).name) == "Out Max");
    CHECK(setting(Kind::Remap, 4).name == nullptr);
    const Stage env = Stage::made(Kind::Envelope);
    CHECK(near(env.a, 0.005) && near(env.b, 0.3) && near(env.c, 0) && near(env.d, 0.3));
    CHECK(Stage::made(Kind::Curve).curve.size() == 2);
    const Stage r = Stage::remap(1, 2, 3, 4);
    CHECK(r.kind == Kind::Remap && r.a == 1 && r.b == 2 && r.c == 3 && r.d == 4);
}

void operations()
{
    CHECK_EQ(apply(MathOp::Add, 2, 3), 5.0);
    CHECK_EQ(apply(MathOp::Divide, 2, 0), 2.0);       // ÷ 0: as it is
    CHECK_EQ(apply(MathOp::Modulo, -1, 3), 2.0);      // never below 0
    CHECK_EQ(apply(MathOp::Power, -2, 2), -4.0);      // the sign is kept
    CHECK_EQ(apply(MathOp::Round, 0.26, 0.25), 0.25); // to a step
    CHECK_EQ(apply(MathOp::OneMinus, 0.25, 9), 0.75);
    CHECK(!usesValue(MathOp::Abs) && usesValue(MathOp::Min));
}

void nothingYet()
{
    Chain c;
    CHECK(!c.step(std::vector<Stage>{Stage::made(Kind::Average)}, 0.1));
    c.input(std::nan(""));
    CHECK(!c.step(std::vector<Stage>{}, 0.1)); // not a number: ignored
    c.input(0.4);
    CHECK(c.step(std::vector<Stage>{}, 0.1) == 0.4); // no filter: as it comes
}

void shapes()
{
    Chain c;
    Stage m = Stage::made(Kind::Math);
    m.op = MathOp::Multiply;
    m.a = 2;
    CHECK_EQ(feed(c, {m}, 0.3), 0.6);
    Chain cl;
    Stage clamp = Stage::made(Kind::Clamp);
    clamp.a = 0.8;
    clamp.b = 0.2; // either way round
    CHECK_EQ(feed(cl, {clamp}, 0.9), 0.8);
    Chain s;
    CHECK(near(feed(s, {Stage::made(Kind::Steps)}, 0.4), 1.0 / 3)); // 4 levels: 0, 1/3, 2/3, 1
    Chain r;
    CHECK(near(feed(r, {Stage::remap(0.2, 0.6, 10, 20)}, 0.4), 15));
    CHECK(near(feed(r, {Stage::remap(0.2, 0.6, 10, 20)}, 1), 20)); // held at its end
    Chain t;
    const std::vector<Stage> th{Stage::made(Kind::Threshold)}; // 0.5, margin 0.05
    CHECK_EQ(feed(t, th, 0.6), 1.0);
    CHECK_EQ(feed(t, th, 0.49), 1.0); // within the margin: stays on
    CHECK_EQ(feed(t, th, 0.47), 0.0);
    Chain cu;
    const std::vector<Stage> curve{Stage::made(Kind::Curve)}; // straight
    CHECK(near(feed(cu, curve, 0.3), 0.3));
    CHECK(cu.curveInput() == 0.3);
    CHECK(near(feed(cu, curve, 2), 1)); // its input is held within 0..1
}

void smoothing()
{
    Chain w;
    Stage wa = Stage::made(Kind::WeightedAverage);
    wa.a = 0.3; // a time constant of 0.1 s
    const std::vector<Stage> chain{wa};
    CHECK_EQ(feed(w, chain, 0), 0.0); // starts where its input is
    CHECK(near(feed(w, chain, 1, 0.1), 1 - std::exp(-1.0)));
    Chain av;
    Stage avg = Stage::made(Kind::Average);
    avg.a = 1;
    CHECK_EQ(feed(av, {avg}, 0, 0.1), 0.0);
    CHECK(near(feed(av, {avg}, 1, 0.1), 0.5)); // the mean of 0 and 1
    Chain d;
    const std::vector<Stage> damper{Stage::made(Kind::Damper)};
    feed(d, damper, 0);
    double last = 0;
    for (int i = 0; i < 300; ++i) last = feed(d, damper, 1, 0.01);
    CHECK(near(last, 1, 1e-3)); // settled after three seconds
}

void timing()
{
    Chain dl;
    Stage delay = Stage::made(Kind::Delay);
    delay.a = 0.25;
    const std::vector<Stage> chain{delay};
    CHECK_EQ(feed(dl, chain, 1, 0.1), 1.0); // the first one until then
    CHECK_EQ(feed(dl, chain, 2, 0.1), 1.0);
    CHECK_EQ(feed(dl, chain, 3, 0.1), 1.0);
    CHECK_EQ(feed(dl, chain, 4, 0.1), 1.0);
    CHECK_EQ(feed(dl, chain, 5, 0.1), 2.0); // 0.25 s ago
    Chain h;
    Stage hold = Stage::made(Kind::Hold); // 0.5 s
    const std::vector<Stage> held{hold};
    CHECK_EQ(feed(h, held, 1, 0.1), 1.0);
    CHECK_EQ(feed(h, held, 0, 0.2), 1.0); // a fall waits
    CHECK_EQ(feed(h, held, 0, 0.2), 1.0);
    CHECK_EQ(feed(h, held, 0, 0.2), 0.0); // then follows
    Chain db;
    const std::vector<Stage> settle{Stage::made(Kind::Debounce)}; // 0.1 s
    CHECK_EQ(feed(db, settle, 0, 0.05), 0.0);
    CHECK_EQ(feed(db, settle, 1, 0.05), 0.0); // changed: not yet
    CHECK_EQ(feed(db, settle, 1, 0.05), 0.0);
    CHECK_EQ(feed(db, settle, 1, 0.05), 1.0); // stayed 0.1 s
    Chain ar;
    const std::vector<Stage> back{Stage::made(Kind::AutoReturn)}; // after 1 s, back to 0
    CHECK_EQ(feed(ar, back, 0.5, 0.25), 0.5);
    CHECK_EQ(feed(ar, back, 0.5, 0.25), 0.5);
    CHECK_EQ(feed(ar, back, 0.5, 0.25), 0.5);
    CHECK_EQ(feed(ar, back, 0.5, 0.25), 0.0); // a second away: back to rest
    CHECK_EQ(feed(ar, back, 0, 0.1), 0.0);
    CHECK_EQ(feed(ar, back, 0.7, 0.1), 0.7); // at rest again: ready
    Chain sp;
    Stage speed = Stage::made(Kind::Speed);
    speed.b = 0.001; // no smoothing to speak of
    CHECK_EQ(feed(sp, {speed}, 0, 0.1), 0.0);
    CHECK(near(feed(sp, {speed}, 0.05, 0.1), 0.5, 1e-6)); // 0.5 per second, full scale 1
}

void triggers()
{
    Chain t;
    const std::vector<Stage> toggle{Stage::made(Kind::Toggle)};
    CHECK_EQ(feed(t, toggle, 0), 0.0);
    CHECK_EQ(feed(t, toggle, 1), 1.0); // the first press: on
    CHECK_EQ(feed(t, toggle, 0), 1.0);
    CHECK_EQ(feed(t, toggle, 1), 0.0); // the next: off
    Chain p;
    const std::vector<Stage> pulse{Stage::made(Kind::PulseDetection)}; // 0.2, decay 0.3 s
    CHECK_EQ(feed(p, pulse, 0), 0.0);
    CHECK_EQ(feed(p, pulse, 1, 0.01), 1.0);
    CHECK(near(p.step(pulse, 0.15).value_or(-1), 0.5)); // nothing new came in: it fades
    Chain e;
    const std::vector<Stage> env{Stage::made(Kind::Envelope)}; // a hit: attack 5 ms
    CHECK_EQ(feed(e, env, 0.8, 0.01), 0.8); // the top at once, at its velocity
    double last = 1;
    for (int i = 0; i < 100; ++i) last = feed(e, env, 0.8, 0.01);
    CHECK_EQ(last, 0.0); // a sustain of 0: a pulse that ends by itself
}

void physicsFilter()
{
    const Setting friction = setting(Kind::Physics, 0), bounce = setting(Kind::Physics, 3);
    CHECK(std::string(friction.name) == "Friction" && std::string(bounce.name) == "Bounce");
    CHECK(std::string(key(Kind::Physics)) == "physics" && std::string(group(Kind::Physics)) == "Smoothing");

    // Held while values come: pulled to the value, it gets there
    Stage s = Stage::made(Kind::Physics);
    std::vector<Stage> stages{s};
    Chain c;
    c.input(0.2);
    CHECK(near(*c.step(stages, 0.016), 0.2)); // starts where its input is
    double y = 0;
    for (int i = 0; i < 300; ++i) {
        c.input(0.8);
        y = *c.step(stages, 0.016);
    }
    CHECK(near(y, 0.8, 1e-3));

    // Thrown: a fast rise, then the source goes quiet. With no tension it goes on past where the value stopped, hits 1, comes back
    // off the wall and stays within 0..1
    Chain t;
    stages[0].c = 0;   // tension: free
    stages[0].a = 0.5; // little friction
    double peak = 0, lowAfterPeak = 1;
    for (int i = 0; i < 20; ++i) {
        t.input(0.05 * i); // up to 0.95, fast
        y = *t.step(stages, 0.016);
    }
    for (int i = 0; i < 200; ++i) {
        y = *t.step(stages, 0.016); // nothing comes
        CHECK(y >= 0.0 && y <= 1.0);
        if (y > peak) peak = y;
        if (peak >= 1.0) lowAfterPeak = std::min(lowAfterPeak, y);
    }
    CHECK(near(peak, 1.0));      // went on to the wall
    CHECK(lowAfterPeak < 0.999); // and came back off it

    // With tension, let go, it is pulled back to where the value last was
    Chain back;
    stages[0] = Stage::made(Kind::Physics);
    stages[0].c = 200;
    for (int i = 0; i < 20; ++i) {
        back.input(0.03 * i);
        y = *back.step(stages, 0.016);
    }
    for (int i = 0; i < 600; ++i) y = *back.step(stages, 0.016);
    CHECK(near(y, 0.57, 2e-3));

    // Out of 0..1: held within it
    Chain out;
    out.input(3);
    CHECK(near(*out.step(stages, 0.016), 1.0));
}

void chains()
{
    Chain c;
    Stage off = Stage::made(Kind::Math);
    off.a = 10;
    off.enabled = false;
    Stage half = Stage::made(Kind::Math);
    half.a = 0.5;
    const std::vector<Stage> two{off, half};
    CHECK_EQ(feed(c, two, 0.8), 0.4); // the one off lets it through
    CHECK(c.stageOutputs().size() == 2);
    CHECK_EQ(c.stageOutputs()[0], 0.8);
    CHECK(c.output() == 0.4);
    CHECK(c.level() == 0.8);
    // Another kind of filter in a place: its state made again
    Chain k;
    CHECK_EQ(feed(k, {Stage::made(Kind::Toggle)}, 1), 1.0);
    CHECK_EQ(feed(k, {Stage::made(Kind::Hold)}, 0.3), 0.3);
    // Views: the application's own stages, no copy of a curve
    Chain v;
    const auto straight = straightCurve();
    StageView sv;
    sv.kind = Kind::Curve;
    sv.curve = &straight;
    v.input(0.25);
    CHECK(v.step(&sv, 1, 0.01) == 0.25);
    c.reset();
    CHECK(!c.output() && !c.level());
}

} // namespace

int main()
{
    names();
    settings();
    operations();
    nothingYet();
    shapes();
    smoothing();
    timing();
    triggers();
    physicsFilter();
    chains();
    return pxs_test::finish("filter");
}
