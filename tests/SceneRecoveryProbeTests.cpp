#include "SceneRecoveryProbe.h"
#include "TriggerInputSmoke.h"
#include "PlayerGrenadeReceiver.h"
#include "BodyInspectionProbe.h"
#include "Bc2KnifeMotionGeometry.h"
#include "fvr/interaction/TrackedRig.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <sstream>

namespace {
using namespace fvr;
constexpr math::Quaternion ForwardKnifeRotation{.0928197848f,.531863553f,0,.841727776f};
bool SameRotation(math::Quaternion a,math::Quaternion b){return a.x==b.x&&a.y==b.y&&a.z==b.z&&a.w==b.w;}
math::Matrix4 Identity(){math::Matrix4 out;for(unsigned n=0;n<4;++n)out.values[n][n]=1;return out;}
math::Matrix4 RecordedKnifeCalibration(){
    // Retained neutral direction42703 generations565/569/575. Fixture constants
    // test production geometry only; a new native run must measure its own A.
    math::Matrix4 out;out.values={{{-.958921465f,-.281519055f,-.0348678379f,0},
        {-.190171635f,.729188383f,-.657357431f,0},{.210483820f,-.623723745f,-.752771072f,0},{0,0,0,1}}};return out;
}
interaction::InputFrame RotationInput(probe::TriggerInputSmoke& fixture,probe::PlayerGrenadeReceiver& menu,
    unsigned elapsed,unsigned fault=0){
    const auto now=1000000000ll+std::int64_t(elapsed)*1000000;
    runtime::TrackingFrame t{};t.spaceGeneration=33;t.predictedNs=now;t.focused=t.headValid=true;
    auto input=fixture.Input(t,elapsed+1,elapsed);input.trackingBasisGeneration=4;
    auto read=ipc::ChannelResult::Ok;
    ipc::MenuState state{elapsed+1,5,now+100000000,ipc::MenuMode::Gameplay,1280,720,1,0};
    if(fault==1)state.mode=ipc::MenuMode::Menu;
    if(fault==2)read=ipc::ChannelResult::Closed;
    if(fault==3)state.epoch=6;
    if(fault==4)input.trackingBasisGeneration=9;
    if(fault==5)++input.spaceGeneration;
    if(fault==6)input.hands[1].gripTracked=false;
    if(fault==7)input.focused=false;
    menu.Decorate(input,0,read,state,now,1000000000);
    fixture.GateMenu(input,elapsed,now);return input;
}
void MeasuredRotationAndRealGesture(bool approach,bool prepared=false){
    using namespace interaction;
    probe::TriggerInputSmoke fixture;assert(fixture.Motion({0,0,-1}));
    assert(fixture.MotionRotation(ForwardKnifeRotation));
    if(approach)assert(fixture.Approach(1000,0,.4f)&&fixture.ApproachCrouch());
    if(prepared)assert(fixture.ApproachPrepared());
    assert(fixture.JournalEveryInput());
    probe::PlayerGrenadeReceiver menu;TrackedRig rig;HandInteraction hands;MeleeStroke stroke;ControllerActions actions;
    const auto calibration=RecordedKnifeCalibration(),identity=Identity();
    const auto weapon=Multiply(*InverseRigid(bc2::private_knife::RightHandInKnife),calibration);
    const TrackedRigOwner rigOwner{700,1,42,3,2};
    const AuthoredGripAttachment attachment{bc2::private_knife::RightHandInKnife};
    MeleePoint previousTip{},strokeStart{};unsigned intents=0;float maximumPreparationSpeed=0;
    for(unsigned ms=0;ms<fixture.Duration();ms+=5){
        const auto input=RotationInput(fixture,menu,ms);const auto now=input.predictedNs;
        const auto local=fixture.MotionSequenceMs(ms);
        assert(!fixture.Failed()&&GameplayOwnsInput(input.menu,now));
        assert(input.hands[1].trigger==0&&input.hands[1].squeeze==0&&SameRotation(input.hands[1].grip.orientation,input.hands[1].aim.orientation));
        assert(SameRotation(input.hands[0].grip.orientation,{}));
        if((!prepared&&ms<fixture.AttackStartMs())||local<=500)assert(SameRotation(input.hands[1].grip.orientation,{}));
        if(local>=1500)assert(SameRotation(input.hands[1].grip.orientation,fixture.MotionOrientation(1500)));
        if(prepared){
            const bool moving=ms>=fixture.MovementStartMs()&&ms<fixture.MovementStartMs()+fixture.ApproachMs();
            const auto mapped=actions.Update(input,{700,1,true,true},now);
            assert((mapped.forward>0)==moving&&bool(mapped.held&Crouch)==moving&&!mapped.turnDegrees&&
                !(mapped.held&(Fire|Use|Menu|Reload|Jump|Sprint|NextWeapon|PreviousWeapon)));
            if(ms>=2000&&ms<fixture.AttackStartMs())assert(input.hands[1].grip.position.z==-.20f);
        }
        // Use the actual initial wrist calibration, tracked rig and geometry
        // functions; the expected forward direction is not another quaternion
        // implementation in the test.
        const auto tracked=rig.Update(rigOwner,input,identity,identity,calibration,weapon,{}, {}, {}, {},attachment);
        assert(tracked&&tracked->weaponAttachmentAuthored&&!tracked->weaponAttachmentPending);
        const auto measured=bc2::private_knife::KnifeWristCalibration(input,identity,tracked->right);assert(measured);
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)assert(std::abs(measured->values[r][c]-calibration.values[r][c])<.00001f);
        const auto geometry=bc2::private_knife::KnifeMotionFromGrip(input.hands[1].grip,*measured);assert(geometry);
        if(local<=500)assert(std::abs(geometry->direction.x-.895368651f)<.00001f);
        if(local>=1500)assert(std::abs(geometry->direction.x)<.00001f&&std::abs(geometry->direction.y)<.00001f&&std::abs(geometry->direction.z-1)<.00001f);
        if(local>500&&local<=1500)maximumPreparationSpeed=(std::max)(maximumPreparationSpeed,
            std::hypot(geometry->tip.x-previousTip.x,geometry->tip.y-previousTip.y,geometry->tip.z-previousTip.z)/.005f);
        previousTip=geometry->tip;
        if(local==2000)strokeStart=geometry->tip;
        if(local==2120)assert(std::abs(geometry->tip.x-strokeStart.x)<.00001f&&std::abs(geometry->tip.y-strokeStart.y)<.00001f&&
            std::abs(geometry->tip.z-strokeStart.z-.12f)<.00001f);
        MeleeStrokeSample sample{};
        sample.source={{700,1,2,33},input.generation,now,now+100000000,now,input.focused,
            {input.hands[0].gripTracked,input.hands[1].gripTracked},{false,false}};
        hands.Update(sample.source);const HandContactProof contact{{11,1},input.generation,sample.source.deadlineNs,true};
        const auto held=hands.Current(InteractionHand::Right);
        const auto claim=held?hands.Renew(sample.source,held->token,contact):hands.Acquire(sample.source,
            {sample.source.owner,InteractionHand::Right,HandClaimKind::GunHold,{42,1},contact,1,0});
        assert(claim.claim);sample.hold=claim.claim->token;sample.tip=geometry->tip;sample.direction=geometry->direction;
        // Explicit fixture readiness for gesture testing, never a native miss,
        // completion, target, or damage receipt.
        sample.attackAvailable=true;const auto result=stroke.Update(sample,hands);
        if(result.intent){++intents;assert((local>=2000&&local<=2120)||(local>=5200&&local<=5320));}
        if(local<2000||(prepared&&ms<fixture.AttackStartMs()))assert(intents==0);
        if(local==1500){
            std::ostringstream journal;journal.precision(9);probe::WriteNeutralSceneInput(journal,input,ms);
            const auto text=journal.str();
            for(const auto* label:{"\"grip\":[","\"aim\":["}){
                const auto offset=text.rfind(label);assert(offset!=std::string::npos);
                const auto first=offset+std::string_view(label).size();const auto last=text.find(']',first);assert(last!=std::string::npos);
                auto values=text.substr(first,last-first);std::replace(values.begin(),values.end(),',',' ');
                std::istringstream parsed(values);std::array<float,7> pose{};for(auto& value:pose)assert(bool(parsed>>value));
                assert(SameRotation({pose[3],pose[4],pose[5],pose[6]},input.hands[1].grip.orientation));
            }
        }
        fixture.Published(input,ms,now,now+1000,true,false);
    }
    assert(intents==2&&maximumPreparationSpeed>.25f&&maximumPreparationSpeed<.27f&&
        maximumPreparationSpeed<MeleeStrokeConfig{}.minimumSpeed&&fixture.Complete(fixture.Duration(),true));
    std::ostringstream report;fixture.Report(report,fixture.Duration(),true);
    assert(report.str().find("\"rotation_window_ms\":[500,1500]")!=std::string::npos&&report.str().find("\"damage_verified\":false")!=std::string::npos);
    if(prepared)assert(fixture.MovementStartMs()==2000&&fixture.AttackStartMs()==3500&&fixture.Duration()==9500&&
        report.str().find("\"scheduled_stroke_windows_ms\":[[3500,3620],[6700,6820]]")!=std::string::npos&&
        report.str().find("\"preparation_samples\":[100,200,100]")!=std::string::npos);
    // Starting with the final quaternion makes the actual TrackedRig learn its
    // inverse. This negative control would catch an always-rotated fixture.
    TrackedRig wrongRig;probe::TriggerInputSmoke neutral;probe::PlayerGrenadeReceiver neutralMenu;
    auto input=RotationInput(neutral,neutralMenu,0);input.hands[1].grip.orientation=input.hands[1].aim.orientation=ForwardKnifeRotation;
    const auto wrong=wrongRig.Update(rigOwner,input,identity,identity,calibration,weapon,{}, {}, {}, {},attachment);assert(wrong);
    const auto learned=bc2::private_knife::KnifeWristCalibration(input,identity,wrong->right);assert(learned);
    const auto stillDiagonal=bc2::private_knife::KnifeMotionFromGrip(input.hands[1].grip,*learned);assert(stillDiagonal&&stillDiagonal->direction.x>.89f);
}
void RotationBoundsAndCancellation(){
    using probe::TriggerInputSmoke;
    assert(probe::TriggerInputSmokeOption(L"--selected-knife-motion-rotation"));
    assert(!probe::NeutralSceneOption(L"--selected-knife-motion-rotation")&&!probe::SelectedKnifeNeutralOption(L"--selected-knife-motion-rotation"));
    TriggerInputSmoke bounds;assert(!bounds.MotionRotation(ForwardKnifeRotation)&&bounds.Motion({0,0,-1}));
    for(const auto bad:{math::Quaternion{0,0,0,0},math::Quaternion{0,1,0,0},math::Quaternion{0,0,0,-1},
        math::Quaternion{0,0,0,2},math::Quaternion{std::numeric_limits<float>::quiet_NaN(),0,0,1}})assert(!bounds.MotionRotation(bad));
    assert(bounds.MotionRotation(ForwardKnifeRotation)&&!bounds.MotionRotation(ForwardKnifeRotation));
    for(unsigned fault=1;fault<=7;++fault){
        TriggerInputSmoke fixture;assert(fixture.Motion({0,0,-1})&&fixture.MotionRotation(ForwardKnifeRotation));
        probe::PlayerGrenadeReceiver menu;
        for(unsigned ms=0;ms<=2200;ms+=10){auto input=RotationInput(fixture,menu,ms,ms==1000?fault:0);
            if(ms>=1000)assert(fixture.Failed()&&!input.hands[1].gripTracked&&!input.hands[1].aimTracked&&probe::NeutralSceneActions(input));
            fixture.Published(input,ms,input.predictedNs,input.predictedNs+1000,true,false);
        }
        assert(!fixture.Complete(8000,true));
    }
    for(unsigned fault=0;fault<3;++fault){
        TriggerInputSmoke fixture;assert(fixture.Motion({0,0,-1})&&fixture.MotionRotation(ForwardKnifeRotation));
        probe::PlayerGrenadeReceiver menu;auto first=RotationInput(fixture,menu,0);fixture.Published(first,0,1000000000,1000001000,true,false);
        auto changed=RotationInput(fixture,menu,1500);
        if(fault==0)changed.hands[1].grip.orientation.y+=.01f;
        if(fault==1)changed.hands[1].aim.orientation.w+=.01f;
        if(fault==2)changed.hands[1].aim.position.x+=.01f;
        fixture.Published(changed,1500,2500000000,2500001000,true,false);assert(fixture.Failed());
    }
    TriggerInputSmoke late;assert(late.Motion({0,0,-1})&&late.MotionRotation(ForwardKnifeRotation));
    probe::PlayerGrenadeReceiver lateMenu;auto input=RotationInput(late,lateMenu,700);
    late.Published(input,700,1700000000,1700001000,true,false);assert(late.Failed());
    TriggerInputSmoke old;assert(old.Motion({0,0,-1})&&!old.JournalEveryInput());
    probe::PlayerGrenadeReceiver oldMenu;auto first=RotationInput(old,oldMenu,0);old.Published(first,0,1000000000,1000001000,true,false);
    assert(!old.MotionRotation(ForwardKnifeRotation));std::ostringstream report;old.Report(report,0,false);
    assert(report.str().find("motion_rotation_xyzw")==std::string::npos);
}
void TurnPrefixAndReviewImages(){
    using namespace interaction;using probe::TriggerInputSmoke;
    assert(probe::TriggerInputSmokeOption(L"--selected-knife-approach-turn")&&
        probe::TriggerInputSmokeOption(L"--selected-knife-review-images"));
    assert(!probe::SelectedKnifeNeutralOption(L"--selected-knife-approach-turn")&&
        !probe::NeutralSceneOption(L"--selected-knife-review-images"));
    TriggerInputSmoke bounds;assert(!bounds.ApproachTurn(1)&&!bounds.EnableReviewImages());
    assert(bounds.Approach(1000,0,.5f)&&!bounds.ApproachTurn(0)&&!bounds.ApproachTurn(7)&&!bounds.ApproachTurn(-7));
    assert(bounds.ApproachTurn(-6)&&!bounds.ApproachTurn(1)&&bounds.EnableReviewImages()&&!bounds.EnableReviewImages());
    assert(bounds.MovementStartMs()==3500&&bounds.AttackStartMs()==5000&&bounds.Duration()==10000);
    assert(probe::TriggerInputSmokeArguments(true,true,true,320,true,true,false,true,true));
    assert(!probe::TriggerInputSmokeArguments(true,true,true,320,true,true,true,true,true)&&
        !probe::TriggerInputSmokeArguments(true,true,true,320,true,true,false,false,true));
    const auto run=[](int count,bool motion,unsigned fault){
        TriggerInputSmoke fixture;assert(fixture.Approach(1000,.3f,.4f)&&fixture.ApproachCrouch()&&fixture.ApproachTurn(count));
        assert(fixture.EnableReviewImages());
        if(motion)assert(fixture.Motion({0,0,-1})&&fixture.MotionRotation(ForwardKnifeRotation));
        const auto movement=fixture.MovementStartMs(),attack=fixture.AttackStartMs();
        probe::PlayerGrenadeReceiver menu;ControllerActions actions;const InputOwner owner{700,1,true,true};
        unsigned turns=0,fireEdges=0,reviews=0;float totalDegrees=0;bool heldFire=false;
        for(unsigned ms=0;ms<fixture.Duration();ms+=10){
            const unsigned inputFault=ms==600?(fault<=5?fault:0):0;
            auto input=RotationInput(fixture,menu,ms,inputFault);const auto now=input.predictedNs;
            const bool delivered=!((fault==6&&ms>=500&&ms<650)||(fault==7&&ms>=650&&ms<1000));
            if(fault>=1&&fault<=5&&ms>=600)assert(fixture.Failed()&&probe::NeutralSceneActions(input));
            if((fault==6||fault==7)&&ms>=movement)assert(fixture.Failed()&&probe::NeutralSceneActions(input));
            const bool turning=input.hands[1].stickX!=0,moving=input.hands[0].stickY!=0;
            assert(!(turning&&moving)&&!((turning||moving)&&input.hands[1].trigger));
            assert(!(input.hands[0].held&(Primary|MenuClick|StickClick))&&input.hands[1].held==0);
            if(ms<attack)assert(input.hands[1].trigger==0&&SameRotation(input.hands[1].grip.orientation,{}));
            if(delivered){const auto mapped=actions.Update(input,owner,now);
                assert(!(mapped.held&(Use|Menu|Reload|Jump|Sprint|NextWeapon|PreviousWeapon)));
                if(mapped.turnDegrees){++turns;totalDegrees+=mapped.turnDegrees;assert(mapped.turnDegrees==(count<0?-30.f:30.f));
                    assert(turning&&!mapped.forward&&!mapped.strafe&&!mapped.held&&ms>=500&&ms<movement);}
                const bool fire=(mapped.held&Fire)!=0;if(fire&&!heldFire)++fireEdges;heldFire=fire;
            }
            fixture.Published(input,ms,now,now+1000,delivered,!delivered);
            assert(!fixture.ApproachTurn(1));
            // Arriving after a boundary is insufficient: an old, future or
            // wrong-space stereo generation cannot become a review image.
            assert(fixture.ReviewPair(input.generation+1,input.spaceGeneration)==0);
            assert(fixture.ReviewPair(input.generation,input.spaceGeneration+1)==0);
            if(ms==movement+1000||ms==fixture.ReviewFinalMs())
                assert(fixture.ReviewPair(input.generation-1,input.spaceGeneration)==0);
            const auto stage=fixture.ReviewPair(input.generation,input.spaceGeneration);
            if(stage){++reviews;assert(stage==reviews&&reviews<=2);
                assert(ms==(stage==1?movement+1000:fixture.ReviewFinalMs()));
                assert(fixture.ReviewPair(input.generation,input.spaceGeneration)==0);}
        }
        if(!fault){assert(fixture.Complete(fixture.Duration(),true)&&turns==unsigned(std::abs(count))&&
            totalDegrees==30.f*count&&fireEdges==(motion?0u:1u)&&reviews==2&&fixture.ReviewPairs()==2);}
        else assert(!fixture.Complete(fixture.Duration(),true)&&fireEdges==0&&reviews==0);
        std::ostringstream report;fixture.Report(report,fixture.Duration(),true);
        assert(report.str().find("\"approach_turn_snaps\":"+std::to_string(count))!=std::string::npos&&
            report.str().find("\"review_max_pairs\":2")!=std::string::npos);
    };
    for(int count=-6;count<=6;++count)if(count)run(count,false,0);
    run(4,true,0);for(unsigned fault=1;fault<=7;++fault)run(2,false,fault);
    // Missing or wrong-sign delivered pulse never authorizes movement.
    for(unsigned fault=0;fault<3;++fault){
        TriggerInputSmoke fixture;assert(fixture.Approach(1000,0,.5f)&&fixture.ApproachTurn(1));probe::PlayerGrenadeReceiver menu;
        auto first=RotationInput(fixture,menu,490);fixture.Published(first,490,first.predictedNs,first.predictedNs+1000,true,false);
        auto changed=RotationInput(fixture,menu,500);
        if(fault==0)changed.hands[1].stickX=0;
        if(fault==1)changed.hands[1].stickX=-.9f;
        if(fault==2)changed.hands[0].held=Primary;
        fixture.Published(changed,500,changed.predictedNs,changed.predictedNs+1000,true,false);
        assert(fixture.Failed()&&fixture.Reason()==TriggerInputSmoke::Failure::UnexpectedInput);
    }
    TriggerInputSmoke old;assert(old.Approach(1000,0,.5f)&&old.MovementStartMs()==500&&old.AttackStartMs()==2000);
    assert(old.ReviewPair(123,33)==0);std::ostringstream legacy;old.Report(legacy,0,false);
    assert(legacy.str().find("approach_turn_snaps")==std::string::npos&&legacy.str().find("review_images")==std::string::npos);
}
void ApproachResumeRegressions(){
    using namespace interaction;using probe::TriggerInputSmoke;
    assert(probe::TriggerInputSmokeOption(L"--selected-knife-approach-resume")&&
        !probe::SelectedKnifeNeutralOption(L"--selected-knife-approach-resume"));
    TriggerInputSmoke bounds;assert(!bounds.ApproachResume()&&bounds.Approach(1000,0,.4f)&&bounds.ApproachResume());
    assert(!bounds.ApproachResume()&&!bounds.ApproachTurn(1)&&bounds.MaximumWallDurationMs()==9500);
    TriggerInputSmoke turn;assert(turn.Approach(1000,0,.4f)&&turn.ApproachTurn(1)&&!turn.ApproachResume());
    const auto number=[](const std::string& text,const char* key){
        const auto at=text.find(std::string("\"")+key+"\":");assert(at!=std::string::npos);
        return std::stoull(text.substr(at+std::string_view(key).size()+3));
    };
    const auto run=[&](unsigned fault,bool motion){
        TriggerInputSmoke fixture;assert(fixture.Approach(1000,.3f,.4f)&&fixture.ApproachCrouch());
        if(fault!=16)assert(fixture.ApproachResume()); // Default remains terminal.
        if(motion)assert(fixture.Motion({0,0,-1})&&fixture.MotionRotation(ForwardKnifeRotation));
        assert(fixture.EnableReviewImages());
        probe::PlayerGrenadeReceiver menu;ControllerActions actions;const InputOwner owner{700,1,true,true};
        HandInteraction hands;MeleeStroke stroke;unsigned intents=0,fireEdges=0,reviews=0,movementSegments=0;
        bool previousFire=false,previousMovement=false;unsigned wall=0;std::uint64_t lastRecipe=0;
        for(;fixture.ContinueInput(wall);wall+=10){
            // A slow successful IPC call can break delivered-neutral coverage
            // even when its start is less than100ms after the prior completion.
            if(fault==12&&((wall>1200&&wall<1290)||(wall>1290&&wall<1320)))continue;
            const auto now=1000000000ll+std::int64_t(wall)*1000000;
            runtime::TrackingFrame t{};t.spaceGeneration=33;t.predictedNs=now;t.focused=t.headValid=true;
            auto input=fixture.Input(t,wall+1,wall);input.trackingBasisGeneration=4;
            auto read=ipc::ChannelResult::Ok;
            ipc::MenuState state{wall+1,5,now+100000000,ipc::MenuMode::Gameplay,1280,720,1,0};
            // Reproduce a live pump gap using the real decorator: Busy
            // retains the original lease; Timeout never invents a fresh one.
            const bool firstGap=fault!=13&&fault!=14&&fault!=20;
            if(firstGap&&wall>=700&&wall<1100)read=wall<890?ipc::ChannelResult::Busy:ipc::ChannelResult::Timeout;
            if(fault==3&&wall>=700)read=wall<790?ipc::ChannelResult::Busy:ipc::ChannelResult::Timeout;
            if(fault==11&&wall>=1900&&wall<2200)read=ipc::ChannelResult::Busy;
            if(fault==13&&wall>=2200&&wall<2500)read=ipc::ChannelResult::Busy;
            if(fault==14&&wall>=200&&wall<500)read=ipc::ChannelResult::Busy;
            if(wall==1200){
                if(fault==4)state.mode=ipc::MenuMode::Menu;
                if(fault==5)++state.epoch;
                if(fault==6)++input.trackingBasisGeneration;
                if(fault==7)++input.spaceGeneration;
                if(fault==8)input.hands[1].gripTracked=false;
                if(fault==9)read=ipc::ChannelResult::Closed;
                if(fault==10)read=ipc::ChannelResult::Invalid;
                if(fault==19)state.sequence=0;
            }
            if((fault==2&&wall==1400)||(fault==20&&wall==600))state.deadlineQpc=now+500;
            menu.Decorate(input,0,read,state,now,1000000000);
            fixture.GateMenu(input,wall,now,read);
            const auto recipe=fixture.RecipeMs(wall);assert(recipe>=lastRecipe);lastRecipe=recipe;
            if(fixture.Paused()&&!fixture.Failed()){
                assert(probe::NeutralSceneActions(input)&&SameRotation(input.hands[1].grip.orientation,{}));
                assert(input.hands[1].grip.position.z==-.2f&&input.hands[1].aim.position.z==-.2f);
                if(!GameplayOwnsInput(input.menu,now))assert(input.menu.owner==InputMenuOwner::Unknown);
            }
            if(fault==17&&wall==1200)input.hands[1].grip.position.z=-.21f;
            const bool busy=(fault==1&&wall==1400)||(fault==15&&wall>=790&&wall<1000);
            const bool hardFailure=fault==18&&wall==1200;
            const bool delivered=!busy&&!hardFailure;
            if(delivered){
                const auto mapped=actions.Update(input,owner,now);
                assert(!mapped.turnDegrees&&!(mapped.held&(Use|Menu|Reload|Jump|Sprint|NextWeapon|PreviousWeapon)));
                const bool moving=mapped.forward!=0||mapped.strafe!=0;
                assert(bool(mapped.held&Crouch)==moving);
                if(moving&&!previousMovement)++movementSegments;previousMovement=moving;
                const bool fire=bool(mapped.held&Fire);if(fire&&!previousFire)++fireEdges;previousFire=fire;
                if(fixture.Paused())assert(!moving&&!mapped.held);
            }
            // Join actual geometry and the physical stroke detector in the
            // successful rotated case. Readiness is a fixture input only.
            if(motion&&delivered&&!fixture.Failed()){
                const auto geometry=bc2::private_knife::KnifeMotionFromGrip(input.hands[1].grip,RecordedKnifeCalibration());assert(geometry);
                MeleeStrokeSample sample{};sample.source={{700,1,2,33},input.generation,now,now+100000000,now,true,{true,true},{false,false}};
                hands.Update(sample.source);const HandContactProof contact{{11,1},input.generation,sample.source.deadlineNs,true};
                const auto held=hands.Current(InteractionHand::Right);
                const auto claim=held?hands.Renew(sample.source,held->token,contact):hands.Acquire(sample.source,
                    {sample.source.owner,InteractionHand::Right,HandClaimKind::GunHold,{42,1},contact,1,0});assert(claim.claim);
                sample.hold=claim.claim->token;sample.tip=geometry->tip;sample.direction=geometry->direction;sample.attackAvailable=true;
                const auto result=stroke.Update(sample,hands);
                if(result.intent){++intents;assert(recipe>=fixture.AttackStartMs()+2000);}
                if(recipe<fixture.AttackStartMs()+2000)assert(!intents);
            }
            const auto after=now+(fault==12&&wall==1290?20000000:1000);
            fixture.Published(input,wall,now,after,delivered,busy);
            if(fault==12&&wall==1600)assert(fixture.Paused());
            const auto review=fixture.ReviewPair(input.generation,input.spaceGeneration);
            if(review){++reviews;assert(review==reviews);assert(recipe>=(review==1?1500:fixture.ReviewFinalMs()));}
            assert(!fixture.ApproachResume());
        }
        const bool expected=fault<=2||fault==12;
        assert(fixture.Complete(wall,fixture.DurationEnded(wall))==expected);
        if(expected){assert(movementSegments==2&&reviews==2&&fireEdges==(motion?0u:1u));
            assert(wall==fixture.Duration()&&fixture.RecipeMs(wall)==fixture.AttackStartMs()+(motion?8000:5000));
            if(motion)assert(intents==2);
        }else assert(fixture.Failed()&&fireEdges==0&&intents==0);
        std::ostringstream out;fixture.Report(out,wall,fixture.DurationEnded(wall));const auto report=out.str();
        if(fault!=16){
            assert(number(report,"maximum_pause_ms")==2500&&number(report,"pause_count")<=1);
            if(expected){assert(number(report,"pause_duration_ms")==unsigned(fault==12?1020:fault?1120:810));
                assert(number(report,"max_movement_gap_ns")>800000000&&number(report,"max_movement_segment_gap_ns")<=100000000);
                assert(number(report,"rearm_last_ns")-number(report,"rearm_first_ns")>=500000000);
                if(fault)assert(number(report,"rearm_resets")>=1);
            }
            if(fault==3)assert(wall>=3290&&wall<=3300&&number(report,"pause_count")==1);
            if(fault==13||fault==14||fault==20)assert(number(report,"pause_count")==0);
        }else assert(report.find("approach_resume")==std::string::npos);
    };
    run(0,false);run(0,true);run(1,false);run(2,false);
    for(unsigned fault=3;fault<=20;++fault)run(fault,false);
}
void PreparedApproachGuards(){
    using namespace interaction;using probe::TriggerInputSmoke;
    assert(probe::TriggerInputSmokeOption(L"--selected-knife-approach-prepared")&&
        !probe::SelectedKnifeNeutralOption(L"--selected-knife-approach-prepared"));
    TriggerInputSmoke bounds;assert(!bounds.ApproachPrepared()&&bounds.Approach(1000,0,.4f)&&!bounds.ApproachPrepared());
    assert(bounds.Motion({0,0,-1})&&!bounds.ApproachPrepared()&&bounds.MotionRotation(ForwardKnifeRotation));
    assert(bounds.ApproachPrepared()&&!bounds.ApproachPrepared()&&!bounds.ApproachTurn(1)&&!bounds.ApproachResume());
    for(bool turn:{false,true}){
        TriggerInputSmoke incompatible;assert(incompatible.Approach(1000,0,.4f)&&incompatible.Motion({0,0,-1})&&
            incompatible.MotionRotation(ForwardKnifeRotation));
        assert(turn?incompatible.ApproachTurn(1):incompatible.ApproachResume());assert(!incompatible.ApproachPrepared());
    }
    for(unsigned fault=1;fault<=12;++fault){
        TriggerInputSmoke fixture;assert(fixture.Approach(1000,0,.4f)&&fixture.Motion({0,0,-1})&&
            fixture.MotionRotation(ForwardKnifeRotation)&&fixture.ApproachPrepared());
        probe::PlayerGrenadeReceiver menu;unsigned ms=0;
        for(;ms<fixture.Duration()&&!fixture.Failed();ms+=5){
            if(fault==1&&ms<100)continue; // Short initial calibration is not a complete preparation.
            const unsigned inputFault=ms==2100?(fault==6?2:fault==7?4:fault==8?5:fault==9?6:0):0;
            auto input=RotationInput(fixture,menu,ms,inputFault);const auto now=input.predictedNs;
            if((fault==4&&ms==2100)||(fault==5&&ms==3200)){
                if(fault==4)input.hands[1].grip.orientation={};
                else input.hands[1].aim.position.z-=.01f;
            }
            if(fault==10&&ms==2100)input.menu.deadlineNs=now+500;
            const bool busy=(fault==2&&ms>=700&&ms<1000)||(fault==3&&ms>=1500&&ms<2000)||
                (fault==11&&ms>=2000&&ms<3000)||(fault==12&&ms>=3000&&ms<3020);
            if(fault==12&&ms>=3500&&ms<3520){
                // A late release cannot snap the prepared wrist to identity
                // while the actual release-neutral interval is still pending.
                assert(SameRotation(input.hands[1].grip.orientation,fixture.MotionOrientation(2000))&&
                    input.hands[1].grip.position.z==-.2f&&probe::NeutralSceneActions(input));
            }
            fixture.Published(input,ms,now,now+1000,!busy,busy);
            assert(!fixture.ApproachPrepared());
        }
        if(fault==12)assert(!fixture.Failed()&&fixture.Complete(ms,fixture.DurationEnded(ms)));
        else {
            assert(fixture.Failed()&&!fixture.Complete(fixture.Duration(),true));
            auto cancelled=RotationInput(fixture,menu,ms);
            assert(probe::NeutralSceneActions(cancelled)&&!cancelled.hands[1].gripTracked&&!cancelled.hands[1].aimTracked);
        }
    }
}
}

