#pragma once
#include <array>
#include <cstdint>

namespace fvr::probe {
// Diagnostic only: one stationary input window, with neutral input before and
// after. This does not authorize native fire; the separate native mode does.
struct BoatFireProbe {
    static constexpr std::uint64_t BeginMs=2000, EndMs=2080, DurationMs=6500;
    static constexpr bool Trigger(std::uint64_t elapsedMs) noexcept {
        return elapsedMs>=BeginMs&&elapsedMs<EndMs;
    }
    bool Capture(std::uint64_t elapsedMs) noexcept {
        constexpr std::array<std::uint64_t,8> targets{0,1800,2000,2040,2100,2200,2600,6000};
        bool due=false;
        for(unsigned i=0;i<targets.size();++i)if(elapsedMs>=targets[i]&&!(captured_&(1u<<i))){captured_|=1u<<i;due=true;}
        return due;
    }
private:
    unsigned captured_{};
};
}
