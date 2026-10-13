#pragma once
#include <cstdint>
#include <optional>
namespace fvr::bc2 {
struct NativeInputEpochs {
    std::uint64_t equipment=0,input=0;
};
// The caller has resolved and compared the complete native owner. A scheduling
// gap rearms semantic buttons; it is not replacement of the selected weapon.
inline std::optional<NativeInputEpochs> AdvanceNativeInputEpochs(
    NativeInputEpochs current,bool ownerChanged,bool inputPause)noexcept {
    if(ownerChanged){
        if(current.equipment==UINT64_MAX||current.input==UINT64_MAX)return {};
        ++current.equipment;++current.input;
    }else if(inputPause){
        if(!current.equipment||!current.input||current.input==UINT64_MAX)return {};
        ++current.input;
    }
    return current;
}
}
