#pragma once
// Minimal self-registering test harness: every tests/bf4/*Tests.cpp links into ONE
// executable (Bf4CoreTests) to keep the build tree small. Run a subset with a name filter:
//   Bf4CoreTests Holster
#include <cmath>
#include <cstdio>
#include <vector>

namespace fvrtest {
struct Case {
    const char* name;
    void (*fn)();
};
inline std::vector<Case>& Registry() {
    static std::vector<Case> cases;
    return cases;
}
struct Registrar {
    Registrar(const char* name, void (*fn)()) { Registry().push_back({name, fn}); }
};
inline int& Failures() { static int n = 0; return n; }
inline int& Checks() { static int n = 0; return n; }
inline bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
} // namespace fvrtest

#define FVR_TEST_CAT2(a, b) a##b
#define FVR_TEST_CAT(a, b) FVR_TEST_CAT2(a, b)
#define TEST_CASE(name)                                                                       \
    static void name();                                                                       \
    static const fvrtest::Registrar FVR_TEST_CAT(fvr_registrar_, name)(#name, &name);         \
    static void name()
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        ++fvrtest::Checks();                                                                  \
        if (!(cond)) {                                                                        \
            ++fvrtest::Failures();                                                            \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                     \
        }                                                                                     \
    } while (0)
