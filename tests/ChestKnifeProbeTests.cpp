#include "ChestKnifeProbeReceiver.h"
#include "NativeProbeConfig.h"
#include "Bc2KnifeCopyReceipt.h"
#include "Test.h"
#include <sstream>
using namespace fvr;
namespace {
constexpr std::int64_t Start=1000000000,Frequency=1000000000;
constexpr unsigned Flags=9u|0x197800u|0x2000000u|0x10000000u;
int Config(){
    CHECK(bc2::ValidChestKnifeProbe(0,0,0,0,false,false));
    for(unsigned mode:{1u,2u}){CHECK(bc2::ValidChestKnifeProbe(mode,Flags,20000,3,false,true));
        CHECK(!bc2::ValidChestKnifeProbe(mode,Flags,20000,3,false,false));
        CHECK(!bc2::ValidChestKnifeProbe(mode,Flags,19000,3,false,true));
        CHECK(!bc2::ValidChestKnifeProbe(mode,Flags|0x400u,20000,3,false,true));
        CHECK(!bc2::ValidChestKnifeProbe(mode,Flags,20000,3,true,true));
        CHECK(!bc2::ValidChestKnifeProbe(mode,Flags&~0x800u,20000,3,false,true));}
    CHECK(!bc2::ValidChestKnifeProbe(3,Flags,20000,3,false,true));
    CHECK(sizeof(bc2::NativeProbeConfig)==1220);return 0;
}
interaction::InputFrame Input(unsigned ms){interaction::InputFrame i;i.generation=ms/10+2;i.spaceGeneration=1;
    i.predictedNs=Start+std::int64_t(ms)*1000000;i.focused=i.headValid=true;
    for(auto& h:i.hands){h.active=interaction::Components;h.gripTracked=h.aimTracked=true;}return i;
}
bc2::ChestKnifeProbeWitness Witness(const interaction::InputFrame& i,std::int64_t now,probe::KnifeRecipePhase phase){
    bc2::ChestKnifeProbeWitness w;w.pid=1;w.creation=2;w.input=i.generation-1;w.space=1;w.actor=3;w.actorGeneration=4;
    w.observedNs=std::uint64_t(now-10000000);w.deadlineNs=std::uint64_t(now+90000000);w.weapon=5;
    w.wristInGrip.values={{{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}}};
    if(phase==probe::KnifeRecipePhase::AwaitEmpty||phase==probe::KnifeRecipePhase::Reach||phase==probe::KnifeRecipePhase::ChestNeutral)w.empty=1;
    else if(phase==probe::KnifeRecipePhase::Release||phase==probe::KnifeRecipePhase::Complete){w.empty=1;w.draws=w.returns=1;}
    else {w.held=w.nativeOwned=w.draws=w.attackReady=w.recognizerReady=1;w.bodyOperation=8;}
    return w;
}
void Step(probe::ChestKnifeProbeReceiver& receiver,interaction::InputFrame& i,unsigned ms,unsigned mode,
    const bc2::ChestKnifeProbeWitness* w,ipc::MenuMode menu=ipc::MenuMode::Gameplay,bool publication=true,unsigned failureCode=unsigned(ipc::ChannelResult::Invalid),bool explicitRelease=false){
    const auto now=Start+std::int64_t(ms)*1000000;ipc::MenuState state{i.generation,1,now+100000000,menu,1280,720,1,0};
    receiver.Update(i,ms,now,mode,1,2,w,ipc::ChannelResult::Ok,state,now,Frequency);
    if(explicitRelease)i.hands[1].squeeze=0;
    receiver.Published(i,true,publication,publication?0u:failureCode,now);
}
int NoAuthorityNoGesture(){probe::ChestKnifeProbeReceiver receiver;
    for(unsigned ms=0;ms<18000;ms+=10){auto i=Input(ms);Step(receiver,i,ms,2,nullptr);CHECK(i.hands[1].squeeze==0);CHECK(!receiver.Completed());}
    CHECK(receiver.Strokes()==0);return 0;
}
int DeliberateAndReturn(){for(unsigned mode:{1u,2u}){probe::ChestKnifeProbeReceiver receiver;unsigned strokeSamples=0;
    std::optional<math::Vec3> previous;float maximumDrawSpeed=0;bool sawRelease=false;
    for(unsigned ms=0;ms<18000;ms+=10){auto i=Input(ms);const auto before=receiver.Phase();
        auto w=Witness(i,i.predictedNs,before);Step(receiver,i,ms,mode,&w);
        for(auto& h:i.hands)CHECK(h.held==0&&h.trigger==0&&h.stickX==0&&h.stickY==0);
        if(before==probe::KnifeRecipePhase::Stroke)++strokeSamples;
        if(before==probe::KnifeRecipePhase::Withdraw&&previous){const auto& p=i.hands[1].grip.position;
            maximumDrawSpeed=(std::max)(maximumDrawSpeed,std::hypot(p.x-previous->x,p.y-previous->y,p.z-previous->z)/.01f);}
        previous=i.hands[1].grip.position;sawRelease|=before==probe::KnifeRecipePhase::Release;
    }
    CHECK(receiver.Completed());CHECK(receiver.Strokes()==(mode==2?1u:0u));CHECK((strokeSamples>0)==(mode==2));
    CHECK(maximumDrawSpeed<.65f);CHECK(sawRelease);std::ostringstream report;receiver.Report(report);
    CHECK(report.str().find("\"grip_presses\":1,\"grip_releases\":1")!=std::string::npos);
}return 0;}
int IncompleteIsNotHide(){probe::ChestKnifeProbeReceiver receiver;
    for(unsigned ms=0;ms<18000;ms+=10){auto i=Input(ms);auto w=Witness(i,i.predictedNs,receiver.Phase());
        if(receiver.Phase()==probe::KnifeRecipePhase::Release){w.empty=0;w.nativeOwned=1;w.bodyOperation=8;w.returns=0;}
        Step(receiver,i,ms,1,&w);}
    CHECK(!receiver.Completed());return 0;
}
int InterruptionsNeverRetry(){for(unsigned kind=0;kind<4;++kind){probe::ChestKnifeProbeReceiver receiver;bool interrupted=false;
    for(unsigned ms=0;ms<18000;ms+=10){auto i=Input(ms);auto w=Witness(i,i.predictedNs,receiver.Phase());auto menu=ipc::MenuMode::Gameplay;bool published=true;
        if(!interrupted&&receiver.Phase()==probe::KnifeRecipePhase::Settle){interrupted=true;
            if(kind==0)menu=ipc::MenuMode::Menu;if(kind==1)w.actorGeneration++;
            if(kind==2)published=false;if(kind==3)w.held=0;}
        Step(receiver,i,ms,2,&w,menu,published);
        if(interrupted&&published)CHECK(i.hands[1].squeeze==0);
    }
    CHECK(interrupted);CHECK(!receiver.Completed());CHECK(receiver.Strokes()==0);
}return 0;}
int RenderPairTimeline(){
    using Receiver=probe::ChestKnifeProbeReceiver;
    CHECK(Receiver::PairRequestDue(0,0));CHECK(!Receiver::PairRequestDue(0,1));
    unsigned consumed=0;std::uint64_t pending=0,lastCompleted=0,maxGap=0;bool waiting=false;
    for(std::uint64_t ms=0;ms<18000;++ms){
        if(waiting&&ms>=pending){if(consumed)maxGap=(std::max)(maxGap,ms-lastCompleted);
            lastCompleted=ms;++consumed;waiting=false;}
        if(!waiting&&Receiver::PairRequestDue(ms,consumed)){pending=ms+20;waiting=true;}
        if(ms==13000)CHECK(consumed<240&&lastCompleted>=12900);
    }
    CHECK(consumed==240&&lastCompleted>=13941&&lastCompleted<14000&&maxGap<100);
    CHECK(!Receiver::PairRequestDue(18000,240));
    // A delayed pair stays eligible; no fabricated consumption or renewed lease.
    CHECK(Receiver::PairRequestDue(7000,100));CHECK(!Receiver::PairRequestDue(10,1));
    return 0;
}
int BusyIsNotDeliveryOrRelease(){
    probe::ChestKnifeProbeReceiver receiver;bool busy=false,recovered=false,releaseBusy=false;
    for(unsigned ms=0;ms<18000;ms+=10){
        auto i=Input(ms);auto w=Witness(i,i.predictedNs,receiver.Phase());
        const auto before=receiver.Phase();const auto now=i.predictedNs;
        ipc::MenuState state{i.generation,1,now+100000000,ipc::MenuMode::Gameplay,1280,720,1,0};
        receiver.Update(i,ms,now,1,1,2,&w,ipc::ChannelResult::Ok,state,now,Frequency);
        const bool missHeld=!busy&&before==probe::KnifeRecipePhase::Withdraw;
        const bool missRelease=!releaseBusy&&busy&&i.hands[1].squeeze==0;
        const bool miss=missHeld||missRelease;
        receiver.Published(i,true,!miss,unsigned(miss?ipc::ChannelResult::Busy:ipc::ChannelResult::Ok),now);
        if(missHeld){busy=true;CHECK(receiver.Phase()==before&&i.hands[1].squeeze==1);
            std::ostringstream report;receiver.Report(report);
            CHECK(report.str().find("\"grip_presses\":1,\"grip_releases\":0")!=std::string::npos);
            CHECK(report.str().find("\"publication_busy\":1")!=std::string::npos);
        }else if(missRelease){releaseBusy=true;CHECK(i.hands[1].squeeze==0&&!receiver.Completed());
            std::ostringstream report;receiver.Report(report);
            CHECK(report.str().find("\"grip_presses\":1,\"grip_releases\":0")!=std::string::npos);
        }else if(busy&&i.hands[1].squeeze==1)recovered=true;
    }
    CHECK(busy&&releaseBusy&&recovered&&receiver.Completed());std::ostringstream report;receiver.Report(report);
    CHECK(report.str().find("\"grip_presses\":1,\"grip_releases\":1")!=std::string::npos);
    CHECK(report.str().find("\"publication_busy\":2")!=std::string::npos);
    // Missing publications do not extend the original absolute recipe bound.
    probe::ChestKnifeProbeReceiver blocked;
    for(unsigned ms=0;ms<=13000;ms+=10){auto i=Input(ms);auto w=Witness(i,i.predictedNs,blocked.Phase());
        const bool failDelivery=blocked.Phase()==probe::KnifeRecipePhase::Draw;
        if(failDelivery){w.held=0;w.nativeOwned=0;w.bodyOperation=0;w.draws=0;}
        Step(blocked,i,ms,1,&w,ipc::MenuMode::Gameplay,!failDelivery,unsigned(ipc::ChannelResult::Busy));
    }
    CHECK(blocked.Phase()==probe::KnifeRecipePhase::Failed&&!blocked.Completed());return 0;
}
int PerCallHash(){bc2::OriginalFiringToken token;token.words[4]=token.words[5]=UINT32_MAX;
    std::array<std::byte,0x30> original{};for(unsigned n=0;n<original.size();++n)original[n]=std::byte(n);
    const auto hash=bc2::private_knife::KnifeContextHash(original);bc2::private_knife::SetKnifeContextHash(token,hash);
    CHECK(std::uint32_t(token.words[4])==UINT32_MAX&&std::uint32_t(token.words[5])==UINT32_MAX);
    CHECK(bc2::private_knife::KnifeContextHash(token)==hash);
    auto other=original;other[0]^=std::byte(1);CHECK(bc2::private_knife::KnifeContextHash(other)!=hash);
    CHECK(bc2::private_knife::KnifeContextHash(token)==hash);return 0;
}

int StrokeObservationIsNotPhysicalRelease(){
    for(unsigned variant=0;variant<4;++variant){probe::ChestKnifeProbeReceiver receiver;
        bool interrupted=false;unsigned strokeAt=0,waitAt=0,releaseAt=0;std::string first;
        for(unsigned ms=0;ms<18000;ms+=10){auto i=Input(ms);const auto before=receiver.Phase();
            auto w=Witness(i,i.predictedNs,before);const auto* observed=&w;
            if(before==probe::KnifeRecipePhase::Stroke){
                if(!strokeAt)strokeAt=ms-10;
                if(ms>=strokeAt+40){
                    interrupted=true;
                    if(variant==0)observed=nullptr;
                    if(variant==1)w.deadlineNs=std::uint64_t(i.predictedNs); // Never renewed.
                    if(variant==2){w.held=0;w.attackReady=0;w.phase=6;} // Actual busy is not a released grip.
                    if(variant==3)w.attackReady=0;
                }
            }
            Step(receiver,i,ms,2,observed);
            if(before==probe::KnifeRecipePhase::Stroke){
                CHECK(i.hands[1].squeeze==1); // Fails with frozen403 at the interruption.
                CHECK(receiver.Phase()!=probe::KnifeRecipePhase::Failed);
                if(receiver.Phase()==probe::KnifeRecipePhase::WaitAttack)waitAt=ms;
                if(interrupted&&first.empty()){
                    std::ostringstream out;receiver.Report(out);first=out.str();
                }
            }
            if(before==probe::KnifeRecipePhase::WaitAttack&&receiver.Phase()==probe::KnifeRecipePhase::Release)releaseAt=ms;
            if(releaseAt)CHECK(i.hands[1].squeeze==0);
        }
        CHECK(interrupted&&receiver.Completed()&&receiver.Strokes()==1);
        CHECK(waitAt-strokeAt==120&&releaseAt-waitAt==3000);
        std::ostringstream out;receiver.Report(out);const auto report=out.str();
        CHECK(report.find("\"grip_presses\":1,\"grip_releases\":1")!=std::string::npos);
        CHECK(report.find("\"first_failure\":{\"recorded\":0")!=std::string::npos);
        if(variant<3){
            const auto begin=first.find("\"first_stroke_observation\":");const auto end=first.find(",\"first_failure\":",begin);
            CHECK(begin!=std::string::npos&&end!=std::string::npos);
            CHECK(report.find(first.substr(begin,end-begin))!=std::string::npos); // Immutable original bytes/times.
            CHECK(report.find("\"first_stroke_observation\":{\"recorded\":1")!=std::string::npos);
        }
    }
    return 0;
}
int StrokeStopsForCurrentReplacement(){
    for(unsigned variant=0;variant<15;++variant){probe::ChestKnifeProbeReceiver receiver;bool stopped=false;
        for(unsigned ms=0;ms<18000;ms+=10){auto i=Input(ms);auto w=Witness(i,i.predictedNs,receiver.Phase());
            auto menu=ipc::MenuMode::Gameplay;bool published=true;
            const bool interrupt=!stopped&&receiver.Phase()==probe::KnifeRecipePhase::Stroke;
            if(interrupt){
                if(variant==0)++w.pid;if(variant==1)++w.creation;if(variant==2)++w.actor;
                if(variant==3)++w.actorGeneration;if(variant==4)++i.spaceGeneration;
                if(variant==5)++w.bodyOperation;if(variant==6)w.nativeOwned=0;
                if(variant==7)++w.draws;if(variant==8)w.returns=1;
                if(variant==9)menu=ipc::MenuMode::Menu;
                if(variant==10)i.focused=false;if(variant==11)i.hands[1].gripTracked=false;
                if(variant==12)published=false;
                if(variant==13)++w.space;
            }
            Step(receiver,i,ms,2,&w,menu,published,unsigned(ipc::ChannelResult::Invalid),interrupt&&variant==14);
            if(interrupt){stopped=true;CHECK(receiver.Phase()==probe::KnifeRecipePhase::Failed);}
            if(stopped&&!interrupt)CHECK(i.hands[1].squeeze==0);
        }
        CHECK(stopped&&!receiver.Completed()&&receiver.Strokes()==1);
        std::ostringstream out;receiver.Report(out);CHECK(out.str().find("\"first_failure\":{\"recorded\":1")!=std::string::npos);
    }
    return 0;
}
int InterruptedRecognizerSettle(){
    // Actual recognizer + shared GunHold; only native witness facts are synthetic.
    // Interrupt at650ms of the driver's700ms wait. Native-ready remains true,
    // but the recognizer must accumulate its unchanged120ms quiet history again.
    for(const bool neverReady:{false,true}){
        probe::ChestKnifeProbeReceiver receiver;interaction::MeleeStroke motion;interaction::HandInteraction hands;
        interaction::MeleeStrokeSample sample;unsigned settleAt=0,strokeAt=0,intents=0;bool gap=false;
        std::optional<interaction::HandInteractionSample> previous;
        for(unsigned ms=0;ms<18000;ms+=10){
            auto i=Input(ms);const auto before=receiver.Phase();auto w=Witness(i,i.predictedNs,before);
            if(before==probe::KnifeRecipePhase::Settle&&!settleAt)settleAt=ms;
            w.recognizerReady=!neverReady&&previous&&motion.ReadyFor(*previous);
            Step(receiver,i,ms,2,&w);
            if(settleAt&&ms==settleAt+700){CHECK(receiver.Phase()==probe::KnifeRecipePhase::Settle);CHECK(receiver.Strokes()==0);}
            if(before==probe::KnifeRecipePhase::Settle&&receiver.Phase()==probe::KnifeRecipePhase::Stroke){
                strokeAt=ms;CHECK(previous&&motion.ReadyFor(*previous));CHECK(strokeAt>=settleAt+780);}
            if(before==probe::KnifeRecipePhase::Settle||before==probe::KnifeRecipePhase::Stroke||before==probe::KnifeRecipePhase::WaitAttack){
                sample.source={{3,4,5,1},i.generation,i.predictedNs,i.predictedNs+100000000,i.predictedNs,true,{true,true},{false,false}};
                hands.Update(sample.source);const interaction::HandContactProof contact{{7,1},i.generation,sample.source.deadlineNs,true};
                auto held=hands.Current(interaction::InteractionHand::Right);
                if(held){const auto renewed=hands.Renew(sample.source,held->token,contact);CHECK(renewed.claim);sample.hold=renewed.claim->token;}
                else {const auto acquired=hands.Acquire(sample.source,{sample.source.owner,interaction::InteractionHand::Right,
                    interaction::HandClaimKind::GunHold,{5,1},contact,i.generation,0});CHECK(acquired.claim);sample.hold=acquired.claim->token;}
                const auto geometry=bc2::private_knife::KnifeMotionFromGrip(i.hands[1].grip,w.wristInGrip);CHECK(geometry);
                sample.tip=geometry->tip;sample.direction=geometry->direction;
                const bool interrupt=!gap&&ms==settleAt+650;sample.attackAvailable=!interrupt;
                const auto observed=motion.Update(sample,hands);intents+=unsigned(observed.intent.has_value());
                if(interrupt){gap=true;CHECK(motion.Phase()==interaction::MeleeStrokePhase::Unavailable);}
                previous=sample.source;
            }
        }
        CHECK(gap);
        if(neverReady){CHECK(!receiver.Completed()&&!receiver.Strokes()&&!intents);CHECK(receiver.Phase()==probe::KnifeRecipePhase::Failed);}
        else {CHECK(strokeAt&&receiver.Completed()&&receiver.Strokes()==1&&intents==1);}
    }
    // Explicit private wire version boundary: an old216-byte witness cannot
    // masquerade as recognizer-ready. NativeProbeConfig remains unchanged1220.
    CHECK(sizeof(bc2::ChestKnifeProbeWitness)==224&&sizeof(bc2::NativeProbeConfig)==1220);
    probe::ChestKnifeProbeReceiver old;auto i=Input(1000);auto w=Witness(i,i.predictedNs,old.Phase());w.version=1;
    Step(old,i,1000,2,&w);CHECK(old.Phase()==probe::KnifeRecipePhase::AwaitEmpty);
    return 0;
}

}
int main(int argc,char** argv){
    if(argc==2&&std::string_view(argv[1])=="--recognizer-ready"){
        if(InterruptedRecognizerSettle())return 1;
        std::puts("Knife recognizer-ready PASS: interrupted actual settle, one later stroke, unchanged timeout and private wire rejection.");return 0;}
    if(argc==2&&std::string_view(argv[1])=="--stroke-continuation"){
        if(StrokeObservationIsNotPhysicalRelease()||StrokeStopsForCurrentReplacement())return 1;
        std::puts("Knife finite-stroke PASS: four observation interruptions, fifteen current-stop boundaries, immutable clocks/guards. No native actions.");return 0;}
    if(argc==2&&std::string_view(argv[1])=="--publication"){
        if(BusyIsNotDeliveryOrRelease()||InterruptionsNeverRetry())return 1;
        std::puts("Knife publication PASS: Busy nondelivery, unchanged absolute bound, hard failure/ownership negatives.");return 0;}
    if(InterruptedRecognizerSettle()||StrokeObservationIsNotPhysicalRelease()||StrokeStopsForCurrentReplacement()||Config()||NoAuthorityNoGesture()||DeliberateAndReturn()||IncompleteIsNotHide()||InterruptionsNeverRetry()||PerCallHash()||RenderPairTimeline()||BusyIsNotDeliveryOrRelease())return 1;
    std::puts("Knife fixture PASS: ten focused admission/recipe/receipt/timeline/publication/finite-stroke groups. No native, graphics or live actions.");return 0;}
