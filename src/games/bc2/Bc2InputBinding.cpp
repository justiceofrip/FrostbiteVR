#include "Bc2InputBinding.h"
#include <bit>
#include <algorithm>
#include <cmath>
#include <cstring>
namespace fvr::bc2 {
namespace {
std::uint32_t Get(std::span<std::byte> b,std::size_t at)noexcept {std::uint32_t out=0;std::memcpy(&out,b.data()+at,4);return out;}
void Put(std::span<std::byte> b,std::size_t at,std::uint32_t value)noexcept {std::memcpy(b.data()+at,&value,4);}
}
bool InputOverride::Apply(std::span<std::byte> cache,const interaction::ActionOutput& action,std::optional<bool> diagnosticGrenade,std::optional<bool> diagnosticCycle,std::optional<EntryAction> verifiedWeaponMode,bool verifiedContextUseVehicleAlias) noexcept {
    if(active_||cache.size()<InputBytes||!action.active||!action.owner||
       !std::isfinite(action.forward)||!std::isfinite(action.strafe)||
       std::abs(action.forward)>1||std::abs(action.strafe)>1)return false;
    // Mode commands are adapter-resolved native map actions. Do not accept
    // arbitrary entry bits through this narrow path.
    if(verifiedWeaponMode&&*verifiedWeaponMode!=EntryAction::GrenadeLauncher&&*verifiedWeaponMode!=EntryAction::DynamicGadget2)return false;
    // Axis signedness verified by native float setter mask 0x6f3.
    const float values[]={action.forward,action.strafe,(action.held&interaction::Fire)?1.f:0.f};
    const EntryAction axes[]={EntryAction::Throttle,EntryAction::Strafe,EntryAction::Fire};
    for(unsigned i=0;i<3;++i){auto& e=edits_[i];e.offset=8+4*unsigned(axes[i]);e.before=Get(cache,e.offset);
        if(!std::isfinite(std::bit_cast<float>(e.before)))return false;e.written=std::bit_cast<std::uint32_t>(values[i]);}
    editCount_=3;
    {auto& e=edits_[editCount_++];e.offset=8+4*unsigned(EntryAction::SwitchPrimaryWeapon);e.before=Get(cache,e.offset);
        if(!std::isfinite(std::bit_cast<float>(e.before)))return false;
        const bool next=(action.pressed&interaction::NextWeapon)!=0,previous=(action.pressed&interaction::PreviousWeapon)!=0;
        const float cycle=diagnosticCycle.value_or(false)?1.f:(next==previous?0.f:(next?1.f:-1.f));
        e.written=std::bit_cast<std::uint32_t>(cycle);}
    masks_[0]=masks_[1]=written_[0]=written_[1]=0;
    // Match native keyboard semantics: ChangePose remains set while held.
    for(auto pair:{std::pair{interaction::AlternateFire,EntryAction::Zoom},
        std::pair{interaction::Jump,EntryAction::Jump},std::pair{interaction::Use,EntryAction::Interact},
        std::pair{interaction::Reload,EntryAction::Reload},std::pair{interaction::Sprint,EntryAction::Sprint},
        std::pair{interaction::Crouch,EntryAction::ChangePose}}){
        const auto bit=unsigned(pair.second),word=bit/32,mask=1u<<(bit%32);masks_[word]|=mask;
        if(action.held&pair.first)written_[word]|=mask;
    }
    // Native Gather writes both boolean EntryActions for this shared concept.
    // Mirror the same already-filtered Use level; this is not another edge or
    // direct pickup/vehicle call. Native restrictions still run afterward.
    if(verifiedContextUseVehicleAlias){const auto mask=1u<<unsigned(EntryAction::ChangeVehicle);masks_[0]|=mask;if(action.held&interaction::Use)written_[0]|=mask;}
    if(diagnosticGrenade){const auto bit=unsigned(EntryAction::ThrowGrenade),word=bit/32,mask=1u<<(bit%32);masks_[word]|=mask;if(*diagnosticGrenade)written_[word]|=mask;}
    if(verifiedWeaponMode){const auto bit=unsigned(*verifiedWeaponMode),word=bit/32,mask=1u<<(bit%32);masks_[word]|=mask;written_[word]|=mask;}
    // Snap turn uses the separately verified aiming binding. Menu stays unbound.
    for(unsigned i=0;i<2;++i){before_[i]=Get(cache,0x98+4*i);written_[i]|=before_[i]&~masks_[i];}
    cache_=cache;for(const auto& e:std::span(edits_).first(editCount_))Put(cache_,e.offset,e.written);
    for(unsigned i=0;i<2;++i)if(masks_[i])Put(cache_,0x98+4*i,written_[i]);active_=true;return true;
}
bool InputOverride::ApplyVehicleExit(std::span<std::byte> cache,const interaction::ActionOutput& action) noexcept {
    if(active_||cache.size()<InputBytes||!action.active||!action.owner)return false;
    editCount_=0;masks_[0]=1u<<unsigned(EntryAction::ChangeVehicle);masks_[1]=0;
    before_[0]=Get(cache,0x98);before_[1]=Get(cache,0x9c);
    written_[0]=(before_[0]&~masks_[0])|((action.held&interaction::Use)?masks_[0]:0);written_[1]=before_[1];
    cache_=cache;Put(cache_,0x98,written_[0]);active_=true;return true;
}
bool InputOverride::Restore() noexcept {
    if(!active_)return true;bool exact=true;
    for(const auto& e:std::span(edits_).first(editCount_)){if(Get(cache_,e.offset)==e.written)Put(cache_,e.offset,e.before);else exact=false;}
    for(unsigned i=0;i<2;++i){const auto at=0x98+4*i,current=Get(cache_,at);
        // Restore only individually unchanged bits that this scope owns. A
        // native sprint/fire restriction must not be undone by our cleanup.
        const auto unchanged=~(current^written_[i])&masks_[i];
        if(unchanged!=masks_[i])exact=false;
        Put(cache_,at,(current&~unchanged)|(before_[i]&unchanged));}
    active_=false;cache_={};return exact;
}
}

namespace fvr::bc2 {
float RoomscaleNativeAxis(float speed) noexcept {
    if(!std::isfinite(speed)||std::abs(speed)<.003f)return 0;
    return std::copysign(std::clamp(std::abs(speed)/6.f,.105f,.8f/6.f),speed);
}
}
