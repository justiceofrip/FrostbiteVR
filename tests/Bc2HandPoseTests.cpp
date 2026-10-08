#include "Test.h"
#include "Bc2HandPose.h"
#include <limits>
#include "fvr/interaction/TrackedRig.h"
#include "fvr/interaction/SightGrasp.h"
#include "fvr/interaction/SightVisualHandoff.h"
using namespace fvr;using namespace bc2;using namespace interaction;
namespace {
math::Matrix4 At(float x=0,float y=0,float z=0){auto m=bc2_hand_detail::Identity();m.values[3]={x,y,z,1};return m;}
bool Close(math::Vec3 a,math::Vec3 b){return Near(a.x,b.x)&&Near(a.y,b.y)&&Near(a.z,b.z);}
bool Close(const math::Matrix4& a,const math::Matrix4& b){
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)if(!Near(a.values[r][c],b.values[r][c]))return false;return true;
}
struct Fixture {
    std::vector<std::string> names{"root","LeftHand"};
    std::vector<std::int32_t> parents{-1,0};
    std::vector<math::Matrix4> reference{At(),At(.2f,.3f,.4f)},inverse;
    Fixture(){
        const std::array<const char*,5> fingers{"Thumb","Index","Middle","Ring","Pinky"};
        for(unsigned finger=0;finger<5;++finger)for(unsigned joint=0;joint<3;++joint){
            const unsigned index=unsigned(names.size());names.push_back(std::string("LeftHand")+fingers[finger]+std::to_string(joint+1));
            parents.push_back(joint?std::int32_t(index-1):1);
            math::Matrix4 local;
            if(!joint)local=finger==0?At(.03f,-.018f,.022f):At((2.5f-float(finger))*.02f,0,.065f);
            else local=finger==0?At(.016f,0,.023f):At(0,0,joint==1?.03f:.022f);
            reference.push_back(Multiply(local,reference[parents.back()]));
        }
        names.push_back("hidden unrelated weapon");parents.push_back(0);reference.push_back({});
        for(unsigned n=0;n<reference.size();++n)inverse.push_back(n+1==reference.size()?math::Matrix4{}:*InverseAnimatedTransform(reference[n]));
    }
    std::optional<Bc2HandBinding> Derive(float units=1){return bc2_hand_detail::Derive(names,parents,inverse,units);}
};
int SightContactAnatomicalCalibration(){
    // Compose the production TrackedRig calibration with the existing synthetic
    // anatomy fixture. No game asset or fabricated runtime binding is used.
    Fixture fixture;const auto binding=fixture.Derive();CHECK(binding);
    InputFrame input{};input.generation=12;input.spaceGeneration=1;input.predictedNs=100;
    input.focused=input.headValid=true;
    for(auto& hand:input.hands){hand.gripTracked=hand.aimTracked=true;hand.active=Components;hand.grip.position={.2f,.3f,-.4f};}
    TrackedRig rig;const std::array<ArmAnchor,2> arms{};
    const auto targets=rig.Update({1,2,3,4},input,At(),fixture.reference[1],At(.2f,.3f,.4f),At(),arms);
    CHECK(targets&&targets->tracked[0]&&targets->tracked[1]);
    const auto relative=math::MakeRelativePose(input.referenceHead,input.hands[0].grip);CHECK(relative);
    const auto view=math::MakeLhViewFromOpenXRPose(*relative);CHECK(view);
    const auto grip=InverseRigid(*view);CHECK(grip);
    auto freeLeft=Multiply(binding->wristToGrip,*grip);freeLeft.values[3]=targets->left.values[3];
    const auto palm=MeasureSightPalm(fixture.names,fixture.parents,fixture.reference,1,1);CHECK(palm);
    const auto anchor=sight_grasp_detail::Point(binding->mechanismPointWristMeters,freeLeft);
    WeaponSightObservation s;s.assetName="XM8_sp_s";s.rootName=s.sightParentName="jntWpn_1";s.sightName="jntWpn_9";
    s.skeletonFingerprint=0xa7f219a1426216abull;s.generation=12;s.deadline=34;s.attachmentReady=s.palmValid=true;
    s.nativeWeapon=s.placedWeapon=At();s.nativeSight=At(anchor.x,anchor.y,anchor.z+.075f);
    s.rawLeftTarget=targets->left;s.palmPointWristMeters=*palm;
    s.frontValid=s.visualHandValid=true;s.nativeFront=s.nativeSight;s.visualLeftTarget=freeLeft;s.visualPointWristMeters=binding->mechanismPointWristMeters;
    const auto oldContact=MeasureWeaponSightContact(s);CHECK(oldContact.valid);
    std::printf("legacy_contact_m=%.9f mechanism_point=(%.9f,%.9f,%.9f) palm=(%.9f,%.9f,%.9f)\n",oldContact.contactDistanceMeters,binding->mechanismPointWristMeters.x,binding->mechanismPointWristMeters.y,binding->mechanismPointWristMeters.z,palm->x,palm->y,palm->z);
    CHECK(oldContact.contactDistanceMeters>.08f);
    s.rawLeftTarget=freeLeft;s.palmPointWristMeters=binding->mechanismPointWristMeters;
    const auto fixed=MeasureWeaponSightContact(s);CHECK(fixed.valid&&fixed.previewValid&&fixed.contactDistanceMeters<.00001f);
    CHECK(Close(fixed.handLocalMeters,anchor));
    CHECK(fixed.generation==s.generation&&fixed.deadline==s.deadline);
    CHECK(Close(fixed.handLocalFrameMeters,fixed.rawHandLocalFrameMeters));
    CHECK(Close(fixed.palmPointWristMeters,fixed.rawPalmPointWristMeters));
    // The unmodified interaction policy rejects the old contact but latches
    // this same stationary controller after release/press with the new source.
    SightFlipConfig config;config.pivotMeters=fixed.pivotMeters;config.axis=fixed.axis;
    config.grabRadiusMeters=.08f;config.holdRadiusMeters=.2f;config.minLeverMeters=.015f;
    config.thresholdRadians=.4f;config.hysteresisRadians=.08f;config.maxStepRadians=.7f;
    config.detentHoldNs=70000000;config.gestureTimeoutNs=2500000000;
    config.ackTimeoutNs=1500000000;config.maxSampleGapNs=250000000;
    for(bool anatomical:{false,true}){
        SightFlip policy{config};const auto& contact=anatomical?fixed:oldContact;
        SightFlipSample sample;sample.owner={1,2,3,4};sample.sequence=1;sample.nowNs=100000000;
        sample.focused=sample.tracked=sample.nativeModeValid=true;
        sample.contactValid=contact.contactDistanceMeters<=config.grabRadiusMeters;
        sample.contactDistanceMeters=contact.contactDistanceMeters;sample.handLocalMeters=contact.handLocalMeters;
        CHECK(!policy.Update(sample).grabbed);
        ++sample.sequence;sample.nowNs+=10000000;sample.squeeze=1;
        CHECK(policy.Update(sample).grabbed==anatomical);
    }
    // A zero-angle grasp does not shift a landmark already on the frame.
    const auto grasp=SightGraspBinding::Begin(fixed.pivotMeters,fixed.axis,fixed.sightLocalMeters,
        fixed.handLocalFrameMeters,fixed.graspPointMeters,fixed.palmPointWristMeters);CHECK(grasp);
    const auto zero=grasp->Evaluate(0,SightMode::Primary);CHECK(zero&&Close(zero->wrist,freeLeft));
    // Activation and the post-switch raw handoff retain one anatomical basis.
    // Displayed/IK wrist feedback is never used to manufacture this motion.
    auto handoff=SightVisualHandoff::Begin(fixed.sightLocalMeters,SightMode::Primary,
        SightVisualHandCapture{fixed.rawHandLocalFrameMeters,fixed.rawPalmPointWristMeters,12});CHECK(handoff);
    const auto moved=Multiply(freeLeft,sight_grasp_detail::Hinge(fixed.pivotMeters,fixed.axis,.6f));
    const auto raw=SightVisualRawHand::Fresh(moved,13,13,100,200);CHECK(raw);
    const auto progress=handoff->Update(SightFlipPhase::Latched,.4f,100,{},raw);
    CHECK(progress&&progress->rawObserved&&Near(progress->rawProgressRadians,.6f)&&Near(progress->appliedRadians,.6f));
    // Body/world transform and engine-unit scale preserve the same contact.
    for(float units:{1.f,2.f}){
        auto transformed=s;transformed.unitsPerMetre=units;
        auto world=At(7,-3,4);world.values[0]={0,0,-1,0};world.values[2]={1,0,0,0};
        for(auto* matrix:{&transformed.nativeWeapon,&transformed.nativeSight,&transformed.nativeFront,
            &transformed.placedWeapon,&transformed.rawLeftTarget,&transformed.visualLeftTarget}){
            for(unsigned axis=0;axis<3;++axis)matrix->values[3][axis]*=units;
            *matrix=Multiply(*matrix,world);
        }
        const auto again=MeasureWeaponSightContact(transformed);
        CHECK(again.valid&&again.contactDistanceMeters<.00001f&&Close(again.handLocalMeters,fixed.handLocalMeters));
        CHECK(Close(again.rawHandLocalFrameMeters,fixed.rawHandLocalFrameMeters));
    }
    // Moving only the independent raw target away must cancel contact even if
    // the displayed grasp remains on the frame. This is not visual self-grip.
    s.rawLeftTarget.values[3][0]+=.3f;
    CHECK(MeasureWeaponSightContact(s).contactDistanceMeters>.2f);
    return 0;
}