int main(){
    using namespace fvr;using namespace fvr::probe;
    for(const auto* option:{L"--pairs",L"--request-lifetime",L"--static-pose",L"--async",L"--scene-recovery-probe",
        L"--scene-controls-neutral",L"--controls-observe",L"--capture-poses"})assert(NeutralSceneOption(option));
    for(const auto* option:{L"--controls-fire",L"--controls-pulse",L"--controls-turn",L"--controls-buttons",L"--controls-aim",
        L"--controls-roomscale",L"--controls-hands",L"--controls-hand-independence",L"--controls-tracking-recovery",
        L"--controls-equip",L"--controls-weapon-cycle",L"--controls-weapon-mode",L"--controls-sight-flip",L"--controls-use",
        L"--controls-vehicle",L"--boat-head-aim-probe",L"--boat-fire-probe",L"--controls-shot-probe",L"--controls-death-probe",
        L"--controls-support-grip",L"--controls-support-reload",L"--physical-reload-probe",L"--reload-request-probe",
        L"--weapon-visibility-probe",L"--body-holster-probe",L"--xm8-magazine-probe",L"--magazine-physical-probe",
        L"--asymmetric",L"--fov-sweep",L"--future-action-script"})assert(!NeutralSceneOption(option));
    assert(NeutralSceneArguments(true,true,true,240,true));
    assert(!NeutralSceneArguments(false,true,true,240,true));
    assert(!NeutralSceneArguments(true,false,true,240,true));
    assert(!NeutralSceneArguments(true,true,false,240,true));
    assert(!NeutralSceneArguments(true,true,true,239,true));
    assert(!NeutralSceneArguments(true,true,true,240,false));
    for(const auto* option:{L"--selected-knife-neutral",L"--pairs",L"--request-lifetime",L"--static-pose",L"--async",L"--capture-poses"})
        assert(SelectedKnifeNeutralOption(option));
    for(const auto* option:{L"--scene-controls-neutral",L"--scene-recovery-probe",L"--controls-observe",L"--trigger-input-smoke",
        L"--selected-knife-trigger-smoke",L"--selected-knife-motion-smoke",L"--controls-fire",L"--no-image-files",L"--future-action-script"})
        assert(!SelectedKnifeNeutralOption(option));
    assert(!NeutralSceneOption(L"--selected-knife-neutral"));
    assert(SelectedKnifeNeutralArguments(true,true,true,240,true));
    assert(!SelectedKnifeNeutralArguments(false,true,true,240,true)&&!SelectedKnifeNeutralArguments(true,false,true,240,true)&&
        !SelectedKnifeNeutralArguments(true,true,false,240,true)&&!SelectedKnifeNeutralArguments(true,true,true,239,true)&&
        !SelectedKnifeNeutralArguments(true,true,true,240,false));
    runtime::TrackingFrame tracking{};tracking.spaceGeneration=987;tracking.predictedNs=1234;
    tracking.focused=tracking.headValid=true;tracking.worldUnitsPerMeter=2;
    tracking.referenceHead.position={1,2,3};tracking.head.position={4,5,6};
    for(std::uint64_t sequence:{1ull,55ull,999999ull}){
        const auto input=NeutralSceneInput(tracking,sequence);
        assert(input.generation==sequence&&input.spaceGeneration==987&&input.predictedNs==1234);
        assert(input.focused&&input.headValid&&!input.floorRelative&&input.worldUnitsPerMeter==2);
        assert(input.referenceHead.position.y==2&&input.head.position.z==6&&NeutralSceneActions(input));
        for(const auto& hand:input.hands){assert(hand.gripTracked&&hand.aimTracked&&hand.active==interaction::Components);
            assert(hand.grip.position.x==hand.aim.position.x&&hand.grip.position.y==hand.aim.position.y&&
                   hand.grip.position.z==hand.aim.position.z&&hand.grip.orientation.w==1&&hand.aim.orientation.w==1);}
        assert(input.hands[0].grip.position.x==-.2f&&input.hands[1].grip.position.z==-.20f);
    }
    for(unsigned hand=0;hand<2;++hand)for(unsigned mutation=0;mutation<8;++mutation){
        auto input=NeutralSceneInput(tracking,1);auto& h=input.hands[hand];
        switch(mutation){case 0:h.held=interaction::Primary;break;case 1:h.touched=1;break;
        case 2:h.touchActive=1;break;case 3:h.stickX=.1f;break;case 4:h.stickY=-.1f;break;
        case 5:h.trigger=.1f;break;case 6:h.squeeze=.1f;break;case 7:h.trigger=std::numeric_limits<float>::quiet_NaN();break;}
        assert(!NeutralSceneActions(input));
    }
    tracking.focused=tracking.headValid=false;const auto absent=NeutralSceneInput(tracking,3);
    assert(!absent.focused&&!absent.headValid&&NeutralSceneActions(absent));
    std::ostringstream out;WriteNeutralSceneInput(out,NeutralSceneInput(tracking,3),4000);
    assert(out.str().find("\"tick_ms\":4000")!=std::string::npos&&out.str().find("\"generation\":3")!=std::string::npos);
    assert(out.str().find("\"axes\":[0,0,0,0]")!=std::string::npos&&out.str().back()=='\n');
    auto owned=NeutralSceneInput(tracking,4);owned.trackingBasisGeneration=7;
    owned.menu={interaction::InputMenuOwner::Gameplay,12,3,456789};
    std::ostringstream ownedOut;WriteNeutralSceneInput(ownedOut,owned,4001);
    assert(ownedOut.str().find("\"tracking_basis\":7")!=std::string::npos&&
        ownedOut.str().find("\"menu\":{\"owner\":1,\"sequence\":12,\"epoch\":3,\"deadline_ns\":456789}")!=std::string::npos);
    for(const math::Vec3 contact: {math::Vec3{-.2f,-.12f,.18f},math::Vec3{.2f,-.12f,.18f},math::Vec3{.22f,-.65f,0.f}}){
        unsigned edges=0;float previous=0;math::Vec3 last{.2f,-.25f,-.45f};
        for(unsigned ms=0;ms<14000;++ms){
            auto input=NeutralSceneInput(tracking,100+ms);
            assert(BodyInspectionInput(input,contact,ms)&&BodyInspectionActions(input));
            assert(input.generation==100+ms&&input.spaceGeneration==987&&input.worldUnitsPerMeter==2);
            const auto& hand=input.hands[1];const auto p=hand.grip.position;
            assert(std::abs(p.x-last.x)<.003f&&std::abs(p.y-last.y)<.003f&&std::abs(p.z-last.z)<.003f);
            assert(p.x==hand.aim.position.x&&p.y==hand.aim.position.y&&p.z==hand.aim.position.z);
            if(hand.squeeze>previous){++edges;assert(p.x==contact.x&&p.y==contact.y&&p.z==contact.z);}
            if(ms>=4000)assert(p.x==.2f&&p.y==-.25f&&p.z==-.45f&&hand.squeeze==1);
            previous=hand.squeeze;last=p;
            for(unsigned h=0;h<2;++h){auto bad=input;bad.hands[h].trigger=.1f;assert(!BodyInspectionActions(bad));}
        }
        assert(edges==1);
    }
    for(float bad:{1.01f,-1.01f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
        for(unsigned axis=0;axis<3;++axis){math::Vec3 p{};if(axis==0)p.x=bad;else if(axis==1)p.y=bad;else p.z=bad;
            auto input=NeutralSceneInput(tracking,5);input.hands[1].trigger=.4f;
            assert(!ValidBodyInspectionContact(p)&&!BodyInspectionInput(input,p,2000)&&input.hands[1].trigger==.4f);}
    }
    // Independent of pair target: five seconds, one80ms pulse, and a real
    // neutral tail. Successful IPC publication is not native consumption.
    for(const auto* option:{L"--trigger-input-smoke",L"--selected-knife-trigger-smoke",L"--selected-knife-motion-smoke",L"--no-image-files",L"--pairs",L"--static-pose",L"--async",L"--request-lifetime",L"--capture-poses"})
        assert(TriggerInputSmokeOption(option));
    for(const auto* option:{L"--controls-fire",L"--controls-hands",L"--controls-observe",L"--normal-knife-held-close",
        L"--scene-controls-neutral",L"--selected-knife-neutral",L"--body-props-cache",L"--future-action-script"})assert(!TriggerInputSmokeOption(option));
    assert(TriggerInputSmokeArguments(true,true,true,8,true)&&TriggerInputSmokeArguments(true,true,true,240,true));
    assert(!TriggerInputSmokeArguments(false,true,true,240,true)&&!TriggerInputSmokeArguments(true,false,true,240,true)&&
        !TriggerInputSmokeArguments(true,true,false,240,true)&&!TriggerInputSmokeArguments(true,true,true,240,false)&&
        !TriggerInputSmokeArguments(true,true,true,7,true)&&!TriggerInputSmokeArguments(true,true,true,241,true));
    tracking.focused=tracking.headValid=true;
    const auto sample=[&](TriggerInputSmoke& smoke,std::uint64_t ms,std::uint64_t seq,bool delivered=true,bool busy=false){
        const auto input=smoke.Input(tracking,seq,ms);
        assert(input.hands[0].grip.position.x==-.2f&&input.hands[0].grip.position.y==-.25f&&input.hands[0].grip.position.z==-.45f);
        assert(input.hands[1].grip.position.x==.15f&&input.hands[1].grip.position.y==-.10f&&input.hands[1].grip.position.z==-.20f);
        for(const auto& hand:input.hands)assert(hand.aim.position.x==hand.grip.position.x&&hand.aim.position.z==hand.grip.position.z&&
            hand.squeeze==0&&hand.stickX==0&&hand.stickY==0&&hand.held==0&&hand.touched==0);
        const auto before=1000000000+std::int64_t(ms)*1000000;
        return smoke.Published(input,ms,before,before+1000,delivered,busy);
    };
    using Receipt=TriggerInputSmoke::Receipt;
    TriggerInputSmoke smoke;
    assert(sample(smoke,0,1)==Receipt::Neutral);
    assert(smoke.Input(tracking,2,399).hands[1].trigger==0&&smoke.Input(tracking,2,400).hands[1].trigger==1&&
        smoke.Input(tracking,2,479).hands[1].trigger==1&&smoke.Input(tracking,2,480).hands[1].trigger==0);
    assert(sample(smoke,400,2)==Receipt::Press&&sample(smoke,450,3)==Receipt::None);
    assert(sample(smoke,480,4)==Receipt::Release&&sample(smoke,979,5)==Receipt::None);
    assert(sample(smoke,980,6)==Receipt::NeutralTail&&sample(smoke,4999,7)==Receipt::None);
    assert(TriggerInputSmoke::Continue(4999)&&!TriggerInputSmoke::Continue(5000));
    assert(!smoke.Complete(4999,true)&&!smoke.Complete(5000,false)&&smoke.Complete(5000,true));
    std::ostringstream smokeReport;smoke.Report(smokeReport,5000,true);
    assert(smokeReport.str().find("\"publication_complete\":true")!=std::string::npos&&
        smokeReport.str().find("\"native_input_observed\":false")!=std::string::npos);
    // Busy during the complete pulse cannot become a fabricated press; the
    // schedule never retries the attack after480ms to make the test pass.
    TriggerInputSmoke missed;
    sample(missed,0,1);assert(sample(missed,400,2,false,true)==Receipt::None);
    assert(sample(missed,479,3,false,true)==Receipt::None&&sample(missed,480,4)==Receipt::None);
    sample(missed,1000,5);assert(!missed.Complete(5000,true)&&!missed.Failed());
    TriggerInputSmoke late;assert(late.Input(tracking,1,420).hands[1].trigger==0);
    sample(late,490,1);sample(late,1000,2);assert(!late.Complete(5000,true));
    TriggerInputSmoke retried;sample(retried,0,1);sample(retried,400,2,false,true);
    assert(sample(retried,410,3)==Receipt::Press);sample(retried,480,4,false,true);
    assert(sample(retried,490,5)==Receipt::Release);sample(retried,990,6);assert(retried.Complete(5000,true));
    TriggerInputSmoke noTail;sample(noTail,0,1);sample(noTail,400,2);sample(noTail,480,3);sample(noTail,979,4);
    assert(!noTail.Complete(5000,true));
    TriggerInputSmoke unknown;sample(unknown,0,1,false,false);
    assert(unknown.Failed()&&unknown.Input(tracking,2,400).hands[1].trigger==0);
    TriggerInputSmoke duplicate;sample(duplicate,0,1);sample(duplicate,400,1);
    assert(duplicate.Failed()&&!duplicate.Complete(5000,true));
    TriggerInputSmoke changedSpace;sample(changedSpace,0,1);++tracking.spaceGeneration;sample(changedSpace,400,2);--tracking.spaceGeneration;
    assert(changedSpace.Failed());
    TriggerInputSmoke clock;sample(clock,0,1);sample(clock,400,2);sample(clock,399,3);assert(clock.Failed());
    TriggerInputSmoke lostTracking;sample(lostTracking,0,1);tracking.headValid=false;sample(lostTracking,400,2);tracking.headValid=true;
    assert(lostTracking.Failed());
    TriggerInputSmoke stalePulse;sample(stalePulse,0,1);auto invalid=stalePulse.Input(tracking,2,400);
    assert(stalePulse.Published(invalid,480,1480000000,1480001000,true,false)==Receipt::None&&stalePulse.Failed());
    for(unsigned hand=0;hand<2;++hand){TriggerInputSmoke extra;sample(extra,0,1);auto bad=extra.Input(tracking,2,400);
        bad.hands[hand].squeeze=1;extra.Published(bad,400,1400000000,1400001000,true,false);assert(extra.Failed());}
    assert(TriggerInputSmokeArguments(true,true,true,240,true,true,true));
    assert(!TriggerInputSmokeArguments(true,true,true,8,true,true,true));
    assert(!TriggerInputSmokeArguments(true,true,true,240,true,false,true));
    TriggerInputSmoke motion;assert(!motion.Motion({0,0,0})&&!motion.Motion({0,0,2}));
    assert(motion.Motion({0,0,-1})&&!motion.Motion({0,0,-1}));
    assert(motion.Duration()==8000&&motion.ContinueInput(7999)&&!motion.ContinueInput(8000));
    assert(motion.PairRequestDue(0,0,240)&&!motion.PairRequestDue(1,1,240)&&motion.PairRequestDue(32,1,240));
    assert(motion.PairRequestDue(7469,239,240)&&!motion.PairRequestDue(8000,240,240));
    const auto motionSample=[&](TriggerInputSmoke& fixture,unsigned ms,bool delivered=true,bool busy=false){
        const auto input=fixture.Input(tracking,ms+1,ms);
        assert(NeutralSceneActions(input)&&input.hands[0].grip.position.x==-.2f);
        assert(input.hands[1].aim.position.x==input.hands[1].grip.position.x&&
            input.hands[1].aim.position.y==input.hands[1].grip.position.y&&input.hands[1].aim.position.z==input.hands[1].grip.position.z);
        const auto before=1000000000+std::int64_t(ms)*1000000;
        return fixture.Published(input,ms,before,before+1000,delivered,busy);
    };
    unsigned motionRows=0;
    for(unsigned ms=0;ms<8000;ms+=10){
        const auto input=motion.Input(tracking,ms+1,ms);const auto z=input.hands[1].grip.position.z;
        if(ms==2000||ms==5200||ms==7500)assert(std::abs(z+.20f)<.00001f);
        if(ms==2120||ms==5320)assert(std::abs(z+.32f)<.00001f);
        if(motionSample(motion,ms)==Receipt::Motion)++motionRows;
    }
    assert(motionRows>=24&&motion.Complete(8000,true)&&!motion.Complete(7999,true));
    std::ostringstream motionReport;motion.Report(motionReport,8000,true);
    assert(motionReport.str().find("\"stroke_samples\":[12,12]")!=std::string::npos&&
        motionReport.str().find("\"miss_verified\":false")!=std::string::npos);
    TriggerInputSmoke lostJab;assert(lostJab.Motion({0,0,-1}));
    for(unsigned ms=0;ms<8000;ms+=10)motionSample(lostJab,ms,!(ms>=2000&&ms<2120),ms>=2000&&ms<2120);
    assert(!lostJab.Complete(8000,true)&&!lostJab.Failed());
    TriggerInputSmoke lateMotion;assert(lateMotion.Motion({0,0,-1}));
    motionSample(lateMotion,2050);assert(lateMotion.Failed());
    TriggerInputSmoke changedMotion;assert(changedMotion.Motion({0,0,-1}));motionSample(changedMotion,0);
    auto wrong=changedMotion.Input(tracking,2001,2000);wrong.hands[1].grip.position.x+=.01f;
    changedMotion.Published(wrong,2000,3000000000,3000001000,true,false);assert(changedMotion.Failed());
    // The optional prefix uses the actual menu decorator and ordinary controller
    // mapper. Native movement/damage are deliberately not simulated as success.
    assert(TriggerInputSmokeOption(L"--selected-knife-approach"));
    assert(!NeutralSceneOption(L"--selected-knife-approach")&&!SelectedKnifeNeutralOption(L"--selected-knife-approach"));
    assert(TriggerInputSmokeArguments(true,true,true,320,true,false,true,true));
    assert(TriggerInputSmokeArguments(true,true,true,320,true,true,true,true));
    assert(!TriggerInputSmokeArguments(true,true,true,240,true,false,true,true)&&
        !TriggerInputSmokeArguments(true,true,true,320,true,false,false,true)&&
        !TriggerInputSmokeArguments(true,true,true,321,true,true,true,true));
    TriggerInputSmoke bounds;
    assert(!bounds.Approach(0,0,.5f)&&!bounds.Approach(10001,0,.5f)&&!bounds.Approach(1000,.5f,.5f)&&
        !bounds.Approach(1000,0,-.1f)&&!bounds.Approach(1000,0,0)&&
        !bounds.Approach(1000,0,std::numeric_limits<float>::quiet_NaN()));
    assert(bounds.Approach(10000,0,.5f)&&!bounds.Approach(1000,0,.5f));
    assert(bounds.Duration()==16000&&bounds.Motion({0,0,-1})&&bounds.Duration()==19000);
    assert(bounds.PairRequestDue(0,0,320)&&!bounds.PairRequestDue(1,1,320)&&bounds.PairRequestDue(58,1,320));
    const auto approachRun=[](bool motion,unsigned fault,bool crouch=false){
        TriggerInputSmoke fixture;assert(fixture.Approach(1000,.3f,.4f));
        if(crouch)assert(fixture.ApproachCrouch());
        if(motion)assert(fixture.Motion({0,0,-1}));
        probe::PlayerGrenadeReceiver menu;interaction::ControllerActions actions;
        const interaction::InputOwner owner{700,1,true,true};
        unsigned movementActions=0,fireEdges=0,crouchEdges=0,crouchReleases=0;bool wasFire=false,wasCrouch=false;
        for(unsigned ms=0;ms<fixture.Duration();ms+=10){
            const auto now=1000000000ll+std::int64_t(ms)*1000000;
            runtime::TrackingFrame t{};t.spaceGeneration=33;t.predictedNs=now;t.focused=t.headValid=true;
            auto input=fixture.Input(t,ms+1,ms);input.trackingBasisGeneration=4;
            auto read=ipc::ChannelResult::Ok;
            ipc::MenuState state{ms+1,5,now+100000000,ipc::MenuMode::Gameplay,1280,720,1,0};
            if(fault==1&&ms==800)state.mode=ipc::MenuMode::Menu;
            if(fault==2&&ms==800)read=ipc::ChannelResult::Closed;
            if(fault==3&&ms==800)state.epoch=6;
            if(fault==4&&ms==800)input.focused=false;
            if(fault==5&&ms==800)input.trackingBasisGeneration=9;
            if(fault==6&&ms==800)++input.spaceGeneration;
            if(fault==7&&ms==800)input.hands[0].gripTracked=false;
            if(fault==10&&ms<200)read=ipc::ChannelResult::Closed; // Initial observation may arrive late.
            menu.Decorate(input,0,read,state,now,1000000000);
            fixture.GateMenu(input,ms,now);
            if(fault>=1&&fault<=7&&ms>=800){assert(fixture.Failed());assert(NeutralSceneActions(input));}
            if(ms<fixture.AttackStartMs())assert(input.hands[1].trigger==0&&input.hands[1].grip.position.z==-.20f);
            if(ms>=1500)assert(input.hands[0].stickX==0&&input.hands[0].stickY==0);
            const auto mapped=actions.Update(input,owner,now);
            const bool movingCrouched=crouch&&ms>=500&&ms<1500&&!(fault>=1&&fault<=7&&ms>=800);
            assert(input.hands[0].held==(movingCrouched?interaction::Secondary:0u));
            assert(!(mapped.held&(interaction::Menu|interaction::Use)));
            const bool isCrouched=(mapped.held&interaction::Crouch)!=0;
            assert(isCrouched==movingCrouched);
            if(isCrouched&&!wasCrouch)++crouchEdges;
            if(!isCrouched&&wasCrouch){++crouchReleases;assert(ms==((fault>=1&&fault<=7)?800u:1500u));}
            wasCrouch=isCrouched;
            if(ms>=500&&ms<1500&&(!fault||fault==10)){
                assert(mapped.forward>0&&mapped.strafe>0&&!(mapped.held&interaction::Fire));++movementActions;
            }
            const bool fire=(mapped.held&interaction::Fire)!=0;if(fire&&!wasFire)++fireEdges;wasFire=fire;
            // A gap or entirely missed movement cannot authorize the later jab.
            const bool busy=(fault==8&&ms>=800&&ms<1000)||(fault==9&&ms>=500&&ms<1500);
            fixture.Published(input,ms,now,now+1000,!busy,busy);
        }
        const bool expected=fault==0||fault==10;
        assert(fixture.Complete(fixture.Duration(),true)==expected);
        if(expected){assert(movementActions==100);assert(fireEdges==(motion?0u:1u));}
        else assert(fireEdges==0);
        assert(crouchEdges==(crouch?1u:0u)&&crouchReleases==(crouch?1u:0u));
        std::ostringstream result;fixture.Report(result,fixture.Duration(),true);
        assert(result.str().find("\"native_movement_verified\":false")!=std::string::npos&&
            result.str().find("\"attack_start_ms\":2000")!=std::string::npos);
        assert((result.str().find("\"approach_crouch\":true")!=std::string::npos)==crouch);
    };
    approachRun(false,0);approachRun(true,0);
    for(unsigned fault=1;fault<=10;++fault)approachRun(false,fault);
    approachRun(false,0,true);approachRun(true,0,true);
    for(unsigned fault=1;fault<=10;++fault)approachRun(false,fault,true);
    assert(TriggerInputSmokeOption(L"--selected-knife-approach-crouch")&&
        !NeutralSceneOption(L"--selected-knife-approach-crouch")&&!SelectedKnifeNeutralOption(L"--selected-knife-approach-crouch"));
    TriggerInputSmoke crouchBounds;assert(!crouchBounds.ApproachCrouch()&&crouchBounds.Approach(1000,0,.4f));
    assert(crouchBounds.ApproachCrouch()&&!crouchBounds.ApproachCrouch());
    TriggerInputSmoke lateCrouch;assert(lateCrouch.Approach(1000,0,.4f));probe::PlayerGrenadeReceiver lateCrouchMenu;
    auto neutral=RotationInput(lateCrouch,lateCrouchMenu,0);lateCrouch.Published(neutral,0,1000000000,1000001000,true,false);
    assert(!lateCrouch.ApproachCrouch());
    for(unsigned fault=0;fault<4;++fault){
        TriggerInputSmoke fixture;assert(fixture.Approach(1000,0,.4f)&&fixture.ApproachCrouch());probe::PlayerGrenadeReceiver menu;
        auto first=RotationInput(fixture,menu,0);fixture.Published(first,0,1000000000,1000001000,true,false);
        const unsigned ms=fault==3?1500:500;auto changed=RotationInput(fixture,menu,ms);
        if(fault==0)changed.hands[0].held=0;
        if(fault==1)changed.hands[0].held|=interaction::Primary;
        if(fault==2)changed.hands[0].held|=interaction::MenuClick;
        if(fault==3)changed.hands[0].held|=interaction::Secondary;
        fixture.Published(changed,ms,changed.predictedNs,changed.predictedNs+1000,true,false);
        assert(fixture.Reason()==TriggerInputSmoke::Failure::UnexpectedInput);
        auto cancelled=RotationInput(fixture,menu,ms+10);assert(NeutralSceneActions(cancelled));
    }
    MeasuredRotationAndRealGesture(false);MeasuredRotationAndRealGesture(true);RotationBoundsAndCancellation();
    MeasuredRotationAndRealGesture(true,true);PreparedApproachGuards();
    TurnPrefixAndReviewImages();
    ApproachResumeRegressions();
    std::cout<<"8 scene/body groups, trigger/two-jab, menu-aware approach/crouch/turn, two review pairs and measured forward rotation regressions PASS\n";
}
