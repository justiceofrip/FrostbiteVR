#pragma once
#include "Bc2NativeCycleService.h"
#include <ostream>

namespace fvr::bc2 {
// Private diagnostic only. Call both methods inside the existing native-cycle
// policy gate. No reads, waits, claim changes or native writes occur here.
class NativeCycleExpiredControlProbe {
public:
    enum class State:unsigned {Waiting,Armed,Replayed,Passed,Failed};
    enum class Reason:unsigned {None,ScopeChanged,CurrentNotFresh,NativeDidNotProgress,ReplayAccepted,DebtChanged,InputChanged,FreshRejected};
    struct Evidence {
        Bc2NativeCycleControl captured{},replayed{},fresh{};
        Bc2NativeCycleView capturedView{},before{},after{},afterFresh{};
        interaction::HandInteractionSample lastBefore{},lastAfter{};
        std::int64_t capturedNs=0,replayedNs=0;
        std::uint64_t capturedEpoch=0,replayedEpoch=0,freshEpoch=0;
        std::uint64_t capturedUpdates=0,replayedUpdates=0,freshUpdates=0;
        unsigned submissions=0;
        bool replayAccepted=false,freshAccepted=false,legacyWouldCancel=false;
        bool debtPreserved=false,inputPreserved=false;
    };
    State Status()const noexcept{return state_;}
    Reason Failure()const noexcept{return reason_;}
    const Evidence& Record()const noexcept{return evidence_;}
    bool Passed()const noexcept{return state_==State::Passed&&evidence_.submissions==1;}

