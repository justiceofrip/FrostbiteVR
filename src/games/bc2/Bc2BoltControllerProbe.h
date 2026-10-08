#pragma once
#include "Bc2BoltTrackedMapping.h"
#include "Bc2AmmoReserve.h"
#include "Bc2BoltInputStartup.h"
#include "fvr/interaction/ControllerInput.h"
#include <ostream>
namespace fvr::bc2 {
std::optional<math::Pose> BoltProbeController(const Bc2BoltControllerContact&,const interaction::InputFrame& original,
    interaction::InteractionHand,const math::Matrix4& desiredWristWorldMeters)noexcept;
std::optional<std::array<math::Matrix4,2>> BoltCurrentWrists(const Bc2BoltControllerContact&,
    const interaction::InputFrame&,const math::Matrix4& currentBodyWorldMeters)noexcept;
class Bc2BoltControllerProbe {
public:
    enum class Phase:unsigned {Warmup,SupportApproach,SupportGrip,Fire,WaitHeld,EnterCustody,ApproachBolt,Grip,
        Unlock,Rear,Forward,Lock,ReturnNeutral,ApproachGun,ReturnGrip,WaitReady,Settle,Done,Failed};
    enum class Controls:unsigned {ExplicitCustodyFixture,OrdinaryPlayer};
    Bc2BoltControllerProbe(Bc2BoltCalibration calibration,unsigned cycles=1,Controls controls=Controls::ExplicitCustodyFixture)noexcept:
        calibration_(std::move(calibration)),requested_(cycles),controls_(controls){}
    // Observation only. This driver has no hand arbiter, native API or player
    // policy reference and cannot submit a custody transaction or acknowledgement.
    void ObservePlayer(bool referencesReady,bool selected)noexcept {referencesReady_=referencesReady;selected_=selected;}
    void ObserveBody(std::shared_ptr<const BodyHolsterProbeSample> body)noexcept {body_=std::move(body);}
    bool InputOnly()const noexcept{return controls_==Controls::OrdinaryPlayer;}
    void Prepare(interaction::InputFrame&,const ReloadStateOwner&,std::string_view asset,
        const Bc2BoltControllerContact&,const std::optional<Bc2NativeCycleView>&,const std::optional<Bc2AmmoReserveLease>&,
        std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept;
    void Observe(const Bc2PhysicalBoltResult&,const std::optional<Bc2NativeCycleView>&,Bc2BoltPackCounters,std::int64_t now)noexcept;
    void ObserveSupport(bool holding,std::int64_t now)noexcept;
    bool WantsEnter()const noexcept{return !InputOnly()&&phase_==Phase::EnterCustody;}
    bool WantsReturn()const noexcept{return !InputOnly()&&phase_==Phase::ReturnGrip;}
    bool NeedsNeutralFire(std::int64_t now)const noexcept{return Failed()||Completed()||phase_!=Phase::Fire||now<phaseAt_||now-phaseAt_>=100000000;}
    bool Failed()const noexcept{return phase_==Phase::Failed;}
    bool Completed()const noexcept{return phase_==Phase::Done;}
    std::optional<std::array<math::Matrix4,2>> OriginalGripReferences()const noexcept {
        return armed_&&!InputOnly()?std::optional{originalGripReferences_}:std::nullopt;
    }
    Phase Current()const noexcept{return phase_;}
    void Report(std::ostream&)const;
private:
    void Move(Phase,std::int64_t)noexcept;
    void Fail(unsigned,std::int64_t)noexcept;
    struct Source {interaction::InputFrame input;ReloadStateOwner owner;std::int64_t observed=0,deadline=0;};
    std::array<std::optional<Source>,32> history_{};unsigned next_=0;
    std::optional<interaction::InputFrame> last_;
    Bc2BoltCalibration calibration_;ReloadStateOwner owner_{};unsigned requested_=1,completed_=0,failure_=0;
    Bc2BoltInputStartup startup_;std::shared_ptr<const BodyHolsterProbeSample> body_;
    Controls controls_=Controls::ExplicitCustodyFixture;bool referencesReady_=false,selected_=false;
    struct Edge {std::uint64_t sequence;std::int64_t observed,deadline;Phase phase;bool left,right,fire;};
    std::array<Edge,64> edges_{};unsigned edgeCount_=0,edgeDrops_=0;
    Phase phase_=Phase::Warmup;std::int64_t first_=0,phaseAt_=0,lastNow_=0,alignedAt_=0;
    bool armed_=false,support_=false;std::array<math::Pose,2> command_{};
    math::Matrix4 originalGunWristInWeapon_{};
    std::array<math::Matrix4,2> originalGripReferences_{};
    Bc2PhysicalBoltResult physical_{};Bc2BoltPackCounters packs_{};
    std::uint64_t lastRaw_=0,inputs_=0,rawMatches_=0,poses_=0;
    int loaded_=0,reserve_=0;unsigned baselinePairs_=0;
    std::optional<interaction::WeaponCycleLease> held_;
    std::optional<interaction::WeaponCycleReady> ready_;
    struct Receipt {interaction::WeaponCycleReady ready{};ReloadHoldIdentity native{};int loaded=0,reserve=0;unsigned pairs=0;bool custodyReturned=false;};
    std::array<Receipt,2> receipts_{};
    struct CustodyReceipt {interaction::HandInteractionSample input{};interaction::BoltCustodyPhase phase{};interaction::HandGunCustodyResult result{};};
    std::array<CustodyReceipt,4> custodyReceipts_{};unsigned custodyReceiptCount_=0,custodyDrops_=0;
    struct Row {Phase phase;unsigned failure;std::int64_t now;};std::array<Row,64> rows_{};unsigned rowCount_=0,eventDrops_=0;
};
}
