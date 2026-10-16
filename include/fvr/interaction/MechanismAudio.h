#pragma once
#include <array>
#include <atomic>
#include <cstdint>

namespace fvr::interaction {
// Presentation receipts only. These never release a weapon cycle, alter ammo,
// acquire a hand or make a native action ready.
enum class MechanismAudioStage : unsigned { RearTravel=1, Closure=2 };
struct MechanismAudioIdentity {
    std::uint64_t actor=0,actorGeneration=0,item=0,itemGeneration=0,cycle=0,shot=0;
    bool operator==(const MechanismAudioIdentity&)const=default;
};
// A current controller publication may lack a pure native-cycle observation
// for one tick. Zero cycle/shot means unavailable, not a new lifetime. Explicit
// owner/item changes or a known different debt still retire the short cue.
inline bool MechanismAudioSourceCurrent(const MechanismAudioIdentity& source,
    const MechanismAudioIdentity& cue)noexcept {
    return cue.actor&&cue.actorGeneration&&cue.item&&cue.itemGeneration&&cue.cycle&&cue.shot&&
        source.actor==cue.actor&&source.actorGeneration==cue.actorGeneration&&
        source.item==cue.item&&source.itemGeneration==cue.itemGeneration&&
        (!source.cycle||source.cycle==cue.cycle)&&(!source.shot||source.shot==cue.shot);
}
inline bool MechanismAudioRouteFresh(std::int64_t verifiedNs,std::int64_t now)noexcept {
    return verifiedNs>0&&now>=verifiedNs&&now-verifiedNs<250000000;
}
class MechanismAudioStockPassLatch {
    std::atomic<bool> passed_{false};
public:
    void ObserveStart(bool localReplacementIntended,bool suppressed)noexcept {
        if(localReplacementIntended&&!suppressed)passed_.store(true,std::memory_order_release);
    }
    bool AllowsReplacement()const noexcept{return !passed_.load(std::memory_order_acquire);}
    // Only at a fully drained new session, never on source recovery/regrip.
    void ResetSession()noexcept{passed_.store(false,std::memory_order_release);}
};
// Route failure is terminal for this session. Reopening mid-cycle could replay
// a replacement after its native start has already passed through to stock.
class MechanismAudioRoutePolicy {
    std::int64_t verifiedNs_=0;bool failed_=false;
public:
    bool Open(std::int64_t now)noexcept {if(failed_||verifiedNs_||now<=0)return false;verifiedNs_=now;return true;}
    bool Fresh(std::int64_t now)const noexcept {return !failed_&&MechanismAudioRouteFresh(verifiedNs_,now);}
    bool CheckDue(std::int64_t now)const noexcept {return !failed_&&(!verifiedNs_||now-verifiedNs_>=100000000);}
    bool Validate(std::int64_t now,bool exactSameRoute)noexcept {
        if(!Fresh(now)||!exactSameRoute){Fail();return false;}verifiedNs_=now;return true;
    }
    void Fail()noexcept {failed_=true;verifiedNs_=0;}
    std::int64_t VerifiedNs()const noexcept{return verifiedNs_;}
};
enum class MechanismAudioBufferPhase { Empty,Prepared,Queued,Returned,Retained,Closed };
// A failed driver return/unprepare/close never authorizes freeing/reusing the
// header or PCM. The adapter parks its single bounded allocation in that case.
class MechanismAudioBufferLease {
    MechanismAudioBufferPhase phase_=MechanismAudioBufferPhase::Empty;
public:
    MechanismAudioBufferPhase Phase()const noexcept{return phase_;}
    bool CanPrepare()const noexcept{return phase_==MechanismAudioBufferPhase::Empty;}
    bool Prepared(bool ok)noexcept {if(!CanPrepare())return false;if(ok)phase_=MechanismAudioBufferPhase::Prepared;return ok;}
    bool Submitted(bool ok)noexcept {if(phase_!=MechanismAudioBufferPhase::Prepared)return false;if(ok)phase_=MechanismAudioBufferPhase::Queued;return ok;}
    bool Returned(bool ok)noexcept {
        if(phase_!=MechanismAudioBufferPhase::Queued)return false;
        phase_=ok?MechanismAudioBufferPhase::Returned:MechanismAudioBufferPhase::Retained;return ok;
    }
    bool CanUnprepare()const noexcept{return phase_==MechanismAudioBufferPhase::Prepared||phase_==MechanismAudioBufferPhase::Returned;}
    bool Unprepared(bool ok)noexcept {if(!CanUnprepare())return false;phase_=ok?MechanismAudioBufferPhase::Empty:MechanismAudioBufferPhase::Retained;return ok;}
    bool Closed(bool ok)noexcept {if(!CanPrepare())return false;phase_=ok?MechanismAudioBufferPhase::Closed:MechanismAudioBufferPhase::Retained;return ok;}
    bool Retained()const noexcept{return phase_==MechanismAudioBufferPhase::Retained;}
};
template<std::size_t Capacity=32> class MechanismAudioSuppressionLedger {
    std::array<MechanismAudioIdentity,Capacity> rows_{};
public:
    bool Record(const MechanismAudioIdentity& id)noexcept {
        if(!id.actor||!id.actorGeneration||!id.item||!id.itemGeneration||!id.cycle||!id.shot)return false;
        MechanismAudioIdentity* empty=nullptr;
        for(auto& row:rows_){
            if(!row.actor&&!empty)empty=&row;
            if(row.actor!=id.actor||row.actorGeneration!=id.actorGeneration||row.item!=id.item)continue;
            if(id.itemGeneration<row.itemGeneration)return false;
            if(id.itemGeneration==row.itemGeneration&&(id.cycle<row.cycle||id.shot<row.shot||
               (id.cycle==row.cycle&&id.shot!=row.shot)))return false;
            row=id;return true;
        }
        if(!empty)return false;*empty=id;return true;
    }
    bool Contains(const MechanismAudioIdentity& id)const noexcept {
        if(!id.actor||!id.cycle||!id.shot)return false;
        for(const auto& row:rows_)if(row==id)return true;return false;
    }
    void RetireActor(std::uint64_t actor,std::uint64_t generation)noexcept {
        for(auto& row:rows_)if(row.actor==actor&&row.actorGeneration==generation)row={};
    }
};
struct MechanismAudioEvent {
    MechanismAudioIdentity identity{};
    MechanismAudioStage stage=MechanismAudioStage::RearTravel;
    std::uint64_t sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    bool acceptedPhysicalStage=false,focused=false,tracked=false;
};
enum class MechanismAudioAdmission { Accepted, Invalid, Stale, Duplicate, Order, Capacity };

// One caller serializes this small presentation ledger. Item generations and
// cycle numbers are supplied by the adapter, not inferred from animation time.
// Keep it across regrips, tracking loss and selection; retire on actor lifetime.
template<std::size_t Capacity=32> class MechanismAudioLedger {
    struct Row {MechanismAudioIdentity id{};std::uint64_t sequence=0;std::int64_t observedNs=0;unsigned stages=0;};
    std::array<Row,Capacity> rows_{};
    static bool Item(const MechanismAudioIdentity& a,const MechanismAudioIdentity& b) noexcept {
        return a.actor==b.actor&&a.actorGeneration==b.actorGeneration&&a.item==b.item;
    }
public:
    MechanismAudioAdmission Accept(const MechanismAudioEvent& event,std::int64_t now) noexcept {
        const auto& id=event.identity;const auto stage=unsigned(event.stage);
        if(!id.actor||!id.actorGeneration||!id.item||!id.itemGeneration||!id.cycle||!id.shot||
           !event.sequence||!event.acceptedPhysicalStage||!event.focused||!event.tracked||stage<1||stage>2)
            return MechanismAudioAdmission::Invalid;
        if(event.observedNs<=0||event.observedNs>now||event.deadlineNs<=now||
           event.deadlineNs<event.observedNs||event.deadlineNs-event.observedNs>200000000)
            return MechanismAudioAdmission::Stale;
        Row* row=nullptr;Row* empty=nullptr;
        for(auto& r:rows_){if(!r.id.actor&&!empty)empty=&r;if(Item(r.id,id)){row=&r;break;}}
        if(!row){if(!empty)return MechanismAudioAdmission::Capacity;row=empty;}
        const bool generationNew=row->id.actor&&id.itemGeneration>row->id.itemGeneration;
        const bool fresh=!row->id.actor||generationNew||id.cycle>row->id.cycle;
        if(row->id.actor&&(id.itemGeneration<row->id.itemGeneration||
           (!generationNew&&(id.cycle<row->id.cycle||id.shot<row->id.shot))||
           event.sequence<row->sequence||event.observedNs<row->observedNs))return MechanismAudioAdmission::Stale;
        if(!fresh&&row->id!=id)return MechanismAudioAdmission::Invalid;
        const unsigned bit=1u<<(stage-1);
        if(!fresh&&(row->stages&bit))return MechanismAudioAdmission::Duplicate;
        if(stage==2&&(fresh||!(row->stages&1)))return MechanismAudioAdmission::Order;
        if(fresh)*row={id,event.sequence,event.observedNs,bit};
        else {row->sequence=event.sequence;row->observedNs=event.observedNs;row->stages|=bit;}
        return MechanismAudioAdmission::Accepted;
    }
    void RetireActor(std::uint64_t actor,std::uint64_t generation) noexcept {
        for(auto& row:rows_)if(row.id.actor==actor&&row.id.actorGeneration==generation)row={};
    }
};
}
