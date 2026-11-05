#pragma once
#include "Bc2InputBinding.h"
#include "fvr/interaction/ControllerInput.h"
#include <atomic>
#include <cstring>
#include <string_view>
#include <mutex>
#include <ostream>

namespace fvr::bc2 {
// One observed control transition in one local Gather. -1 means that stage was
// not observed. A failed readback never contributes zero action values.
struct ExpackAttempt {
    std::uint64_t equipmentEpoch=0,inputEpoch=0,space=0,sequence=0,bSource=0;
    std::int64_t receivedQpc=0,deadlineQpc=0;
    unsigned gather=0,weapon=0,data=0,controls=0,held=0,changed=0,firstHeld=0;
    bool grip=false,aim=false,focused=false,head=false,heldFollowup=false;
    int nativePlaying=-1,nativeAim=-1,semanticReload=-1,routeReload=-1,settled=-1,snapshot=-1;
    int leftKind=-1,rightKind=-1,rightHold=-1,releasedRightKind=-1,releasedRightReason=-1;
    int holsterPhase=-1,bodyBlocked=-1,tool=-1,postHolsterBlocked=-1,holsterSuppression=-1,detachedSuppression=-1;
    int dispatchZoom=-1,dispatchReload=-1,dispatchFire=-1;
    const char* route="not_reached";
    const char* write="no_ordinary_commit";
    struct Cache {bool zoom14,reload29;std::uint32_t fire8Bits;};
    std::optional<Cache> final;

    // Invoked only after ordinary Commit and the last same-Gather overrides.
    // One bounded read samples the float and the two buttons together.
    template<class Fresh,class SameOwner,class Read>
    void ReadCommitted(Fresh fresh,SameOwner sameOwner,Read read) noexcept {
        final.reset();write="committed_unverified";
        if(!fresh()||!sameOwner()||!fresh())return;
        constexpr unsigned fire=8+4*unsigned(EntryAction::Fire),buttons=0x98;
        std::array<std::byte,buttons+4-fire> bytes;
        if(!read(fire,bytes.data(),bytes.size())||!fresh()||!sameOwner()||!fresh())return;
        std::uint32_t word=0,fire8=0;
        std::memcpy(&fire8,bytes.data(),4);
        std::memcpy(&word,bytes.data()+buttons-fire,4);
        final=Cache{bool(word&(1u<<unsigned(EntryAction::Zoom))),bool(word&(1u<<unsigned(EntryAction::Reload))),fire8};
        write="committed_readback";
    }

    static int ReloadLevels(const interaction::ActionOutput& out) noexcept {
        return int(bool(out.held&interaction::Reload)) | (int(bool(out.pressed&interaction::Reload))<<1)
            | (int(bool(out.released&interaction::Reload))<<2);
    }

