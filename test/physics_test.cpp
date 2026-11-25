// Physics: a number that follows the hand (Interpolate), one held to it by a spring and thrown (Mass-Spring), the walls, the settings
#include "check.h"

#include <pxs/core/physics/physics.h>

#include <cmath>
#include <limits>

using namespace pxs::core::physics;

namespace {

bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) <= eps; }

Body body(double at, double target)
{
    Body b;
    b.pos = {at};
    b.target = {target};
    return b;
}

void none()
{
    Body b = body(0.2, 0.8);
    CHECK(!step(b, Settings{}, 0.016)); // no physics: where the hand is, at once
    CHECK_EQ(b.pos[0], 0.8);
}

void interpolate()
{
    Settings s;
    s.mode = Settings::Interpolate;
    s.rate = 10;
    Body b = body(0, 1);
    CHECK(step(b, s, 0.1));
    CHECK(near(b.pos[0], 1 - std::exp(-1.0)));
    for (int i = 0; i < 200; ++i) step(b, s, 0.016);
    CHECK_EQ(b.pos[0], 1.0); // there, and snapped
    CHECK(!step(b, s, 0.016));
}

void massSpring()
{
    Settings s;
    s.mode = Settings::MassSpring;
    // Held: attracted to the finger
    Body b = body(0, 1);
    b.held = true;
    CHECK(step(b, s, 0.01));
    CHECK(b.vel[0] > 0 && b.pos[0] > 0);
    // Let go with a push and no tension: thrown, it bounces on the top wall and loses some of its speed
    Body t = body(0.95, 0.95);
    t.vel = {2};
    step(t, s, 0.05);
    CHECK_EQ(t.pos[0], 1.0);
    CHECK(t.vel[0] < 0 && std::abs(t.vel[0]) < 2);
    for (int i = 0; i < 1000; ++i) step(t, s, 0.016);
    CHECK_EQ(t.vel[0], 0.0); // friction stopped it
    // The hand holds one ball of a pad: the other goes on by itself
    Body pad;
    pad.pos = {0, 0};
    pad.target = {1, 1};
    pad.held = true;
    pad.hand = {1, 0};
    step(pad, s, 0.01);
    CHECK(pad.pos[0] > 0 && pad.pos[1] == 0); // no tension: nothing pulls the other one
}

void eachNumber()
{
    Settings glide, spring;
    glide.mode = Settings::Interpolate;
    spring.mode = Settings::MassSpring;
    const Settings each[] = {glide, spring};
    Body b;
    b.pos = {0, 0, 0};
    b.target = {1, 1, 1};
    step(b, each, 2, 0.1);
    CHECK(b.pos[0] > 0.5);        // the first glides
    CHECK(b.pos[1] == 0);         // the others spring (not held, no tension): nothing pulls them
    CHECK(b.pos[2] == b.pos[1]);  // fewer settings than numbers: the last one for the rest
}

void ranges()
{
    CHECK_EQ(toUnit(5, 0, 10), 0.5);
    CHECK_EQ(toUnit(20, 0, 10), 1.0);
    CHECK_EQ(toUnit(3, 4, 4), 0.0); // an empty range
    CHECK_EQ(fromUnit(0.25, -1, 1), -0.5);
    Settings s;
    s.mode = 7;
    s.friction = -1;
    s.rate = std::numeric_limits<double>::quiet_NaN();
    const Settings c = s.clamped();
    CHECK_EQ(c.mode, int(Settings::MassSpring));
    CHECK_EQ(c.friction, 0.0);
    CHECK_EQ(c.rate, Settings{}.rate); // not a number: the default
    CHECK(Settings{} == Settings{}.clamped());
}

} // namespace

int main()
{
    none();
    interpolate();
    massSpring();
    eachNumber();
    ranges();
    return pxs_test::finish("physics");
}
