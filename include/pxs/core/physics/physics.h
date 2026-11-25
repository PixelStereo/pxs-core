#pragma once
// The physics of a control's numbers: one body per number, in the 0..1 of its range, that goes toward where the hand is (Interpolate)
// or is held to it by a spring and goes on by itself when the hand lets go (Mass-Spring: attracted to the finger, then thrown or pulled
// back to where it was, with friction, and a bounce on the ends of the range). The logic only: which control a number belongs to, its
// range and how its settings are saved is the application's (pxs-qt has the Qt face), and so is the clock (step every frame).
#include <cstddef>
#include <vector>

namespace pxs::core::physics {

struct Settings {
    enum Mode { None = 0, Interpolate = 1, MassSpring = 2 };
    int mode = None;
    double friction = 5;     // a rate per second
    double attraction = 120; // the pull to the finger while it is on the control
    double tension = 0;      // the pull to where the finger last was, once it has let go (0: thrown, free, it bounces on the ends)
    double wall = 0.6;       // what is kept of the speed against an end of the range
    double rate = 12;        // per second, how fast Interpolate follows the hand
    bool on() const { return mode != None; }
    bool operator==(const Settings &o) const
    {
        return mode == o.mode && friction == o.friction && attraction == o.attraction && tension == o.tension && wall == o.wall && rate == o.rate;
    }
    bool operator!=(const Settings &o) const { return !(*this == o); }
    Settings clamped() const; // each setting within its range (below), the mode one of the three
};

// The ranges of the settings (what an editor offers, what clamped() keeps)
inline constexpr double kFrictionMax = 30, kAttractionMax = 400, kTensionMax = 400, kWallMax = 1, kRateMin = 0.5, kRateMax = 60;

struct Body {
    std::vector<double> pos, vel, target; // 0..1 of each number's range (vel per second)
    bool held = false;                    // the hand is on the control
    std::vector<char> hand;               // which numbers it holds (empty: all of them); a pad's other balls go on by themselves
    bool holds(std::size_t i) const { return held && (hand.empty() || (i < hand.size() && hand[i])); }
};

// A number of a range as 0..1 (clamped; an empty range: 0), and back
double toUnit(double value, double lo, double hi);
double fromUnit(double unit, double lo, double hi);

// One step of dt seconds; number i moves by each[i] (by the last one when there are fewer: a single one for all of them). True while
// something still moves (or is held by the hand and not there yet).
bool step(Body &b, const Settings *each, std::size_t count, double dt);
inline bool step(Body &b, const Settings &all, double dt) { return step(b, &all, 1, dt); }

} // namespace pxs::core::physics
