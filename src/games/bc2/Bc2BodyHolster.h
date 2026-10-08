#pragma once
#include "Bc2BodyInventory.h"
#include "Bc2OrdinaryEquipment.h"
#include "Bc2BodyHolsterDiagnostic.h"
#include "Bc2HolsterInput.h"
#include "Bc2WeaponVisibility.h"
#include "Bc2HandPose.h"
#include "fvr/interaction/HandTouch.h"

namespace fvr::bc2 {
struct BodyHolsterCapabilities {
    // Adapter acceptance after an actual hide/show test, NOT a Pack receipt or
    // configured mesh boolean. Explicit BodyInventory admits individually verified profiles.
    bool nativeVisibilityAccepted=false,nativeInputSuppressionAccepted=false;
    unsigned acceptedProfileMask=0; // bit0 SPAS, bit1 XM8+ACOG; individually tested.
};
// Explicit BodyInventory only: SPAS plus scoped XM8_sp_s. ProfileAccepted still
// requires exact current asset/mesh evidence. Default construction is disabled;
// diagnostic admission remains separate. See XM8-HOLSTER-DIAGNOSTIC-20261002.md
// for native hide/show/post-draw evidence and remaining headset/shot-pair limits.
inline constexpr BodyHolsterCapabilities BodyInventoryHolsterAcceptance{true,true,3u};

enum class BodyHolsterPhase:unsigned {Disabled,Held,HidePending,Empty,ShowPending,Recovering};
struct BodyHolsterSample {
    interaction::BodyInventorySample body{}; // Existing assigned items/contact intent.
    ReloadStateOwner nativeOwner{};
    interaction::HandInteractionSample hand{};
    interaction::HandInteractionKey gun{};
    std::shared_ptr<const SelectedMeshesSnapshot> selected;
    std::optional<BodyVisibleRig> ordinary;
    std::optional<WeaponVisibilityReceipt> visibility;
    std::optional<HolsterSuppressionReceipt> suppression;
    std::uint64_t nativeTick=0;std::uint32_t cache=0;
    std::optional<BodyCarriedIdentity> carried; // Current exact coherent inventory, for semantic rebase only.
    bool cancel=false,reloadBusy=false;
    WeaponEquipmentIdentity equipment{};
    bool triggerNeutral=false; // Fresh actual source trigger state, never a masked action.
    std::optional<OrdinaryEquipmentPair> ordinaryPair;
};
struct BodyFreeRightEvidence {
    WeaponVisibilityRequest visibility;
    WeaponVisibilityReceipt receipt;
    HolsterSuppressionReceipt suppression;
    interaction::HandInteractionSample input;
    std::int64_t authorizationDeadlineNs=0; // Separate optional trial bound; original input is unchanged.
};
enum class BodyInventoryEvaluation:unsigned {NotEvaluated,Evaluated,Disabled,DiagnosticExpired,UnsupportedProfile};
struct BodyHolsterResult {
    BodyInventoryEvaluation inventoryEvaluation=BodyInventoryEvaluation::NotEvaluated;
    BodyDrawResult ordinaryDraw{};
    interaction::BodyInventoryResult inventory;
    BodyHolsterPhase phase=BodyHolsterPhase::Disabled;
    WeaponVisibilityRequest visibility{};
    std::optional<BodyFreeRightEvidence> freeRight;
    // Use the ordinary ResolveBodyDraw + repeated dispatch validation. This is
    // an exact physical-item request, not a direct native setter/cycle injection.
    std::optional<interaction::BodyItemKey> select;
    bool blockWeaponActions=false,allowAutomaticGunHold=true;
    std::optional<OrdinaryEquipmentRequest> ordinaryRecovery;
    bool ordinaryRetired=false;
};
// Composes the EXISTING BodyInventory policy and shared HandInteraction. This
// instance owns render request IDs only, not another item/hand/slot registry.
// Tick runs after this gather's suppression commit; Demand is read beforehand.
class Bc2BodyHolster {
public:
    explicit Bc2BodyHolster(BodyHolsterCapabilities capabilities={})noexcept:capabilities_(capabilities){}
    std::optional<HolsterSuppressionRequest> Demand(const BodyHolsterSample&)const noexcept;
    BodyHolsterResult Tick(interaction::BodyInventory&,const BodyHolsterSample&,interaction::HandInteraction&,std::uint64_t& sharedIntent)noexcept;
    BodyHolsterPhase Phase()const noexcept{return phase_;}
    void Report(std::ostream&)const;
    std::uint64_t RequestId()const noexcept{return render_;}
    bool BlocksActions()const noexcept{return Enabled()&&phase_!=BodyHolsterPhase::Held;}
    // A tracking gap can retain a committed user intent, never its old pose or
    // native receipt. All ordinary cancellation/owner paths discard that intent.
    void Invalidate(bool trackingGap=false)noexcept{ordinaryRecovery_.reset();if(!trackingGap){emptyIntent_=false;rebindEmpty_=false;inputGapOwner_.reset();}
        if(BlocksActions()){pending_.reset();phase_=BodyHolsterPhase::Recovering;}}
    // UpdateHook calls only after an observed timeout with the EXACT same native
    // owner tuple. Preserve intent across its synthetic input epoch, never a
    // render/native receipt. The next Tick still proves the carried inventory.
    bool ObserveNativeInputGap(const ReloadStateOwner& before,const ReloadStateOwner& after)noexcept;
    // Semantic bookkeeping only. This does not admit a pose, a claim or a
    // native write; the next Tick still requires fresh exact carried identity.
    bool PendingNativeInputGap(const ReloadStateOwner& owner)const noexcept {
        return emptyIntent_&&!pending_&&phase_==BodyHolsterPhase::Recovering&&inputGapOwner_&&*inputGapOwner_==owner;
    }
    bool AcceptsProfile(const ReloadStateOwner&,std::shared_ptr<const SelectedMeshesSnapshot>,std::int64_t nowNs)const noexcept;
    // One bounded trial, separately authorized from production acceptance.
    // Explicit single profile. It never sets acceptance bits or extends its deadline.
    bool AdmitDiagnostic(std::int64_t nowNs,std::int64_t deadlineNs,BodyHolsterDiagnosticProfile profile=BodyHolsterDiagnosticProfile::Spas)noexcept;
    BodyHolsterDiagnosticProfile DiagnosticProfile()const noexcept{return diagnosticProfile_;}
    std::int64_t DiagnosticStart()const noexcept{return diagnosticStart_;}
    std::int64_t DiagnosticDeadline()const noexcept{return diagnosticDeadline_;}
private:
    bool Enabled()const noexcept{return (capabilities_.nativeVisibilityAccepted&&capabilities_.nativeInputSuppressionAccepted&&capabilities_.acceptedProfileMask)||diagnosticDeadline_;}
    bool DiagnosticFresh(std::int64_t now)const noexcept{return !diagnosticDeadline_||(now>=diagnosticStart_&&now<diagnosticDeadline_);}
    bool ProfileAccepted(const BodyHolsterSample&)const noexcept;
    bool Begin(const BodyHolsterSample&,bool hide)noexcept;
    bool Replacement(const BodyHolsterSample&)const noexcept;
    void ObserveReplacement(const BodyHolsterSample&,bool eligible)noexcept;
    struct Exchange {ReloadStateOwner before{},after{};BodyCarriedIdentity oldCarried{},currentCarried{};
        std::uint64_t input=0;bool eligible=false;};
    std::array<Exchange,8> exchanges_{};unsigned exchangeCount_=0;std::uint64_t exchangeDropped_=0;
    std::optional<BodyCarriedIdentity> renderCarried_;
    std::optional<OrdinaryEquipmentRequest> ordinaryRecovery_;
    std::optional<BodyCarriedIdentity> ordinaryRecoveryCarried_;
    std::uint64_t ordinaryRetirements_=0;
    bool CanRebindEmpty(const BodyHolsterSample&)const noexcept;
    void RememberEmpty(const BodyHolsterSample&)noexcept;
    void Record(const BodyHolsterSample&,const BodyHolsterResult&,const interaction::HandInteraction&)noexcept;
    struct Transition {BodyHolsterPhase phase=BodyHolsterPhase::Disabled;ReloadStateOwner owner{};
        std::uint64_t input=0,request=0,commit=0,right=0,receiptRequest=0,receiptInput=0,draw=0;
        std::int64_t observed=0,deadline=0,now=0;unsigned copyMask=0,gates=0;
        std::uint64_t physicalItem=0,physicalEquip=0;
        bool block=false,free=false,hidden=false,receiptCurrent=false,suppressionCurrent=false;};
    std::array<Transition,64> transitions_{};unsigned transitionCount_=0,transitionNext_=0;std::uint64_t transitionDropped_=0;
    BodyHolsterCapabilities capabilities_{};
    std::int64_t diagnosticStart_=0,diagnosticDeadline_=0;
    BodyHolsterDiagnosticProfile diagnosticProfile_=BodyHolsterDiagnosticProfile::Disabled;
    BodyHolsterPhase phase_=BodyHolsterPhase::Held;
    std::uint64_t nextRender_=0,render_=0,revision_=0;
    ReloadStateOwner renderOwner_{};interaction::HandInteractionOwner physicalOwner_{};
    std::optional<interaction::BodyInventoryRequest> pending_;
    bool emptyIntent_=false,rebindEmpty_=false;
    std::optional<BodyCarriedIdentity> emptyCarried_;
    std::optional<ReloadStateOwner> inputGapOwner_;
    std::uint64_t emptyReceiptGaps_=0,emptyReceiptRehides_=0,emptyReceiptRejected_=0;
};
bool BodyFreeRightCurrent(const BodyFreeRightEvidence&,const BodyHolsterSample&,const interaction::HandInteraction&)noexcept;
// Render-thread validation of the immutable evidence emitted only AFTER actual
// shared-hand release. Publication additionally matches its own input/owner.
bool BodyFreeRightEvidenceCurrent(const BodyFreeRightEvidence&,std::int64_t nowNs)noexcept;
bool BodyFreeRightRenderCurrent(const BodyFreeRightEvidence& source,const BodyFreeRightEvidence* current,
    const WeaponVisibilityRequest* activeVisibility,std::int64_t nowNs)noexcept;
std::optional<math::Matrix4> HolsterRightTarget(const BodyFreeRightEvidence&,std::int64_t nowNs,
    const Bc2HandBinding&,const math::Matrix4& rawGripWorld,const math::Matrix4& calibratedWrist)noexcept;
std::optional<interaction::HandPose> PoseHolsteredRight(const BodyFreeRightEvidence&,std::int64_t nowNs,
    const Bc2HandBinding&,std::span<const std::int32_t> parents,std::span<const math::Matrix4> currentWorld,
    const math::Matrix4& solvedWrist,const interaction::ControllerState&);
// Actual anatomical free-hand output, including capacitive touch. The caller
// supplies the independent raw grip world and calibrated roomscale wrist
// position, then uses this target in SolveTrackedArms. Rebuild after the solved
// wrist for the final16-bone hand layer. Never use the gun attachment as input.
std::optional<interaction::HandPose> PoseHolsteredRight(const BodyFreeRightEvidence&,const BodyHolsterSample&,
    const interaction::HandInteraction&,const Bc2HandBinding&,std::span<const std::int32_t> parents,
    std::span<const math::Matrix4> currentWorld,const math::Matrix4& rawGripWorld,
    const math::Matrix4& calibratedWrist,const interaction::ControllerState&);
}
