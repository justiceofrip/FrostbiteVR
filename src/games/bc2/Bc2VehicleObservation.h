#pragma once
#include "Bc2CampaignVehicleProfile.h"
#include "Bc2NavigationObservation.h"
#include "Bc2VehicleSeatCycle.h"

namespace fvr::bc2 {
enum class VehicleObservationReason : unsigned {
    Available,Disabled,Stopped,GatherPending,Busy,Changed,OwnerUnavailable,RouteChanged,InputPause,
    StaleInput,InvalidInput,FocusLoss,HeadUntracked,HandsUntracked,NotSeated,NativeInactive,MenuUnavailable,
    SourceClockChanged,MenuClockChanged,RoutesUnavailable,ProfileUnavailable,ConsumersUnavailable
};
inline const char* VehicleObservationReasonName(VehicleObservationReason reason)noexcept {
    constexpr const char* names[]={"available","disabled","stopped","gather_pending","observation_busy",
        "observation_changed","owner_unavailable","route_changed","input_pause","stale_input","invalid_input",
        "focus_loss","head_untracked","hands_untracked","not_seated","native_inactive","menu_unavailable",
        "source_clock_changed","menu_clock_changed","routes_unavailable","profile_unavailable","consumers_unavailable"};
    return names[unsigned(reason)];
}
struct VehicleObservationOwner {
    std::uint32_t manager=0,player=0,soldier=0,weak=0,controlled=0,entry=0,router=0,cache=0;
    std::uint64_t actorGeneration=0,seatGeneration=0,inputEpoch=0,movementEpoch=0;
    VehicleSeatIdentity Seat()const noexcept {
        return {(std::uint64_t(weak)<<32)|soldier,actorGeneration,seatGeneration,controlled,entry,router,cache};
    }
    bool operator==(const VehicleObservationOwner&)const=default;
};
struct VehicleObservationAxis {
    bool mapped=false;unsigned action=0,conceptId=0,consumer=0;
    bool permitted=false; // Native float permission, independent of neutral arming.
};
struct VehicleObservation {
    std::uint64_t serial=0,gatherSequence=0,routeFingerprint=0;
    std::int64_t sampledNs=0,inputObservedNs=0,inputDeadlineNs=0,validUntilNs=0;
    VehicleObservationOwner owner{};
    bool seated=false,allowedRead=false,nativePlaying=false,routesVerified=false,profileVerified=false,consumersVerified=false;
    unsigned allowedMask=0,slot=0;VehicleSeatRole role=VehicleSeatRole::Unclassified;
    // Fire deliberately has no observation capability or semantic axis.
    std::array<VehicleObservationAxis,4> axes{};
    bool headAim=false;unsigned driver=0,camera=0,weaponConsumerCount=0;
    interaction::InputFrame input{};interaction::VehicleControlOutput controls{};
    bool exitRoute=false,seatCycleRoute=false,seatCycleCodeVerified=false;
    bool permissionRead=false,conceptPermissionVerified=false,seatBindingVerified=false;
    unsigned playerPermissionMask=0;seatCycle::Binding seatBinding{};
    VehicleObservationReason reason=VehicleObservationReason::Disabled;
    bool Available()const noexcept{return reason==VehicleObservationReason::Available;}
};
inline bool VehicleObservationRoute(const VehicleRouteSnapshot& seat,unsigned action,unsigned conceptId)noexcept {
    return seat.count<=seat.routes.size()&&std::find(seat.routes.begin(),seat.routes.begin()+seat.count,
        VehicleRoute{action,conceptId})!=seat.routes.begin()+seat.count;
}
// Production assembly joins typed, already verified native readers. It never
// classifies a seat by its route hash, asset name, or desired controller action.
inline VehicleObservation AssembleVehicleObservation(VehicleObservation value,const VehicleRouteResult& routes,
    const VehicleSeatProfile* profile,const CampaignVehicleSnapshot* consumers,bool headAim)noexcept {
    using R=VehicleObservationReason;
    value.reason=R::RoutesUnavailable;
    if(routes.status!=VehicleRouteStatus::Okay)return value;
    const auto& seat=routes.snapshot;
    if(seat.identity!=value.owner.Seat()||seat.player!=value.owner.player||seat.weak!=value.owner.weak||
       !seat.fingerprint||!seat.count||seat.count>seat.routes.size()){value.reason=R::OwnerUnavailable;return value;}
    value.routesVerified=true;value.routeFingerprint=seat.fingerprint;value.slot=seat.slot;
    value.exitRoute=VehicleObservationRoute(seat,16,36);value.seatCycleRoute=VehicleObservationRoute(seat,17,42);
    value.reason=R::ProfileUnavailable;
    if(!profile||profile->role==VehicleSeatRole::Unclassified||profile->role>VehicleSeatRole::DriverGunner||
       profile->routeFingerprint!=seat.fingerprint)return value;
    value.profileVerified=true;value.role=profile->role;value.headAim=headAim;
    value.reason=R::ConsumersUnavailable;
    if(!consumers||consumers->owner!=seat.identity||consumers->profile.routeFingerprint!=seat.fingerprint)return value;
    value.driver=consumers->driver;value.camera=consumers->camera;value.weaponConsumerCount=consumers->weapons;
    constexpr unsigned concepts[]={0,1,8,7};
    for(unsigned i=0;i<value.axes.size();++i){
        if(!profile->axes[i])continue;
        if(profile->axes[i]!=consumers->profile.axes[i]||!VehicleObservationRoute(seat,unsigned(*profile->axes[i]),concepts[i])||
           (i<2?!consumers->driver:!consumers->camera||!consumers->weapons))return value;
        const auto action=unsigned(*profile->axes[i]);if(action>10)return value;
        value.axes[i]={true,action,concepts[i],i<2?consumers->driver:consumers->camera,
            value.allowedRead&&(value.allowedMask&(1u<<action))!=0};
    }
    const bool drives=value.role==VehicleSeatRole::Driver||value.role==VehicleSeatRole::DriverGunner;
    const bool aims=value.role==VehicleSeatRole::Gunner||value.role==VehicleSeatRole::DriverGunner;
    if((drives&&(!value.axes[0].mapped||!value.axes[1].mapped))||
       (aims&&(!value.axes[2].mapped||!value.axes[3].mapped)))return value;
    value.consumersVerified=true;value.reason=R::Available;return value;
}
inline void ObserveVehicleSeatBinding(VehicleObservation& value,const VehicleRouteMemory& memory,
    const VehicleRouteSnapshot& seat,bool verified)noexcept {
    value.seatCycleCodeVerified=verified;
    value.permissionRead=value.conceptPermissionVerified=value.seatBindingVerified=false;
    value.playerPermissionMask=0;value.seatBinding={};
    if(!value.Available()||!verified||seat.identity!=value.owner.Seat()||seat.player!=value.owner.player||
       seat.weak!=value.owner.weak||seat.fingerprint!=value.routeFingerprint)return;
    seatCycle::BindingObservation firstProof,secondProof;
    const auto first=seatCycle::ReadBinding(memory,seat,true,nullptr,&firstProof);
    const auto second=seatCycle::ReadBinding(memory,seat,true,nullptr,&secondProof);
    constexpr unsigned permissionIndex=unsigned(seatCycle::BindingField::Permission),permissionBit=1u<<permissionIndex;
    value.permissionRead=(firstProof.successMask&permissionBit)&&(secondProof.successMask&permissionBit)&&
        firstProof.values[permissionIndex]==secondProof.values[permissionIndex];
    if(value.permissionRead)value.playerPermissionMask=secondProof.values[permissionIndex];
    value.seatBindingVerified=value.permissionRead&&first&&second&&*first==*second&&second->CanRequest();
    value.conceptPermissionVerified=value.seatBindingVerified;
    if(value.seatBindingVerified)value.seatBinding=*second;
}
inline VehicleObservationReason VehicleObservationRejection(const VehicleObservation& v,std::int64_t now)noexcept {
    using R=VehicleObservationReason;const auto& o=v.owner;const auto& input=v.input;
    if(!o.manager||!o.player||!o.soldier||!o.weak||!o.controlled||!o.entry||!o.router||!o.cache||
       !o.actorGeneration||!o.seatGeneration||!o.inputEpoch||!o.movementEpoch||!v.gatherSequence)return R::OwnerUnavailable;
    if(!v.seated||o.controlled==o.soldier)return R::NotSeated;
    if(!v.routesVerified)return v.reason==R::OwnerUnavailable?R::OwnerUnavailable:R::RoutesUnavailable;
    if(!v.profileVerified||v.role==VehicleSeatRole::Unclassified||v.role>VehicleSeatRole::DriverGunner)return R::ProfileUnavailable;
    if(!v.consumersVerified)return R::ConsumersUnavailable;
    if(!v.allowedRead||!v.nativePlaying)return R::NativeInactive;
    if(now<=0||v.inputObservedNs<=0||v.inputObservedNs>v.sampledNs||v.sampledNs>now||v.inputDeadlineNs<=now||
       v.inputDeadlineNs<=v.inputObservedNs||v.inputDeadlineNs-v.inputObservedNs>100000000)return R::StaleInput;
    if(!input.focused)return R::FocusLoss;
    if(!input.headValid)return R::HeadUntracked;
    for(const auto& hand:input.hands)if(!hand.gripTracked||!hand.aimTracked)return R::HandsUntracked;
    if(!interaction::ValidInput(input))return R::InvalidInput;
    if(!interaction::GameplayOwnsInput(input.menu,now)||v.validUntilNs!=(std::min)(v.inputDeadlineNs,input.menu.deadlineNs))return R::MenuUnavailable;
    return R::Available;
}
// Same nonblocking value mailbox contract as navigation. Revocation never
// waits on the copy gate and a reentrant Gather cannot publish its predecessor.
class VehicleObservationPublication {
public:
    bool Enable()noexcept {bool expected=false;if(!enabled_.compare_exchange_strong(expected,true,std::memory_order_acq_rel))return false;
        Invalidate(VehicleObservationReason::GatherPending);return true;}
    bool Enabled()const noexcept{return enabled_.load(std::memory_order_acquire);}
    std::uint64_t Invalidate(VehicleObservationReason reason)noexcept {
        if(!Enabled())return 0;reason_.store(reason,std::memory_order_release);return serial_.fetch_add(1,std::memory_order_acq_rel)+1;
    }
    std::uint64_t BeginGather()noexcept{return Invalidate(VehicleObservationReason::GatherPending);}
    void InvalidatePlayer(std::uint32_t player,VehicleObservationReason reason)noexcept {
        if(Enabled()&&player&&player_.load(std::memory_order_acquire)==player)Invalidate(reason);
    }
    void Stop()noexcept {enabled_.store(false,std::memory_order_release);reason_.store(VehicleObservationReason::Stopped,std::memory_order_release);
        serial_.fetch_add(1,std::memory_order_acq_rel);}
    bool Publish(std::uint64_t serial,VehicleObservation value,const VehicleObservationOwner& current,bool sourceCurrent,std::int64_t now)noexcept {
        if(!serial||!Enabled()||serial!=serial_.load(std::memory_order_acquire))return false;
        value.serial=serial;value.sampledNs=now;value.validUntilNs=(std::min)(value.inputDeadlineNs,value.input.menu.deadlineNs);
        auto reason=value.owner==current&&sourceCurrent?VehicleObservationRejection(value,now):VehicleObservationReason::OwnerUnavailable;
        if(reason!=VehicleObservationReason::Available){Invalidate(reason);return false;}
        if(gate_.test_and_set(std::memory_order_acquire)){Invalidate(VehicleObservationReason::Busy);return false;}
        if(!Enabled()||serial_.load(std::memory_order_acquire)!=serial){gate_.clear(std::memory_order_release);return false;}
        if(latest_.input.generation==value.input.generation&&latest_.input.spaceGeneration==value.input.spaceGeneration&&
           (latest_.inputObservedNs!=value.inputObservedNs||latest_.inputDeadlineNs!=value.inputDeadlineNs))reason=VehicleObservationReason::SourceClockChanged;
        if(latest_.input.menu.sequence&&latest_.input.menu.sequence==value.input.menu.sequence&&latest_.input.menu.epoch==value.input.menu.epoch&&
           latest_.input.menu.deadlineNs!=value.input.menu.deadlineNs)reason=VehicleObservationReason::MenuClockChanged;
        if(reason==VehicleObservationReason::Available){value.reason=reason;latest_=value;player_.store(value.owner.player,std::memory_order_release);}
        gate_.clear(std::memory_order_release);
        if(reason!=VehicleObservationReason::Available){Invalidate(reason);return false;}
        return Enabled()&&serial_.load(std::memory_order_acquire)==serial;
    }
    VehicleObservation Read(std::int64_t now)const noexcept {
        VehicleObservation value;if(!Enabled()){value.reason=reason_.load(std::memory_order_acquire);return value;}
        const auto serial=serial_.load(std::memory_order_acquire);
        if(gate_.test_and_set(std::memory_order_acquire)){value.reason=VehicleObservationReason::Busy;return value;}
        value=latest_;gate_.clear(std::memory_order_release);
        if(value.serial!=serial||serial!=serial_.load(std::memory_order_acquire)||!Enabled())value.reason=reason_.load(std::memory_order_acquire);
        else value.reason=VehicleObservationRejection(value,now);return value;
    }
    VehicleObservationReason Current(const VehicleObservation& value,std::int64_t now)const noexcept {
        if(!value.Available())return value.reason;if(!Enabled())return reason_.load(std::memory_order_acquire);
        if(value.serial!=serial_.load(std::memory_order_acquire))return VehicleObservationReason::Changed;
        return VehicleObservationRejection(value,now);
    }
private:
    friend struct VehicleObservationTestAccess;
    std::atomic<bool> enabled_{false};std::atomic<std::uint64_t> serial_{0};
    std::atomic<VehicleObservationReason> reason_{VehicleObservationReason::Disabled};
    std::atomic<std::uint32_t> player_{0};mutable std::atomic_flag gate_=ATOMIC_FLAG_INIT;VehicleObservation latest_{};
};
inline void WriteVehicleObservation(std::ostream& out,const VehicleObservation& v){
    const auto flag=[&](bool b){out<<(b?"true":"false");};
    out<<"{\"observationOnly\":true,\"available\":";flag(v.Available());out<<",\"reason\":";
    if(v.Available())out<<"null";else out<<'"'<<VehicleObservationReasonName(v.reason)<<'"';
    out<<",\"gatherSequence\":"<<v.gatherSequence<<",\"sampledNs\":"<<v.sampledNs<<",\"inputObservedNs\":"<<v.inputObservedNs
        <<",\"inputDeadlineNs\":"<<v.inputDeadlineNs<<",\"validUntilNs\":"<<v.validUntilNs;
    const auto& o=v.owner;
    out<<",\"owner\":{\"manager\":"<<o.manager<<",\"player\":"<<o.player<<",\"soldier\":"<<o.soldier<<",\"weak\":"<<o.weak
        <<",\"controlled\":"<<o.controlled<<",\"entry\":"<<o.entry<<",\"router\":"<<o.router<<",\"cache\":"<<o.cache
        <<",\"actorGeneration\":"<<o.actorGeneration<<",\"seatGeneration\":"<<o.seatGeneration
        <<",\"inputEpoch\":"<<o.inputEpoch<<",\"movementEpoch\":"<<o.movementEpoch<<'}';
    out<<",\"routeFingerprint\":"<<v.routeFingerprint<<",\"slot\":"<<v.slot<<",\"allowedRead\":";flag(v.allowedRead);
    out<<",\"allowedMask\":"<<v.allowedMask<<",\"nativePlaying\":";flag(v.nativePlaying);
    out<<",\"routesVerified\":";flag(v.routesVerified);out<<",\"profileVerified\":";flag(v.profileVerified);
    out<<",\"consumersVerified\":";flag(v.consumersVerified);
    // Retained owner labels after revocation are diagnostic only; no stale
    // controls, permissions or capabilities survive as an actionable section.
    if(!v.Available()){out<<",\"input\":null,\"capabilities\":null,\"neutralRearm\":null}";return;}
    constexpr const char* roles[]={"unclassified","driver","gunner","driver_gunner"};
    out<<",\"role\":\""<<roles[unsigned(v.role)]<<"\",\"consumers\":{\"driver\":"<<v.driver<<",\"camera\":"<<v.camera
        <<",\"weaponCount\":"<<v.weaponConsumerCount<<"},\"neutralRearm\":{\"controlsActive\":";flag(v.controls.active);
    out<<",\"armedLeft\":";flag(v.controls.armedLeft);out<<",\"armedRight\":";flag(v.controls.armedRight);
    out<<",\"seatButtonKnown\":false,\"seatButtonReason\":\"button_rearm_unavailable\"},\"consumedControls\":{\"throttle\":"<<v.controls.axes.throttle
        <<",\"steer\":"<<v.controls.axes.steer<<",\"lookYaw\":"<<v.controls.axes.lookYaw<<",\"lookPitch\":"<<v.controls.axes.lookPitch<<",\"exitHeld\":";flag(v.controls.axes.exit);
    out<<"},\"capabilities\":{\"fire\":false,\"axes\":[";
    constexpr const char* semantics[]={"throttle","steer","lookYaw","lookPitch"};
    constexpr const char* sources[]={"left_stick_y","left_stick_x","right_stick_x","right_stick_y"};
    for(unsigned i=0;i<v.axes.size();++i){if(i)out<<',';const auto& a=v.axes[i];
        out<<"{\"semantic\":\""<<semantics[i]<<"\",\"available\":";flag(a.mapped&&a.permitted);
        out<<",\"reason\":";if(a.mapped&&a.permitted)out<<"null";else out<<'"'<<(a.mapped?"native_permission_denied":"consumer_unavailable")<<'"';
        out<<",\"mapping\":";if(!a.mapped)out<<"null";else out<<"{\"action\":"<<a.action<<",\"concept\":"<<a.conceptId<<",\"consumer\":"<<a.consumer
            <<",\"source\":\""<<(v.headAim&&i>=2?(i==2?"head_aim_yaw":"head_aim_pitch"):sources[i])<<"\"}";out<<'}';}
    const bool exitPermission=v.permissionRead&&(v.playerPermissionMask&(1u<<5));
    const bool seatPermission=v.permissionRead&&(v.playerPermissionMask&(1u<<6))&&v.conceptPermissionVerified;
    const bool seatFlagsClear=!(v.allowedMask&(seatCycle::InputBit|seatCycle::ConflictingBits));
    out<<"],\"exit\":{\"available\":false,\"reason\":\"exit_binding_unavailable\",\"action\":16,\"concept\":36,\"routeVerified\":";flag(v.exitRoute);
    out<<",\"permissionRead\":";flag(v.permissionRead);out<<",\"permitted\":";flag(exitPermission);
    // No existing neutral reader establishes exit's device/concept suppression.
    out<<",\"bindingVerified\":false,\"bindingReason\":\"exit_binding_unavailable\"},\"seatCycle\":{\"available\":";
    flag(v.seatCycleCodeVerified&&v.seatCycleRoute&&v.seatBindingVerified&&seatPermission&&seatFlagsClear);
    out<<",\"action\":17,\"concept\":42,\"codeVerified\":";flag(v.seatCycleCodeVerified);out<<",\"routeVerified\":";flag(v.seatCycleRoute);
    out<<",\"permissionRead\":";flag(v.permissionRead);out<<",\"permitted\":";flag(seatPermission);
    out<<",\"nativeFlagsClear\":";flag(seatFlagsClear);out<<",\"requestReady\":false,\"requestReason\":\"button_rearm_unavailable\"";
    out<<",\"bindingVerified\":";flag(v.seatBindingVerified);out<<",\"bindingReason\":";if(v.seatBindingVerified)out<<"null";else out<<"\"current_binding_unavailable\"";
    out<<",\"unboundConcept\":";flag(v.seatBindingVerified&&v.seatBinding.unboundConcept);
    out<<",\"binding\":";if(!v.seatBindingVerified)out<<"null";else {const auto& b=v.seatBinding;
        out<<"{\"player\":"<<b.player<<",\"router\":"<<b.router<<",\"device\":"<<b.device<<",\"map\":"<<b.map<<",\"table\":"<<b.table
            <<",\"tableEnd\":"<<b.tableEnd<<",\"actions\":"<<b.actions<<",\"actionsEnd\":"<<b.actionsEnd<<",\"row\":"<<b.row<<",\"key\":"<<b.key<<",\"keyboard\":"<<b.keyboard<<'}';}
    out<<"}},\"playerPermissionMask\":"<<v.playerPermissionMask<<",\"input\":";
    const auto pose=[&](const math::Pose& p){out<<"{\"position\":["<<p.position.x<<','<<p.position.y<<','<<p.position.z
        <<"],\"orientation\":["<<p.orientation.x<<','<<p.orientation.y<<','<<p.orientation.z<<','<<p.orientation.w<<"]}";};
    const auto& input=v.input;
    out<<"{\"generation\":"<<input.generation<<",\"spaceGeneration\":"<<input.spaceGeneration<<",\"predictedNs\":"<<input.predictedNs<<",\"focused\":";flag(input.focused);
    out<<",\"headValid\":";flag(input.headValid);out<<",\"floorRelative\":";flag(input.floorRelative);
    out<<",\"worldUnitsPerMeter\":"<<input.worldUnitsPerMeter<<",\"trackingBasisGeneration\":"<<input.trackingBasisGeneration<<",\"referenceHead\":";pose(input.referenceHead);
    out<<",\"head\":";pose(input.head);out<<",\"hands\":[";
    for(unsigned i=0;i<2;++i){if(i)out<<',';const auto& h=input.hands[i];out<<"{\"grip\":";pose(h.grip);out<<",\"aim\":";pose(h.aim);
        out<<",\"gripTracked\":";flag(h.gripTracked);out<<",\"aimTracked\":";flag(h.aimTracked);
        out<<",\"active\":"<<h.active<<",\"held\":"<<h.held<<",\"touchActive\":"<<h.touchActive<<",\"touched\":"<<h.touched
            <<",\"stickX\":"<<h.stickX<<",\"stickY\":"<<h.stickY<<",\"trigger\":"<<h.trigger<<",\"squeeze\":"<<h.squeeze<<'}';}
    out<<"],\"menu\":{\"owner\":"<<unsigned(input.menu.owner)<<",\"sequence\":"<<input.menu.sequence<<",\"epoch\":"<<input.menu.epoch<<",\"deadlineNs\":"<<input.menu.deadlineNs<<"}}}";
}
inline std::string VehicleObservationJson(const VehicleObservation& value){
    std::ostringstream out;out.imbue(std::locale::classic());out.precision(9);WriteVehicleObservation(out,value);return out.str();
}
// One final bookend for both independent copies. Every payload branch retains
// both observations, including a missing firearm and the bounded last fallback.
template<class NavigationCurrent,class VehicleCurrent>
std::string CompleteVehicleNavigationSnapshot(std::string payload,const std::string& navigationSection,NavigationObservation navigation,
    const std::string& vehicleSection,VehicleObservation vehicle,NavigationCurrent navigationCurrent,VehicleCurrent vehicleCurrent,
    std::uint64_t nonce,std::int64_t observed,const char* invalidReason,std::size_t limit){
    const auto refresh=[&](auto& value,auto current,const std::string& section,const char* key,auto json){
        const auto reason=current(value);if(reason==value.reason)return;value.reason=reason;
        const auto at=payload.find(section);if(at!=std::string::npos)payload.replace(at,section.size(),std::string(",\"")+key+"\":"+json(value));
    };
    refresh(navigation,navigationCurrent,navigationSection,"navigationObservation",NavigationObservationJson);
    refresh(vehicle,vehicleCurrent,vehicleSection,"vehicleObservation",VehicleObservationJson);
    if(payload.size()<=limit)return payload;
    std::ostringstream fallback;fallback.imbue(std::locale::classic());
    fallback<<"{\"schema\":\"fvr.bc2.live-mechanism-snapshot.v1\",\"nonce\":\""<<nonce<<"\",\"valid\":false,\"invalid_reason\":\""
        <<invalidReason<<"\",\"observed_ns\":"<<observed<<",\"navigationObservation\":"<<NavigationObservationJson(navigation)
        <<",\"vehicleObservation\":"<<VehicleObservationJson(vehicle)<<",\"diagnosticsOmitted\":\"payload_limit\"}\n";
    return fallback.str();
}
}
