// Timelines: the easings, the waves, the curves and the transport, at values worked out by hand
#include "check.h"

#include <pxs/core/timeline/timeline.h>

#include <cmath>

using namespace pxs::core::timeline;

namespace {

bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) <= eps; }

void easings()
{
    for (int c = 0; c < kEaseCount; ++c) {
        CHECK_EQ(ease(0, c), 0.0);
        CHECK_EQ(ease(1, c), 1.0);
        CHECK_EQ(ease(-1, c), 0.0);
        CHECK_EQ(ease(2, c), 1.0);
    }
    CHECK(near(ease(0.5, 0), 0.5));
    CHECK(near(ease(0.5, 1), 0.25));
    CHECK(near(ease(0.5, 2), 0.75));
    CHECK(near(ease(0.5, 3), 0.5));
    CHECK(near(ease(0.5, 4), 0.125));
    CHECK(near(ease(0.5, 5), 0.875));
    CHECK(near(ease(0.5, 99), ease(0.5, 5))); // out of range: the nearest
    CHECK(near(ease(0.5, -3), ease(0.5, 0)));
}

void waves()
{
    Oscillator o;
    o.center = 0;
    o.amplitude = 1;
    o.period = 2;
    o.wave = Wave::Sine;
    CHECK(near(o.valueAt(0), 0));
    CHECK(near(o.valueAt(0.5), 1)); // a quarter of a period
    CHECK(near(o.valueAt(1.5), -1));
    o.wave = Wave::Triangle;
    CHECK(near(o.valueAt(0), 0));
    CHECK(near(o.valueAt(0.5), 1));
    CHECK(near(o.valueAt(1), 0));
    CHECK(near(o.valueAt(1.5), -1));
    o.wave = Wave::Saw;
    CHECK(near(o.valueAt(0), -1));
    CHECK(near(o.valueAt(1), 0));
    o.wave = Wave::Square;
    CHECK_EQ(o.valueAt(0.2), 1.0);
    CHECK_EQ(o.valueAt(1.2), -1.0);
    // The phase moves the wave by part of a period
    o.wave = Wave::Sine;
    o.phase = 0.25;
    CHECK(near(o.valueAt(0), 1));
    o.phase = 0;
    // Center and amplitude
    o.center = 0.5;
    o.amplitude = 0.25;
    CHECK(near(o.valueAt(0.5), 0.75));
    // The random waves: the same for the same seed and time, within the amplitude, held for a period
    o.center = 0;
    o.amplitude = 1;
    o.wave = Wave::Random;
    o.seed = 7;
    const double r = o.valueAt(0.3);
    CHECK_EQ(o.valueAt(0.3), r);
    CHECK_EQ(o.valueAt(1.9), r); // the same period
    CHECK(r >= -1 && r <= 1);
    Oscillator other = o;
    other.seed = 8;
    CHECK(other.valueAt(0.3) != r);
    // Smooth random: from one value to the next, without a jump at the period's end
    o.wave = Wave::SmoothRandom;
    CHECK(near(o.valueAt(2 - 1e-9), o.valueAt(2), 1e-6));
    CHECK(near(o.valueAt(0), r)); // starts at the period's random value
    // A period too short is 0.01 s
    o.wave = Wave::Saw;
    o.period = 0;
    CHECK(near(o.valueAt(0.005), 0));
}