    void Report(std::ostream& out)const {
        out<<"{\"equipment_epoch\":"<<equipmentEpoch<<",\"input_epoch\":"<<inputEpoch<<",\"space\":"<<space<<",\"input\":"<<sequence
           <<",\"b_source\":"<<bSource<<",\"held_followup\":"<<int(heldFollowup)<<",\"gather\":"<<gather<<",\"weapon\":"<<weapon<<",\"data\":"<<data<<",\"active_controls\":"<<controls
           <<",\"held_controls\":"<<held<<",\"changed_controls\":"<<changed<<",\"first_seen_held\":"<<firstHeld
           <<",\"focused\":"<<int(focused)<<",\"head_valid\":"<<int(head)<<",\"right_grip_tracked\":"<<int(grip)<<",\"right_aim_tracked\":"<<int(aim)<<",\"native_playing\":"<<nativePlaying
           <<",\"native_aim_valid\":"<<nativeAim<<",\"semantic_reload\":"<<semanticReload<<",\"route_input_reload\":"<<routeReload
           <<",\"settled\":"<<settled<<",\"snapshot_status\":"<<snapshot<<",\"route\":\""<<route<<"\",\"left_claim_kind\":"<<leftKind
           <<",\"right_claim_kind\":"<<rightKind<<",\"right_exact_gun_hold\":"<<rightHold<<",\"released_right_kind\":"<<releasedRightKind
           <<",\"released_right_reason\":"<<releasedRightReason<<",\"holster_phase\":"<<holsterPhase<<",\"body_draw_blocked\":"<<bodyBlocked
           <<",\"temporary_tool\":"<<tool<<",\"post_holster_blocked\":"<<postHolsterBlocked<<",\"holster_suppression_receipt\":"<<holsterSuppression
           <<",\"detached_suppression_receipt\":"<<detachedSuppression<<",\"dispatch_zoom\":"<<dispatchZoom<<",\"dispatch_reload\":"<<dispatchReload
           <<",\"dispatch_fire\":"<<dispatchFire<<",\"write\":\""<<write<<"\",\"final\":";
        if(final)out<<"{\"zoom14\":"<<int(final->zoom14)<<",\"reload29\":"<<int(final->reload29)<<",\"fire8_float_bits\":"<<final->fire8Bits<<'}';
        else out<<"null";
        out<<'}';
    }
};

class ExpackAttemptTrace {
    // Gather has a verified single input thread. Only completed rows cross to Report.
    std::uint64_t equipment_=0,epoch_=0,space_=0,sequence_=0,bSource_=0;
    std::int64_t received_=0,deadline_=0;
    bool heldCheck_=false;
    unsigned controls_=0,held_=0;
    std::array<ExpackAttempt,32> rows_{};
    std::uint64_t total_=0;
    std::mutex mutex_;
    std::atomic<unsigned> drops_=0;
    ExpackAttempt Row(const interaction::InputFrame& input)const noexcept {
        ExpackAttempt row;
        row.equipmentEpoch=equipment_;row.inputEpoch=epoch_;row.space=space_;row.sequence=sequence_;row.bSource=bSource_;
        row.receivedQpc=received_;row.deadlineQpc=deadline_;row.controls=controls_;row.held=held_;
        row.grip=input.hands[1].gripTracked;row.aim=input.hands[1].aimTracked;row.focused=input.focused;row.head=input.headValid;
        return row;
    }
public:
    std::optional<ExpackAttempt> Observe(std::uint64_t equipment,std::uint64_t inputEpoch,
        const interaction::InputFrame& input,std::int64_t now,std::int64_t deadline) noexcept {
        if(!equipment||!input.generation||!input.spaceGeneration||!inputEpoch||now<=0||now>=deadline){
            controls_=0;bSource_=0;heldCheck_=false;return {};
        }
        const bool same=equipment==equipment_&&inputEpoch==epoch_&&input.spaceGeneration==space_;
        received_=now;
        if(same&&input.generation==sequence_)return {};
        const unsigned previous=same&&input.generation>sequence_&&now<deadline_?controls_:0;
        const auto& right=input.hands[1];
        const unsigned controls=(right.active&interaction::Secondary?1u:0u)|(right.active&interaction::Trigger?2u:0u);
        const unsigned held=controls&((right.held&interaction::Secondary?1u:0u)|(right.trigger>=.75f?2u:0u));
        const unsigned changed=(held_^held)&previous&controls,firstHeld=held&~previous;
        const auto previousB=(previous&controls&1)?bSource_:0;
        if(!(held&1)){bSource_=0;heldCheck_=false;}
        else if((changed|firstHeld)&1){bSource_=input.generation;heldCheck_=true;}
        equipment_=equipment;epoch_=inputEpoch;space_=input.spaceGeneration;sequence_=input.generation;
        deadline_=deadline;controls_=controls;held_=held;
        if(!changed&&!firstHeld)return {};
        auto row=Row(input);row.changed=changed;row.firstHeld=firstHeld;
        if(!row.bSource)row.bSource=previousB;
        return row;
    }

    // At most one extra read per observed B hold. Caller has already committed
    // an exact selected route with its held bit requested and no suppression receipt.
    std::optional<ExpackAttempt> TakeHeld(std::uint64_t equipment,std::uint64_t epoch,const interaction::InputFrame& input) noexcept {
        if(!heldCheck_||!bSource_||equipment!=equipment_||epoch!=epoch_||input.spaceGeneration!=space_||
           input.generation!=sequence_||!(held_&1))return {};
        heldCheck_=false;auto row=Row(input);row.heldFollowup=true;return row;
    }

    void Publish(const ExpackAttempt& row) {
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock){++drops_;return;}
        if(row.equipmentEpoch==equipment_&&row.inputEpoch==epoch_&&row.space==space_&&row.bSource==bSource_&&
           (row.held&1)&&row.routeReload>=0&&(row.routeReload&1)&&row.final&&((row.final->zoom14&&std::string_view(row.route)=="zoom14")||
            (row.final->reload29&&std::string_view(row.route)=="reload29")))heldCheck_=false;
        rows_[total_%rows_.size()]=row;++total_;
    }

    void Report(std::ostream& out) {
        std::array<ExpackAttempt,32> rows;std::uint64_t total;
        {std::lock_guard lock(mutex_);rows=rows_;total=total_;}
        out<<"{\"capacity\":"<<rows.size()<<",\"published_total\":"<<total<<",\"lock_drops\":"<<drops_.load()
             <<",\"sampling\":\"B/trigger transitions, first seen held, and at most one later eligible held commit; final covers this gather only; unsampled outcomes unknown\""
               ",\"control_bits\":\"1=B,2=trigger>=0.75\""
             ",\"reload_bits\":\"1=held,2=pressed,4=released\",\"unobserved\":-1,\"rows\":[";
        const auto first=total>rows.size()?total-rows.size():0;
        for(auto n=first;n<total;++n){if(n!=first)out<<',';rows[n%rows.size()].Report(out);}
        out<<"]}";
    }
};
}
