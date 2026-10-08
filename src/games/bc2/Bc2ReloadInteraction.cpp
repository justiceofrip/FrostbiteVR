#include "Bc2ReloadInteraction.h"
namespace fvr::bc2 {
interaction::ReloadInsertionProfile SpasReloadInsertionProfile()noexcept {
    interaction::ReloadInsertionProfile p{};
    // reports/spas-reload-candidate-20261001.json: native row58 grasp, row62
    // terminal pose; 50mm stroke/angles/timing are design choices, not a socket.
    p.id=0x535041530001ull;p.revision=5;
    p.family=interaction::ReloadInsertionFamily::SingleShell;
    // This round shell uses its authored longitudinal tip axis without a roll
    // key. Preserve the player's axial twist while aligning that axis to rail.
    p.orientation=interaction::ReloadInsertionOrientation::AxialSymmetry;
    // Explicitly held shell: assist near the whole bottom-entry path and align
    // orientation after contact. A latch can occur while already near the mouth,
    // but seating requires20mm new raw inward tip movement plus alignment/dwell.
    p.approach=interaction::ReloadInsertionApproach::RailContact;
    p.itemFromHand={{{{0.5216128812f,0.8433950571f,0.1288553864f,0.f},{0.8289782374f,-0.4652830701f,-0.3103332119f,0.f},{-0.2017792085f,0.2686923375f,-0.9418538283f,-0.f},{-0.03649051857f,-0.1033049136f,0.07940265825f,1.f}}}};
    p.itemFromInsertion={{{{-1.f,0.f,0.f,0.f},{0.f,1.f,0.f,0.f},{0.f,0.f,-1.f,0.f},{0.f,0.f,-0.03182983398f,1.f}}}};
    // Preserve terminal shell orientation; travel rises from below independently.
    // Direction is measured row60->62 motion. The straight50mm extension is a
    // deliberate interaction candidate, not a verified native loading socket.
    p.weaponFromEntry={{{{-0.9962383471f,-0.08630882375f,-0.007750413495f,0.f},{-0.08643524304f,0.9960983307f,0.01780569523f,0.f},{0.006183381705f,0.01840863071f,-0.999811401f,0.f},{-0.001390417511f,-0.08541089337f,-0.5065066047f,1.f}}}};
    p.travelDirection={-0.0970328845f,0.8000316178f,0.5920591202f};
    p.travelMeters=0.05f;
    p.captureDistanceMeters=0.06f;
    p.releaseDistanceMeters=0.12f;
    p.postCaptureTravelMeters=0.02f;
    p.captureAngleRadians=0.6108652382f;
    p.releaseAngleRadians=1.221730476f;
    p.seatToleranceMeters=0.003f;
    p.maxStepMeters=0.04f;
    p.maxStepRadians=1.047197551f;
    p.alignmentNs=120000000ll;
    p.seatDwellNs=60000000ll;
    p.maxSampleGapNs=100000000ll;
    p.maxGuidedNs=5000000000ll;
    return p;
}
Bc2ReloadInteractionResult Bc2ReloadInteraction::Reject(Bc2ReloadInteractionReason reason)noexcept {
    Bc2ReloadInteractionResult out;out.reason=reason;out.insertion=insertion_.Reset();cycle_=0;return out;
}
Bc2ReloadInteractionResult Bc2ReloadInteraction::Update(const Bc2ReloadInteractionSample& sample)noexcept {
    using namespace interaction;
    if(!enabled_)return Reject(Bc2ReloadInteractionReason::Disabled);
    if(sample.assetName!=SpasReloadAsset||sample.meshPath!=SpasReloadMesh||sample.rigFingerprint!=SpasReloadRig)
        return Reject(Bc2ReloadInteractionReason::UnsupportedAsset);
    if(!sample.selectedMeshIdentityVerified)return Reject(Bc2ReloadInteractionReason::MeshUnverified);
    const auto& s=sample.insertion;const auto& n=sample.native;
    if(!n.cycle||!n.allFiringCopiesHeld||n.owner!=s.identity.owner||n.weapon!=s.identity.weapon||
       n.observedNs<=0||n.observedNs>s.nowNs||n.deadlineNs<=s.nowNs||n.deadlineNs<n.observedNs||
       n.deadlineNs-n.observedNs>250000000ll)
        return Reject(Bc2ReloadInteractionReason::NativeGateUnavailable);
    if(cycle_&&cycle_!=n.cycle){auto out=Reject(Bc2ReloadInteractionReason::NativeCycleChanged);cycle_=n.cycle;return out;}
    cycle_=n.cycle;
    if(s.itemClaim.token.hand!=InteractionHand::Left||s.weaponClaim.token.hand!=InteractionHand::Right)
        return Reject(Bc2ReloadInteractionReason::WrongHands);
    using namespace reload_insertion_detail;
    if(!Rigid(sample.rawLeftWristWorldMeters)||!Rigid(sample.weaponWorldMeters))
        return Reject(Bc2ReloadInteractionReason::InvalidWorldPose);
    auto raw=s;const auto profile=SpasReloadInsertionProfile();raw.profile={profile.id,profile.revision};
    raw.weaponFromHand=Multiply(sample.rawLeftWristWorldMeters,*InverseRigid(sample.weaponWorldMeters));
    Bc2ReloadInteractionResult out;out.insertion=insertion_.Update(raw);
    const auto& r=out.insertion;
    if(!r.rawItem){out.reason=Bc2ReloadInteractionReason::InsertionRejected;return out;}
    const auto item=r.guidedItem.value_or(*r.rawItem);
    const auto hand=Multiply(profile.itemFromHand,item); // Duplicate packets retain the original coherent geometry.
    out.targets=Bc2ReloadTargets{s.identity,s.itemClaim.token,s.weaponClaim.token,s.sequence,n.cycle,s.observedNs,
        std::min({s.deadlineNs,n.deadlineNs,s.itemClaim.deadlineNs,s.weaponClaim.deadlineNs}),item,hand};
    // A physical seat remains a candidate. Only native completion reconciles ammo.
    return out;
}
}
