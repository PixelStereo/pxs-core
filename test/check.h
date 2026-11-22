#pragma once
// A minimal test harness: no framework to install, and CHECK stays active in Release (assert() does not).
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

namespace pxs_test {
inline int &failures()
{
    static int n = 0;
    return n;
}
template <typename T> std::string show(const T &v)
{
    std::ostringstream s;
    s << v;
    return s.str();
}
inline std::string show(bool v) { return v ? "true" : "false"; }
inline std::string show(unsigned char v) { return std::to_string(int(v)); }
inline int finish(const char *name)
{
    if (failures()) std::fprintf(stderr, "%s: %d check(s) failed\n", name, failures());
    else std::printf("%s: ok\n", name);
    return failures() ? 1 : 0;
}
} // namespace pxs_test

#define CHECK(expr)                                                                                      \
    do {                                                                                                 \
        if (!(expr)) {                                                                                   \
            std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #expr);                \
            ++pxs_test::failures();                                                                      \
        }                                                                                                \
    } while (0)

#define CHECK_EQ(a, b)                                                                                   \
    do {                                                                                                 \
        const auto &va_ = (a);                                                                           \
        const auto &vb_ = (b);                                                                           \
        if (!(va_ == vb_)) {                                                                             \
            std::fprintf(stderr, "%s:%d: CHECK_EQ(%s, %s) failed: %s != %s\n", __FILE__, __LINE__, #a, #b, \
                         pxs_test::show(va_).c_str(), pxs_test::show(vb_).c_str());                      \
            ++pxs_test::failures();                                                                      \
        }                                                                                                \
    } while (0)
