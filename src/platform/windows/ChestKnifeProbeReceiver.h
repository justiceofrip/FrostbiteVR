#pragma once
#include "../../games/bc2/Bc2ChestKnifeProbeWitness.h"
#include "Bc2KnifeMotionGeometry.h"
#include "PlayerGrenadeReceiver.h"
#include <optional>
namespace fvr::probe {
constexpr bool ChestKnifeProbeOption(std::wstring_view s)noexcept {
    return s==L"--chest-knife-draw-probe"||s==L"--chest-knife-motion-probe"||
        s==L"--pairs"||s==L"--static-pose"||s==L"--async";
}
enum class KnifeRecipePhase:unsigned {AwaitEmpty,Reach,ChestNeutral,Draw,Withdraw,Settle,Stroke,WaitAttack,Release,Complete,Failed};
// One finite physical recipe. Witnesses cannot grant native authority: native
// policy independently revalidates original input, menu, hand and tool custody.
class ChestKnifeProbeReceiver {
public:
    static constexpr std::uint64_t DurationMs=18000;
    // Keep paired render receipts available through the recipe's 13s bound.
    // 240 requests over 14s leave about 42ms of the unchanged 100ms receipt
    // lifetime for delivery. Input publication remains independent of pacing.
    static constexpr bool PairRequestDue(std::uint64_t elapsed,unsigned consumed)noexcept {
        return consumed<240&&elapsed>=std::uint64_t(consumed)*14000/240;
    }
    void Update(interaction::InputFrame& input,std::uint64_t elapsed,std::int64_t now,
        unsigned mode,std::uint64_t pid,std::uint64_t creation,const bc2::ChestKnifeProbeWitness* witness,
        ipc::ChannelResult read,const ipc::MenuState& incoming,std::int64_t qpc,std::int64_t frequency)noexcept {
        for(auto& h:input.hands){h.held=h.touched=h.touchActive=0;h.squeeze=h.trigger=h.stickX=h.stickY=0;}
        input.hands[1].grip.position=position_;
        // Same policy and original source expiry used by the normal host.
        menu_.Decorate(input,0,read,incoming,qpc,frequency);
        const bool gameplay=interaction::GameplayOwnsInput(input.menu,now);
        const bool fresh=witness&&Fresh(*witness,pid,creation,input,now);
        const auto fail=[&](Guard reason){
            RecordGuard(firstFailure_,reason,input,elapsed,now,pid,creation,witness,fresh);Fail();};
        if(mode!=1&&mode!=2){fail(Guard::Mode);return;}
        if(lastMs_&&elapsed<lastMs_){fail(Guard::ClockReversed);return;}lastMs_=elapsed;
        // A missing diagnostic observation cannot impersonate a foreign owner.
        // A received different process identity or current tracking epoch can.
        if(witness&&(witness->pid!=pid||witness->creation!=creation))fail(Guard::ProcessChanged);
        if(pin_&&input.spaceGeneration!=pin_->space)fail(Guard::SpaceChanged);
        if(witness&&pin_&&FreshWitnessSource(*witness,pid,creation,input,now)){
            if(witness->space!=pin_->space)fail(Guard::SpaceChanged);
            if(witness->actor!=pin_->actor||witness->actorGeneration!=pin_->actorGeneration)fail(Guard::ActorChanged);
        }
        if(elapsed>=13000&&phase_!=KnifeRecipePhase::Complete)fail(Guard::RecipeExpired);
        const bool active=phase_!=KnifeRecipePhase::AwaitEmpty&&phase_!=KnifeRecipePhase::Complete&&phase_!=KnifeRecipePhase::Failed;
        if(active&&!gameplay)fail(Guard::MenuLost);
        if(active&&(!input.focused||!input.headValid||!input.hands[0].gripTracked||!input.hands[0].aimTracked||
            !input.hands[1].gripTracked||!input.hands[1].aimTracked))fail(Guard::TrackingLost);
        if(operation_&&fresh&&(phase_==KnifeRecipePhase::Withdraw||phase_==KnifeRecipePhase::Settle||
            phase_==KnifeRecipePhase::Stroke||phase_==KnifeRecipePhase::WaitAttack)&&
            (!witness->nativeOwned||witness->bodyOperation!=operation_||witness->draws!=1||witness->returns))fail(Guard::OperationChanged);
        const auto age=elapsed>=at_?elapsed-at_:0;
        switch(phase_){
        case KnifeRecipePhase::AwaitEmpty:
            if(elapsed>=1000&&gameplay&&fresh&&witness->empty&&!witness->nativeOwned&&!witness->bodyOperation&&!witness->draws&&!witness->returns){
                pin_=*witness;Move(KnifeRecipePhase::Reach,elapsed,input);}
            break;
        case KnifeRecipePhase::Reach:
            position_=Lerp(Home,Chest,float(age)/700.f);
            if(age>=700)Move(KnifeRecipePhase::ChestNeutral,elapsed,input);
            break;
        case KnifeRecipePhase::ChestNeutral:
            position_=Chest;
            if(age>=250){if(!fresh||!witness->empty)fail(Guard::DrawProof);else Move(KnifeRecipePhase::Draw,elapsed,input);}
            break;
        case KnifeRecipePhase::Draw:
            if(age>2500){fail(Guard::DrawTimeout);break;}
            squeeze_=true;
            if(fresh&&witness->held&&witness->draws==1&&witness->nativeOwned&&witness->bodyOperation){
                operation_=witness->bodyOperation;Move(KnifeRecipePhase::Withdraw,elapsed,input);}
            break;
        case KnifeRecipePhase::Withdraw:
            squeeze_=true;position_=Lerp(Chest,Home,float(age)/1800.f);
            if(fresh&&!Held(*witness)){fail(Guard::HeldAdmission);break;}
            if(age>=1800)Move(KnifeRecipePhase::Settle,elapsed,input);
            break;
        case KnifeRecipePhase::Settle:
            squeeze_=true;position_=Home;
            if(age>2500){fail(Guard::SettleTimeout);break;}
            if(fresh&&!Held(*witness)){fail(Guard::HeldAdmission);break;}
            if(age>=700&&fresh&&Held(*witness)){
                if(mode==1)Move(KnifeRecipePhase::Release,elapsed,input);
                else if(witness->attackReady&&witness->recognizerReady){
                    const auto geometry=bc2::private_knife::KnifeMotionFromGrip(input.hands[1].grip,witness->wristInGrip);
                    if(!geometry){fail(Guard::Geometry);break;}
                    // Engine LH direction -> original OpenXR LOCAL translation.
                    direction_={geometry->direction.x,geometry->direction.y,-geometry->direction.z};
                    strokeWitness_=*witness;Move(KnifeRecipePhase::Stroke,elapsed,input);++strokes_;
                }
            }
            break;
        case KnifeRecipePhase::Stroke:
            squeeze_=true;
            // The admitted physical stroke has one fixed120ms trajectory.
            // Native busy/read/render gaps do not make the emulated hand let go.
            // This uses no witness as renewed native authority: each ordinary
            // controller publication and native consumer keeps its own checks.
            if(!fresh||!Held(*witness))RecordGuard(firstStrokeObservation_,
                fresh?Guard::HeldObservationMissing:Guard::WitnessUnavailable,
                input,elapsed,now,pid,creation,witness,fresh);
            position_={Home.x+direction_.x*.12f*(std::min)(1.f,float(age)/120.f),
                Home.y+direction_.y*.12f*(std::min)(1.f,float(age)/120.f),Home.z+direction_.z*.12f*(std::min)(1.f,float(age)/120.f)};
            if(age>=120)Move(KnifeRecipePhase::WaitAttack,elapsed,input);
            break;
        case KnifeRecipePhase::WaitAttack:
            squeeze_=true;
            // The receiver does not infer attack completion from this delay.
            // Release is a fresh normal cleanup request; native policy retains
            // any accepted operation until its own exact receipt permits return.
            if(age>=3000)Move(KnifeRecipePhase::Release,elapsed,input);
            break;
        case KnifeRecipePhase::Release:
            squeeze_=false;
            if(fresh&&witness->empty&&witness->draws==1&&witness->returns==1&&
               !witness->nativeOwned&&!witness->bodyOperation&&releaseInput_&&witness->input>=releaseInput_){
                returned_=*witness;Move(KnifeRecipePhase::Complete,elapsed,input);}
            break;
        case KnifeRecipePhase::Complete:case KnifeRecipePhase::Failed:squeeze_=false;break;
        }
        if(!gameplay||phase_==KnifeRecipePhase::Failed)squeeze_=false;
        if(phase_==KnifeRecipePhase::Release||phase_==KnifeRecipePhase::Complete)squeeze_=false;
        input.hands[1].grip.position=position_;input.hands[1].squeeze=squeeze_?1.f:0.f;
        for(auto& h:input.hands)h.aim=h.grip;
    }
    void Published(const interaction::InputFrame& i,bool known,bool succeeded,unsigned code,std::int64_t now)noexcept {
        const bool delivered=known&&succeeded&&code==unsigned(ipc::ChannelResult::Ok);
        menu_.Published(i,known,delivered,code,now);
        if(!delivered){++publicationMisses_;
            // Busy means this packet was not delivered. Continue with the next
            // ordinary fresh packet, without inventing a release, replaying the
            // missed packet or changing any recipe/witness deadline.
            if(known&&!succeeded&&code==unsigned(ipc::ChannelResult::Busy)){++publicationBusy_;return;}
            if(squeeze_||lastSqueeze_){
                if(firstFailure_.reason==Guard::None){
                    RecordGuard(firstFailure_,Guard::Publication,i,lastMs_,now,0,0,nullptr,false);
                    firstFailure_.publicationKnown=known;firstFailure_.publicationSucceeded=succeeded;firstFailure_.publicationCode=code;}
                Fail();}return;}
        ++published_;const bool held=i.hands[1].squeeze>=.65f;
        if(held&&!lastSqueeze_){++presses_;pressInput_=i.generation;if(presses_>1){
            RecordGuard(firstFailure_,Guard::ExtraPress,i,lastMs_,now,0,0,nullptr,false);Fail();}}
        if(!held&&lastSqueeze_){++releases_;releaseInput_=i.generation;
            if(phase_!=KnifeRecipePhase::Release&&phase_!=KnifeRecipePhase::Complete&&phase_!=KnifeRecipePhase::Failed){
                RecordGuard(firstFailure_,Guard::ExplicitRelease,i,lastMs_,now,0,0,nullptr,false);Fail();}}
        lastSqueeze_=held;
        if(phase_==KnifeRecipePhase::Complete&&i.generation>returned_.input&&!held){neutralInput_=i.generation;neutralNs_=now;}
    }
    bool Completed()const noexcept{return phase_==KnifeRecipePhase::Complete&&presses_==1&&releases_==1&&neutralInput_>returned_.input;}
    KnifeRecipePhase Phase()const noexcept{return phase_;}
    unsigned Strokes()const noexcept{return strokes_;}
    void Report(std::ostream& out)const {
        out<<"{\"contract\":1,\"normal_custody_path\":true,\"direct_native_calls\":0,\"hand_directed_hit_override\":false,\"phase\":"<<unsigned(phase_)
           <<",\"completed\":"<<Completed()<<",\"published\":"<<published_<<",\"publication_misses\":"<<publicationMisses_
           <<",\"publication_busy\":"<<publicationBusy_
           <<",\"grip_presses\":"<<presses_<<",\"grip_releases\":"<<releases_<<",\"strokes\":"<<strokes_
           <<",\"press_input\":"<<pressInput_<<",\"release_input\":"<<releaseInput_<<",\"return_input\":"<<returned_.input
           <<",\"neutral_input\":"<<neutralInput_<<",\"neutral_ns\":"<<neutralNs_<<",\"body_operation\":"<<operation_
           <<",\"return_observed_ns\":"<<returned_.observedNs<<",\"return_deadline_ns\":"<<returned_.deadlineNs
           <<",\"stroke_witness_input\":"<<strokeWitness_.input<<",\"stroke_witness_observed_ns\":"<<strokeWitness_.observedNs
           <<",\"stroke_witness_deadline_ns\":"<<strokeWitness_.deadlineNs<<",\"stroke_recognizer_ready\":"<<strokeWitness_.recognizerReady
           <<",\"stroke_actor\":"<<strokeWitness_.actor
           <<",\"stroke_actor_generation\":"<<strokeWitness_.actorGeneration<<",\"stroke_space\":"<<strokeWitness_.space
           <<",\"stroke_axis_xr\":["<<direction_.x<<','<<direction_.y<<','<<direction_.z<<"],\"wrist_in_grip\":[";
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){if(r||c)out<<',';out<<strokeWitness_.wristInGrip.values[r][c];}
        out<<"],\"events_dropped\":"<<dropped_<<",\"events\":[";
        for(unsigned n=0;n<eventsCount_;++n){if(n)out<<',';const auto& e=events_[n];out<<'['<<e.phase<<','<<e.ms<<','<<e.input<<']';}
        out<<"],\"first_stroke_observation\":";ReportGuard(out,firstStrokeObservation_);
        out<<",\"first_failure\":";ReportGuard(out,firstFailure_);
        out<<",\"menu\":";menu_.Report(out);out<<'}';
    }
private:
    enum class Guard:unsigned {None,Mode,ClockReversed,ProcessChanged,SpaceChanged,ActorChanged,
        RecipeExpired,MenuLost,TrackingLost,OperationChanged,DrawProof,DrawTimeout,SettleTimeout,
        HeldAdmission,Geometry,WitnessUnavailable,HeldObservationMissing,Publication,ExtraPress,ExplicitRelease};
    struct GuardSnapshot {
        Guard reason=Guard::None;KnifeRecipePhase phase=KnifeRecipePhase::AwaitEmpty;
        std::uint64_t input=0,elapsedMs=0,expectedPid=0,expectedCreation=0,space=0;
        std::int64_t nowNs=0;bool present=false,fresh=false,held=false,focused=false,headValid=false;
        std::array<bool,2> tracked{};interaction::InputMenuOwnership menu{};
        bc2::ChestKnifeProbeWitness witness{};
        bool publicationKnown=false,publicationSucceeded=false;unsigned publicationCode=0;
    };
    void RecordGuard(GuardSnapshot& record,Guard reason,const interaction::InputFrame& input,
        std::uint64_t elapsed,std::int64_t now,std::uint64_t pid,std::uint64_t creation,
        const bc2::ChestKnifeProbeWitness* witness,bool fresh)noexcept {
        if(record.reason!=Guard::None)return;
        record.reason=reason;record.phase=phase_;record.input=input.generation;record.elapsedMs=elapsed;
        record.nowNs=now;record.expectedPid=pid;record.expectedCreation=creation;record.space=input.spaceGeneration;
        record.present=witness!=nullptr;record.fresh=fresh;record.held=witness&&Held(*witness);
        record.focused=input.focused;record.headValid=input.headValid;
        record.tracked={input.hands[0].gripTracked&&input.hands[0].aimTracked,input.hands[1].gripTracked&&input.hands[1].aimTracked};
        record.menu=input.menu;if(witness)record.witness=*witness;
    }
    static void ReportGuard(std::ostream& out,const GuardSnapshot& record){
        const auto& w=record.witness;
        out<<"{\"recorded\":"<<(record.reason!=Guard::None)<<",\"reason\":"<<unsigned(record.reason)
           <<",\"phase\":"<<unsigned(record.phase)<<",\"input\":"<<record.input
           <<",\"elapsed_ms\":"<<record.elapsedMs<<",\"now_ns\":"<<record.nowNs
           <<",\"expected_pid\":"<<record.expectedPid<<",\"expected_creation\":"<<record.expectedCreation
           <<",\"space\":"<<record.space<<",\"present\":"<<record.present<<",\"fresh\":"<<record.fresh<<",\"held\":"<<record.held
           <<",\"focused\":"<<record.focused<<",\"head_valid\":"<<record.headValid<<",\"tracked\":["<<record.tracked[0]<<','<<record.tracked[1]
           <<"],\"menu_owner\":"<<unsigned(record.menu.owner)<<",\"menu_epoch\":"<<record.menu.epoch<<",\"menu_deadline_ns\":"<<record.menu.deadlineNs
           <<",\"publication_known\":"<<record.publicationKnown<<",\"publication_succeeded\":"<<record.publicationSucceeded<<",\"publication_code\":"<<record.publicationCode
           <<",\"witness\":{\"version\":"<<w.version<<",\"pid\":"<<w.pid<<",\"creation\":"<<w.creation<<",\"tick_ms\":"<<w.tickMs
           <<",\"observed_ns\":"<<w.observedNs<<",\"deadline_ns\":"<<w.deadlineNs<<",\"input\":"<<w.input<<",\"space\":"<<w.space
           <<",\"actor\":"<<w.actor<<",\"actor_generation\":"<<w.actorGeneration<<",\"weapon\":"<<w.weapon
           <<",\"body_operation\":"<<w.bodyOperation<<",\"draws\":"<<w.draws<<",\"returns\":"<<w.returns<<",\"phase\":"<<w.phase
           <<",\"empty\":"<<w.empty<<",\"held\":"<<w.held<<",\"attack_ready\":"<<w.attackReady<<",\"native_owned\":"<<w.nativeOwned<<",\"recognizer_ready\":"<<w.recognizerReady
           <<",\"wrist_in_grip\":[";
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){if(r||c)out<<',';out<<w.wristInGrip.values[r][c];}
        out<<"]}}";
    }
    GuardSnapshot firstFailure_{},firstStrokeObservation_{};
    bool Fresh(const bc2::ChestKnifeProbeWitness& w,std::uint64_t pid,std::uint64_t creation,
        const interaction::InputFrame& i,std::int64_t now)const noexcept {
        return w.space==i.spaceGeneration&&FreshWitnessSource(w,pid,creation,i,now);
    }
    bool FreshWitnessSource(const bc2::ChestKnifeProbeWitness& w,std::uint64_t pid,std::uint64_t creation,
        const interaction::InputFrame& i,std::int64_t now)const noexcept {
        return now>0&&w.version==2&&w.pid==pid&&w.creation==creation&&w.actor&&w.actorGeneration&&
            w.input&&w.input<i.generation&&w.observedNs&&w.observedNs<=std::uint64_t(now)&&
            w.deadlineNs>std::uint64_t(now)&&w.deadlineNs>w.observedNs&&w.deadlineNs-w.observedNs<=100000000;
    }
    bool Held(const bc2::ChestKnifeProbeWitness& w)const noexcept{return w.held&&w.nativeOwned&&w.bodyOperation==operation_&&w.draws==1&&!w.returns;}
    static math::Vec3 Lerp(math::Vec3 a,math::Vec3 b,float t)noexcept{t=std::clamp(t,0.f,1.f);return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};}
    void Move(KnifeRecipePhase p,std::uint64_t ms,const interaction::InputFrame& i)noexcept{
        phase_=p;at_=ms;if(eventsCount_<events_.size())events_[eventsCount_++]={unsigned(p),ms,i.generation};else ++dropped_;
    }
    void Fail()noexcept{phase_=KnifeRecipePhase::Failed;squeeze_=false;}
    static constexpr math::Vec3 Home{.15f,-.10f,-.20f},Chest{.12f,-.25f,-.15f};
    PlayerGrenadeReceiver menu_;std::optional<bc2::ChestKnifeProbeWitness> pin_;
    bc2::ChestKnifeProbeWitness strokeWitness_{},returned_{};
    struct Event{unsigned phase=0;std::uint64_t ms=0,input=0;};std::array<Event,16> events_{};
    KnifeRecipePhase phase_=KnifeRecipePhase::AwaitEmpty;
    math::Vec3 position_=Home,direction_{};
    std::uint64_t at_=0,lastMs_=0,operation_=0,pressInput_=0,releaseInput_=0,neutralInput_=0;
    std::int64_t neutralNs_=0;
    unsigned eventsCount_=0,dropped_=0,presses_=0,releases_=0,strokes_=0,published_=0,publicationMisses_=0,publicationBusy_=0;
    bool squeeze_=false,lastSqueeze_=false;
};
}
