#include <pxs/core/tree/parameter.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace pxs::core::tree {
namespace {

constexpr std::string_view kCategorySeparator = " \xE2\x80\xBA "; // " › "

std::string lowerAscii(std::string s)
{
    for (char &c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

int toInt(double x)
{
    // `x` is within the parameter's limits, which are finite and fit an int by construction of what is declared; a limit beyond an
    // int is held to one rather than wrapped around.
    const double r = std::round(x);
    if (r >= double(std::numeric_limits<int>::max())) return std::numeric_limits<int>::max();
    if (r <= double(std::numeric_limits<int>::min())) return std::numeric_limits<int>::min();
    return int(r);
}

} // namespace

std::string ParamInfo::name() const
{
    const auto cut = label.rfind(kCategorySeparator);
    return cut == std::string::npos ? label : label.substr(cut + kCategorySeparator.size());
}

Parameter::Parameter(std::string path, std::string label, Type type)
{
    m_info.path = std::move(path);
    m_info.label = std::move(label);
    m_info.type = type;
    switch (type) {
    case Type::Float: m_info.ramp = Ramp::Linear; break;
    case Type::Int:
        m_info.ramp = Ramp::Linear;
        m_info.step = 1;
        break;
    case Type::Trigger:
        m_info.access = ParamInfo::Write;
        m_info.ramp = Ramp::Cut;
        break;
    default: m_info.ramp = Ramp::Cut; break;
    }
    m_info.animatable = m_info.isNumber();
    if (type == Type::Bool) m_info.min = 0, m_info.max = 1, m_info.lo = 0, m_info.hi = 1;
}

Parameter &Parameter::range(double min, double max)
{
    m_info.min = m_info.lo = std::min(min, max);
    m_info.max = m_info.hi = std::max(min, max);
    return *this;
}

Parameter &Parameter::limits(double lo, double hi)
{
    m_info.lo = std::min(lo, hi);
    m_info.hi = std::max(lo, hi);
    return *this;
}

Parameter &Parameter::step(double s)
{
    m_info.step = std::max(0.0, s);
    return *this;
}

Parameter &Parameter::ramp(Ramp r)
{
    m_info.ramp = r;
    return *this;
}

Parameter &Parameter::readOnly()
{
    m_info.access = ParamInfo::Read;
    m_info.animatable = false;
    return *this;
}

Parameter &Parameter::writeOnly()
{
    m_info.access = ParamInfo::Write;
    m_info.animatable = false;
    return *this;
}

Parameter &Parameter::transient()
{
    m_transient = true;
    return *this;
}

Parameter &Parameter::animatable(bool on)
{
    m_info.animatable = on && m_info.isNumber() && (m_info.access & ParamInfo::Write);
    return *this;
}

Parameter &Parameter::values(std::vector<int> v)
{
    m_info.values = std::move(v);
    if (!m_info.values.empty()) {
        const auto [a, b] = std::minmax_element(m_info.values.begin(), m_info.values.end());
        range(*a, *b);
    }
    return *this;
}

Parameter &Parameter::choices(std::vector<std::string> keys)
{
    m_info.choices = std::move(keys);
    return *this;
}

Parameter &Parameter::byDefault(Value v)
{
    m_default = [v = std::move(v)] { return v; };
    return *this;
}

Parameter &Parameter::byDefault(Getter f)
{
    m_default = std::move(f);
    return *this;
}

Parameter &Parameter::bind(Getter get, Setter set)
{
    m_get = std::move(get);
    m_set = std::move(set);
    return *this;
}

ParamInfo Parameter::info() const
{
    ParamInfo i = m_info;
    if (m_default) i.defaultValue = m_default();
    i.stored = isStored();
    i.action = (m_info.access & ParamInfo::Write) && m_set && (m_info.type == Type::Trigger || !(m_info.access & ParamInfo::Read));
    i.presettable = m_presetValue.value_or(i.stored || m_transient || i.action);
    return i;
}

Value Parameter::value() const
{
    if (!(m_info.access & ParamInfo::Read) || !m_get) return {};
    return m_get();
}

double Parameter::asNumber() const
{
    const Value v = value();
    if (m_info.type == Type::Bool) return v.truthy() ? 1.0 : 0.0;
    if (m_info.type == Type::Choice) {
        const std::string key = v.text();
        const auto it = std::find(m_info.choices.begin(), m_info.choices.end(), key);
        return it != m_info.choices.end() ? double(it - m_info.choices.begin()) : std::numeric_limits<double>::quiet_NaN();
    }
    return v.number().value_or(0.0);
}

bool Parameter::setValue(const Value &v)
{
    const bool written = writeValue(v);
    if (written && m_feed) m_feed->parameter(m_holder, m_info.path);
    return written;
}

bool Parameter::writeValue(const Value &v)
{
    if (!(m_info.access & ParamInfo::Write) || !m_set) return false;
    switch (m_info.type) {
    case Type::Float: {
        const auto x = v.number();
        if (!x || !std::isfinite(*x)) return false;
        m_set(Value(std::clamp(*x, m_info.lo, m_info.hi)));
        return true;
    }
    case Type::Int: {
        const auto number = v.number();
        if (!number || !std::isfinite(*number)) return false;
        const double x = *number;
        int n = toInt(std::clamp(x, m_info.lo, m_info.hi));
        if (!m_info.values.empty()) { // the nearest of its values
            int best = m_info.values.front();
            for (int c : m_info.values)
                if (std::abs(double(c) - x) < std::abs(double(best) - x)) best = c;
            n = best;
        }
        m_set(Value(n));
        return true;
    }
    case Type::Bool: m_set(Value(v.truthy())); return true;
    case Type::Choice: {
        const std::string key = lowerAscii(v.text());
        if (std::find(m_info.choices.begin(), m_info.choices.end(), key) == m_info.choices.end()) return false;
        m_set(Value(key));
        return true;
    }
    case Type::Text: m_set(Value(v.text())); return true;
    case Type::Trigger: m_set(Value()); return true;
    }
    return false;
}

bool Parameter::isPresetValue() const
{
    return m_presetValue.value_or(!m_transient) && m_info.access == ParamInfo::ReadWrite
        && m_info.type != Type::Trigger && m_get && m_set;
}

bool Parameter::isStored() const
{
    return m_info.access == ParamInfo::ReadWrite && m_info.type != Type::Trigger && !m_transient && m_get && m_set;
}

Value Parameter::stored() const
{
    const Value v = value();
    switch (m_info.type) {
    case Type::Float: return Value(v.number().value_or(0.0));
    case Type::Int: return Value(toInt(v.number().value_or(0.0)));
    case Type::Bool: return Value(v.truthy());
    case Type::Choice:
    case Type::Text: return Value(v.text());
    case Type::Trigger: break;
    }
    return {};
}

} // namespace pxs::core::tree
