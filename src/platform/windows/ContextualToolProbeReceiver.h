#pragma once
#include "../../games/bc2/Bc2ContextualToolProbeWitness.h"
#include "PlayerGrenadeReceiver.h"
#include <string_view>

namespace fvr::probe {
constexpr bool ContextualToolProbeOption(std::wstring_view s)noexcept {
    return s==L"--contextual-tool-draw-probe"||s==L"--pairs"||s==L"--static-pose"||s==L"--async";
}
enum class ToolRecipePhase:unsigned {AwaitEmpty,GripNeutral,DrawClick,AwaitHeld,Hold,ReleaseNeutral,ReturnClick,AwaitHide,Complete,Failed};
// Exactly one finite draw/hold/return recipe. Only native observations advance
// selection/hold/hide; elapsed time never acknowledges native consumption.
class ContextualToolProbeReceiver {
public:
    static constexpr std::uint64_t DurationMs=18000;
    static constexpr bool PairRequestDue(std::uint64_t ms,unsigned consumed)noexcept {
        return consumed<240&&ms>=std::uint64_t(consumed)*14000/240;
    }
    void Update(interaction::InputFrame& i,std::uint64_t ms,std::int64_t now,
        std::uint64_t pid,std::uint64_t creation,const bc2::ContextualToolProbeWitness* w,
        ipc::ChannelResult read,const ipc::MenuState& state,std::int64_t qpc,std::int64_t frequency)noexcept {
        for(auto& h:i.hands){h.held=h.touched=h.touchActive=0;h.squeeze=h.trigger=h.stickX=h.stickY=0;}
        menu_.Decorate(i,0,read,state,qpc,frequency);
        const bool gameplay=interaction::GameplayOwnsInput(i.menu,now);
        const bool fresh=w&&Fresh(*w,pid,creation,i,now);
        if((lastMs_&&ms<lastMs_)||(ms>=13000&&phase_!=ToolRecipePhase::Complete))Fail();
        lastMs_=ms;
        if(fresh&&pin_&&!Same(*w,*pin_))Fail();
        if(phase_!=ToolRecipePhase::AwaitEmpty&&phase_!=ToolRecipePhase::Complete&&!gameplay)Fail();
        const auto age=ms>=at_?ms-at_:0;
        switch(phase_){
        case ToolRecipePhase::AwaitEmpty:
            if(ms>=1000&&gameplay&&fresh&&w->empty&&!w->bodyOperation&&!w->equipCommits&&!w->hideCompletions&&
                w->selectedSlot<=1&&CampaignBinoculars(*w)){
                pin_=*w;Move(ToolRecipePhase::GripNeutral,ms,i);}
            break;
        case ToolRecipePhase::GripNeutral:
            if(age>=250){if(!fresh||!w->empty)Fail();else Move(ToolRecipePhase::DrawClick,ms,i);}
            break;
        case ToolRecipePhase::DrawClick:
            if(age>=120)Move(ToolRecipePhase::AwaitHeld,ms,i);
            break;
        case ToolRecipePhase::AwaitHeld:
            if(fresh&&w->held&&w->holdToken&&w->bodyOperation&&w->selectedSlot==4&&w->equipCommits==1&&
                w->selectedObservations==1&&!w->returnCommits&&!w->hideCompletions&&!w->revoked){
                operation_=w->bodyOperation;hold_=*w;Move(ToolRecipePhase::Hold,ms,i);}
            break;
        case ToolRecipePhase::Hold:
            if(fresh&&!Held(*w)){Fail();break;}
            if(age>=700&&fresh&&Held(*w))Move(ToolRecipePhase::ReleaseNeutral,ms,i);
            break;
        case ToolRecipePhase::ReleaseNeutral:
            if(age>=250&&fresh){
                if(w->bodyOperation!=operation_||w->selectedSlot!=4||w->held||w->holdToken||w->revoked)Fail();
                else Move(ToolRecipePhase::ReturnClick,ms,i);}
            break;
        case ToolRecipePhase::ReturnClick:
            if(age>=120)Move(ToolRecipePhase::AwaitHide,ms,i);
            break;
        case ToolRecipePhase::AwaitHide:
            if(fresh&&w->empty&&!w->bodyOperation&&!w->held&&!w->holdToken&&w->lastBodyReturn==operation_&&
                w->equipCommits==1&&w->selectedObservations==1&&w->returnCommits==1&&w->hideCompletions==1&&
                pin_&&w->selectedSlot==pin_->selectedSlot&&returnPressInput_&&w->input>=returnPressInput_){
                returned_=*w;Move(ToolRecipePhase::Complete,ms,i);}
            break;
        case ToolRecipePhase::Complete:case ToolRecipePhase::Failed:break;
        }
        if(gameplay&&phase_>=ToolRecipePhase::GripNeutral&&phase_<=ToolRecipePhase::Hold)i.hands[1].squeeze=1;
        if(gameplay&&(phase_==ToolRecipePhase::DrawClick||phase_==ToolRecipePhase::ReturnClick))i.hands[1].held=interaction::StickClick;
    }
    void Published(const interaction::InputFrame& i,bool known,bool succeeded,unsigned code,std::int64_t now)noexcept {
        const bool delivered=known&&succeeded&&code==unsigned(ipc::ChannelResult::Ok);
        menu_.Published(i,known,delivered,code,now);
        if(!delivered){++misses_;if(known&&!succeeded&&code==unsigned(ipc::ChannelResult::Busy)){++busy_;return;}Fail();return;}
        ++published_;const bool click=(i.hands[1].held&interaction::StickClick)!=0;
        if(click&&!lastClick_){++presses_;if(presses_==1&&phase_==ToolRecipePhase::DrawClick)drawPressInput_=i.generation;
            else if(presses_==2&&phase_==ToolRecipePhase::ReturnClick&&operation_)returnPressInput_=i.generation;else Fail();}
        if(!click&&lastClick_)++releases_;
        lastClick_=click;
        if(phase_==ToolRecipePhase::Complete&&i.generation>returned_.input&&!click&&i.hands[1].squeeze==0){
            neutralInput_=i.generation;neutralNs_=now;}
    }
    ToolRecipePhase Phase()const noexcept{return phase_;}
    bool Completed()const noexcept{return phase_==ToolRecipePhase::Complete&&presses_==2&&releases_==2&&neutralInput_>returned_.input;}
    void Report(std::ostream& out)const {
        out<<"{\"contract\":1,\"finite_contextual_custody\":true,\"normal_enabled\":false,\"native_calls\":0,\"fire_or_zoom_requested\":false"
            <<",\"phase\":"<<unsigned(phase_)<<",\"completed\":"<<Completed()<<",\"published\":"<<published_
            <<",\"publication_misses\":"<<misses_<<",\"publication_busy\":"<<busy_<<",\"click_presses\":"<<presses_<<",\"click_releases\":"<<releases_
            <<",\"draw_press_input\":"<<drawPressInput_<<",\"return_press_input\":"<<returnPressInput_<<",\"body_operation\":"<<operation_
            <<",\"hold_input\":"<<hold_.input<<",\"hold_token\":"<<hold_.holdToken<<",\"return_input\":"<<returned_.input
            <<",\"hold_observed_ns\":"<<hold_.observedNs<<",\"hold_deadline_ns\":"<<hold_.deadlineNs
            <<",\"actor\":"<<hold_.actor<<",\"actor_generation\":"<<hold_.actorGeneration<<",\"space\":"<<hold_.space
            <<",\"original_weapon\":"<<hold_.originalWeapon<<",\"original_data\":"<<hold_.originalData
            <<",\"tool_weapon\":"<<hold_.toolWeapon<<",\"tool_data\":"<<hold_.toolData
            <<",\"return_observed_ns\":"<<returned_.observedNs<<",\"return_deadline_ns\":"<<returned_.deadlineNs
            <<",\"last_body_return\":"<<returned_.lastBodyReturn<<",\"neutral_input\":"<<neutralInput_<<",\"neutral_ns\":"<<neutralNs_
            <<",\"events_dropped\":"<<dropped_<<",\"events\":[";
        for(unsigned n=0;n<eventsCount_;++n){if(n)out<<',';const auto& e=events_[n];out<<'['<<e.phase<<','<<e.ms<<','<<e.input<<']';}
        out<<"],\"menu\":";menu_.Report(out);out<<'}';
    }
private:
    static bool CampaignBinoculars(const bc2::ContextualToolProbeWitness& w)noexcept {
        constexpr std::string_view asset="Objects/Weapons/Handheld/LaserDesignator/SP_LaserDesignator_Dummy";
        return w.toolAsset[asset.size()]=='\0'&&std::string_view(w.toolAsset.data(),asset.size())==asset;
    }
    static bool Same(const bc2::ContextualToolProbeWitness& a,const bc2::ContextualToolProbeWitness& b)noexcept {
        return a.actor==b.actor&&a.actorGeneration==b.actorGeneration&&a.weak==b.weak&&a.inventory==b.inventory&&a.space==b.space&&
            a.originalWeapon==b.originalWeapon&&a.originalData==b.originalData&&a.toolWeapon==b.toolWeapon&&a.toolData==b.toolData&&a.toolAsset==b.toolAsset;
    }
    static bool Fresh(const bc2::ContextualToolProbeWitness& w,std::uint64_t pid,std::uint64_t creation,
        const interaction::InputFrame& i,std::int64_t now)noexcept {
        return bc2::ContextualToolProbeFresh(w,now)&&w.pid==pid&&w.creation==creation&&w.actor&&w.actorGeneration&&w.weak&&w.inventory&&
            w.originalWeapon&&w.originalData&&w.toolWeapon&&w.toolData&&w.space==i.spaceGeneration&&w.input&&w.input<i.generation;
    }
    bool Held(const bc2::ContextualToolProbeWitness& w)const noexcept {
        return w.held&&w.holdToken==hold_.holdToken&&w.bodyOperation==operation_&&w.selectedSlot==4&&
            w.equipCommits==1&&w.selectedObservations==1&&!w.returnCommits&&!w.hideCompletions&&!w.revoked;
    }
    void Move(ToolRecipePhase phase,std::uint64_t ms,const interaction::InputFrame& i)noexcept {
        phase_=phase;at_=ms;if(eventsCount_<events_.size())events_[eventsCount_++]={unsigned(phase),ms,i.generation};else ++dropped_;
    }
    void Fail()noexcept{phase_=ToolRecipePhase::Failed;}
    PlayerGrenadeReceiver menu_;std::optional<bc2::ContextualToolProbeWitness> pin_;
    bc2::ContextualToolProbeWitness hold_{},returned_{};
    struct Event{unsigned phase=0;std::uint64_t ms=0,input=0;};std::array<Event,16> events_{};
    ToolRecipePhase phase_=ToolRecipePhase::AwaitEmpty;
    std::uint64_t at_=0,lastMs_=0,operation_=0,drawPressInput_=0,returnPressInput_=0,neutralInput_=0;
    std::int64_t neutralNs_=0;
    unsigned eventsCount_=0,dropped_=0,published_=0,misses_=0,busy_=0,presses_=0,releases_=0;
    bool lastClick_=false;
};
}
