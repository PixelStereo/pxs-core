#include <pxs/core/timeline/timeline.h>

#include <algorithm>
#include <cmath>

namespace pxs::core::timeline {

namespace {

constexpr double kPi = 3.14159265358979323846;

// A random number in -1..1 for (seed, n): the same every time
double hashUnit(std::uint32_t seed, long long n)
{
    std::uint64_t x = (std::uint64_t(seed) << 32) ^ std::uint64_t(n) ^ 0x9E3779B97F4A7C15ull;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdull;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ull;
    x ^= x >> 33;
    return double(x >> 11) / double(1ull << 53) * 2.0 - 1.0;
}

// The value of a cubic Bézier (one coordinate) at s in 0..1
double cubic(double a, double b, double c, double d, double s)
{
    const double u = 1 - s;
    return u * u * u * a + 3 * u * u * s * b + 3 * u * s * s * c + s * s * s * d;
}

bool allFinite(const std::array<double, 3> &rgb)
{
    return std::all_of(rgb.begin(), rgb.end(), [](double v) { return std::isfinite(v); });
}

} // namespace

double ease(double t, int curve)
{
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    switch (curve < 0 ? 0 : curve > 5 ? 5 : curve) {
    case 0: return t;
    case 1: return t * t;
    case 2: return t * (2 - t);
    case 3: return t * t * (3 - 2 * t);
    case 4: return t * t * t;
    default: return 1 - (1 - t) * (1 - t) * (1 - t);
    }
}

double Oscillator::valueAt(double played) const
{
    const double per = std::max(0.01, period);
    const double total = played / per + phase; // periods since the start
    const double p = total - std::floor(total); // 0..1 within the period
    double w = 0;
    switch (wave) {
    case Wave::Sine: w = std::sin(2 * kPi * p); break;
    case Wave::Triangle: w = p < 0.25 ? 4 * p : p < 0.75 ? 2 - 4 * p : 4 * p - 4; break; // starts at 0, rises
    case Wave::Saw: w = 2 * p - 1; break;
    case Wave::Square: w = p < 0.5 ? 1 : -1; break;
    case Wave::Random: w = hashUnit(seed, static_cast<long long>(std::floor(total))); break;
    case Wave::SmoothRandom: {
        const long long n = static_cast<long long>(std::floor(total));
        const double e = (1 - std::cos(kPi * p)) / 2; // eased from one value to the next
        w = hashUnit(seed, n) + (hashUnit(seed, n + 1) - hashUnit(seed, n)) * e;
        break;
    }
    }
    return center + amplitude * w;
}

double keyValue(const std::vector<Key> &keys, std::size_t k, double captured)
{
    if (k == 0 && !keys.empty() && keys[0].isCurrentValue && std::isfinite(captured)) return captured;
    return k < keys.size() ? keys[k].v : kNoValue;
}

void bezierHandles(const std::vector<Key> &keys, std::size_t k, double captured, double *t1, double *v1, double *t2, double *v2)
{
    const Key &a = keys[k], &b = keys[k + 1];
    const double va = keyValue(keys, k, captured), vb = keyValue(keys, k + 1, captured), span = std::max(0.0, b.t - a.t);
    // Automatic: flat, a third of the way; a handle never reaches beyond the other key
    *t1 = a.t + (a.hasOut() ? std::clamp(a.outDt, 0.0, span) : span / 3);
    *v1 = va + (a.hasOut() ? a.outDv : 0.0);
    *t2 = b.t + (b.hasIn() ? std::clamp(b.inDt, -span, 0.0) : -span / 3);
    *v2 = vb + (b.hasIn() ? b.inDv : 0.0);
}

double curveAt(const std::vector<Key> &keys, double position, double captured)
{
    if (keys.empty()) return kNoValue;
    if (position <= keys.front().t) return keyValue(keys, 0, captured);
    if (position >= keys.back().t) return keyValue(keys, keys.size() - 1, captured);
    for (std::size_t k = 0; k + 1 < keys.size(); ++k) {
        const Key &a = keys[k], &b = keys[k + 1];
        if (position >= b.t) continue;
        const double va = keyValue(keys, k, captured), vb = keyValue(keys, k + 1, captured);
        if (a.curve == kHold || b.t - a.t <= 1e-9) return va;
        if (a.curve == kBezier) {
            double t1, v1, t2, v2;
            bezierHandles(keys, k, captured, &t1, &v1, &t2, &v2);
            // The curve's time grows with s (the handles stay within the segment): s found by halving
            double lo = 0, hi = 1;
            for (int i = 0; i < 40; ++i) {
                const double mid = (lo + hi) / 2;
                (cubic(a.t, t1, t2, b.t, mid) < position ? lo : hi) = mid;
            }
            return cubic(va, v1, v2, vb, (lo + hi) / 2);
        }
        return va + (vb - va) * ease((position - a.t) / (b.t - a.t), a.curve);
    }
    return keyValue(keys, keys.size() - 1, captured);
}

std::array<double, 3> keyColor(const std::vector<Key> &keys, std::size_t k, const std::array<double, 3> &captured)
{
    if (k == 0 && !keys.empty() && keys.front().isCurrentValue && allFinite(captured)) return captured;
    return k < keys.size() ? keys[k].rgb : kNoColor;
}

std::array<double, 3> colorAt(const std::vector<Key> &keys, double position, const std::array<double, 3> &captured)
{
    if (keys.empty()) return kNoColor;
    if (position <= keys.front().t) return keyColor(keys, 0, captured);
    for (std::size_t k = 0; k + 1 < keys.size(); ++k) {
        const Key &a = keys[k], &b = keys[k + 1];
        if (position >= b.t) continue;
        const auto from = keyColor(keys, k, captured), to = keyColor(keys, k + 1, captured);
        if (a.curve == kHold || b.t - a.t <= 1e-9) return from;
        const double u = ease((position - a.t) / (b.t - a.t), a.curve);
        std::array<double, 3> rgb{};
        for (std::size_t c = 0; c < 3; ++c) rgb[c] = from[c] + (to[c] - from[c]) * u;
        return rgb;
    }
    return keyColor(keys, keys.size() - 1, captured);
}

// ---- the transport

namespace {

// The pass at clock c (0, 1…) and the time within it
void passAt(const Transport &a, double c, double *pass, double *within)
{
    const double d = a.passDuration();
    c = std::clamp(c, 0.0, a.length());
    if (a.loop == Loop::Once) {
        *pass = 0;
        *within = std::min(c, d);
        return;
    }
    *pass = std::floor(c / d);
    *within = c - *pass * d;
    if (std::isfinite(a.length()) && c >= a.length()) { // the end of the last pass
        *pass = std::max(0, a.repeat - 1);
        *within = d;
    }
}

} // namespace

double Transport::passDuration() const { return std::max(kMinDuration, duration); }

double Transport::length() const
{
    const double d = passDuration();
    if (loop == Loop::Once) return d;
    return repeat > 0 ? d * repeat : std::numeric_limits<double>::infinity();
}

bool Transport::backwardsAt(double c) const
{
    double pass = 0, within = 0;
    passAt(*this, c, &pass, &within);
    if (loop == Loop::PingPong) return std::fmod(pass + (reversed ? 1 : 0), 2.0) == 1.0;
    return reversed && pass == 0;
}

double Transport::position(double c) const
{
    double pass = 0, within = 0;
    passAt(*this, c, &pass, &within);
    return backwardsAt(c) ? passDuration() - within : within;
}

double Transport::progress() const { return std::clamp(position() / passDuration(), 0.0, 1.0); }

int Transport::pass() const
{
    double pass = 0, within = 0;
    passAt(*this, clock, &pass, &within);
    return int(std::min(1e9, pass)) + 1;
}

double Transport::played() const { return std::min(clock, length()) + oscillatorOffset; }

double Transport::clockAt(double pos) const
{
    const double d = passDuration();
    pos = std::clamp(pos, 0.0, d);
    double pass = 0, within = 0;
    passAt(*this, clock, &pass, &within);
    return pass * d + (backwardsAt(clock) ? d - pos : pos);
}

void Transport::setLoop(Loop l, int n)
{
    n = std::max(0, n);
    if (l == loop && n == repeat) return;
    if (state == State::Stopped) {
        loop = l;
        repeat = n;
        clock = 0;
        oscillatorOffset = 0;
        reversed = false;
        return;
    }
    const double pos = position(), d = passDuration(), elapsed = played();
    const bool back = backwardsAt(clock);
    loop = l;
    repeat = n;
    reversed = back;
    clock = back ? d - pos : pos; // within the first pass of the new mode
    oscillatorOffset = elapsed - std::min(clock, length());
}

bool Transport::advance(double dt)
{
    clock += std::max(0.0, dt) * speed;
    return clock < length();
}

} // namespace pxs::core::timeline
