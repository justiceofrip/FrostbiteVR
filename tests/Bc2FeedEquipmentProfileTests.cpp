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
    const auto& g=Geometry();CHECK(g.chainCount==4&&g.chain[0].index==38&&g.chain[3].index==116);
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
// Independent synthetic evidence: no M60 transform, role or topology is copied.
// These local rows exercise pure spans and are never part of the live registry.
Bc2FeedGeometry SyntheticGeometry(unsigned count){
    Bc2FeedGeometry g;g.asset="SyntheticFeed";g.configurationPath="Fixture/Feed/Configuration";
    g.meshPath="Fixture/Feed/Mesh";g.rigFingerprint=0x76543210;g.chainCount=count;
    g.meshSha256=g.lodSha256=g.skeletonSha256=g.clipSha256=
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    g.rootBone={"fixtureRoot","",2,0};g.pouchBone={"fixtureBox","fixtureRoot",5,2};
    g.coverBone={"fixtureLid","fixtureRoot",7,2};
    g.chain={{{"c0","fixtureRoot",11,2},{"c1","c0",13,11},{"c2","c1",15,13},
        {"c3","c2",17,15},{"c4","c3",19,17},{"c5","c4",21,19},
        {"c6","c5",23,21},{"c7","c6",25,23}}};
    const auto identity=bc2_feed_profile_detail::MeasuredFrame({1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1});
    g.weaponFromHinge=g.hingeFromOpeningContact=g.hingeFromClosingContact=g.weaponFromCoverReference=identity;
    g.weaponFromHinge.values[3][0]=.31f;g.weaponFromCoverReference.values[3][1]=.27f;
    g.hingeFromOpeningContact.values[3][0]=.06f;g.coverTravelRadians=.9f;
    g.hingeFromClosingContact=feed_interaction_detail::Along(g.hingeFromOpeningContact,FeedMotionKind::Hinge,g.coverTravelRadians);
    for(unsigned n=0;n<g.poses.size();++n){auto& p=g.poses[n];p.authoredSeconds=n*.25;p.chainCount=count;
        p.weaponFromPouch=p.weaponFromFeedRoot=p.sourceFromWrist=identity;
        p.weaponFromPouch.values[3][0]=.2f+.09f*n;p.weaponFromFeedRoot.values[3][1]=-.3f+.07f*n;
        p.sourceFromWrist.values[3][2]=.02f;
        for(unsigned i=0;i+1<count&&i<p.parentFromChainChild.size();++i){
            p.parentFromChainChild[i]=identity;p.parentFromChainChild[i].values[3][0]=.01f*(i+1);
        }
    }
    return g;
}
Bc2FeedRegistration SyntheticRegistration(){
    static constexpr std::array<ReloadTimingWord,6> timing{{
        {0x10,std::bit_cast<std::uint32_t>(.5f)},{0x14,0},{0x18,std::bit_cast<std::uint32_t>(3.25f)},
        {0x20,1},{0x24,2},{0x2c,0}}};
    Bc2FeedRegistration row;row.key=0x123456789abcdef0ull;
    row.profileDigest="123456789abcdef0000000000000000000000000000000000000000000000000";
    row.geometry=SyntheticGeometry(2);row.interactionPolicy=Policy();
    auto& p=row.mechanism;p.family=FeedFamily::Belt;p.id=0x654321;p.revision=3;p.ammunitionKind=11;
    p.cover=p.container=p.containerNeedsOpen=p.feedNeedsOpen=p.sameContainerReplenishment=p.coupledContainerDetach=true;
    p.maxAgeNs=100000000;p.contacts[0]={31,.04f,.3f};p.contacts[2]={32,.04f,.3f};p.contacts[3]={33,.04f,.3f};
    row.config={row.geometry.asset,row.geometry.configurationPath,{2,1,8,29,60,3,0,3.25f,.5f,0,0,0,false,false},
        timing,ReloadDescriptorAdmission::ReviewedNative};
    row.nativeClass=Bc2FeedNativeClass::AutomaticMagazineZeroBoltTransfer;row.enabled=row.reviewed=true;return row;
}
ReloadObservedConfig Observed(const Bc2FeedRegistration& row){
    ReloadObservedConfig c;const auto& d=row.config;const auto& v=d.values;
    std::copy(d.assetName.begin(),d.assetName.end(),c.assetName.begin());
    std::copy(d.assetPath.begin(),d.assetPath.end(),c.assetPath.begin());
    c.weaponData=0x10000;c.firingData=0x20000;c.primaryFire=0x30000;c.ammoAddress=c.primaryFire+0x170;
    c.fireLogicType=v.fireLogicType;c.reloadType=v.reloadType;c.fireInputAction=v.fireInputAction;c.reloadInputAction=v.reloadInputAction;
    c.baseCapacity=v.baseCapacity;c.numberOfMagazines=v.numberOfMagazines;c.reloadDelay=v.reloadDelay;
    c.reloadTime=v.reloadTime;c.reloadThreshold=v.reloadThreshold;c.postReloadTime=v.postReloadTime;
    c.boltDelay=v.boltDelay;c.boltTime=v.boltTime;c.holdBoltUntilFireRelease=v.holdBoltUntilFireRelease;
    c.holdBoltUntilZoomRelease=v.holdBoltUntilZoomRelease;return c;
}
int RegistryPinsExactIndependentConfiguration(){
    const auto& builtin=bc2_feed_profile_detail::M60Registration;CHECK(ValidateBc2FeedRegistration(builtin));
    const auto* live=FindBc2FeedRegistration(builtin.key);CHECK(live&&live->mechanism.id==builtin.mechanism.id);
    CHECK(FindBc2FeedRegistration(Observed(builtin))==live);
    CHECK(FindBc2FeedRegistration(builtin.key,Observed(builtin),Geometry().asset,Geometry().configurationPath,
        Geometry().meshPath,Geometry().rigFingerprint)==live);
    const std::array<Bc2FeedRegistration,2> rows{builtin,SyntheticRegistration()};const auto& g=rows[1].geometry;
    CHECK(SelectBc2FeedRegistration(rows,rows[1].key)==&rows[1]);
    CHECK(SelectBc2FeedRegistration(rows,Observed(rows[1]))==&rows[1]);
    CHECK(SelectBc2FeedRegistration(rows,g.asset,g.configurationPath,g.meshPath,g.rigFingerprint)==&rows[1]);
    CHECK(!FindBc2FeedRegistration(rows[1].key)); // The fixture did not enroll anything.
    auto c=Observed(rows[1]);c.baseCapacity=100;CHECK(!SelectBc2FeedRegistration(rows,c));
    c=Observed(rows[1]);c.reloadTime=7.8f;CHECK(!SelectBc2FeedRegistration(rows,c));
    c=Observed(rows[1]);c.ammoAddress++;CHECK(!SelectBc2FeedRegistration(rows,c));
    CHECK(!SelectBc2FeedRegistration(rows,g.asset,g.configurationPath,Geometry().meshPath,g.rigFingerprint));
    CHECK(!SelectBc2FeedRegistration(rows,g.asset,Geometry().configurationPath,g.meshPath,g.rigFingerprint));
    CHECK(!SelectBc2FeedRegistration(rows,g.asset,g.configurationPath,g.meshPath,g.rigFingerprint+1));
    CHECK(!SelectBc2FeedRegistration(rows,"syntheticfeed",g.configurationPath,g.meshPath,g.rigFingerprint));return 0;
}
int RegistryRejectsCollisionsUnreviewedAndInvalidRows(){
    const auto good=SyntheticRegistration();CHECK(ValidateBc2FeedRegistration(good));
    const auto rejects=[&](Bc2FeedRegistration bad){const std::array<Bc2FeedRegistration,1> rows{bad};
        return !ValidateBc2FeedRegistration(bad)&&!SelectBc2FeedRegistration(rows,bad.key);};
    auto bad=good;bad.key=0;CHECK(rejects(bad));bad=good;bad.key++;CHECK(rejects(bad));
    bad=good;bad.profileDigest="1234";CHECK(rejects(bad));
    bad=good;bad.enabled=false;CHECK(rejects(bad));bad=good;bad.reviewed=false;CHECK(rejects(bad));
    bad=good;bad.nativeClass=Bc2FeedNativeClass::Unsupported;CHECK(rejects(bad));
    bad=good;bad.config.admission=ReloadDescriptorAdmission::Candidate;CHECK(rejects(bad));
    bad=good;bad.config.values.boltTime=.1f;CHECK(rejects(bad));
    bad=good;bad.config.values.fireLogicType=0;CHECK(rejects(bad));
    bad=good;bad.config.assetPath="Fixture/Other";CHECK(rejects(bad));
    bad=good;bad.config.values.reloadTime=std::numeric_limits<float>::quiet_NaN();CHECK(rejects(bad));
    bad=good;bad.config.values.baseCapacity=0;CHECK(rejects(bad));
    bad=good;bad.mechanism.coupledContainerDetach=false;CHECK(rejects(bad));
    bad=good;bad.interactionPolicy={};CHECK(rejects(bad));
    bad=good;bad.geometry.meshSha256="unmeasured";CHECK(rejects(bad));
    bad=good;bad.geometry.poses[2].chainCount++;CHECK(rejects(bad));
    bad=good;bad.geometry.chain[1].parentIndex=2;CHECK(rejects(bad));
    bad=good;bad.geometry.coverBone=bad.geometry.pouchBone;CHECK(rejects(bad));
    auto timing=std::array<ReloadTimingWord,6>{};std::copy(good.config.timing.begin(),good.config.timing.end(),timing.begin());
    timing[0].expected=std::bit_cast<std::uint32_t>(.7f);bad=good;bad.config.timing=timing;CHECK(rejects(bad));
    std::array<Bc2FeedRegistration,2> duplicate{good,good};CHECK(!SelectBc2FeedRegistration(duplicate,good.key));
    CHECK(!SelectBc2FeedRegistration(duplicate,Observed(good)));
    duplicate[1].key=0x223456789abcdef0ull;
    duplicate[1].profileDigest="223456789abcdef0000000000000000000000000000000000000000000000000";
    duplicate[1].geometry.meshPath="Fixture/AlternativeMesh"; // Same config is still ambiguous.
    CHECK(!SelectBc2FeedRegistration(duplicate,good.key));
    CHECK(!SelectBc2FeedRegistration(duplicate,duplicate[1].key));
    duplicate[1].geometry.asset="Other";duplicate[1].config.assetName="Other";
    duplicate[1].profileDigest=good.profileDigest;CHECK(!SelectBc2FeedRegistration(duplicate,good.key));
    duplicate[1].profileDigest="223456789abcdef0000000000000000000000000000000000000000000000000";
    duplicate[1].config.assetName=good.config.assetName; // Invalid row still conflicts in native config identity.
    CHECK(!SelectBc2FeedRegistration(duplicate,good.key));return 0;
}
int IndependentGeometryAndVariableChainDriveMotion(){
    const FeedSupplyKey supply{{8,1},{9,1}};
    for(unsigned count:{1u,2u,6u,MaxBc2FeedChainBones}){
        const auto g=SyntheticGeometry(count);CHECK(ValidateBc2FeedGeometry(g));
        const auto motion=BuildBc2FeedMotion(g,Op::SeatFeed,Policy());CHECK(motion);
        const auto& source=g.poses[unsigned(Bc2FeedPoseId::PouchAttachedFeedUnseated)];
        const auto p=ComposeBc2FeedPresentation(supply,source);CHECK(p&&p->chainCount==count);
        CHECK(SamePose(motion->placement.weaponFromSource,p->weaponFromChain[count-1]));
        CHECK(Near(p->weaponFromChain[count-1].values[3][0],.005f*count*(count-1)));
        CHECK(Near(motion->placement.weaponFromDestination.values[3][1],.12f));
        const auto open=BuildBc2FeedMotion(g,Op::OpenCover,Policy());CHECK(open&&Near(open->travel,.9f));
        CHECK(Near(open->weaponFromMotion.values[3][0],.31f));
        auto target=p->weaponFromChain[count-1];target.values[3][2]+=.12f;
        const auto carried=PlaceBc2FeedTerminal(supply,source,target);CHECK(carried);
        CHECK(SamePose(carried->weaponFromChain[count-1],target));
        CHECK(SamePose(carried->weaponFromPouch,source.weaponFromPouch));
        auto retained=source;retained.weaponFromFeedRoot.values[3][2]+=.003f;
        const auto next=BuildBc2FeedMotionFromRetainedPose(g,Op::SeatFeed,Policy(),retained);CHECK(next);
        CHECK(Near(next->placement.weaponFromSource.values[3][2],.003f));
        CHECK(SamePose(next->placement.weaponFromDestination,motion->placement.weaponFromDestination));
    }
    auto g=SyntheticGeometry(2);g.chainCount=0;CHECK(!BuildBc2FeedMotion(g,Op::SeatFeed,Policy()));
    g=SyntheticGeometry(2);g.poses[0].parentFromChainChild.back().values[0][0]=std::numeric_limits<float>::quiet_NaN();
    CHECK(ValidateBc2FeedGeometry(g)&&ComposeBc2FeedPresentation(supply,g.poses[0])); // Inactive capacity is never consumed.
    g=SyntheticGeometry(2);g.chainCount=MaxBc2FeedChainBones+1;CHECK(!ValidateBc2FeedGeometry(g));
    auto p=SyntheticGeometry(2).poses[0];p.chainCount=MaxBc2FeedChainBones+1;
    CHECK(!ComposeBc2FeedPresentation(supply,p));CHECK(!PlaceBc2FeedTerminal(supply,p,p.weaponFromFeedRoot));return 0;
}
int RigidCoverCompanionsRequireMeasuredUniqueRootRoles(){
    CHECK(MaxBc2FeedParts==14&&MaxBc2FeedCoverCompanions==4);
    CHECK(Geometry().coverCompanionCount==0&&Bc2FeedPartCount(Geometry())==6);
    auto good=SyntheticRegistration();auto& g=good.geometry;
    g.coverCompanionCount=4;
    g.coverCompanions={{{{"side0","fixtureRoot",29,2},g.weaponFromCoverReference},
        {{"side1","fixtureRoot",31,2},g.weaponFromCoverReference},
        {{"side2","fixtureRoot",33,2},g.weaponFromCoverReference},
        {{"side3","fixtureRoot",35,2},g.weaponFromCoverReference}}};
    for(unsigned i=0;i<g.coverCompanionCount;++i)g.coverCompanions[i].weaponFromReference.values[3][0]=.03f*(i+1);
    CHECK(ValidateBc2FeedRegistration(good)&&Bc2FeedPartCount(g)==8);
    const auto rejects=[](Bc2FeedRegistration row){return !ValidateBc2FeedGeometry(row.geometry)&&
        !ValidateBc2FeedRegistration(row)&&!BuildBc2FeedMotion(row.geometry,Op::OpenCover,Policy());};
    auto bad=good;bad.geometry.coverCompanionCount=5;CHECK(rejects(bad));
    bad=good;bad.geometry.coverCompanions[0].weaponFromReference={};CHECK(rejects(bad));
    bad=good;bad.geometry.coverCompanions[0].weaponFromReference.values[3][0]=std::numeric_limits<float>::quiet_NaN();CHECK(rejects(bad));
    bad=good;bad.geometry.coverCompanions[0].bone.bone={};CHECK(rejects(bad));
    bad=good;bad.geometry.coverCompanions[0].bone.index=4096;CHECK(rejects(bad));
    bad=good;bad.geometry.coverCompanions[0].bone.parentIndex=7;CHECK(rejects(bad));
    bad=good;bad.geometry.coverCompanions[0].bone.parent="fixtureLid";CHECK(rejects(bad));
    for(const auto role:{g.rootBone,g.pouchBone,g.coverBone,g.chain[0],g.chain[1],g.coverCompanions[1].bone}){
        bad=good;bad.geometry.coverCompanions[0].bone.bone=role.bone;CHECK(rejects(bad));
        bad=good;bad.geometry.coverCompanions[0].bone.index=role.index;CHECK(rejects(bad));
    }
    bad=good;bad.geometry.coverCompanionCount=0;bad.geometry.coverCompanions={};
    CHECK(ValidateBc2FeedRegistration(bad)&&Bc2FeedPartCount(bad.geometry)==4);
    auto maximum=SyntheticGeometry(MaxBc2FeedChainBones);maximum.coverCompanions=g.coverCompanions;
    maximum.coverCompanionCount=4;CHECK(ValidateBc2FeedGeometry(maximum)&&Bc2FeedPartCount(maximum)==MaxBc2FeedParts);
    return 0;
}
}
int main(){
    if(ExactSelectorAndNoFallback()||MeasuredFramesComposeInMetres()||ExplicitSourceDestinationAndOperationTypes()||
       UnknownOperationsDoNotBecomeAbsenceOrReverseSeat()||PolicyMustBeExplicitAndCannotRelabelOtherGeometry()||
       RetainedSourcesDoNotSnapToCanonicalEndpoints()||
       ArticulatedChildrenAndSeparatePouchStayDistinct()||MeasuredSeatAllowsActualGripOffsetAndNeedsFreshDwellRelease()||
       ReplacementPreciselyStopsAtUnmeasuredWithdrawal()||RegistryPinsExactIndependentConfiguration()||
       RegistryRejectsCollisionsUnreviewedAndInvalidRows()||IndependentGeometryAndVariableChainDriveMotion()||
       RigidCoverCompanionsRequireMeasuredUniqueRootRoles())return 1;
    std::puts("Bc2FeedEquipmentProfile: 13 deterministic cases passed; offline geometry, no native/headset acceptance.");return 0;
}
