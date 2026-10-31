#pragma once
#include <cstdint>
#include <optional>
namespace fvr::bc2 {
struct NativeInputEpochs {
    std::uint64_t equipment=0,input=0,movement=0;
};
// The caller has resolved and compared the complete native owner. A scheduling
// gap rearms semantic buttons; it is not replacement of the selected weapon.
// Only a resolved actor/control-route comparison may preserve movement while
// equipment changes. A coincident scheduling gap still requires neutral input.
inline std::optional<NativeInputEpochs> AdvanceNativeInputEpochs(
    NativeInputEpochs current,bool ownerChanged,bool inputPause,bool movementOwnerChanged)noexcept {
    if(bool(current.equipment)!=bool(current.input)||(current.movement&&!current.equipment))return {};
    if(ownerChanged){
        if(current.equipment==UINT64_MAX||current.input==UINT64_MAX)return {};
        ++current.equipment;++current.input;
    }else if(inputPause){
        if(!current.equipment||!current.input||current.input==UINT64_MAX)return {};
        ++current.input;
    }
    if(movementOwnerChanged||inputPause||(!current.movement&&current.equipment)){
        if(!current.equipment||!current.input||current.movement==UINT64_MAX)return {};
        ++current.movement;
    }
    return current;
}
// Existing callers have no narrower movement ownership proof.
inline std::optional<NativeInputEpochs> AdvanceNativeInputEpochs(
    NativeInputEpochs current,bool ownerChanged,bool inputPause)noexcept {
    return AdvanceNativeInputEpochs(current,ownerChanged,inputPause,ownerChanged);
}
}
