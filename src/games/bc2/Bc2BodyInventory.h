#pragma once
#include "fvr/interaction/BodyGripApproach.h"
#include "fvr/interaction/BodyGripRelease.h"
#include "Bc2ReloadState.h"
#include "Bc2WeaponMode.h"
#include "fvr/interaction/BodyAnchors.h"
#include "fvr/interaction/BodyInventory.h"
#include <ostream>

namespace fvr::bc2 {
class Bc2BodyHolster;struct BodyHolsterSample;struct BodyHolsterResult;
struct BodyNativeItem {
    std::uint32_t weapon=0,data=0,persistence=0,category=20,slot=0;
    bool operator==(const BodyNativeItem&)const=default;
};
// Complete carried-item identity from one coherent native read. Space/input
// observation generations are deliberately separate from physical items.
struct BodyCarriedIdentity {
    std::uint32_t inventory=0,switching=0,count=0;
    std::array<BodyNativeItem,32> items{};
    bool operator==(const BodyCarriedIdentity&)const=default;
};
struct BodyNativeRoute {
    std::uint32_t from=0,action=0,count=0;
    std::array<std::uint32_t,11> targets{};
    bool operator==(const BodyNativeRoute&)const=default;
};
struct BodyNativeInventory {
    ReloadStateOwner owner{};
    std::uint32_t inventory=0,switching=0,selected=0,count=0,routeCount=0,begin=0,end=0,mapBegin=0,mapEnd=0;
    std::array<BodyNativeItem,32> items{};
    std::array<BodyNativeRoute,128> routes{};
    bool operator==(const BodyNativeInventory&)const=default;
};
// Display-only copy of assignments from the same coherent native read used by
// the actual policy. It grants no selection, hide, input or ammunition authority.
struct BodyInventoryDisplaySlot {
    interaction::BodySlotAssignment assignment{};BodyNativeItem native{};
    bool operator==(const BodyInventoryDisplaySlot&)const=default;
};
struct BodyInventoryDisplay {
    ReloadStateOwner selectedOwner{};interaction::HandInteractionOwner physicalOwner{};
    BodyCarriedIdentity carried{};interaction::BodyItemKey physicalSelected{};
    std::uint64_t revision=0,cohort=0,sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    std::array<BodyInventoryDisplaySlot,8> slots{};unsigned count=0;
};
bool BodyInventoryDisplayFresh(const BodyInventoryDisplay&,std::int64_t now)noexcept;
bool SameBodyInventoryDisplayCohort(const BodyInventoryDisplay&,const BodyInventoryDisplay&)noexcept;

enum class BodyReadStatus:std::uint8_t {Okay,Disabled,InvalidOwner,ReadFailure,Layout,Bounds,Changed};
struct BodyInventoryRead {BodyReadStatus status=BodyReadStatus::Disabled;std::optional<BodyNativeInventory> snapshot;unsigned reads=0;};
// Bounded, callback-only repeated coherent observation. Exact static selector
// code is verified by DiscoverWeaponMode/live comparison at the Gameplay enable
// boundary; reflected WeaponClass field/type is verified here before +84 reads.
BodyInventoryRead ReadBodyInventory(const WeaponModeMemory&,const ReloadStateOwner&,bool enabled=false)noexcept;
bool BodyLongGunCategory(unsigned category)noexcept;
std::optional<WeaponModeCommand> ResolveBodyDraw(const BodyNativeInventory&,std::uint32_t weapon)noexcept;

// Current ordinary native/private-rig presentation, not a hide acknowledgement.
// Gameplay fills this only from exact-owner ReadWeaponShotFrame with its original
// generation/deadline. EmptyHands requires a separate proven hide/show consumer.
struct BodyVisibleRig {
    ReloadStateOwner owner{};std::uint64_t generation=0;
    std::int64_t observedNs=0,deadlineNs=0;
};
enum class BodyInteractionState : unsigned { Available, Suspended };
struct BodyDrawSample {
    ReloadStateOwner owner{};
    interaction::HandInteractionSample hand{};
    interaction::InputFrame input{};
    interaction::HandInteractionKey gun{};
    std::optional<BodyVisibleRig> visible;
    std::optional<WeaponModeCommand> verifiedFamily; // Existing exact XM8/launcher resolver; no persistence-only inference.
    bool cancel=false,reloadBusy=false;
    // Exact Gameplay provenance: ordinary Fire was the sole cancellation cause.
    // Never set for lost tracking, reload, Use/equip, or invalid aiming.
    BodyInteractionState interaction=BodyInteractionState::Available;
    bool cancelOnlyForFire=false;
};
struct BodyDrawResult {
    std::optional<WeaponModeCommand> command;
    bool blockFire=false,pending=false;
    std::uint64_t committed=0,cancelled=0;
};
// Connected native adapter, one instance per Gameplay lifetime. Uses the shared
// hand arbiter; never a second hand owner or a fake AmmoObject/reload cycle.
class Bc2BodyInventory {
public:
    explicit Bc2BodyInventory(bool enabled=false,interaction::BodyAnchorConfig config={})noexcept:enabled_(enabled),config_(config){}
    BodyDrawResult Tick(const WeaponModeMemory&,const BodyDrawSample&,interaction::HandInteraction&,std::uint64_t& intent)noexcept;
    // Called only AFTER current gather suppression commit. Reuses this exact
    // reader/lifetime/slot/policy domain, including the ordinary draw fallback.
    BodyHolsterResult TickHolster(const WeaponModeMemory&,const BodyDrawSample&,BodyHolsterSample,
        Bc2BodyHolster&,interaction::HandInteraction&,std::uint64_t& intent)noexcept;
    bool Pending()const noexcept{return pending_.has_value();}
    std::optional<interaction::BodySlotAssignment> AssignedSlot(std::uint32_t weapon)const noexcept {
        const auto found=std::find_if(slots_.begin(),slots_.end(),[&](const auto& slot){return slot.item.id==weapon;});
        return found==slots_.end()?std::nullopt:std::optional(*found);
    }
    std::optional<BodyInventoryDisplay> Display(std::int64_t now)const noexcept {
        return display_&&BodyInventoryDisplayFresh(*display_,now)?display_:std::nullopt;
    }
    // Production preference only. Verified actor/seat observations queue one
    // stow on first on-foot entry, actor replacement, or actual vehicle exit.
    // It creates no native/render receipts and is independent of input epochs.
    void EnableAutomaticStow()noexcept{automaticEnabled_=enabled_;}
    void ObserveNativeActor(std::uint32_t manager,std::uint32_t player,std::uint32_t soldier,std::uint32_t weak,bool onFoot)noexcept;
    bool AutomaticStowPending()const noexcept{return automaticPending_;}
    void Cancel()noexcept;
    void SuspendInteraction()noexcept;
    void Report(std::ostream&)const;
private:
    // Ephemeral same-gather contact, never a synthetic controller edge. The
    // ordinary native route revalidates it after its own coherent inventory read.
    struct DrawApproach {
        interaction::BodySlotAssignment target{};interaction::HandClaimToken hold{};
        std::uint64_t revision=0,input=0;std::int64_t observed=0,deadline=0;
    };
    BodyDrawResult TickDraw(const WeaponModeMemory&,const BodyDrawSample&,interaction::HandInteraction&,
        std::uint64_t& intent,const std::optional<DrawApproach>&)noexcept;
    void PublishDisplay(const BodyNativeInventory&,const BodyDrawSample&,interaction::BodyItemKey)noexcept;
    std::optional<BodyInventoryDisplay> display_;std::uint64_t displayCohort_=0;
    struct Life {BodyNativeItem native{};interaction::BodyItemKey key{};};
    struct Actor {std::uint32_t manager=0,player=0,soldier=0,weak=0;bool operator==(const Actor&)const=default;};
    Actor automaticActor_{};bool automaticEnabled_=false,automaticFoot_=false,automaticPending_=false,automaticNeutral_=false;
    std::optional<BodyNativeItem> automaticItem_;
    std::uint64_t automaticEntries_=0,automaticRequests_=0,automaticUnsupported_=0,automaticSuperseded_=0;
    void ObserveAutomaticSelection(const BodyNativeItem&)noexcept;
    bool Observe(const BodyNativeInventory&,const BodyDrawSample&)noexcept;
    BodyDrawResult ObserveSuspended(const WeaponModeMemory&,const BodyDrawSample&)noexcept;
    bool Visible(const BodyDrawSample&)const noexcept;
    bool enabled_=false,armed_=false;interaction::BodyAnchorConfig config_{};
    interaction::BodyGripApproach heldApproach_;
    interaction::BodyGripRelease heldRelease_;
    interaction::BodyInventory policy_{};std::vector<Life> life_;
    std::vector<interaction::BodyInventoryItem> items_;std::vector<interaction::BodySlotAssignment> slots_;
    interaction::BodyInventoryOwner owner_{};
    std::uint32_t inventory_=0,switching_=0;
    std::uint64_t generation_=0,revision_=0,sequence_=0,lastInput_=0,lastHolsterInput_=0;
    std::int64_t lastNow_=0,nativeProofNs_=0;
    std::optional<interaction::BodyInventoryRequest> pending_;
    std::uint64_t requests_=0,commits_=0,cancels_=0,blocked_=0,unavailable_=0;
    struct Event {unsigned kind=0;std::uint64_t request=0,input=0;std::int64_t now=0;unsigned from=0,to=0,action=0;};
    std::array<Event,128> events_{};unsigned eventCount_=0,eventDropped_=0;
    void EventRow(unsigned,const BodyDrawSample&,std::uint64_t,unsigned=0,unsigned=0)noexcept;
    std::array<std::uint64_t,7> readStatuses_{};
    // Change-only ring: original input time, actual zone, edge/arming and shared
    // ownership. It describes rejected gestures; it never supplies an intent.
    struct Gesture {std::uint64_t input=0,space=0,request=0;std::int64_t observed=0,deadline=0,now=0;
        unsigned weapon=0,slot=0,contactWeapon=0,phase=0,nextPhase=0,flags=0,left=0,right=0,operation=0,reason=0,evaluation=0;
        float squeeze=0;std::array<float,3> point{};};
    std::array<Gesture,128> gestures_{};unsigned gestureCount_=0,gestureNext_=0;std::uint64_t gestureDropped_=0;
    std::uint64_t gestureInput_=0,gestureSpace_=0;std::optional<Gesture> lastGesture_;
    void GestureRow(const Gesture&)noexcept;
};
}
