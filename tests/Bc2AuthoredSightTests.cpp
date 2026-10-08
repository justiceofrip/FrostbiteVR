#include "Test.h"
#include "Bc2AuthoredSight.h"
#include "Bc2HandPose.h"
#include "fvr/interaction/SightVisualHandoff.h"
#include <limits>
#include <vector>
using namespace fvr;using namespace bc2;using namespace interaction;
namespace {
math::Matrix4 At(float x=0,float y=0,float z=0){auto m=bc2_hand_detail::Identity();m.values[3]={x,y,z,1};return m;}
bool Close(math::Vec3 a,math::Vec3 b){return Near(a.x,b.x)&&Near(a.y,b.y)&&Near(a.z,b.z);}
struct Fixture {
    std::array<std::string_view,3> chain{"sight","mount","weapon"};
    std::vector<std::string> names{"weapon","mount","sight","unskinned child","LeftHand"};
    std::vector<std::int32_t> parents{-1,0,1,2,0};
    std::vector<math::Matrix4> native{At(),At(.03f,.04f,-.7f),At(.03f,.04f,-.77f),At(.03f,.04f,-.8f),At(.2f,.3f,.4f)},inverse;
    std::vector<unsigned> hidden;
    Fixture(){
        for(const char* digit:{"Thumb","Index","Middle","Ring","Pinky"}){
            const unsigned finger=unsigned((names.size()-5)/3);
            for(unsigned joint=0;joint<3;++joint){
                const unsigned index=unsigned(names.size());names.push_back(std::string("LeftHand")+digit+std::to_string(joint+1));
                parents.push_back(joint?std::int32_t(index-1):4);
                const auto local=joint?(finger==0?At(.016f,0,.023f):At(0,0,joint==1?.03f:.022f)):
                    (finger==0?At(.03f,-.018f,.022f):At((2.5f-float(finger))*.02f,0,.065f));
                native.push_back(Multiply(local,native[parents.back()]));
            }
        }
        for(const auto& matrix:native)inverse.push_back(*InverseAnimatedTransform(matrix));
    }
    AuthoredSightGeometry Profile(){return {"exact/mesh","rifle","primary-guid","40mmgl","gp30-config-guid",chain,
        SightRigFingerprint(names,parents,inverse),{-.003f,-.013f,-.038f},{.004f,.013f,0},{0,-1,0},.718f};}
    Bc2HandBinding Hand(){return *bc2_hand_detail::Derive(names,parents,inverse,1);}
    AuthoredSightObservation Sample(){
        const auto hand=Hand();auto wrist=At(.03f,.04f,-.79f);const auto p=sight_grasp_detail::Point(hand.mechanismPointWristMeters,wrist);
        wrist.values[3][0]+=.03f-p.x;wrist.values[3][1]+=.04f-p.y;wrist.values[3][2]+=-.79f-p.z;
        return {"exact/mesh","rifle","primary-guid",Profile().rigFingerprint,12,100,200,names,parents,native,hidden,
            At(),wrist,hand.mechanismPointWristMeters,1,true};
    }
};
int ContactAndWorld(){
    Fixture f;const auto profile=f.Profile();auto sample=f.Sample();const auto contact=MeasureAuthoredSightContact(profile,sample);
    CHECK(contact.valid&&!contact.previewValid&&contact.generation==12&&contact.deadline==200);
    CHECK(contact.contactDistanceMeters<.00001f&&Close(contact.axis,{0,-1,0}));
    CHECK(Close(contact.graspPointMeters,{.03f,.04f,-.79f}));
    for(float units:{1.f,2.f}){
        Fixture moved;auto s=moved.Sample();auto world=sight_grasp_detail::Hinge({}, {0,1,0},1.f);world.values[3]={7,-3,4,1};
        for(auto& matrix:moved.native){for(unsigned k=0;k<3;++k)matrix.values[3][k]*=units;matrix=Multiply(matrix,world);}
        for(auto* matrix:{&s.placedWeapon,&s.rawAnatomicalWrist}){for(unsigned k=0;k<3;++k)matrix->values[3][k]*=units;*matrix=Multiply(*matrix,world);}
        s.unitsPerMetre=units;const auto actual=MeasureAuthoredSightContact(profile,s);
        CHECK(actual.valid&&Close(actual.graspPointMeters,contact.graspPointMeters)&&Close(actual.axis,contact.axis));
        CHECK(actual.contactDistanceMeters<.00001f);
    }
    // Current descendant sight animation changes the physical contact in all
    // axes; the intermediate mount never masquerades as the weapon root.
    f.native[2]=*RotateAboutHinge(f.native[2],contact.pivotMeters,contact.axis,.718f);
    sample.rawAnatomicalWrist=*RotateAboutHinge(sample.rawAnatomicalWrist,contact.pivotMeters,contact.axis,.718f);
    const auto open=MeasureAuthoredSightContact(profile,sample);CHECK(open.valid&&open.contactDistanceMeters<.00001f);
    sample.rawAnatomicalWrist.values[3][0]+=.3f;
    CHECK(MeasureAuthoredSightContact(profile,sample).contactDistanceMeters>.25f);
    return 0;
}
int RejectIdentityAndGeometry(){
    for(unsigned mutation=0;mutation<18;++mutation){
        Fixture f;auto p=f.Profile();auto s=f.Sample();
        if(mutation==0)s.selectedMeshPath="other/shared/40mmgl";
        if(mutation==1)s.configuration="other-40mmgl-config";
        if(mutation==2){s.asset="40mmgl";s.configuration="primary-guid";}
        if(mutation==3)++s.rigFingerprint;
        if(mutation==4)f.parents[2]=0;
        if(mutation==5)f.names[3]="sight";
        if(mutation==6){f.hidden.push_back(1);s.hidden=f.hidden;}
        if(mutation==7)s.deadline=s.now;
        if(mutation==8)s.generation=0;
        if(mutation==9)s.attachmentReady=false;
        if(mutation==10)s.unitsPerMetre=0;
        if(mutation==11)s.rawAnatomicalWrist.values[0][0]=-1;
        if(mutation==12)s.mechanismPointWristMeters.x=std::numeric_limits<float>::quiet_NaN();
        if(mutation==13)p.maximumMeters=p.minimumMeters;
        if(mutation==14)p.axisPart={0,0,0};
        if(mutation==15)p.travelRadians=2;
        if(mutation==16)f.native[1]={};
        if(mutation==17)f.chain[1]="sight";
        CHECK(!MeasureAuthoredSightContact(p,s).valid);
    }
    Fixture f;auto s=f.Sample();s.asset="40mmgl";s.configuration="gp30-config-guid";
    CHECK(MeasureAuthoredSightContact(f.Profile(),s).valid);
    return 0;
}
int GestureGraspAndHandoff(){
    Fixture f;const auto p=f.Profile();const auto s=f.Sample();const auto c=MeasureAuthoredSightContact(p,s);CHECK(c.valid);
    SightFlipConfig config;config.pivotMeters=c.pivotMeters;config.axis=c.axis;config.grabRadiusMeters=.08f;config.holdRadiusMeters=.2f;
    config.minLeverMeters=.015f;config.thresholdRadians=.4f;config.hysteresisRadians=.08f;config.maxStepRadians=.7f;
    config.detentHoldNs=70000000;config.gestureTimeoutNs=2500000000;config.ackTimeoutNs=1500000000;config.maxSampleGapNs=250000000;
    SightFlip policy(config);SightFlipSample sample;sample.owner={1,2,3,4};sample.sequence=1;sample.nowNs=100000000;
    sample.focused=sample.tracked=sample.contactValid=sample.nativeModeValid=true;sample.handLocalMeters=c.handLocalMeters;
    CHECK(!policy.Update(sample).grabbed);++sample.sequence;sample.nowNs+=10000000;sample.squeeze=1;
    CHECK(policy.Update(sample).grabbed);
    const auto grasp=SightGraspBinding::Begin(c.pivotMeters,c.axis,c.sightLocalMeters,c.handLocalFrameMeters,c.graspPointMeters,c.palmPointWristMeters,p.travelRadians);CHECK(grasp);
    auto handoff=SightVisualHandoff::BeginWithGeometry(c.sightLocalMeters,SightMode::Primary,c.pivotMeters,c.axis,p.travelRadians,
        SightVisualHandCapture{c.rawHandLocalFrameMeters,c.rawPalmPointWristMeters,12});CHECK(handoff);
    const auto moved=grasp->Evaluate(.5f,SightMode::Primary);CHECK(moved);
    sample.handLocalMeters=moved->graspPoint;++sample.sequence;sample.nowNs+=10000000;
    CHECK(policy.Update(sample).detent);++sample.sequence;sample.nowNs+=80000000;
    const auto request=policy.Update(sample);CHECK(request.request&&request.phase==SightFlipPhase::AwaitingAcknowledgement);
    ++sample.sequence;sample.nowNs+=10000000;sample.nativeMode=SightMode::Secondary;sample.acknowledgedRequest=request.request->id;
    CHECK(policy.Update(sample).committedMode==SightMode::Secondary);
    // The generalized consumer respects the actual41-degree detent rather
    // than stretching the GP30 into the legacy90-degree XM8 hinge.
    const auto end=grasp->Evaluate(1.2f,SightMode::Primary);CHECK(end&&Near(end->appliedRadians,p.travelRadians));
    const auto raw=SightVisualRawHand::Fresh(end->wrist,13,13,100,200);CHECK(raw);
    const auto progress=handoff->Update(SightFlipPhase::Latched,.5f,100,end->sight,raw);
    CHECK(progress&&progress->rawObserved&&progress->nativeObserved);
    CHECK(Near(progress->appliedRadians,p.travelRadians)&&Near(progress->nativeProgressRadians,p.travelRadians));
    const auto repeated=grasp->Evaluate(progress->appliedRadians,SightMode::Primary);CHECK(repeated);
    const auto again=grasp->Evaluate(progress->appliedRadians,SightMode::Primary);CHECK(again);
    CHECK(repeated->sight.values==again->sight.values&&repeated->wrist.values==again->wrist.values);
    CHECK(Close(repeated->graspPoint,end->graspPoint));
    // A fresh mode cannot be committed by geometry alone or an unrelated ack.
    SightFlip pending(config);sample.sequence=1;sample.nowNs=100;sample.squeeze=0;sample.nativeMode=SightMode::Primary;sample.acknowledgedRequest=0;
    CHECK(!pending.Update(sample).committedMode);
    return 0;
}
int CompleteProceduralGrasp(){
    Fixture f;const auto hand=f.Hand();const auto p=f.Profile();const auto c=MeasureAuthoredSightContact(p,f.Sample());CHECK(c.valid);
    const auto grasp=SightGraspBinding::Begin(c.pivotMeters,c.axis,c.sightLocalMeters,c.handLocalFrameMeters,c.graspPointMeters,c.palmPointWristMeters,p.travelRadians);CHECK(grasp);
    std::optional<HandPose> previous;
    for(float angle:{0.f,.3f,.718f}){
        const auto posed=grasp->Evaluate(angle,SightMode::Primary);CHECK(posed);
        const auto fingers=GenerateHandPose(f.parents,hand.referenceWorld,f.native,hand.pose,posed->wrist,LeftHandTargets(HandPoseRole::MechanismGrip,0,0));CHECK(fingers);
        CHECK(fingers->writes.size()==16); // Wrist + all15 finger joints.
        CHECK(Close(sight_grasp_detail::Point(hand.mechanismPointWristMeters,posed->wrist),posed->graspPoint));
        for(const auto& write:fingers->writes){CHECK(write.index>=4);CHECK(sight_grasp_detail::Proper(write.transform));}
        previous=fingers;
    }
    CHECK(previous);return 0;
}
int HandoffRejectionsAndClosure(){
    Fixture f;const auto p=f.Profile();const auto c=MeasureAuthoredSightContact(p,f.Sample());CHECK(c.valid);
    for(float bad:{0.f,-1.f,2.f,std::numeric_limits<float>::quiet_NaN()}){
        CHECK(!SightGraspBinding::Begin(c.pivotMeters,c.axis,c.sightLocalMeters,c.handLocalFrameMeters,c.graspPointMeters,c.palmPointWristMeters,bad));
        CHECK(!SightVisualHandoff::BeginWithGeometry(c.sightLocalMeters,SightMode::Primary,c.pivotMeters,c.axis,bad));
    }
    const auto opened=*RotateAboutHinge(c.sightLocalMeters,c.pivotMeters,c.axis,p.travelRadians);
    auto handoff=SightVisualHandoff::BeginWithGeometry(opened,SightMode::Secondary,c.pivotMeters,c.axis,p.travelRadians);CHECK(handoff);
    CHECK(handoff->Update(SightFlipPhase::Manipulating,-.4f,100));
    const auto native=handoff->Update(SightFlipPhase::Latched,-.4f,100000100,c.sightLocalMeters);
    CHECK(native&&native->nativeObserved&&Near(native->nativeProgressRadians,p.travelRadians));
    auto wrong=c.sightLocalMeters;wrong.values[3][0]+=.2f;
    CHECK(!handoff->Update(SightFlipPhase::Latched,-.4f,200000100,wrong)->nativeObserved);
    return 0;
}
}
int main(){CHECK(!ContactAndWorld());CHECK(!RejectIdentityAndGeometry());CHECK(!GestureGraspAndHandoff());CHECK(!CompleteProceduralGrasp());CHECK(!HandoffRejectionsAndClosure());std::puts("Bc2AuthoredSight: 5 groups passed");return 0;}
