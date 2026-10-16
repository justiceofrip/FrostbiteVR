#pragma once
#include "PlayerGrenadeReceiver.h"
#include <algorithm>
#include <string_view>
namespace fvr::probe {
constexpr bool NormalKnifeHeldCloseOption(std::wstring_view option)noexcept {
    return option==L"--normal-knife-held-close"||option==L"--pairs"||option==L"--static-pose"||option==L"--async"||option==L"--no-image-files";
}
// Receiver-only physical schedule. No diagnostic/native readiness file, native
// call or native acknowledgement. The final native close receipt decides whether
// an actually owned knife was returned; an unused session is not acceptance.
class NormalKnifeHeldCloseReceiver {
public:
    static constexpr std::uint64_t DurationMs=12000;
    static constexpr std::int64_t OriginalLeaseNs=100000000;
    static constexpr bool PairRequestDue(std::uint64_t ms,unsigned consumed)noexcept {
        return consumed<240&&ms>=std::uint64_t(consumed)*11000/240;
    }
    enum class Failure:unsigned {None,Clock,Setup,Menu,Tracking,Space,Process,Publication,Gap,ExtraEdge,ExplicitRelease};
    void Update(interaction::InputFrame& input,std::uint64_t ms,std::int64_t now,bool processCurrent,
        unsigned consumed,ipc::ChannelResult read,const ipc::MenuState& state,std::int64_t qpc,std::int64_t frequency)noexcept {
        for(auto& h:input.hands){h.held=h.touched=h.touchActive=0;h.squeeze=h.trigger=h.stickX=h.stickY=0;}
        menu_.Decorate(input,0,read,state,qpc,frequency);
        const bool gameplay=interaction::GameplayOwnsInput(input.menu,now);
        bool tracking=input.focused&&input.headValid;
        for(const auto& h:input.hands)tracking=tracking&&h.gripTracked&&h.aimTracked&&
            (h.active&interaction::Components)==interaction::Components;
        if((lastMs_&&ms<lastMs_)||now<=0)Fail(Failure::Clock,input,ms,now);
        lastMs_=ms;
        if(!processCurrent)Fail(Failure::Process,input,ms,now);
        if(ready_&&input.spaceGeneration!=space_)Fail(Failure::Space,input,ms,now);
        if(ready_&&!gameplay)Fail(Failure::Menu,input,ms,now);
        if(ready_&&!tracking)Fail(Failure::Tracking,input,ms,now);
        if(!ready_&&ms<1500&&gameplay&&tracking&&consumed&&processCurrent){
            ready_=true;space_=input.spaceGeneration;readyInput_=input.generation;readyNs_=now;}
        if(ms>=1500&&!ready_)Fail(Failure::Setup,input,ms,now);
        math::Vec3 position=Home;
        if(ms>=1500&&ms<2200)position=Lerp(Home,Chest,float(ms-1500)/700.f);
        else if(ms>=2200&&ms<4250)position=Chest;
        else if(ms>=4250&&ms<6050)position=Lerp(Chest,Home,float(ms-4250)/1800.f);
        const unsigned phase=ms<1500?0:ms<2200?1:ms<2450?2:ms<4250?3:ms<6050?4:5;
        if(phase!=phase_){phase_=phase;if(eventCount_<events_.size())events_[eventCount_++]={phase,ms,input.generation};else ++dropped_;}
        input.hands[1].grip.position=position;
        input.hands[1].squeeze=ready_&&gameplay&&tracking&&!Failed()&&ms>=2450&&ms<DurationMs?1.f:0.f;
        for(auto& h:input.hands)h.aim=h.grip;
    }
    void Published(const interaction::InputFrame& input,bool known,bool success,unsigned code,
        std::int64_t before,std::int64_t after)noexcept {
        menu_.Published(input,known,known&&success&&code==unsigned(ipc::ChannelResult::Ok),code,after);
        if(before<=0||after<before){Fail(Failure::Clock,input,lastMs_,after);return;}
        if(lastHeld_&&lastSuccessBefore_){
            const auto gap=after-lastSuccessBefore_;maxHeldGapNs_=(std::max)(maxHeldGapNs_,gap);
            if(gap>=OriginalLeaseNs)Fail(Failure::Gap,input,lastMs_,after);
        }
        lastInput_=input.generation;lastBefore_=before;lastAfter_=after;lastKnown_=known;lastSuccess_=success;lastCode_=code;
        const bool delivered=known&&success&&code==unsigned(ipc::ChannelResult::Ok);
        if(!delivered){++misses_;if(known&&!success&&code==unsigned(ipc::ChannelResult::Busy)){++busy_;return;}
            Fail(Failure::Publication,input,lastMs_,after);return;}
        if(after-before>=OriginalLeaseNs)Fail(Failure::Gap,input,lastMs_,after);
        ++published_;const bool held=input.hands[1].squeeze>=.65f;
        if(held&&!lastHeld_){++presses_;if(!pressInput_){pressInput_=input.generation;pressBefore_=before;}
            if(presses_!=1)Fail(Failure::ExtraEdge,input,lastMs_,after);}
        if(!held&&lastHeld_){++releases_;Fail(Failure::ExplicitRelease,input,lastMs_,after);}
        lastHeld_=held;lastSuccessInput_=input.generation;lastSuccessBefore_=before;lastSuccessAfter_=after;
        if(held)++heldPublications_;
    }
    void Finish(std::uint64_t ms,std::int64_t now)noexcept {
        finishedMs_=ms;finishedNs_=now;
        complete_=ms>=DurationMs&&ready_&&!Failed()&&presses_==1&&releases_==0&&lastHeld_&&heldPublications_>1&&
            lastSuccessBefore_>0&&now>=lastSuccessAfter_&&now-lastSuccessBefore_<OriginalLeaseNs;
    }
    bool Failed()const noexcept{return failure_!=Failure::None;}
    bool Complete()const noexcept{return complete_;}
    Failure Reason()const noexcept{return failure_;}
    void Report(std::ostream& out)const {
        out<<"{\"schema\":\"fvr.normal408.knife_held_close_receiver.v1\",\"emulated_host\":true,\"actual_xr\":false"
           <<",\"native_authority\":false,\"native_acceptance\":false,\"witness_files\":0,\"duration_ms\":"<<DurationMs
           <<",\"completed\":"<<complete_<<",\"ready_input\":"<<readyInput_<<",\"ready_ns\":"<<readyNs_<<",\"space\":"<<space_
           <<",\"published\":"<<published_<<",\"held_publications\":"<<heldPublications_<<",\"misses\":"<<misses_<<",\"busy\":"<<busy_
           <<",\"grip_presses\":"<<presses_<<",\"grip_releases\":"<<releases_<<",\"press_input\":"<<pressInput_<<",\"press_before_ns\":"<<pressBefore_
           <<",\"last_success_input\":"<<lastSuccessInput_<<",\"last_success_before_ns\":"<<lastSuccessBefore_<<",\"last_success_after_ns\":"<<lastSuccessAfter_
           <<",\"last_success_held\":"<<lastHeld_<<",\"max_held_publication_gap_ns\":"<<maxHeldGapNs_
           <<",\"finished_ms\":"<<finishedMs_<<",\"finished_ns\":"<<finishedNs_<<",\"trigger_publications\":0,\"stroke_requests\":0"
           <<",\"last_outcome\":{\"input\":"<<lastInput_<<",\"before_ns\":"<<lastBefore_<<",\"after_ns\":"<<lastAfter_
           <<",\"known\":"<<lastKnown_<<",\"succeeded\":"<<lastSuccess_<<",\"code\":"<<lastCode_<<"}"
           <<",\"first_failure\":{\"reason\":"<<unsigned(failure_)<<",\"input\":"<<failureInput_<<",\"elapsed_ms\":"<<failureMs_<<",\"now_ns\":"<<failureNs_<<"}"
           <<",\"events_dropped\":"<<dropped_<<",\"events\":[";
        for(unsigned n=0;n<eventCount_;++n){if(n)out<<',';const auto& e=events_[n];out<<'['<<e.phase<<','<<e.ms<<','<<e.input<<']';}
        out<<"],\"menu\":";menu_.Report(out);out<<'}';
    }
private:
    void Fail(Failure why,const interaction::InputFrame& input,std::uint64_t ms,std::int64_t now)noexcept {
        if(Failed())return;failure_=why;failureInput_=input.generation;failureMs_=ms;failureNs_=now;
    }
    static math::Vec3 Lerp(math::Vec3 a,math::Vec3 b,float t)noexcept {t=std::clamp(t,0.f,1.f);return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};}
    static constexpr math::Vec3 Home{.15f,-.10f,-.20f},Chest{.12f,-.25f,-.15f};
    struct Event{unsigned phase=0;std::uint64_t ms=0,input=0;};std::array<Event,8> events_{};
    PlayerGrenadeReceiver menu_;Failure failure_=Failure::None;
    unsigned phase_=0,eventCount_=0,dropped_=0,published_=0,heldPublications_=0,misses_=0,busy_=0,presses_=0,releases_=0,lastCode_=0;
    std::uint64_t lastMs_=0,space_=0,readyInput_=0,pressInput_=0,lastInput_=0,lastSuccessInput_=0,finishedMs_=0,failureInput_=0,failureMs_=0;
    std::int64_t readyNs_=0,pressBefore_=0,lastBefore_=0,lastAfter_=0,lastSuccessBefore_=0,lastSuccessAfter_=0,maxHeldGapNs_=0,finishedNs_=0,failureNs_=0;
    bool ready_=false,lastHeld_=false,complete_=false,lastKnown_=false,lastSuccess_=false;
};
}
