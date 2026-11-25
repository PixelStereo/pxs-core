#include <pxs/core/physics/physics.h>

#include <algorithm>
#include <cmath>

namespace pxs::core::physics {

Settings Settings::clamped() const
{
    auto keep = [](double v, double lo, double hi, double def) { return std::isfinite(v) ? std::clamp(v, lo, hi) : def; };
    const Settings d;
    Settings s;
    s.mode = std::clamp(mode, int(None), int(MassSpring));
    s.friction = keep(friction, 0, kFrictionMax, d.friction);
    s.attraction = keep(attraction, 0, kAttractionMax, d.attraction);
    s.tension = keep(tension, 0, kTensionMax, d.tension);
    s.wall = keep(wall, 0, kWallMax, d.wall);
    s.rate = keep(rate, kRateMin, kRateMax, d.rate);
    return s;
}

double toUnit(double value, double lo, double hi) { return hi > lo ? std::clamp((value - lo) / (hi - lo), 0.0, 1.0) : 0.0; }

double fromUnit(double unit, double lo, double hi) { return lo + std::clamp(unit, 0.0, 1.0) * (hi - lo); }

bool step(Body &b, const Settings *each, std::size_t count, double dt)
{
    const std::size_t n = b.pos.size();
    b.vel.resize(n, 0.0);
    b.target.resize(n, 0.0);
    if (!each || count == 0) return false;
    bool moving = false;
    for (std::size_t i = 0; i < n; ++i) {
        const Settings &p = each[std::min(i, count - 1)];
        double &u = b.pos[i], &v = b.vel[i];
        const double t = b.target[i];
        if (p.mode == Settings::Interpolate) {
            u += (t - u) * (1 - std::exp(-p.rate * dt));
            v = 0;
            if (std::abs(t - u) < 5e-4) u = t;
            else moving = true;
        } else if (p.mode == Settings::MassSpring) {
            const double pull = b.holds(i) ? p.attraction : p.tension; // attracted to the finger; let go, pulled to where it was
            v += pull * (t - u) * dt;
            v *= std::exp(-p.friction * dt);
            u += v * dt;
            if (u < 0) {
                u = 0;
                v = -v * p.wall;
            }
            if (u > 1) {
                u = 1;
                v = -v * p.wall;
            }
            if (std::abs(v) > 2e-3 || (pull > 0 && std::abs(t - u) > 5e-4)) moving = true;
            else v = 0;
        } else { // None: where the hand is
            u = t;
            v = 0;
        }
    }
    return moving;
}

} // namespace pxs::core::physics
