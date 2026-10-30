#include "Bc2FeedEquipmentProfile.h"
#include "fvr/interaction/ReloadInsertion.h"
#include "Test.h"
#include <limits>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
using Op=FeedPhysicalOperation;using PoseId=Bc2M60FeedPoseId;
const Bc2M60FeedGeometry& Geometry(){
    return *FindBc2M60FeedGeometry("M60_sp","Objects/Weapons/Handheld/BU_lmg_M60/SP_lmg_M60",
        "Objects/Weapons/Handheld/BU_lmg_M60/BU_lmg_M60_Mesh",0xa7f219a1426216abull);
}
Bc2FeedInteractionPolicy Policy(){
    // Synthetic test policy, deliberately separate from the measured frames.
    return {.95f,.85f,.006f,.1f,.03f,.2f,20000000,5000000000,40000000};
}
const Bc2M60FeedPose& Pose(PoseId id){return Geometry().poses[static_cast<unsigned>(id)];}
bool SamePose(const math::Matrix4& a,const math::Matrix4& b){
    return feed_interaction_detail::Distance(a,b)<1e-6&&feed_interaction_detail::Angle(a,b)<.001;
}
int ExactSelectorAndNoFallback(){
    const auto& g=Geometry();CHECK(g.chain.size()==4&&g.chain[0].index==38&&g.chain[3].index==116);
    CHECK(g.chain[0].parentIndex==10&&g.chain[1].parentIndex==38&&
        g.chain[2].parentIndex==100&&g.chain[3].parentIndex==114);
    for(const auto name:{"M60","m60_sp","M60_sp_s","M60_sp_extra","PKM_sp",""})
        CHECK(!FindBc2M60FeedGeometry(name,g.configurationPath,g.meshPath,g.rigFingerprint));
    CHECK(!FindBc2M60FeedGeometry(g.asset,"Objects/Weapons/Handheld/BU_lmg_M60/SP_lmg_M60_s",g.meshPath,g.rigFingerprint));
    CHECK(!FindBc2M60FeedGeometry(g.asset,g.configurationPath,"unknown",g.rigFingerprint));
    CHECK(!FindBc2M60FeedGeometry(g.asset,g.configurationPath,g.meshPath,g.rigFingerprint+1));return 0;
}
int MeasuredFramesComposeInMetres(){
    const FeedSupplyKey key{{100,3},{200,7}};
    for(const auto& pose:Geometry().poses){
        CHECK(feed_mechanism_detail::Pose(pose.sourceFromWrist));
        const auto out=ComposeBc2M60FeedPresentation(key,pose);CHECK(out&&out->supply==key);
        CHECK(out->weaponFromPouch.values==pose.weaponFromPouch.values);
        CHECK(out->weaponFromChain[0].values==pose.weaponFromFeedRoot.values);
    }
    const auto resting=ComposeBc2M60FeedPresentation(key,Pose(PoseId::Resting));CHECK(resting);
    CHECK(Near(resting->weaponFromPouch.values[3][0],.1052855052f,1e-6f));
    CHECK(Near(resting->weaponFromChain[0].values[3][1],.0277554969f,1e-6f));
    CHECK(Near(resting->weaponFromChain[3].values[3][0],.0206640107f,1e-6f));
    CHECK(Near(resting->weaponFromChain[3].values[3][1],.0665491707f,1e-6f));
    CHECK(Near(resting->weaponFromChain[3].values[3][2],-.4063226180f,1e-6f));return 0;
}
int ExplicitSourceDestinationAndOperationTypes(){
    const auto profile=BuildBc2M60FeedInteractionProfile(Geometry(),51,2,Policy());
    CHECK(profile.profileId==51&&profile.profileRevision==2);
    for(auto op:{Op::OpenCover,Op::CloseCover,Op::DetachContainer,Op::AttachContainer,Op::SeatFeed}){
        const auto& motion=profile.operations[static_cast<unsigned>(op)];
        CHECK(motion.operation==op&&motion.measured&&feed_interaction_detail::Motion(motion));
    }
    const auto& open=profile.operations[static_cast<unsigned>(Op::OpenCover)];
    const auto& close=profile.operations[static_cast<unsigned>(Op::CloseCover)];
    CHECK(open.kind==FeedMotionKind::Hinge&&close.kind==FeedMotionKind::Hinge);
    CHECK(Near(open.travel,1.570753062f)&&close.travel==-open.travel);
    const auto atOpen=feed_interaction_detail::Multiply(
        feed_interaction_detail::Along(open.motionFromContact,open.kind,open.travel),open.weaponFromMotion);
    const auto closeStart=feed_interaction_detail::Multiply(close.motionFromContact,close.weaponFromMotion);
    CHECK(SamePose(atOpen,closeStart));
    const auto& detach=profile.operations[static_cast<unsigned>(Op::DetachContainer)];
    const auto& attach=profile.operations[static_cast<unsigned>(Op::AttachContainer)];
    const auto& seat=profile.operations[static_cast<unsigned>(Op::SeatFeed)];
    CHECK(detach.kind==FeedMotionKind::FreePlacement&&attach.kind==FeedMotionKind::FreePlacement&&seat.kind==FeedMotionKind::FreePlacement);
    CHECK(detach.placement.weaponFromSource.values==Pose(PoseId::PouchDetachSource).weaponFromPouch.values);
    CHECK(detach.placement.weaponFromDestination.values==Pose(PoseId::PouchWithdrawn).weaponFromPouch.values);
    CHECK(attach.placement.weaponFromSource.values==Pose(PoseId::PouchWithdrawn).weaponFromPouch.values);
    CHECK(attach.placement.weaponFromDestination.values==Pose(PoseId::PouchAttachedFeedUnseated).weaponFromPouch.values);
    const FeedSupplyKey key{{100,3},{200,7}};
    const auto feedSource=ComposeBc2M60FeedPresentation(key,Pose(PoseId::PouchAttachedFeedUnseated));
    const auto feedTarget=ComposeBc2M60FeedPresentation(key,Pose(PoseId::FeedSeated));
    CHECK(feedSource&&feedTarget);
    CHECK(seat.placement.weaponFromSource.values==feedSource->weaponFromChain[3].values);
    CHECK(seat.placement.weaponFromDestination.values==feedTarget->weaponFromChain[3].values);
    CHECK(feed_interaction_detail::Distance(seat.placement.weaponFromSource,Pose(PoseId::PouchAttachedFeedUnseated).weaponFromFeedRoot)>.07);
    CHECK(detach.placement.weaponFromDestination.values==attach.placement.weaponFromSource.values);
    return 0;
}
int UnknownOperationsDoNotBecomeAbsenceOrReverseSeat(){
    const auto p=BuildBc2M60FeedInteractionProfile(Geometry(),51,2,Policy());
    for(auto op:{Op::None,Op::WithdrawFeed,Op::ReleaseLatch,Op::EngageLatch,Op::CycleAction}){
        CHECK(!BuildBc2M60FeedMotion(Geometry(),op,Policy()));
        CHECK(!p.operations[static_cast<unsigned>(op)].measured);
        CHECK(!feed_interaction_detail::Motion(p.operations[static_cast<unsigned>(op)]));
    }
    CHECK(Bc2M60FeedOperationAvailability(Op::WithdrawFeed)==Bc2FeedGeometryAvailability::SeparateWithdrawalUnmeasured);
    CHECK(Bc2M60FeedOperationAvailability(Op::CycleAction)==Bc2FeedGeometryAvailability::ChargeSemanticsUnassigned);
    return 0;
}
int PolicyMustBeExplicitAndCannotRelabelOtherGeometry(){
    CHECK(!BuildBc2M60FeedMotion(Geometry(),Op::SeatFeed,{}));
    // Both positional and angular regions must overlap to erase the gesture.
    // A broad positional tolerance alone still leaves measured rotation.
    auto p=Policy();p.pathToleranceMeters=1;p.orientationToleranceRadians=1.4f;
    CHECK(!BuildBc2M60FeedMotion(Geometry(),Op::SeatFeed,p));
    p=Policy();p.orientationToleranceRadians=std::numeric_limits<float>::quiet_NaN();
    CHECK(!BuildBc2M60FeedMotion(Geometry(),Op::SeatFeed,p));
    p=Policy();p.maxInputGapNs=0;CHECK(!BuildBc2M60FeedMotion(Geometry(),Op::SeatFeed,p));
    auto other=Geometry();CHECK(!BuildBc2M60FeedMotion(other,Op::SeatFeed,Policy()));
    CHECK(!BuildBc2M60FeedInteractionProfile(Geometry(),0,2,Policy()).profileId);return 0;
}
int RetainedSourcesDoNotSnapToCanonicalEndpoints(){
    auto retained=Pose(PoseId::PouchWithdrawn);retained.weaponFromPouch.values[3][0]+=.004f;
    const auto attach=BuildBc2M60FeedMotionFromRetainedPose(Geometry(),Op::AttachContainer,Policy(),retained);
    const auto canonical=BuildBc2M60FeedMotion(Geometry(),Op::AttachContainer,Policy());CHECK(attach&&canonical);
    CHECK(attach->placement.weaponFromSource.values==retained.weaponFromPouch.values);
    CHECK(attach->placement.weaponFromDestination.values==canonical->placement.weaponFromDestination.values);
    CHECK(attach->placement.sourceFromGrip.values==canonical->placement.sourceFromGrip.values);
    retained=Pose(PoseId::PouchAttachedFeedUnseated);retained.weaponFromFeedRoot.values[3][1]+=.003f;
    const auto seat=BuildBc2M60FeedMotionFromRetainedPose(Geometry(),Op::SeatFeed,Policy(),retained);
    const auto presentation=ComposeBc2M60FeedPresentation({{100,3},{200,7}},retained);CHECK(seat&&presentation);
    CHECK(seat->placement.weaponFromSource.values==presentation->weaponFromChain[3].values);
    CHECK(!BuildBc2M60FeedMotionFromRetainedPose(Geometry(),Op::WithdrawFeed,Policy(),retained));
    CHECK(!BuildBc2M60FeedMotionFromRetainedPose(Geometry(),Op::OpenCover,Policy(),retained));
    retained.parentFromChainChild[0].values[0][0]=2;
    CHECK(!BuildBc2M60FeedMotionFromRetainedPose(Geometry(),Op::SeatFeed,Policy(),retained));return 0;
}
int ArticulatedChildrenAndSeparatePouchStayDistinct(){
    const FeedSupplyKey original{{100,3},{200,7}},replacement{{101,4},{201,8}};
    const auto& a=Pose(PoseId::FeedSeatSource);const auto& b=Pose(PoseId::FeedSeated);
    CHECK(feed_interaction_detail::Distance(a.weaponFromPouch,b.weaponFromPouch)<.00001);
    CHECK(feed_interaction_detail::Distance(a.weaponFromFeedRoot,b.weaponFromFeedRoot)>.07);
    for(unsigned i=0;i<3;++i)CHECK(feed_interaction_detail::Angle(a.parentFromChainChild[i],b.parentFromChainChild[i])>.2);
    const auto before=ComposeBc2M60FeedPresentation(original,a);CHECK(before&&before->supply==original);
    auto changed=a;changed.parentFromChainChild[1].values[3][0]+=.004f;
    const auto after=ComposeBc2M60FeedPresentation(original,changed);CHECK(after&&after->supply==original);
    CHECK(after->weaponFromPouch.values==before->weaponFromPouch.values);
    CHECK(after->weaponFromChain[0].values==before->weaponFromChain[0].values);
    CHECK(after->weaponFromChain[1].values==before->weaponFromChain[1].values);
    CHECK(feed_interaction_detail::Distance(after->weaponFromChain[2],before->weaponFromChain[2])>.0039);
    CHECK(feed_interaction_detail::Distance(after->weaponFromChain[3],before->weaponFromChain[3])>.0039);
    const auto second=ComposeBc2M60FeedPresentation(replacement,b);CHECK(second&&second->supply==replacement);
    auto pouch=Pose(PoseId::PouchWithdrawn).weaponFromPouch;pouch.values[3][0]+=.03f;
    const auto carry=PlaceBc2M60FeedPouch(original,Pose(PoseId::PouchWithdrawn),pouch);
    const auto withdrawn=ComposeBc2M60FeedPresentation(original,Pose(PoseId::PouchWithdrawn));
    CHECK(carry&&withdrawn&&carry->supply==original&&carry->weaponFromPouch.values==pouch.values);
    for(unsigned i=0;i<4;++i){
        const auto beforeLocal=feed_interaction_detail::Multiply(withdrawn->weaponFromChain[i],
            feed_interaction_detail::Inverse(withdrawn->weaponFromPouch));
        const auto afterLocal=feed_interaction_detail::Multiply(carry->weaponFromChain[i],
            feed_interaction_detail::Inverse(carry->weaponFromPouch));
        CHECK(SamePose(beforeLocal,afterLocal));
    }
    // Changing articulation under a carried terminal must move its ancestors,
    // preserving the terminal's actual grip and the independent pouch.
    auto carried=before->weaponFromChain[3];carried.values[3][0]+=.02f;
    const auto articulated=PlaceBc2M60FeedTerminal(original,b,carried);CHECK(articulated&&articulated->supply==original);
    CHECK(SamePose(articulated->weaponFromChain[3],carried));
    CHECK(articulated->weaponFromPouch.values==b.weaponFromPouch.values);
    for(unsigned i=0;i<3;++i){
        const auto relative=feed_interaction_detail::Multiply(articulated->weaponFromChain[i+1],
            feed_interaction_detail::Inverse(articulated->weaponFromChain[i]));
        CHECK(SamePose(relative,b.parentFromChainChild[i]));
    }
    CHECK(!ComposeBc2M60FeedPresentation({original.resource,{}},a));
    changed=a;changed.parentFromChainChild[0].values[0][0]=2;
    CHECK(!ComposeBc2M60FeedPresentation(original,changed));return 0;
}
struct SeatFixture {
    FeedMechanismProfile mechanism{};FeedInteractionProfile geometry{};FeedMechanismSnapshot snapshot{};
    FeedInteraction controller{};FeedInteractionSample sample{};
    FeedSupplyKey supply{{100,3},{200,7}};
    std::int64_t now=1000000000;
    math::Matrix4 sourceHand{},objectFromHand{};
    SeatFixture(){
        // Synthetic mod policy for this isolated seating case; this does not
        // assign native latch absence, chamber state, or replacement ordering.
        mechanism.family=FeedFamily::Belt;mechanism.id=51;mechanism.revision=2;
        mechanism.ammunitionKind=7;mechanism.cover=mechanism.container=mechanism.feedNeedsOpen=true;
        mechanism.maxAgeNs=100000000;
        mechanism.contacts[0]={10,.025f,.1f};mechanism.contacts[2]={12,.025f,.1f};mechanism.contacts[3]={13,.025f,.1f};
        geometry=BuildBc2M60FeedInteractionProfile(Geometry(),51,2,Policy());
        snapshot.owner={1,2,3,4,5};snapshot.mechanism={6,7};snapshot.profileId=51;snapshot.profileRevision=2;
        snapshot.sequence=10;snapshot.inputSequence=20;snapshot.observedNs=now;snapshot.reconciled=true;
        snapshot.cover=FeedCoverState::Open;snapshot.latch=FeedLatchState::Absent;
        snapshot.container=FeedContainerState::Attached;snapshot.attached=supply;
        snapshot.feed=FeedSeatState::Unseated;snapshot.cycle=FeedCycleState::Absent;
        snapshot.supply={supply,7,FeedSupplyState::Usable};
        for(unsigned n:{0u,2u,3u})snapshot.contacts[n]={mechanism.contacts[n].role,Geometry().weaponFromHinge};
        const auto& m=geometry.operations[static_cast<unsigned>(Op::SeatFeed)];
        sourceHand=feed_interaction_detail::Multiply(m.placement.sourceFromGrip,m.placement.weaponFromSource);
        snapshot.contacts[3].weaponFromContact=sourceHand;
        sourceHand.values[3][0]+=.004f; // Actual pickup offset must survive.
        objectFromHand=feed_interaction_detail::Multiply(sourceHand,feed_interaction_detail::Inverse(m.placement.weaponFromSource));
        sample.hand={90,1};sample.trackingGeneration=1;sample.focused=sample.operationPermitted=true;
    }
    FeedInteractionResult At(const math::Matrix4& object,bool grip){
        now+=10000000;++snapshot.sequence;++snapshot.inputSequence;snapshot.observedNs=now;
        sample.contact={snapshot.owner,snapshot.mechanism,snapshot.sequence,snapshot.inputSequence,now,true,true,
            feed_interaction_detail::Multiply(objectFromHand,object)};
        sample.grip=grip;return controller.Update(mechanism,geometry,snapshot,supply,sample,now);
    }
};
int MeasuredSeatAllowsActualGripOffsetAndNeedsFreshDwellRelease(){
    SeatFixture f;const auto& motion=f.geometry.operations[static_cast<unsigned>(Op::SeatFeed)];
    const auto& source=motion.placement.weaponFromSource;const auto& destination=motion.placement.weaponFromDestination;
    CHECK(f.At(source,false).status==FeedInteractionStatus::Armed);
    auto result=f.At(source,true);CHECK(result.status==FeedInteractionStatus::Acquired&&result.target);
    CHECK(result.targetKind==FeedInteractionTargetKind::Object&&SamePose(*result.target,source));
    for(unsigned i=1;i<=20;++i){
        const auto carried=reload_insertion_detail::BlendRigid(source,destination,double(i)/20);
        result=f.At(carried,true);CHECK(result.target&&SamePose(*result.target,carried)&&!result.completion);
    }
    CHECK(f.At(destination,true).status==FeedInteractionStatus::Endpoint);
    CHECK(f.At(destination,true).status==FeedInteractionStatus::Endpoint);
    result=f.At(destination,false);CHECK(result.status==FeedInteractionStatus::Completed&&result.completion);
    CHECK(result.completion->candidate.supply==f.supply&&result.completion->candidate.goal==f.supply);
    CHECK(result.completion->candidate.operation==Op::SeatFeed&&SamePose(*result.target,destination));
    CHECK(f.snapshot.feed==FeedSeatState::Unseated&&f.snapshot.attached==f.supply&&f.snapshot.seated==FeedSupplyKey{});
    return 0;
}
int ReplacementPreciselyStopsAtUnmeasuredWithdrawal(){
    SeatFixture f;const FeedSupplyKey previous{{99,2},{199,6}};
    f.snapshot.feed=FeedSeatState::Seated;f.snapshot.seated=f.snapshot.attached=previous;
    const auto query=EvaluateFeedMechanism(f.mechanism,f.snapshot,f.supply,f.now);
    CHECK(query.candidate&&query.candidate->operation==Op::WithdrawFeed&&query.candidate->supply==previous);
    const auto& source=f.geometry.operations[static_cast<unsigned>(Op::SeatFeed)].placement.weaponFromSource;
    const auto result=f.At(source,false);CHECK(result.status==FeedInteractionStatus::InvalidProfile&&!result.completion);
    CHECK(f.snapshot.seated==previous&&f.snapshot.attached==previous);
    f.snapshot.cycle=FeedCycleState::Unknown;
    CHECK(EvaluateFeedMechanism(f.mechanism,f.snapshot,f.supply,f.now).status==FeedMechanismStatus::UnknownState);
    CHECK(f.snapshot.cycle==FeedCycleState::Unknown);return 0;
}
}
int main(){
    if(ExactSelectorAndNoFallback()||MeasuredFramesComposeInMetres()||ExplicitSourceDestinationAndOperationTypes()||
       UnknownOperationsDoNotBecomeAbsenceOrReverseSeat()||PolicyMustBeExplicitAndCannotRelabelOtherGeometry()||
       RetainedSourcesDoNotSnapToCanonicalEndpoints()||
       ArticulatedChildrenAndSeparatePouchStayDistinct()||MeasuredSeatAllowsActualGripOffsetAndNeedsFreshDwellRelease()||
       ReplacementPreciselyStopsAtUnmeasuredWithdrawal())return 1;
    std::puts("Bc2FeedEquipmentProfile: 9 deterministic cases passed; offline geometry, no native/headset acceptance.");return 0;
}