void curves()
{
    std::vector<Key> keys;
    CHECK(std::isnan(curveAt(keys, 1)));
    keys.push_back(Key{0, 0});
    keys.push_back(Key{2, 10});
    CHECK_EQ(curveAt(keys, -1), 0.0); // before the first key: its value
    CHECK_EQ(curveAt(keys, 3), 10.0); // after the last
    CHECK(near(curveAt(keys, 1), 5));
    keys[0].curve = 1; // ease in
    CHECK(near(curveAt(keys, 1), 2.5));
    keys[0].curve = kHold;
    CHECK_EQ(curveAt(keys, 1.999), 0.0);
    CHECK_EQ(curveAt(keys, 2), 10.0);
    // A first key "current value": the captured value, when there is one
    keys[0].curve = 0;
    keys[0].isCurrentValue = true;
    keys[0].v = 123; // a placeholder
    CHECK(near(curveAt(keys, 1, 4), 7));
    CHECK(near(curveAt(keys, 1), 66.5)); // nothing captured yet: the placeholder
    keys[0].isCurrentValue = false;
    keys[0].v = 0;
    // Bézier: automatic handles are flat, a third of the way; the curve passes through its keys and is symmetric
    keys[0].curve = kBezier;
    double t1, v1, t2, v2;
    bezierHandles(keys, 0, kNoValue, &t1, &v1, &t2, &v2);
    CHECK(near(t1, 2.0 / 3) && near(v1, 0) && near(t2, 4.0 / 3) && near(v2, 10));
    CHECK(near(curveAt(keys, 1), 5, 1e-6));
    CHECK(curveAt(keys, 0.5) < 2.5); // slow at the start
    CHECK(near(curveAt(keys, 0.5) + curveAt(keys, 1.5), 10, 1e-6));
    // A handle that reaches beyond the other key is kept within the segment
    keys[0].outDt = 5;
    keys[0].outDv = 1;
    bezierHandles(keys, 0, kNoValue, &t1, &v1, &t2, &v2);
    CHECK(near(t1, 2) && near(v1, 1));
    // Three keys: each segment its own
    keys = {Key{0, 0}, Key{1, 1}, Key{2, 0}};
    CHECK(near(curveAt(keys, 0.5), 0.5));
    CHECK(near(curveAt(keys, 1.5), 0.5));
    // Colours: one RGB per key, eased together
    std::vector<Key> c(2);
    c[0].t = 0;
    c[0].rgb = {0, 0, 0};
    c[1].t = 1;
    c[1].rgb = {1, 0.5, 0};
    const auto mid = colorAt(c, 0.5);
    CHECK(near(mid[0], 0.5) && near(mid[1], 0.25) && near(mid[2], 0));
    c[0].isCurrentValue = true;
    const auto from = colorAt(c, 0, {0.2, 0.2, 0.2});
    CHECK(near(from[0], 0.2));
    CHECK(std::isnan(colorAt({}, 0)[0]));
}

void transport()
{
    Transport t;
    t.duration = 2;
    // Loop, endless
    CHECK(std::isinf(t.length()));
    t.clock = 5;
    CHECK(near(t.position(), 1));
    CHECK_EQ(t.pass(), 3);
    CHECK(near(t.progress(), 0.5));
    // Loop, three passes: at its end, the end of the last pass
    t.repeat = 3;
    CHECK(near(t.length(), 6));
    t.clock = 7;
    CHECK(near(t.position(), 2));
    CHECK_EQ(t.pass(), 3);
    // Once
    t.loop = Loop::Once;
    CHECK(near(t.length(), 2));
    t.clock = 1.5;
    CHECK(near(t.position(), 1.5));
    t.clock = 9;
    CHECK(near(t.position(), 2));
    // Ping-pong: the second pass goes back
    t.loop = Loop::PingPong;
    t.repeat = 0;
    t.clock = 3;
    CHECK(t.backwardsAt(3));
    CHECK(near(t.position(), 1));
    t.clock = 4.5;
    CHECK(!t.backwardsAt(4.5));
    CHECK(near(t.position(), 0.5));
    // The clock that gives a position within the pass it is in
    t.clock = 3;
    CHECK(near(t.position(t.clockAt(0.5)), 0.5));
    // A pass is never shorter than kMinDuration
    Transport tiny;
    tiny.duration = 0;
    CHECK_EQ(tiny.passDuration(), Transport::kMinDuration);
    // advance: the speed, and the end of what it plays
    Transport a;
    a.duration = 1;
    a.loop = Loop::Once;
    a.speed = 2;
    CHECK(a.advance(0.25));
    CHECK(near(a.clock, 0.5));
    CHECK(!a.advance(0.5));
    a.speed = 0;
    a.clock = 0.2;
    a.advance(10);
    CHECK(near(a.clock, 0.2)); // frozen
    a.advance(-1);
    CHECK(near(a.clock, 0.2)); // never backwards
    // played: continuous across the loops, for the oscillators
    Transport p;
    p.duration = 1;
    p.repeat = 2;
    p.clock = 5;
    CHECK(near(p.played(), 2));
}

