#pragma once
#include "Bc2ChestKnifeNative.h"
#include "Bc2ChestKnifeRendering.h"
#include "Bc2ChestKnifeProbeWitness.h"
#include "Bc2ChestKnifeLiveObservation.h"
#include "Bc2ChestKnifeCurrentHoldObservation.h"
#include "Bc2KnifeMotionGeometry.h"
#include <atomic>
namespace fvr::bc2 {
// Only a constant rotation is retained, never a rendered pose/source/lease.
// TrackedRig keeps this attachment within its actor/space/units lifetime. Each
// current measured receipt verifies it again; all live custody gates remain
// the coordinator's responsibility, independently of this geometry value.
struct ChestKnifeMotionIdentity {
    ReloadStateOwner owner{};interaction::HandClaimToken hold{};
    interaction::BodyItemKey knife{};
    std::uint64_t bodyOperation=0,nativeLifetime=0,menuEpoch=0;
    std::uint32_t trackingBasis=0;float worldUnitsPerMeter=0;
    bool operator==(const ChestKnifeMotionIdentity&)const=default;
};
class ChestKnifeMotionCalibration {
public:
    const math::Matrix4* Update(const std::optional<ChestKnifeMotionIdentity>& identity,
        const std::optional<math::Matrix4>& measured,bool& continuous,
        interaction::KnifeMotionCalibrationReason* observation=nullptr)noexcept {
        using Reason=interaction::KnifeMotionCalibrationReason;
        const auto reason=[&](Reason value){if(observation)*observation=value;};
        continuous=identity_&&identity&&*identity_==*identity;
        reason(continuous?Reason::Continuous:identity_?Reason::IdentityChanged:Reason::Initial);
        if(!continuous)Reset();
        if(!identity){reason(Reason::IdentityMissing);return nullptr;}
        if(measured){
            if(measured->values[3]!=std::array<float,4>{0,0,0,1}||!interaction::InverseRigid(*measured)){
                Reset();continuous=false;reason(Reason::MeasurementInvalid);return nullptr;
            }
            if(rotation_){
                // Allow only floating-point composition noise. Always retain
                // the first rotation, so repeated small changes cannot drift.
                bool same=true;
                for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)
                    same&=std::abs(rotation_->values[r][c]-measured->values[r][c])<=1.e-4f;
                if(!same){Reset();continuous=false;reason(Reason::MeasurementChanged);}
            }
            if(!rotation_){identity_=identity;rotation_=measured;}
        }
        if(!rotation_&&observation&&*observation==Reason::Initial)reason(Reason::MeasurementMissing);
        return rotation_?&*rotation_:nullptr;
    }
    void Reset()noexcept{identity_.reset();rotation_.reset();}
private:
    std::optional<ChestKnifeMotionIdentity> identity_;
    std::optional<math::Matrix4> rotation_;
};
// Gather's source projection also services the original unconsumed attack.
// AttackStaged suspends recognition, but cannot erase current measured
// geometry/readiness before the native gate observes its first consumer.
inline void PrepareChestKnifeMotionSource(ChestKnifeMotionCalibration& calibration,
    interaction::ChestKnifeCustody& policy,std::optional<ChestKnifeMotionIdentity> identity,
    const std::optional<math::Matrix4>& measured,const interaction::InputFrame& input,
    bool nativeAttackReady,interaction::KnifeSample& sample,
    interaction::KnifeRecognitionCoordinator* observation=nullptr)noexcept {
    using Reason=interaction::KnifeMotionProjectionReason;
    const auto phase=policy.Phase();
    const bool phaseAllowed=phase==interaction::KnifePhase::Held||phase==interaction::KnifePhase::AttackStaged;
    if(!phaseAllowed)identity.reset();
    bool continuous=false;
    const auto wrist=calibration.Update(identity,measured,continuous,observation?&observation->calibration:nullptr);
    if(observation){observation->calibrationContinuous=continuous;
        observation->projection=!phaseAllowed?Reason::Phase:!wrist?Reason::Calibration:Reason::Geometry;}
    if(!continuous)policy.InvalidateMotion();
    // Calibration carries no pose or lease: derive every point from this
    // gather's original grip and require its measured receipt for admission.
    const auto motion=wrist?private_knife::KnifeMotionFromTrackedGrip(input,*wrist):std::nullopt;
    sample.tip={};sample.direction={0,0,1};sample.motionTrackingReady=sample.nativeAttackReady=false;
    if(motion){
        sample.tip=motion->tip;sample.direction=motion->direction;
        sample.motionTrackingReady=phase==interaction::KnifePhase::Held;
        sample.nativeAttackReady=measured.has_value()&&nativeAttackReady;
        if(observation)observation->projection=Reason::Ready;
    }else {calibration.Reset();policy.InvalidateMotion();}
    if(observation){observation->trackingReady=sample.motionTrackingReady;observation->attackReady=sample.nativeAttackReady;}
}
// Actual gather coordinator. Owns no transport, hand allocator or inventory.
// Tick follows current suppression + TickHolster, before Tracking publication.
class Bc2ChestKnifeCoordinator {
public:
    Bc2ChestKnifeCoordinator()noexcept;
    bool TickIdleHeldGun(const BodyDrawSample&,const Bc2BodyHolster&,
        interaction::HandInteraction&,std::uint64_t& sharedIntent)noexcept;
    void Tick(const WeaponModeMemory&,const BodyDrawSample&,BodyHolsterSample,
        const BodyHolsterResult&,const std::optional<ChestKnifeRenderReceipt>&,
        bool otherMechanicsSettled,Bc2BodyInventory&,Bc2BodyHolster&,
        interaction::HandInteraction&,std::uint64_t& sharedIntent,
        const std::optional<math::Matrix4>& currentBody=std::nullopt)noexcept;
    // Input absence revokes unconsumed stages; accepted native custody remains.
    void Missing(std::int64_t nowNs,interaction::HandInteraction&)noexcept;
    bool ClaimsHand()const noexcept{return policy_.Hold().has_value()||nativeOwned_;}
    const std::optional<ChestKnifeRenderSource>& Rendering()const noexcept{return rendering_;}
    std::optional<ChestKnifeProbeWitness> ProbeWitness(std::int64_t nowNs)const noexcept;
    void RequestLiveObservation(std::int64_t nowNs)noexcept{liveObservation_.Request(nowNs);currentHoldObservation_.Request(nowNs);}
    std::shared_ptr<const ChestKnifeCurrentHoldObservation> CurrentNonfiringHold(std::int64_t nowNs)const noexcept{return currentHoldObservation_.Read(nowNs);}
    std::optional<ChestKnifeCurrentHoldFailure> CurrentHoldDiagnostic()const noexcept{return currentHoldObservation_.ReadDiagnostic();}
    std::shared_ptr<const ChestKnifeLiveObservation> LiveObservation(std::int64_t nowNs)const noexcept{return liveObservation_.Read(nowNs);}
    std::optional<ChestKnifePublicationDiagnostic> PublicationDiagnostic()const noexcept{return liveObservation_.ReadDiagnostic();}
    std::shared_ptr<const ChestKnifeReturnObservation> ReturnObservation()const noexcept{return returnObservation_.Read();}
    void Report(std::ostream&)const;
private:
    void InvalidateLiveObservation()noexcept{liveObservation_.Invalidate();currentHoldObservation_.Invalidate();}
    interaction::ChestKnifeCustody policy_;
    ChestKnifeMotionCalibration motionCalibration_;
    interaction::KnifeRecognitionDiagnostic recognition_;
    std::optional<private_knife::Anchor> anchor_;
    std::optional<ChestKnifeRenderSource> rendering_;
    std::uint64_t bodyOperation_=0,draws_=0,returns_=0,rejected_=0,readGaps_=0;
    bool nativeOwned_=false;
    mutable std::atomic_flag witnessGate_=ATOMIC_FLAG_INIT;
    ChestKnifeProbeWitness witness_{};
    ChestKnifeLivePublication liveObservation_;
    ChestKnifeCurrentHoldPublication currentHoldObservation_;
    bool drawOnlyIssued_=false;
    ChestKnifeReturnPublication returnObservation_;
    std::optional<interaction::KnifeCommand> drawCommand_;
    std::optional<interaction::KnifeReceipt> drawReceipt_;
    std::optional<interaction::BodyItemKey> originalGun_;
};
}
