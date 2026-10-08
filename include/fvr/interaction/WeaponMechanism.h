#pragma once
#include "fvr/interaction/ManualReload.h"
#include "fvr/interaction/WeaponCycle.h"

namespace fvr::interaction {
// Declarative adapter contract only: selection grants no native binding,
// ammunition, readiness, chamber observation or fire permission.
// No production runtime consumer is wired to this contract yet.
enum class WeaponFeed:std::uint8_t {DetachableMagazine,InternalTube,Belt,Clip,Single};
enum class WeaponAction:std::uint8_t {Automatic,Pump,Bolt,ChargingHandle,Slide};
enum class ChamberKnowledge:std::uint8_t {Unknown,Empty,Occupied};
enum class WeaponMechanismIntent:std::uint8_t {TacticalFeed,EmptyFeed,AfterShot};
enum class WeaponCyclePurpose:std::uint8_t {None,AfterFeed,AfterShot};
enum class WeaponMechanismStatus:std::uint8_t {InvalidDescriptor,UnknownChamber,ContradictoryChamber,Plan};
struct WeaponMechanismDescriptor {
    std::uint64_t id=0,revision=0;
    WeaponFeed feed=WeaponFeed::DetachableMagazine;
    // Independent on purpose: a tube-fed pump can use a charging handle to
    // chamber after feeding and a pump stroke after each shot.
    WeaponAction afterEmptyFeed=WeaponAction::Automatic,afterShot=WeaponAction::Automatic;
    bool tacticalRetainsChamber=false;
    // Authored semantic feed operations, not an inferred native sequence.
    // CycleAction is excluded: action-cycle completion has its own receipt.
    ManualReloadConfig feedPlan{};
};
struct WeaponMechanismPlan {
    ManualReloadConfig feed{};
    WeaponCyclePurpose purpose=WeaponCyclePurpose::None;
    WeaponAction action=WeaponAction::Automatic;
    std::optional<WeaponCycleFamily> gestureFamily{};
};
struct WeaponMechanismSelection {
    WeaponMechanismStatus status=WeaponMechanismStatus::InvalidDescriptor;
    std::optional<WeaponMechanismPlan> plan{};
};
inline bool ValidWeaponMechanismDescriptor(const WeaponMechanismDescriptor& d)noexcept {
    if(!d.id||!d.revision||d.feed>WeaponFeed::Single||d.afterEmptyFeed>WeaponAction::Slide||d.afterShot>WeaponAction::Slide)return false;
    const auto& p=d.feedPlan;
    if(!ManualReload(p).ValidConfig())return false;
    bool unseat=false,seat=false,insert=false,open=false,close=false;
    for(unsigned n=0;n<p.stepCount;++n){
        const auto op=p.steps[n].operation;
        if(op==ReloadOperation::CycleAction)return false;
        unseat|=op==ReloadOperation::UnseatMagazine;seat|=op==ReloadOperation::SeatMagazine;
        insert|=op==ReloadOperation::InsertRound;open|=op==ReloadOperation::OpenBreech;close|=op==ReloadOperation::CloseBreech;
    }
    // Magazine replacement is explicit, never relabelled as tube/clip feed.
    if(d.feed==WeaponFeed::DetachableMagazine)return unseat&&seat&&!insert&&!open&&!close&&p.stepCount==2&&
        p.steps[0].operation==ReloadOperation::UnseatMagazine&&p.steps[1].operation==ReloadOperation::SeatMagazine&&
        p.steps[0].repeats==1&&p.steps[1].repeats==1;
    if(unseat||seat||!insert)return false;
    if(d.feed==WeaponFeed::InternalTube)return !open&&!close;
    // Single, clip and belt have no dedicated clip/belt resource operations
    // in ManualReload yet. Require authored open/insert/close semantics;
    // this describes a plan, not proof of cover/latch/clip native support.
    return open&&close&&p.stepCount==3&&p.steps[0].operation==ReloadOperation::OpenBreech&&
        p.steps[1].operation==ReloadOperation::InsertRound&&p.steps[2].operation==ReloadOperation::CloseBreech&&
        p.steps[0].repeats==1&&p.steps[2].repeats==1&&
        (d.feed!=WeaponFeed::Single||p.steps[1].repeats==1);
}
inline WeaponMechanismSelection SelectWeaponMechanismPlan(const WeaponMechanismDescriptor& d,
    WeaponMechanismIntent intent,ChamberKnowledge chamber)noexcept {
    if(!ValidWeaponMechanismDescriptor(d)||intent>WeaponMechanismIntent::AfterShot||chamber>ChamberKnowledge::Occupied)return {};
    if(chamber==ChamberKnowledge::Unknown)return {WeaponMechanismStatus::UnknownChamber,{}};
    WeaponMechanismPlan p;
    if(intent==WeaponMechanismIntent::TacticalFeed){
        if(chamber!=ChamberKnowledge::Occupied||!d.tacticalRetainsChamber)return {WeaponMechanismStatus::ContradictoryChamber,{}};
        p.feed=d.feedPlan;
    }else if(intent==WeaponMechanismIntent::EmptyFeed){
        if(chamber!=ChamberKnowledge::Empty)return {WeaponMechanismStatus::ContradictoryChamber,{}};
        p.feed=d.feedPlan;p.action=d.afterEmptyFeed;
        if(p.action!=WeaponAction::Automatic)p.purpose=WeaponCyclePurpose::AfterFeed;
    }else{
        // Caller needs an adapter-issued shot receipt independently. A chamber
        // snapshot alone does not identify a shot or admit a native cycle.
        p.action=d.afterShot;
        if(p.action!=WeaponAction::Automatic)p.purpose=WeaponCyclePurpose::AfterShot;
    }
    if(p.action==WeaponAction::Pump)p.gestureFamily=WeaponCycleFamily::Pump;
    if(p.action==WeaponAction::Bolt)p.gestureFamily=WeaponCycleFamily::Bolt;
    // Handle/slide recognizers are deliberately not implemented by this mapping.
    return {WeaponMechanismStatus::Plan,p};
}
}