void loopChanges()
{
    // Stopped: the new mode from the start
    Transport t;
    t.duration = 2;
    t.clock = 3;
    t.setLoop(Loop::PingPong, 2);
    CHECK(t.loop == Loop::PingPong && t.repeat == 2);
    CHECK_EQ(t.clock, 0.0);
    // Playing, on a ping-pong's way back: it goes on from where it is, the same way, and the time played does not jump
    Transport p;
    p.duration = 2;
    p.loop = Loop::PingPong;
    p.state = State::Playing;
    p.clock = 2.5; // the second pass, going back: position 1.5
    const double where = p.position(), played = p.played();
    p.setLoop(Loop::Loop, 0);
    CHECK(near(p.position(), where));
    CHECK(p.backwardsAt(p.clock)); // the pass it is in still goes back
    CHECK(near(p.played(), played));
    p.advance(0.5);
    CHECK(near(p.position(), where - 0.5));
    // Once while playing: it ends this pass, then stops
    Transport o;
    o.duration = 2;
    o.state = State::Playing;
    o.clock = 5; // a loop, third pass, position 1
    o.setLoop(Loop::Once, 0);
    CHECK(near(o.position(), 1));
    CHECK(o.advance(0.5));
    CHECK(!o.advance(1));
    // Nothing changes when nothing changes
    Transport same;
    same.clock = 3;
    same.state = State::Playing;
    same.setLoop(Loop::Loop, 0);
    CHECK_EQ(same.clock, 3.0);
    // A negative repeat is 0 (endless)
    same.setLoop(Loop::Loop, -4);
    CHECK_EQ(same.repeat, 0);
}

void actions()
{
    Transport t;
    t.duration = 2;
    CHECK(t.control(Action::Play)); // from stopped: it starts
    CHECK(t.state == State::Playing);
    t.advance(1.5);
    CHECK(!t.control(Action::Play)); // already playing: from where it is
    CHECK(near(t.clock, 1.5));
    CHECK(!t.control(Action::Pause));
    CHECK(t.state == State::Paused);
    CHECK(!t.control(Action::Pause)); // paused stays paused
    CHECK(!t.control(Action::Play));
    CHECK(near(t.clock, 1.5)); // a pause goes on from where it was
    CHECK(t.control(Action::Rewind));
    CHECK(near(t.clock, 0) && t.state == State::Playing);
    t.clock = 1;
    CHECK(!t.control(Action::Stop));
    CHECK(t.state == State::Stopped && near(t.clock, 0));
    CHECK(t.control(Action::Seek, 1.25)); // a stopped transport sought: paused there
    CHECK(t.state == State::Paused && near(t.position(), 1.25));
    CHECK(!t.control(Action::Seek, 0.5)); // not stopped: no new start
    t.loop = Loop::Once;
    CHECK(!t.control(Action::Seek, 99)); // within what it plays
    CHECK(near(t.clock, 2));
    CHECK(!t.control(Action::Speed, 20));
    CHECK(near(t.speed, 10));
    t.control(Action::Speed, -1);
    CHECK(near(t.speed, 0));
    t.state = State::Stopped;
    t.control(Action::LoopMode, 0, Loop::PingPong, 3);
    CHECK(t.loop == Loop::PingPong && t.repeat == 3);
}

} // namespace

int main()
{
    easings();
    waves();
    curves();
    transport();
    loopChanges();
    actions();
    return pxs_test::finish("timeline");
}
