#include <pxs/core/value.h>

#include "check.h"
#include "golden.h"

#include <clocale>
#include <cmath>
#include <limits>

using namespace pxs::core;

namespace {

// the number a value reads as (NaN when it reads as none: the tests check has_value() apart)
double num(const Value &v)
{
    const auto n = v.number();
    return n ? *n : std::nan("");
}

void goldenNumbersInText()
{
    // What Qt 6 reads of a string, for the cases that are well defined (the vectors keep what it said; the accidents of its
    // other conversions are not part of the contract).
    for (const auto &g : golden::lines("strnum")) {
        const std::string text = golden::fromHex(g[1]);
        const bool ok = g[2] == "1";
        const std::optional<double> number = Value(text).number();
        CHECK_EQ(number.has_value(), ok);
        if (ok && number) {
            if (g[3] == "nan") CHECK(std::isnan(*number));
            else if (g[3] == "inf") CHECK(std::isinf(*number) && *number > 0);
            else if (g[3] == "-inf") CHECK(std::isinf(*number) && *number < 0);
            else CHECK_EQ(number.value(), std::stod(g[3]));
        }
    }
}

void kinds()
{
    CHECK(Value().kind() == Value::Kind::Nil);
    CHECK(Value(true).kind() == Value::Kind::Bool);
    CHECK(Value(1).kind() == Value::Kind::Int32);
    CHECK(Value(std::int64_t(1) << 40).kind() == Value::Kind::Int64);
    CHECK(Value(1LL).kind() == Value::Kind::Int64);
    CHECK(Value(1L).kind() == Value::Kind::Int64);
    CHECK(Value(1.5f).kind() == Value::Kind::Float);
    CHECK(Value(1.5).kind() == Value::Kind::Double);
    CHECK(Value("a").kind() == Value::Kind::String); // a literal is text, not a bool
    CHECK(Value(std::string("a")).kind() == Value::Kind::String);
    CHECK(Value(Vec3{1, 2, 3}).kind() == Value::Kind::Vec3);
    CHECK(Value(Blob{1, 2}).kind() == Value::Kind::Blob);
    CHECK(Value(static_cast<const char *>(nullptr)).get<std::string>()->empty());
}

void equality()
{
    CHECK(Value(1) == Value(1));
    CHECK(Value(1) != Value(1.0)); // another type is another value
    CHECK(Value() == Value(Nil{}));
    CHECK(Value(Vec2{1, 2}) == Value(Vec2{1, 2}));
    CHECK(Value(Vec2{1, 2}) != Value(Vec3{1, 2, 0}));
    CHECK(Value("a") == Value(std::string("a")));
    const double nan = std::nan("");
    CHECK(Value(nan) != Value(nan)); // as a double is
}

void numbers()
{
    CHECK_EQ(num(Value(true)), 1.0);
    CHECK_EQ(num(Value(false)), 0.0);
    CHECK_EQ(num(Value(7)), 7.0);
    CHECK_EQ(num(Value(std::int64_t(1) << 40)), 1099511627776.0);
    CHECK_EQ(num(Value(0.5f)), 0.5);
    CHECK(!Value().number() && !Value(Vec2{1, 2}).number() && !Value(Blob{1}).number());
    CHECK(!Value("abc").number() && !Value("").number() && !Value("1,5").number() && !Value("0x10").number());
    CHECK(!Value("1e400").number() && !Value("1e-400").number()); // out of what a double holds
    CHECK(Value("5e-324").number().has_value());                  // the smallest denormal is fine
}

void numbersIgnoreTheLocale()
{
    // A program that set a locale with a decimal comma (Qt applications do: QCoreApplication calls setlocale) must still read
    // "0.5" as a half. Skipped silently where the locale is not installed.
    const char *old = std::setlocale(LC_NUMERIC, nullptr);
    const std::string saved = old ? old : "C";
    for (const char *name : {"fr_FR.UTF-8", "fr_FR", "de_DE.UTF-8", "de_DE", "fr_FR.utf8", "de_DE.utf8"}) {
        if (!std::setlocale(LC_NUMERIC, name)) continue;
        CHECK_EQ(num(Value("0.5")), 0.5);
        CHECK_EQ(num(Value("1e3")), 1000.0);
        CHECK(!Value("0,5").number());
        CHECK_EQ(Value(0.25).text(), std::string("0.25"));
        CHECK_EQ(Value(0.1f).text(), std::string("0.1"));
    }
    std::setlocale(LC_NUMERIC, saved.c_str());
}

void truthiness()
{
    CHECK(Value(true).truthy() && !Value(false).truthy());
    CHECK(Value(1).truthy() && !Value(0).truthy() && Value(-3).truthy());
    CHECK(Value(0.1).truthy() && Value(1e-300).truthy() && !Value(0.0).truthy() && !Value(-0.0).truthy());
    CHECK(!Value(std::nan("")).truthy() && !Value(std::nanf("")).truthy());
    CHECK(!Value("").truthy() && !Value("0").truthy() && !Value("false").truthy() && !Value("FALSE").truthy());
    CHECK(Value("1").truthy() && Value("true").truthy() && Value("no").truthy() && Value(" ").truthy());
    CHECK(!Value().truthy() && !Value(Vec2{1, 1}).truthy() && !Value(Blob{1}).truthy());
}

void text()
{
    CHECK_EQ(Value(true).text(), std::string("true"));
    CHECK_EQ(Value(false).text(), std::string("false"));
    CHECK_EQ(Value(-12).text(), std::string("-12"));
    CHECK_EQ(Value(std::int64_t(-9223372036854775807LL - 1)).text(), std::string("-9223372036854775808"));
    CHECK_EQ(Value("é").text(), std::string("é"));
    CHECK_EQ(Value().text(), std::string());
    CHECK_EQ(Value(Vec2{1, 2}).text(), std::string());
}

void shortestRoundTrip()
{
    // formatDouble is the shortest text that reads back as the same double, for every kind of double
    const double samples[] = {0.0, -0.0, 0.5, 0.1, 1.0 / 3, 1e-5, 1e5, 123456789.125, 3.7, 1e20, 1e300, 5e-324, 1.7976931348623157e308,
                              0.49999999999999994, 2147483647.5, 9007199254740993.0, -2.5};
    for (double d : samples) {
        const std::string s = formatDouble(d);
        CHECK(Value(s).number().has_value());
        if (auto back = Value(s).number()) CHECK(*back == d && std::signbit(*back) == std::signbit(d));
    }
    CHECK_EQ(formatDouble(0.1), std::string("0.1"));
    CHECK_EQ(formatDouble(std::numeric_limits<double>::infinity()), std::string("inf"));
    CHECK_EQ(formatDouble(-std::numeric_limits<double>::infinity()), std::string("-inf"));
    CHECK_EQ(formatDouble(std::nan("")), std::string("nan"));
    CHECK_EQ(formatFloat(0.1f), std::string("0.1")); // a float is not shown as its double (0.10000000149011612)
    CHECK_EQ(formatFloat(16777216.0f), std::string("16777216"));
    CHECK_EQ(formatFloat(1e10f), std::string("1e+10"));
}

} // namespace

int main()
{
    goldenNumbersInText();
    kinds();
    equality();
    numbers();
    numbersIgnoreTheLocale();
    truthiness();
    text();
    shortestRoundTrip();
    return pxs_test::finish("value");
}
