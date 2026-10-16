#pragma once
#include <cstdint>

namespace fvr::interaction {
enum class InputMenuOwner : std::uint32_t {Unknown,Gameplay,Pending,Menu};
// Source menu expiry uses the same process-independent monotonic nanoseconds
// as native action admission. A newer controller packet never renews it.
struct InputMenuOwnership {
    InputMenuOwner owner=InputMenuOwner::Unknown;
    std::uint64_t sequence=0,epoch=0;
    std::int64_t deadlineNs=0;
    bool operator==(const InputMenuOwnership&)const=default;
};
inline bool ValidInputMenuOwnership(const InputMenuOwnership& m)noexcept {
    if(m.owner==InputMenuOwner::Unknown)return !m.sequence&&!m.epoch&&!m.deadlineNs;
    return unsigned(m.owner)<=unsigned(InputMenuOwner::Menu)&&m.sequence&&m.epoch&&m.deadlineNs>0;
}
inline bool GameplayOwnsInput(const InputMenuOwnership& m,std::int64_t nowNs)noexcept {
    return ValidInputMenuOwnership(m)&&m.owner==InputMenuOwner::Gameplay&&nowNs>0&&
        m.deadlineNs>nowNs&&m.deadlineNs-nowNs<=100000000;
}
}