    void BeforeControl(Bc2NativeCycleService& service,const Bc2NativeCycleControl& current,
                       std::int64_t now,std::uint64_t epoch,std::uint64_t updates)noexcept {
        if(state_!=State::Armed)return;
        const auto view=service.View(now);
        if(!SameDebt(evidence_.capturedView,view)||!SameScope(evidence_.captured,current)||
           epoch!=evidence_.capturedEpoch){Fail(Reason::ScopeChanged);return;}
        if(now<evidence_.captured.input.deadlineNs)return;
        if(!Fresh(current,now)||current.input.sequence<=evidence_.captured.input.sequence||
           current.input.observedNs<=evidence_.captured.input.observedNs){Fail(Reason::CurrentNotFresh);return;}
        if(updates<=evidence_.capturedUpdates){Fail(Reason::NativeDidNotProgress);return;}
        evidence_.replayed=evidence_.captured;
        // Only processing time changes. Source sequence, observation, deadline
        // and every button/tracking/release/identity field remain the original.
        evidence_.replayed.input.nowNs=now;
        evidence_.replayedNs=now;evidence_.replayedEpoch=epoch;evidence_.replayedUpdates=updates;
        evidence_.before=view;evidence_.lastBefore=service.LastControlInput();
        evidence_.legacyWouldCancel=view.phase==Bc2NativeCyclePhase::Held&&
            !interaction::weapon_cycle_detail::Window(evidence_.replayed.input.observedNs,
                                                     evidence_.replayed.input.deadlineNs,now);
        ++evidence_.submissions;
        evidence_.replayAccepted=service.Control(evidence_.replayed);
        evidence_.after=service.View(now);evidence_.lastAfter=service.LastControlInput();
        evidence_.debtPreserved=SameDebt(view,evidence_.after)&&view.held==evidence_.after.held&&
            view.heldSuspensions==evidence_.after.heldSuspensions&&view.heldResumptions==evidence_.after.heldResumptions;
        evidence_.inputPreserved=SameInput(evidence_.lastBefore,evidence_.lastAfter);
        if(evidence_.replayAccepted)Fail(Reason::ReplayAccepted);
        else if(!evidence_.debtPreserved)Fail(Reason::DebtChanged);
        else if(!evidence_.inputPreserved)Fail(Reason::InputChanged);
        else state_=State::Replayed;
    }
    void AfterControl(const Bc2NativeCycleService& service,const Bc2NativeCycleControl& current,bool accepted,
                      std::uint64_t epoch,std::uint64_t updates)noexcept {
        if(state_==State::Waiting){
            const auto view=service.View(current.input.nowNs);
            if(!accepted||!Fresh(current,current.input.nowNs)||!Established(view)||
               !SameInput(service.LastControlInput(),current.input))return;
            evidence_.captured=current;evidence_.capturedView=view;evidence_.capturedNs=current.input.nowNs;
            evidence_.capturedEpoch=epoch;evidence_.capturedUpdates=updates;state_=State::Armed;return;
        }
        if(state_!=State::Replayed)return;
        evidence_.fresh=current;evidence_.freshAccepted=accepted;evidence_.freshEpoch=epoch;evidence_.freshUpdates=updates;
        evidence_.afterFresh=service.View(current.input.nowNs);
        if(!accepted||!Fresh(current,current.input.nowNs)||epoch!=evidence_.replayedEpoch||
           !SameDebt(evidence_.before,evidence_.afterFresh)||!SameInput(service.LastControlInput(),current.input))
            Fail(Reason::FreshRejected);
        else state_=State::Passed;
    }
    void Report(std::ostream& out,bool drained)const {
        // A live report must not read storage being written under another gate.
        out<<"{\"schema\":\"fvr.bc2.expired-control-probe.v1\",\"enabled\":true,\"drained\":"<<(drained?"true":"false");
        if(!drained){out<<",\"exercised\":false,\"passed\":false,\"state\":\"await_drain\"}";return;}
        out<<",\"exercised\":"<<(evidence_.submissions==1?"true":"false")
           <<",\"passed\":"<<(Passed()?"true":"false")<<",\"state\":\""<<StateName(state_)
           <<"\",\"reason\":\""<<(state_==State::Waiting||state_==State::Armed?"not_exercised":ReasonName(reason_))<<'"'
           <<",\"submissions\":"<<evidence_.submissions<<",\"captured_ns\":"<<evidence_.capturedNs
           <<",\"replayed_ns\":"<<evidence_.replayedNs<<",\"capture_epoch\":"<<evidence_.capturedEpoch
           <<",\"replay_epoch\":"<<evidence_.replayedEpoch<<",\"fresh_epoch\":"<<evidence_.freshEpoch
           <<",\"native_updates\":["<<evidence_.capturedUpdates<<','<<evidence_.replayedUpdates<<','<<evidence_.freshUpdates<<']'
           <<",\"replay_accepted\":"<<(evidence_.replayAccepted?"true":"false")
           <<",\"fresh_accepted\":"<<(evidence_.freshAccepted?"true":"false")
           <<",\"legacy_would_cancel\":"<<(evidence_.legacyWouldCancel?"true":"false")
           <<",\"legacy_basis\":\"source_mapping_window_and_active_held;not_executed\""
           <<",\"debt_preserved\":"<<(evidence_.debtPreserved?"true":"false")
           <<",\"input_preserved\":"<<(evidence_.inputPreserved?"true":"false");
        out<<",\"captured\":";WriteControl(out,evidence_.captured);
        out<<",\"replayed\":";WriteControl(out,evidence_.replayed);
        out<<",\"fresh\":";WriteControl(out,evidence_.fresh);
        out<<",\"last_before\":";WriteInput(out,evidence_.lastBefore);
        out<<",\"last_after\":";WriteInput(out,evidence_.lastAfter);
        out<<",\"captured_view\":";WriteView(out,evidence_.capturedView);
        out<<",\"before\":";WriteView(out,evidence_.before);
        out<<",\"after\":";WriteView(out,evidence_.after);
        out<<",\"after_fresh\":";WriteView(out,evidence_.afterFresh);out<<'}';
    }
    static bool SameInput(const interaction::HandInteractionSample& a,const interaction::HandInteractionSample& b)noexcept {
        return a.owner==b.owner&&a.sequence==b.sequence&&a.observedNs==b.observedNs&&a.deadlineNs==b.deadlineNs&&
            a.nowNs==b.nowNs&&a.focused==b.focused&&a.tracked==b.tracked&&a.released==b.released;
    }
private:
    static bool Fresh(const Bc2NativeCycleControl& c,std::int64_t now)noexcept {
        return c.permitted&&!c.release&&c.input.focused&&c.input.tracked[0]&&c.input.tracked[1]&&
            !c.input.released[0]&&!c.input.released[1]&&
            interaction::weapon_cycle_detail::Window(c.input.observedNs,c.input.deadlineNs,now);
    }
    static bool Established(const Bc2NativeCycleView& v)noexcept {
        return v.phase==Bc2NativeCyclePhase::Held&&v.failure==Bc2NativeCycleFailure::None&&v.held&&
            !v.ready&&!v.heldSuspended&&!v.trackingRebase&&v.blocksFire&&v.cycle&&v.shot;
    }
    static bool SameScope(const Bc2NativeCycleControl& a,const Bc2NativeCycleControl& b)noexcept {
        return a.nativeOwner==b.nativeOwner&&a.input.owner==b.input.owner&&a.item==b.item&&a.mechanism==b.mechanism;
    }
    static bool SameDebt(const Bc2NativeCycleView& a,const Bc2NativeCycleView& b)noexcept {
        return Established(a)&&Established(b)&&a.native==b.native&&a.cycle==b.cycle&&a.shot==b.shot&&
            a.loaded==b.loaded&&a.reserve==b.reserve&&a.capacity==b.capacity;
    }
    void Fail(Reason why)noexcept{reason_=why;state_=State::Failed;}
    static const char* StateName(State s)noexcept {
        switch(s){case State::Waiting:return "waiting";case State::Armed:return "armed";case State::Replayed:return "replayed";
            case State::Passed:return "passed";case State::Failed:return "failed";}return "invalid";
    }
    static const char* ReasonName(Reason r)noexcept {
        switch(r){case Reason::None:return "none";case Reason::ScopeChanged:return "scope_changed";
            case Reason::CurrentNotFresh:return "current_not_fresh";case Reason::NativeDidNotProgress:return "native_did_not_progress";
            case Reason::ReplayAccepted:return "replay_accepted";case Reason::DebtChanged:return "debt_changed";
            case Reason::InputChanged:return "input_changed";case Reason::FreshRejected:return "fresh_rejected";}return "invalid";
    }
    static void WriteOwner(std::ostream& out,const ReloadStateOwner& o) {
        out<<"{\"player\":"<<o.player<<",\"soldier\":"<<o.soldier<<",\"weak\":"<<o.weak<<",\"weapon\":"<<o.weapon
           <<",\"actor_generation\":"<<o.actorGeneration<<",\"equip_generation\":"<<o.equipGeneration<<",\"space\":"<<o.space<<'}';
    }
    static void WriteInput(std::ostream& out,const interaction::HandInteractionSample& s) {
        out<<"{\"owner\":["<<s.owner.actor<<','<<s.owner.actorGeneration<<','<<s.owner.equipGeneration<<','<<s.owner.space
           <<"],\"sequence\":"<<s.sequence<<",\"observed_ns\":"<<s.observedNs<<",\"deadline_ns\":"<<s.deadlineNs<<",\"now_ns\":"<<s.nowNs
           <<",\"focused\":"<<(s.focused?"true":"false")<<",\"tracked\":["<<(s.tracked[0]?"true":"false")<<','<<(s.tracked[1]?"true":"false")
           <<"],\"released\":["<<(s.released[0]?"true":"false")<<','<<(s.released[1]?"true":"false")<<"]}";
    }
    static void WriteControl(std::ostream& out,const Bc2NativeCycleControl& c) {
        out<<"{\"native_owner\":";WriteOwner(out,c.nativeOwner);out<<",\"input\":";WriteInput(out,c.input);
        out<<",\"item\":["<<c.item.id<<','<<c.item.generation<<"],\"mechanism\":["<<c.mechanism.id<<','<<c.mechanism.generation
           <<"],\"release_present\":"<<(c.release?"true":"false")<<",\"permitted\":"<<(c.permitted?"true":"false")<<'}';
    }
    static void WriteView(std::ostream& out,const Bc2NativeCycleView& v) {
        out<<"{\"phase\":"<<unsigned(v.phase)<<",\"failure\":"<<unsigned(v.failure)<<",\"cycle\":"<<v.cycle<<",\"shot\":"<<v.shot
           <<",\"loaded\":"<<v.loaded<<",\"reserve\":"<<v.reserve<<",\"capacity\":"<<v.capacity
           <<",\"blocks_fire\":"<<(v.blocksFire?"true":"false")<<",\"held\":"<<(v.held?"true":"false")
           <<",\"ready\":"<<(v.ready?"true":"false")<<",\"held_suspended\":"<<(v.heldSuspended?"true":"false")
           <<",\"held_suspensions\":"<<v.heldSuspensions<<",\"held_resumptions\":"<<v.heldResumptions<<",\"native_owner\":";
        WriteOwner(out,v.native.owner);out<<",\"firing\":["<<v.native.firing[0]<<','<<v.native.firing[1]<<','<<v.native.firing[2]
           <<"],\"server\":["<<v.native.serverPlayer<<','<<v.native.serverSoldier<<','<<v.native.serverItem<<']';
        if(v.held)out<<",\"held_sequence\":"<<v.held->sequence<<",\"held_observed_ns\":"<<v.held->observedNs<<",\"held_deadline_ns\":"<<v.held->deadlineNs;
        out<<'}';
    }
    State state_=State::Waiting;Reason reason_=Reason::None;Evidence evidence_{};
};
}

