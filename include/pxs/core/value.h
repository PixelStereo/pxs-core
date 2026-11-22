#pragma once
// A value of the address space: what a parameter holds, what an OSC argument carries, what a client writes. A closed set of
// types (the lesson of libossia: a fixed set is easier to keep identical on every platform than a dynamic one), no Qt.
//
//   Nil    no data (an action's argument, a trigger; OSC N and I)
//   bool   on / off (OSC T and F)
//   int32  a whole number (OSC i)         int64   a long whole number (OSC h, t; r and m as a number)
//   float  (OSC f)                        double  (OSC d)
//   string text, UTF-8 (OSC s, S)         blob    bytes (OSC b)
//   Vec2, Vec3, Vec4  floats that go together: a point, a position, a colour. One OSC argument per component ("ff", "fff", "ffff").
//
// The conversions are explicit and portable: they do not depend on a locale, a platform or a toolkit, and they never wrap
// around silently. (QVariant's, which the first tree used, wrap an out-of-range double to an int and treat "0.4" as true: those
// accidents are not kept.)
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace pxs::core {

struct Nil {
    friend constexpr bool operator==(Nil, Nil) { return true; }
    friend constexpr bool operator!=(Nil, Nil) { return false; }
};
using Vec2 = std::array<float, 2>;
using Vec3 = std::array<float, 3>;
using Vec4 = std::array<float, 4>;
using Blob = std::vector<std::uint8_t>;

class Value
{
public:
    using Variant = std::variant<Nil, bool, std::int32_t, std::int64_t, float, double, std::string, Vec2, Vec3, Vec4, Blob>;
    enum class Kind { Nothing, Boolean, Integer32, Integer64, Float, Double, String, Vector2, Vector3, Vector4, Bytes };

    Value() = default; // Nil
    Value(Nil) {}
    Value(bool v) : m_v(v) {}
    Value(int v) : m_v(static_cast<std::int32_t>(v)) {}
    // Any other integer type is an int64 (an unsigned value above INT64_MAX wraps: there is none in OSC).
    template <class T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, int>, int> = 0>
    Value(T v) : m_v(static_cast<std::int64_t>(v)) {}
    Value(float v) : m_v(v) {}
    Value(double v) : m_v(v) {}
    Value(std::string v) : m_v(std::move(v)) {}
    Value(const char *v) : m_v(std::string(v ? v : "")) {}
    Value(Vec2 v) : m_v(v) {}
    Value(Vec3 v) : m_v(v) {}
    Value(Vec4 v) : m_v(v) {}
    Value(Blob v) : m_v(std::move(v)) {}

    Kind kind() const { return static_cast<Kind>(m_v.index()); }
    bool isNil() const { return m_v.index() == 0; }
    template <class T> const T *get() const { return std::get_if<T>(&m_v); }
    template <class T> bool is() const { return std::holds_alternative<T>(m_v); }
    const Variant &variant() const { return m_v; }

    // A number, when the value is one or reads as one: a bool (0 or 1), an integer, a float or double, a string that holds a
    // decimal number — optional sign, digits with an optional '.', optional exponent ("12", "-.5", "5.", "1e-3", " 7 " with
    // spaces around), or "inf" / "nan" in any case — read the same whatever the locale. Nothing for a Nil, a vector, a blob, any
    // other text, or a number that overflows or underflows a double ("1e400", "1e-400").
    std::optional<double> number() const;
    // On / off: a bool as it is; a number is on when it is not 0 (not-a-number is off); a string is off when empty, "0" or
    // "false" (any case), on otherwise; a Nil, a vector or a blob is off.
    bool truthy() const;
    // As text: a string as it is, "true" / "false", an integer in decimal, a float or double in the shortest form that reads
    // back to the same value ("0.5", "1e-05" is "1e-05"), nothing for a Nil, a vector or a blob.
    std::string text() const;

    friend bool operator==(const Value &a, const Value &b) { return a.m_v == b.m_v; }
    friend bool operator!=(const Value &a, const Value &b) { return !(a == b); }

private:
    Variant m_v;
};

using ValueList = std::vector<Value>; // the arguments of an OSC message, one per type tag

// The shortest decimal text that reads back as the same double, independent of the locale ("0.1", "1e+20", "-0", "inf", "nan").
std::string formatDouble(double value);
std::string formatFloat(float value);

// A decimal number in text, as `Value::number` reads it.
std::optional<double> parseNumber(std::string_view text);

} // namespace pxs::core
