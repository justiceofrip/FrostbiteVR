#pragma once
#include "OrdinaryGrenadeProbeInput.h"
#include "fvr/interaction/MenuOwnershipPolicy.h"
#include "fvr/ipc/FrameChannel.h"
#include <array>
#include <limits>
#include <ostream>
namespace fvr::probe {
constexpr bool PlayerGrenadeOption(std::wstring_view s)noexcept {
    return s==L"--player-grenade-probe"||s==L"--pairs"||s==L"--static-pose"||s==L"--async";
}
// Finite receiver fixture only. This class reads menu observations and describes
// ordinary controller packets; it grants no native action or menu authority.
class PlayerGrenadeReceiver {
public:
    static constexpr std::uint64_t DurationMs=12000;
    static std::int64_t Nanos(std::int64_t ticks,std::int64_t frequency)noexcept {
        if(ticks<=0||frequency<=0||frequency>1000000000)return 0;
        const auto seconds=ticks/frequency;
        if(seconds>(std::numeric_limits<std::int64_t>::max()-1000000000)/1000000000)return 0;
        return seconds*1000000000+(ticks%frequency)*1000000000/frequency;
    }
    void Decorate(interaction::InputFrame& input,std::uint64_t ms,ipc::ChannelResult read,
        const ipc::MenuState& incoming,std::int64_t nowQpc,std::int64_t frequency)noexcept {
        read_=read;const auto code=unsigned(read);if(code<reads_.size())Count(reads_[code]);
        if(read==ipc::ChannelResult::Ok)retained_=incoming;
        else if(read!=ipc::ChannelResult::Busy)retained_={};
        const auto now=Nanos(nowQpc,frequency),deadline=Nanos(retained_.deadlineQpc,frequency);
        const bool known=ipc::ValidMenuState(retained_)&&nowQpc>0&&retained_.deadlineQpc>nowQpc&&
            now>0&&deadline>now&&deadline-now<=100000000;
        if(!known)retained_={};
        // This fixture displays no menu raster and never sends menu commands.
        // Raw controls still pass through the same pointer/pending-owner policy.
        const auto pointer=pointer_.Update(input,known?&retained_:nullptr,0,0);
        input.menu=ownership_.Update(input,pointer,known?&retained_:nullptr,known?deadline:0,now);
        pointer.FilterGameplayInput(input);
        const auto owner=unsigned(input.menu.owner);if(owner<owners_.size())Count(owners_[owner]);
        if(input.menu.owner!=lastOwner_){lastOwner_=input.menu.owner;Add(1,input,now,unsigned(read));}
        const bool gameplay=interaction::GameplayOwnsInput(input.menu,now);
        if(ms>=5000&&ms<5650&&!gameplay)cancelled_=true;
        // A lost menu lease cancels the one recipe permanently, never pausing
        // its timer or creating a second press after the menu source recovers.
        if(!gameplay||cancelled_)for(auto& h:input.hands){h.held=0;h.stickX=h.stickY=h.trigger=h.squeeze=0;}
        Count(samples_);
    }
    void Published(const interaction::InputFrame& input,bool known,bool success,unsigned code,std::int64_t now)noexcept {
        if(!known||!success){Count(publicationFailures_);Add(4,input,now,code);return;}
        Count(published_);const bool held=bool(input.hands[1].held&interaction::StickClick);
        if(held&&!held_){Count(presses_);Add(2,input,now,code);}
        if(!held&&held_){Count(releases_);Add(3,input,now,code);}
        held_=held;
    }
    bool OneGesture()const noexcept{return presses_==1&&releases_==1&&!held_&&!cancelled_;}
    void Report(std::ostream& out)const {
        out<<"{\"contract\":1,\"real_native_menu\":true,\"native_acceptance\":false,\"headset_tested\":false,\"samples\":"<<samples_
           <<",\"published\":"<<published_<<",\"publication_failures\":"<<publicationFailures_<<",\"click_presses\":"<<presses_
           <<",\"click_releases\":"<<releases_<<",\"trigger_samples\":0,\"menu_cancelled\":"<<(cancelled_?"true":"false")
           <<",\"read_results\":[";for(unsigned i=0;i<reads_.size();++i){if(i)out<<',';out<<reads_[i];}
        out<<"],\"owner_samples\":[";for(unsigned i=0;i<owners_.size();++i){if(i)out<<',';out<<owners_[i];}
        out<<"],\"events_dropped\":"<<dropped_<<",\"events\":[";
        for(unsigned i=0;i<count_;++i){if(i)out<<',';const auto& e=events_[i];
            out<<"{\"kind\":"<<e.kind<<",\"input\":"<<e.input<<",\"now_ns\":"<<e.now<<",\"owner\":"<<unsigned(e.menu.owner)
               <<",\"menu_sequence\":"<<e.menu.sequence<<",\"menu_epoch\":"<<e.menu.epoch<<",\"menu_deadline_ns\":"<<e.menu.deadlineNs
               <<",\"read_result\":"<<e.read<<",\"result\":"<<e.result<<'}';}
        out<<"]}";
    }
private:
    static void Count(unsigned& n)noexcept{if(n<std::numeric_limits<unsigned>::max())++n;}
    void Add(unsigned kind,const interaction::InputFrame& i,std::int64_t now,unsigned result)noexcept {
        if(count_==events_.size()){Count(dropped_);return;}events_[count_++]={kind,unsigned(read_),result,i.generation,now,i.menu};
    }
    struct Event {unsigned kind=0,read=0,result=0;std::uint64_t input=0;std::int64_t now=0;interaction::InputMenuOwnership menu{};};
    interaction::MenuPointer pointer_;interaction::MenuOwnershipPolicy ownership_;ipc::MenuState retained_{};
    ipc::ChannelResult read_=ipc::ChannelResult::Closed;
    interaction::InputMenuOwner lastOwner_=interaction::InputMenuOwner::Unknown;
    std::array<unsigned,5> reads_{};std::array<unsigned,4> owners_{};std::array<Event,32> events_{};
    unsigned samples_=0,published_=0,publicationFailures_=0,presses_=0,releases_=0,count_=0,dropped_=0;
    bool cancelled_=false,held_=false;
};
}
