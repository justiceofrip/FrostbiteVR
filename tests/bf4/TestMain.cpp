#include "TestHarness.h"

#include <cstring>

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    for (const auto& c : fvrtest::Registry()) {
        if (filter && !std::strstr(c.name, filter)) continue;
        const int before = fvrtest::Failures();
        c.fn();
        ++run;
        std::printf("%s %s\n", fvrtest::Failures() == before ? "PASS" : "FAIL", c.name);
    }
    std::printf("%d cases, %d checks, %d failures\n", run, fvrtest::Checks(), fvrtest::Failures());
    return fvrtest::Failures() == 0 ? 0 : 1;
}