int AnatomyAndPose(){
    Fixture f;const auto binding=f.Derive();CHECK(binding&&binding->pose.wrist==1&&binding->referenceWorld.size()==f.names.size());
    for(unsigned finger=0;finger<5;++finger){
        CHECK(binding->pose.fingers[finger].count==3);
        for(unsigned joint=0;joint<3;++joint){
            const auto& bound=binding->pose.fingers[finger].joints[joint];
            CHECK(bound.index==2+finger*3+joint);
            CHECK(Near(bc2_hand_detail::Dot(bound.curlAxisLocal,bound.curlAxisLocal),1));
        }
    }
    // Synthetic anatomy has forward +Z, indexward +X and palmar -Y.
    CHECK(Close(bc2_hand_detail::Vector({0,0,1},binding->wristToGrip),{0,-1,0}));
    CHECK(Close(bc2_hand_detail::Vector({1,0,0},binding->wristToGrip),{0,0,1}));
    CHECK(Close(bc2_hand_detail::Vector({0,-1,0},binding->wristToGrip),{1,0,0}));
    CHECK(Close(bc2_hand_detail::Position(binding->wristToGrip),{}));
    auto mechanism=GenerateHandPose(f.parents,binding->referenceWorld,f.reference,binding->pose,
        f.reference[1],LeftHandTargets(HandPoseRole::MechanismGrip,0,0));CHECK(mechanism);
    auto posed=f.reference;for(const auto& write:mechanism->writes)posed[write.index]=write.transform;
    const auto thumb=bc2_hand_detail::Position(Multiply(posed[4],f.inverse[1]));
    const auto index=bc2_hand_detail::Position(Multiply(posed[7],f.inverse[1]));
    CHECK(Close(binding->mechanismPointWristMeters,bc2_hand_detail::Scale(bc2_hand_detail::Add(thumb,index),.5f)));
    // Curl goes toward palmar -Y, with unchanged base position and segment length.
    CHECK(posed[6].values[3][1]<f.reference[6].values[3][1]);
    CHECK(Near(posed[5].values[3][0],f.reference[5].values[3][0]));
    // Native support role has no finger-pose rewrite.
    const auto support=GenerateHandPose(f.parents,binding->referenceWorld,posed,binding->pose,
        posed[1],LeftHandTargets(HandPoseRole::WeaponSupport,1,1));CHECK(support);
    for(const auto& write:support->writes)CHECK(write.transform.values==posed[write.index].values);
    return 0;
}
int TargetsAndFrameInvariance(){
    const auto free=LeftHandTargets(HandPoseRole::Free,.7f,.2f);
    CHECK(free.role==HandPoseRole::Free&&Near(free.curl[0],.7f)&&Near(free.curl[1],.2f)&&Near(free.curl[4],.7f)&&Near(free.pinch,0));
    const auto clamped=LeftHandTargets(HandPoseRole::Free,2,-1);CHECK(Near(clamped.curl[0],1)&&Near(clamped.curl[1],0));
    const auto a=LeftHandTargets(HandPoseRole::MechanismGrip,0,0),b=LeftHandTargets(HandPoseRole::MechanismGrip,1,1);
    CHECK(a.curl==b.curl&&a.pinch==b.pinch&&a.pinch>0); // Fixed anchor matches fixed authored mechanism pose.
    Fixture f;const auto original=f.Derive();CHECK(original);
    auto transform=At(2,-3,4);transform.values[0]={0,0,-1,0};transform.values[2]={1,0,0,0};
    for(unsigned n=0;n+1<f.reference.size();++n){f.reference[n]=Multiply(f.reference[n],transform);f.inverse[n]=*InverseAnimatedTransform(f.reference[n]);}
    const auto moved=f.Derive();CHECK(moved&&Close(moved->wristToGrip,original->wristToGrip));
    CHECK(Close(moved->mechanismPointWristMeters,original->mechanismPointWristMeters));
    for(unsigned finger=0;finger<5;++finger)for(unsigned joint=0;joint<3;++joint)
        CHECK(Close(moved->pose.fingers[finger].joints[joint].curlAxisLocal,original->pose.fingers[finger].joints[joint].curlAxisLocal));
    // Unit scale changes native translations, not the canonical metre anchor.
    Fixture scaled;for(unsigned n=0;n+1<scaled.reference.size();++n){
        for(unsigned k=0;k<3;++k)scaled.reference[n].values[3][k]*=2;
        scaled.inverse[n]=*InverseAnimatedTransform(scaled.reference[n]);
    }
    const auto doubled=scaled.Derive(2);CHECK(doubled&&Close(doubled->mechanismPointWristMeters,original->mechanismPointWristMeters));
    return 0;
}
int BindingRejections(){
    Fixture valid;
    // The synthetic rig exercises anatomy but is never accepted as a verified
    // BC2 runtime rig. Production cannot bypass the exact fingerprint gate.
    CHECK(!BindLeftHandPose(valid.names,valid.parents,valid.inverse,1));
    for(unsigned kind=0;kind<10;++kind){
        Fixture f;
        if(kind==0)f.names[3]="UnknownThumb";
        if(kind==1)f.names[17]="LeftHandThumb1"; // Ambiguous name.
        if(kind==2)f.parents[3]=1; // Broken direct chain.
        if(kind==3)f.parents[17]=17; // Cycle outside hand.
        if(kind==4)f.inverse[3].values[0][0]=-1; // Reflected anatomy.
        if(kind==5)f.inverse[3].values[0][0]=0;
        if(kind==6)f.inverse[3].values[3][0]=std::numeric_limits<float>::quiet_NaN();
        if(kind==7)f.inverse[6]=f.inverse[5]; // Zero-length finger segment.
        if(kind==8)f.inverse[14]=f.inverse[5]; // Index and little palm bases coincide.
        if(kind==9)f.inverse[2]=f.inverse[5]; // Thumb has no palmar side.
        CHECK(!f.Derive());
    }
    CHECK(!valid.Derive(0)&&!valid.Derive(-1)&&!valid.Derive(std::numeric_limits<float>::quiet_NaN()));
    const auto nanTargets=LeftHandTargets(HandPoseRole::Free,std::numeric_limits<float>::quiet_NaN(),0);
    const auto binding=valid.Derive();CHECK(binding);
    CHECK(!GenerateHandPose(valid.parents,binding->referenceWorld,valid.reference,binding->pose,valid.reference[1],nanTargets));
    return 0;
}

