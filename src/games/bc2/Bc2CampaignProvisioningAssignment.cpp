#include "Bc2CampaignProvisioningPolicy.h"
namespace fvr::bc2::provisioning_candidate {
bool InvokeOriginalAssignment(void*,std::uint32_t actor,std::uint32_t data,std::uint32_t slot,std::uint32_t& item)noexcept {
    item=0;
#if defined(_MSC_VER) && defined(_M_IX86)
    if(actor<0x10000||data<0x10000||slot>1)return false;
    using Assign=std::uint32_t(__thiscall*)(void*,std::uint32_t,std::uint32_t,bool,bool);
    // Original Flynn crate and normal spawn call this same native routine.
    // It destroys/recreates the item and schedules native replication.
    const auto call=reinterpret_cast<Assign>(0x6bcf80);
    __try {item=call(reinterpret_cast<void*>(actor),data,slot,true,true);return item>=0x10000;}
    __except(1){return false;} // Outcome remains indeterminate, never auto-retry.
#else
    (void)actor;(void)data;(void)slot;return false;
#endif
}
}
