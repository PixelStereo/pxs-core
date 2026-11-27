#pragma once
// A parameter: one value of an object, declared once by the object that holds it, and described the way an interface, OSC and
// OSCQuery need it: its type, how it goes from one value to another in a fade (its ramp), its granularity, its access, its
// default, its range, its limits. It reads and writes the value in its holder through two functions it is bound to.
//
// No Qt, no global state. Not thread safe by itself: the holder's lock is the caller's (the functions it is bound to run under
// it), as everywhere in pxs.
#include <optional>
#include <pxs/core/feed/change_feed.h>
#include <pxs/core/value.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace pxs::core::tree {

// What a parameter is, copied out of its holder (an interface works on these: a holder's parameters may be made again when
// its source or its effects change)
struct ParamInfo {
    enum class Type {
        Float,   // a number
        Int,     // a whole number (or one among `values`)
        Bool,    // on / off
        Choice,  // one key among `choices`
        Text,    // a string
        Trigger, // an action without a value
    };
    // How it goes from one value to another in a fade
    enum class Ramp {
        Cut,    // at once (switches, choices, texts)
        Linear, // through the values in between
        Angle,  // the shortest way round (degrees)
        Typed,  // a text typed in over its time (erased back to what it shares with the new one, then typed)
    };
    enum Access { Read = 1, Write = 2, ReadWrite = 3 };

    std::string path;  // its address in its holder ("opacity", "spatial/rotation", "fx/<fx>/param/<name>/x"…)
    std::string label; // "Spatial › Rotation (°)": the category, then its name
    Type type = Type::Float;
    Ramp ramp = Ramp::Linear;
    int access = ReadWrite;
    double step = 0.01;           // its granularity: a number moves by this (a whole number: 1)
    Value defaultValue;           // what a reset puts back; Nil: none
    double min = 0, max = 1;      // the range shown (bars, lanes, OSC range)
    double lo = 0, hi = 1;        // the limits a value is kept within
    std::vector<int> values;      // Int: the values it can take (empty: any whole number within the limits)
    std::vector<std::string> choices; // Choice: its keys (lower case)
    bool animatable = false;      // a timeline or an animation can drive it (the numbers)
    int colorComponent = -1;      // explicit RGB(A) member at <colour path>/r,g,b,a; -1: not a colour
    int pointComponent = -1;      // explicit XY member at <point path>/x,y; -1: not a point
    bool stored = false;          // saved in a file and a snapshot (Parameter::isStored)
    bool presettable = false;     // a preset can hold it: stored, a state of the show (Enable, Solo, Blackout), or an action
    bool action = false;          // an action rather than a value (a Trigger, or a value it can only be sent)
    bool signal = false;          // a stream the holder emits, with no value of its own: it can be seen, not set
    bool unbounded = false;       // no range to tell (OSCQuery omits RANGE): the value is not limited to what is shown
    std::string unit;             // what it measures, as OSCQuery names it ("distance.m", "angle.degree"); empty: none said
    std::string extendedType;     // what it is, as OSCQuery names it ("color.rgb.r", "filepath"); empty: said by its components

    bool isNumber() const { return type == Type::Float || type == Type::Int; }
    std::string name() const; // its own name, without the category ("Rotation (°)")
};

class Parameter
{
public:
    using Type = ParamInfo::Type;
    using Ramp = ParamInfo::Ramp;
    using Getter = std::function<Value()>;
    using Setter = std::function<void(const Value &)>;

    Parameter(std::string path, std::string label, Type type);

    // Its declaration, set fluently where it is made
    Parameter &range(double min, double max);  // shown; also the limits unless limits() says otherwise
    Parameter &limits(double lo, double hi);
    Parameter &step(double s);
    Parameter &ramp(Ramp r);
    Parameter &readOnly();
    Parameter &writeOnly();
    Parameter &unbounded() { m_info.unbounded = true; return *this; }
    Parameter &unit(std::string name) { m_info.unit = std::move(name); return *this; }
    Parameter &extendedType(std::string name) { m_info.extendedType = std::move(name); return *this; }
    Parameter &transient(); // read and written, never saved (a state of the show, not of a cue: on / off, solo)
    Parameter &animatable(bool on = true);
    Parameter &presetValue(bool enabled) { m_presetValue = enabled; return *this; }
    Parameter &colorComponent(int component) { m_info.colorComponent = component; return *this; }
    Parameter &pointComponent(int component) { m_info.pointComponent = component; return *this; }
    Parameter &values(std::vector<int> v); // Int: among these
    Parameter &choices(std::vector<std::string> keys);
    Parameter &byDefault(Value v);
    Parameter &byDefault(Getter f); // a default that depends on where the holder is (pixels…)
    // Where its value lives: read and written in its holder (the holder's lock is held by the caller)
    Parameter &bind(Getter get, Setter set);
    // Who is told when it is written (see ChangeFeed); none by default. The feed must outlive the parameter.
    Parameter &feed(const ChangeFeed *f) { m_feed = f; return *this; }
    // Whose it is (a layer's id; 0: the composition's, an interaction's, the show's, a fixture's): said with a written value
    Parameter &holder(std::uint64_t id) { m_holder = id; return *this; }
    std::uint64_t holder() const { return m_holder; }

    // The same parameter under its holder's holder: path and the address under it ("fx/blur/" + "speed")
    Parameter prefixed(const std::string &prefix) const
    {
        Parameter p = *this;
        p.m_info.path = prefix + m_info.path;
        return p;
    }
    ParamInfo info() const; // its declaration, its default resolved now
    const std::string &path() const { return m_info.path; }
    bool isNumber() const { return m_info.isNumber(); }
    bool animatable() const { return m_info.animatable; }
    int colorComponent() const { return m_info.colorComponent; }

    Value value() const; // Nil when it cannot be read
    double number() const { return value().number().value_or(0.0); }
    double asNumber() const; // what an Internal interaction follows: a number, a switch 0 / 1, a choice's index (NaN: none of its keys)
    // Written within its limits and its granularity (an Int to the nearest of its values, a Choice only to one of its keys);
    // false if it cannot be written or the value is not one it takes. Tells the feed when it was written.
    bool setValue(const Value &v);
    bool setNumber(double v) { return setValue(Value(v)); }
    bool reset() { return setValue(info().defaultValue); }

    // Its value as a file keeps it (a project, a snapshot, a copied layer): a number, a switch, a key, a text
    bool isPresetValue() const;
    bool isStored() const; // read and written, not an action
    Value stored() const;  // Float -> double, Int -> int32, Bool -> bool, Choice and Text -> string; Nil for a Trigger
    bool setStored(const Value &v) { return !v.isNil() && setValue(v); }

private:
    bool writeValue(const Value &v);

    ParamInfo m_info;
    std::optional<bool> m_presetValue;
    Getter m_default, m_get;
    Setter m_set;
    bool m_transient = false;
    std::uint64_t m_holder = 0;
    const ChangeFeed *m_feed = nullptr;
};

} // namespace pxs::core::tree
