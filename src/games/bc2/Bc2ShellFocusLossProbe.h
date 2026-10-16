#pragma once
#include <cstdint>
#include "fvr/ipc/FrameChannel.h"
#include <optional>
#include <ostream>
namespace fvr::bc2 {
// Private observation/receiver scheduling only. No native consumer accepts
// these values as ownership, ammunition or stop authority.
struct ShellFocusWitness {
    std::uint64_t space=0,cycle=0,input=0;
    std::int64_t observedNs=0,deadlineNs=0,completedObservedNs=0;
    int loaded=0,reserve=0,capacity=0;
};
inline bool ShellFocusSource(std::uint64_t sequence,std::uint64_t space,std::int64_t observed,
    std::int64_t deadline,std::int64_t now)noexcept {
    return sequence&&space&&observed>0&&observed<=now&&now<deadline&&deadline-observed<=100000000;
}
inline bool ValidShellFocusWitness(const ShellFocusWitness& w,std::int64_t now)noexcept {
    return w.cycle&&w.completedObservedNs>0&&w.completedObservedNs<=w.observedNs&&
        w.loaded>0&&w.loaded<w.capacity&&w.reserve>0&&w.capacity<=1000&&
        ShellFocusSource(w.input,w.space,w.observedNs,w.deadlineNs,now);
}
inline void ReportShellFocusWitness(std::ostream& out,const ShellFocusWitness& w){
    out<<"{\"space\":"<<w.space<<",\"cycle\":"<<w.cycle<<",\"input\":"<<w.input
       <<",\"observed_ns\":"<<w.observedNs<<",\"deadline_ns\":"<<w.deadlineNs
       <<",\"completed_observed_ns\":"<<w.completedObservedNs<<",\"loaded\":"<<w.loaded
       <<",\"reserve\":"<<w.reserve<<",\"capacity\":"<<w.capacity<<'}';
}
class ShellFocusReceiver {
public:
    static constexpr std::int64_t LossNs=500000000;
    static void ApplyFocus(interaction::InputFrame& input,bool focused)noexcept {
        input.focused=focused;
        // Match normal unfocused input. Head/space/reference remain current;
        // controller actions and tracking are inactive until a fresh sample.
        if(!focused)input.hands={};
    }
    bool Publication(const interaction::InputFrame& input,bool known,bool succeeded,unsigned code,
        bool neutral,std::int64_t now)noexcept {
        ++publicationAttempts_;lastPublication_={input.generation,input.spaceGeneration,now,code,
            known,succeeded,input.focused,input.headValid,neutral,true};
        for(const auto& h:input.hands)lastPublication_.controllersInactive&=
            !h.active&&!h.held&&!h.touchActive&&!h.touched&&!h.gripTracked&&!h.aimTracked;
        if(!known||succeeded!=(code==unsigned(ipc::ChannelResult::Ok))){failed_=true;return false;}
        if(!succeeded){
            if(code==unsigned(ipc::ChannelResult::Busy)){++publicationBusy_;return true;}
            failed_=true;return false;
        }
        ++publicationSucceeded_;
        return Published(input.generation,input.spaceGeneration,input.focused,neutral,now);
    }
    bool Begin(const ShellFocusWitness& w,std::int64_t now)noexcept {
        if(began_||failed_||!ValidShellFocusWitness(w,now)||now>INT64_MAX-LossNs)return false;
        witness_=w;began_=lastNow_=now;return true;
    }
    bool Focused(std::int64_t now)noexcept {
        if(!began_)return true;
        if(now<lastNow_){failed_=true;return false;}lastNow_=now;
        return firstLost_&&now-firstLost_>=LossNs;
    }
    bool Published(std::uint64_t sequence,std::uint64_t space,bool focused,bool neutral,std::int64_t now)noexcept {
        if(!began_)return true;
        if(failed_||now<lastNow_||space!=witness_.space||sequence<=witness_.input||sequence<=lastInput_||!neutral){failed_=true;return false;}
        lastNow_=now;lastInput_=sequence;
        if(!focused){
            if(returned_){failed_=true;return false;}
            if(!firstLost_){firstLost_=now;firstLostInput_=sequence;}
            lastLost_=now;++lostSamples_;
        }else {
            if(!firstLost_||now-firstLost_<LossNs){failed_=true;return false;}
            if(!returned_){returned_=now;returnInput_=sequence;}
        }return true;
    }
    bool Began()const noexcept{return began_!=0;}
    bool Completed()const noexcept{return returned_&&lostSamples_>=2&&!failed_;}
    void Report(std::ostream& out)const {
        out<<"{\"completed\":"<<(Completed()?"true":"false")<<",\"failed\":"<<(failed_?"true":"false")
           <<",\"begin_ns\":"<<began_<<",\"first_lost_ns\":"<<firstLost_<<",\"last_lost_ns\":"<<lastLost_
           <<",\"first_lost_input\":"<<firstLostInput_<<",\"return_ns\":"<<returned_<<",\"return_input\":"<<returnInput_
           <<",\"lost_samples\":"<<lostSamples_<<",\"tracking_withheld\":false,\"native_authority\":false,\"witness\":";
        ReportShellFocusWitness(out,witness_);
        out<<",\"head_tracking_preserved\":true,\"controller_input_withheld\":true"
           <<",\"publication_attempts\":"<<publicationAttempts_<<",\"publication_succeeded\":"<<publicationSucceeded_
           <<",\"publication_busy\":"<<publicationBusy_<<",\"last_publication\":{\"sequence\":"<<lastPublication_.sequence
           <<",\"space\":"<<lastPublication_.space<<",\"attempted_ns\":"<<lastPublication_.now<<",\"code\":"<<lastPublication_.code
           <<",\"known\":"<<(lastPublication_.known?"true":"false")<<",\"succeeded\":"<<(lastPublication_.succeeded?"true":"false")
           <<",\"focused\":"<<(lastPublication_.focused?"true":"false")<<",\"head_valid\":"<<(lastPublication_.headValid?"true":"false")
           <<",\"neutral\":"<<(lastPublication_.neutral?"true":"false")
           <<",\"controllers_inactive\":"<<(lastPublication_.controllersInactive?"true":"false")<<"}}";
    }
private:
    struct PublicationOutcome {
        std::uint64_t sequence=0,space=0;std::int64_t now=0;unsigned code=0;
        bool known=false,succeeded=false,focused=false,headValid=false,neutral=false,controllersInactive=false;
    } lastPublication_;
    std::uint64_t publicationAttempts_=0,publicationSucceeded_=0,publicationBusy_=0;
    ShellFocusWitness witness_{};bool failed_=false;
    std::int64_t began_=0,lastNow_=0,firstLost_=0,lastLost_=0,returned_=0;
    std::uint64_t firstLostInput_=0,returnInput_=0,lastInput_=0,lostSamples_=0;
};
class ShellFocusEpisode {
public:
    bool Arm(std::uint64_t cycle,std::uint64_t space,int loaded,int reserve,int capacity,std::int64_t now)noexcept {
        if(target_.cycle||!cycle||!space||now<=0||loaded<=0||loaded>=capacity||reserve<=0)return false;
        target_={space,cycle,0,0,0,now,loaded,reserve,capacity};return true;
    }
    bool Source(std::uint64_t sequence,std::uint64_t space,std::int64_t observed,std::int64_t deadline,
        std::int64_t now,bool focused,bool neutral)noexcept {
        if(failed_||!target_.cycle||space!=target_.space||!neutral||
           !ShellFocusSource(sequence,space,observed,deadline,now)||now<lastNow_||
           sequence<lastInput_||observed<lastObserved_||(sequence==lastInput_&&(observed!=lastObserved_||deadline!=lastDeadline_))){failed_=true;return false;}
        lastNow_=now;lastInput_=sequence;lastObserved_=observed;lastDeadline_=deadline;
        if(!focused){
            if(returned_||observed<target_.completedObservedNs){failed_=true;return false;}
            if(!firstLost_){firstLost_=now;firstLostInput_=sequence;firstLostObserved_=observed;firstLostDeadline_=deadline;}
            lastLostInput_=sequence;lastLostObserved_=observed;return true;
        }
        if(firstLost_){
            if(sequence<=lastLostInput_||observed<=lastLostObserved_){failed_=true;return false;}
            if(!returned_){returned_=now;returnInput_=sequence;returnObserved_=observed;returnDeadline_=deadline;}
        }else if(observed>=target_.completedObservedNs){
            ready_=target_;ready_->input=sequence;ready_->observedNs=observed;ready_->deadlineNs=deadline;
        }return true;
    }
    bool Counts(int loaded,int reserve,bool settled,std::int64_t now)noexcept {
        if(failed_||!target_.cycle||loaded!=target_.loaded||reserve!=target_.reserve||now<lastNow_){failed_=true;return false;}
        lastNow_=now;
        if(!returned_||!settled||now>=lastDeadline_){stableStart_=stableLast_=0;stableSamples_=0;return true;}
        if(stableLast_&&now-stableLast_>100000000){stableStart_=0;stableSamples_=0;}
        if(!stableStart_)stableStart_=now;stableLast_=now;++stableSamples_;return true;
    }
    std::optional<ShellFocusWitness> Ready(std::int64_t now)const noexcept {
        return !failed_&&!firstLost_&&ready_&&ValidShellFocusWitness(*ready_,now)?ready_:std::nullopt;
    }
    bool Completed()const noexcept{return !failed_&&returned_&&stableSamples_>=30&&stableLast_-stableStart_>=3000000000ll;}
    void Report(std::ostream& out)const {
        out<<"{\"enabled\":true,\"completed\":"<<(Completed()?"true":"false")<<",\"failed\":"<<(failed_?"true":"false")
           <<",\"first_lost_ns\":"<<firstLost_<<",\"first_lost_input\":"<<firstLostInput_<<",\"first_lost_observed_ns\":"<<firstLostObserved_
           <<",\"first_lost_deadline_ns\":"<<firstLostDeadline_<<",\"last_lost_input\":"<<lastLostInput_<<",\"last_lost_observed_ns\":"<<lastLostObserved_
           <<",\"return_ns\":"<<returned_<<",\"return_input\":"<<returnInput_<<",\"return_observed_ns\":"<<returnObserved_
           <<",\"return_deadline_ns\":"<<returnDeadline_<<",\"stable_start_ns\":"<<stableStart_<<",\"stable_last_ns\":"<<stableLast_
           <<",\"stable_samples\":"<<stableSamples_<<",\"native_authority\":false,\"target\":";
        ReportShellFocusWitness(out,target_);out<<'}';
    }
private:
    ShellFocusWitness target_{};std::optional<ShellFocusWitness> ready_;bool failed_=false;
    std::int64_t lastNow_=0,lastObserved_=0,lastDeadline_=0,firstLost_=0,firstLostObserved_=0,firstLostDeadline_=0,
        lastLostObserved_=0,returned_=0,returnObserved_=0,returnDeadline_=0,stableStart_=0,stableLast_=0;
    std::uint64_t lastInput_=0,firstLostInput_=0,lastLostInput_=0,returnInput_=0,stableSamples_=0;
};
}