Fixture RightFixture(){
    Fixture f;
    for(auto& name:f.names)if(name.starts_with("LeftHand"))name.replace(0,8,"RightHand");
    for(unsigned n=0;n+1<f.reference.size();++n){
        // Reflect the anatomy while keeping each frame proper (C*M*C).
        for(unsigned k=0;k<4;++k){f.reference[n].values[2][k]=-f.reference[n].values[2][k];f.reference[n].values[k][2]=-f.reference[n].values[k][2];}
        f.inverse[n]=*InverseAnimatedTransform(f.reference[n]);
    }
    return f;
}
int RightAnatomyAndFutureFree(){
    auto f=RightFixture();
    const auto right=bc2_hand_detail::Derive(f.names,f.parents,f.inverse,1,true);CHECK(right&&right->rightHand);
    CHECK(!f.Derive()); // Side must be explicit; right anatomy cannot enter left calibration.
    CHECK(!BindRightHandPose(f.names,f.parents,f.inverse,1)); // Unknown synthetic fingerprint stays rejected.
    CHECK(Close(bc2_hand_detail::Vector({0,0,-1},right->wristToGrip),{0,-1,0}));
    CHECK(Close(bc2_hand_detail::Vector({1,0,0},right->wristToGrip),{0,0,1}));
    CHECK(Close(bc2_hand_detail::Vector({0,-1,0},right->wristToGrip),{-1,0,0}));
    const auto targets=RightHandTargets(HandPoseRole::Free,.65f,.25f);
    CHECK(Near(targets.curl[0],.65f)&&Near(targets.curl[1],.25f)&&Near(targets.curl[4],.65f));
    const auto free=GenerateHandPose(f.parents,right->referenceWorld,f.reference,right->pose,f.reference[1],targets);CHECK(free);
    const auto indexSecond=std::find_if(free->writes.begin(),free->writes.end(),[](const auto& w){return w.index==6;});
    CHECK(indexSecond!=free->writes.end()&&indexSecond->transform.values[3][1]<f.reference[6].values[3][1]);
    Fixture left;const auto acceptedLeft=left.Derive();CHECK(acceptedLeft&&!acceptedLeft->rightHand);
    const auto explicitLeft=bc2_hand_detail::Derive(left.names,left.parents,left.inverse,1,false);CHECK(explicitLeft);
    CHECK(acceptedLeft->wristToGrip.values==explicitLeft->wristToGrip.values);
    for(unsigned n=0;n<left.names.size();++n)CHECK(acceptedLeft->referenceWorld[n].values==explicitLeft->referenceWorld[n].values);
    for(unsigned finger=0;finger<5;++finger)for(unsigned joint=0;joint<3;++joint){
        const auto a=acceptedLeft->pose.fingers[finger].joints[joint].curlAxisLocal;
        const auto b=explicitLeft->pose.fingers[finger].joints[joint].curlAxisLocal;
        CHECK(a.x==b.x&&a.y==b.y&&a.z==b.z);
    }
    return 0;
}
int HeldRightTriggerIsolation(){
    auto f=RightFixture();const auto binding=bc2_hand_detail::Derive(f.names,f.parents,f.inverse,1,true);CHECK(binding);
    const auto zero=PoseRightTrigger(f.parents,f.reference,*binding,0);CHECK(zero&&zero->writes.empty());
    const auto half=PoseRightTrigger(f.parents,f.reference,*binding,.5f);
    const auto full=PoseRightTrigger(f.parents,f.reference,*binding,1);
    CHECK(half&&full&&half->writes.size()==3&&full->writes.size()==3);
    auto output=f.reference;
    for(const auto& write:full->writes){CHECK(write.index>=5&&write.index<=7);output[write.index]=write.transform;}
    for(unsigned n=0;n<f.reference.size();++n)if(n<5||n>7)CHECK(output[n].values==f.reference[n].values);
    CHECK(output[6].values[3][1]<f.reference[6].values[3][1]);
    CHECK(!Close(half->writes[1].transform,full->writes[1].transform));
    for(unsigned repeat=0;repeat<100;++repeat){
        const auto again=PoseRightTrigger(f.parents,f.reference,*binding,1);CHECK(again);
        for(unsigned n=0;n<3;++n)CHECK(again->writes[n].transform.values==full->writes[n].transform.values);
    }
    CHECK(PoseRightTrigger(f.parents,f.reference,*binding,0)->writes.empty());
    Fixture left;const auto leftBinding=left.Derive();CHECK(leftBinding);
    CHECK(!PoseRightTrigger(left.parents,left.reference,*leftBinding,.5f));
    CHECK(!PoseRightTrigger(f.parents,f.reference,*binding,-.1f));
    CHECK(!PoseRightTrigger(f.parents,f.reference,*binding,1.1f));
    CHECK(!PoseRightTrigger(f.parents,f.reference,*binding,std::numeric_limits<float>::quiet_NaN()));
    auto bad=*binding;bad.pose.fingers[1].joints[1].index=bad.pose.fingers[2].joints[1].index;
    CHECK(!PoseRightTrigger(f.parents,f.reference,bad,.5f));
    return 0;
}
}
int main(){if(SightContactAnatomicalCalibration()||AnatomyAndPose()||TargetsAndFrameInvariance()||BindingRejections()||RightAnatomyAndFutureFree()||HeldRightTriggerIsolation())return 1;return 0;}
