#pragma once
#include "Bc2EquipmentIdentity.h"
#include "fvr/interaction/HandInteraction.h"
#include <ostream>

namespace fvr::bc2 {
// Copies of an already matched ServerShoot scope. Addresses identify observed
// cohorts, not native lifetimes. Candidate presence is never trigger evidence.
struct ServerFireObservationKey {
    unsigned player=0,soldier=0,weak=0,serverPlayer=0,serverSoldier=0,effects=0,configuration=0;
    WeaponEquipmentIdentity equipment{};
    bool candidatePresent=false;
    std::uint64_t candidateOwnerGeneration=0,space=0;
    bool operator==(const ServerFireObservationKey&)const=default;
};
struct ServerFireObservationSample {
    ServerFireObservationKey key{};
    std::uint64_t invocation=0,returnedOrdinal=0,helperCallbacks=0,candidateGeneration=0,eventGeneration=0;
    std::int64_t beginQpc=0,endQpc=0,beginNs=0,candidateDeadlineQpc=0,eventDeadlineQpc=0;
    unsigned token=0;
    bool tokenValid=false,muzzleSupported=false,compositionWritten=false,eventPresent=false,controllerPresent=false;
    interaction::HandInteractionSample controller{};
    bool Timed()const noexcept{return beginQpc>0&&endQpc>=beginQpc;}
    std::int64_t Span()const noexcept{return Timed()?endQpc-beginQpc:0;}
    const char* CandidateTime()const noexcept {
        if(!key.candidatePresent)return "absent";
        if(beginQpc<=0||candidateDeadlineQpc<=0)return "unknown";
        return beginQpc<candidateDeadlineQpc?"unexpired":"expired";
    }
    const char* ControllerTime()const noexcept {
        if(!controllerPresent)return "absent";
        if(beginNs<=0||controller.observedNs<=0||controller.deadlineNs<=controller.observedNs)return "invalid_time";
        if(beginNs<controller.observedNs)return "future";
        return beginNs<controller.deadlineNs?"unexpired":"expired";
    }
};
struct ServerFireObservationGap {
    bool present=false;
    ServerFireObservationSample previous{},current{};
    std::int64_t qpc=0;
};
struct ServerFireObservationCohort {
    ServerFireObservationKey key{};
    std::uint64_t returned=0,helperCallbacks=0,withoutHelper=0,invalidTiming=0,missingCandidate=0,missingController=0;
    std::uint64_t expiredCandidate=0,expiredController=0,invalidControllerTime=0,gaps=0,unorderedOrInvalidPairs=0;
    std::uint64_t admission=0,lastRecorded=0;
    std::int64_t firstValidBeginQpc=0,lastValidEndQpc=0,maximumSpanQpc=0;
    ServerFireObservationSample latest{};
    ServerFireObservationGap latestGap{},worstGap{};
};
// A caller-owned nonblocking gate protects this fixed state. Observe performs
// bounded comparisons/copies only: no allocations, native reads or policy calls.
class ServerFireObservation {
public:
    static constexpr std::size_t capacity=16;
    void Observe(const ServerFireObservationSample& sample)noexcept {
        ++recorded_;helperCallbacks_+=sample.helperCallbacks;
        latest_=sample;
        if(sample.Timed()){
            if(!latestTimed_||sample.endQpc>=latestTimed_->endQpc)latestTimed_=sample;
            if(!worstSpan_||sample.Span()>worstSpan_->Span())worstSpan_=sample;
        }else ++invalidTiming_;
        std::size_t index=0;
        for(;index<count_;++index)if(cohorts_[index].key==sample.key)break;
        if(index==count_){
            if(count_<capacity)++count_;
            else {
                index=0;for(std::size_t n=1;n<capacity;++n)if(cohorts_[n].lastRecorded<cohorts_[index].lastRecorded)index=n;
                ++evictedCohorts_;evictedReturns_+=cohorts_[index].returned;
            }
            cohorts_[index]={};cohorts_[index].key=sample.key;cohorts_[index].admission=recorded_;
        }
        auto& c=cohorts_[index];
        if(c.returned){
            // Only adjacent retained returns for this observed cohort. Missing
            // callbacks, other cohorts and trigger continuity remain unknown.
            if(c.latest.Timed()&&sample.Timed()&&sample.beginQpc>=c.latest.endQpc){
                c.latestGap={true,c.latest,sample,sample.beginQpc-c.latest.beginQpc};++c.gaps;
                if(!c.worstGap.present||c.latestGap.qpc>c.worstGap.qpc)c.worstGap=c.latestGap;
                if(!worstGap_.present||c.latestGap.qpc>worstGap_.qpc)worstGap_=c.latestGap;
            }else {++c.unorderedOrInvalidPairs;c.latestGap={};}
        }
        ++c.returned;c.helperCallbacks+=sample.helperCallbacks;c.withoutHelper+=sample.helperCallbacks==0;
        c.invalidTiming+=!sample.Timed();c.missingCandidate+=!sample.key.candidatePresent;c.missingController+=!sample.controllerPresent;
        c.expiredCandidate+=std::string_view(sample.CandidateTime())=="expired";
        c.expiredController+=std::string_view(sample.ControllerTime())=="expired";
        c.invalidControllerTime+=sample.controllerPresent&&std::string_view(sample.ControllerTime())!="expired"&&
            std::string_view(sample.ControllerTime())!="unexpired";
        if(sample.Timed()){
            if(!c.firstValidBeginQpc||sample.beginQpc<c.firstValidBeginQpc)c.firstValidBeginQpc=sample.beginQpc;
            c.lastValidEndQpc=std::max(c.lastValidEndQpc,sample.endQpc);c.maximumSpanQpc=std::max(c.maximumSpanQpc,sample.Span());
        }
        c.latest=sample;c.lastRecorded=recorded_;
    }
    const auto& Cohorts()const noexcept{return cohorts_;}
    std::size_t Count()const noexcept{return count_;}
    std::uint64_t Recorded()const noexcept{return recorded_;}
    std::uint64_t EvictedReturns()const noexcept{return evictedReturns_;}
    const auto& LatestTimed()const noexcept{return latestTimed_;}
    const auto& WorstSpan()const noexcept{return worstSpan_;}
    const auto& WorstGap()const noexcept{return worstGap_;}
    void Report(std::ostream& out)const {
        out<<"{\"capacity\":"<<capacity<<",\"recorded_returns\":"<<recorded_<<",\"matched_helper_callbacks\":"<<helperCallbacks_
           <<",\"invalid_timing\":"<<invalidTiming_<<",\"evicted_cohorts\":"<<evictedCohorts_<<",\"evicted_returns\":"<<evictedReturns_
           <<",\"latest_recorded_return\":";Sample(out,latest_);
        out<<",\"latest_timed_return\":";Sample(out,latestTimed_);out<<",\"worst_observed_span\":";Sample(out,worstSpan_);
        out<<",\"worst_observed_gap\":";Gap(out,worstGap_);out<<",\"cohorts\":[";
        for(std::size_t n=0;n<count_;++n){if(n)out<<',';const auto& c=cohorts_[n];
            out<<"{\"key\":";Key(out,c.key);
            out<<",\"admission_record\":"<<c.admission<<",\"last_record\":"<<c.lastRecorded<<",\"returned_invocations\":"<<c.returned
               <<",\"matched_helper_callbacks\":"<<c.helperCallbacks<<",\"without_helper_callback\":"<<c.withoutHelper
               <<",\"invalid_timing\":"<<c.invalidTiming<<",\"missing_candidate\":"<<c.missingCandidate<<",\"missing_controller\":"<<c.missingController
               <<",\"expired_candidate\":"<<c.expiredCandidate<<",\"expired_controller\":"<<c.expiredController
               <<",\"invalid_controller_time\":"<<c.invalidControllerTime<<",\"observed_gap_pairs\":"<<c.gaps
               <<",\"unordered_or_invalid_pairs\":"<<c.unorderedOrInvalidPairs<<",\"first_valid_begin_qpc\":"<<c.firstValidBeginQpc
               <<",\"last_valid_end_qpc\":"<<c.lastValidEndQpc<<",\"maximum_span_qpc\":"<<c.maximumSpanQpc<<",\"latest\":";
            Sample(out,c.latest);out<<",\"latest_gap\":";Gap(out,c.latestGap);out<<",\"worst_gap\":";Gap(out,c.worstGap);out<<'}';
        }out<<"]}";
    }
private:
    static void Text(std::ostream& out,std::string_view text){out<<'"';for(unsigned char c:text){if(c=='"'||c=='\\')out<<'\\';if(c>=32&&c<=126)out<<char(c);else out<<'?';}out<<'"';}
    static void Key(std::ostream& out,const ServerFireObservationKey& k){
        out<<"{\"client_player\":"<<k.player<<",\"client_soldier\":"<<k.soldier<<",\"weak\":"<<k.weak
           <<",\"server_player\":"<<k.serverPlayer<<",\"server_soldier\":"<<k.serverSoldier<<",\"effects\":"<<k.effects
           <<",\"configuration\":"<<k.configuration<<",\"weapon\":"<<k.equipment.weapon<<",\"weapon_data\":"<<k.equipment.data
           <<",\"persistence\":"<<k.equipment.persistence<<",\"asset\":";Text(out,k.equipment.Asset());
        out<<",\"candidate_present\":"<<k.candidatePresent<<",\"candidate_owner_generation\":"<<k.candidateOwnerGeneration<<",\"space\":"<<k.space<<'}';
    }
    static void Sample(std::ostream& out,const std::optional<ServerFireObservationSample>& sample){
        if(!sample){out<<"null";return;}const auto& s=*sample;out<<"{\"key\":";Key(out,s.key);
        out<<",\"native_invocation\":"<<s.invocation<<",\"returned_ordinal\":"<<s.returnedOrdinal<<",\"matched_helper_callbacks\":"<<s.helperCallbacks
           <<",\"begin_qpc\":"<<s.beginQpc<<",\"end_qpc\":"<<s.endQpc<<",\"begin_ns\":"<<s.beginNs
           <<",\"timing_valid\":"<<s.Timed()<<",\"observed_span_qpc\":";if(s.Timed())out<<s.Span();else out<<"null";
        out<<",\"token\":"<<s.token<<",\"token_valid\":"<<s.tokenValid<<",\"muzzle_supported\":"<<s.muzzleSupported
           <<",\"composition_origin_written\":"<<s.compositionWritten<<",\"candidate_generation\":"<<s.candidateGeneration
           <<",\"candidate_deadline_qpc\":"<<s.candidateDeadlineQpc<<",\"candidate_time_at_begin\":";Text(out,s.CandidateTime());
        out<<",\"event_present\":"<<s.eventPresent<<",\"event_generation\":"<<s.eventGeneration<<",\"event_deadline_qpc\":"<<s.eventDeadlineQpc
           <<",\"controller_time_at_begin\":";Text(out,s.ControllerTime());out<<",\"controller_source\":";
        if(!s.controllerPresent)out<<"null";else {const auto& i=s.controller;
            out<<"{\"actor\":"<<i.owner.actor<<",\"actor_generation\":"<<i.owner.actorGeneration<<",\"equipment_generation\":"<<i.owner.equipGeneration
               <<",\"space\":"<<i.owner.space<<",\"sequence\":"<<i.sequence<<",\"observed_ns\":"<<i.observedNs<<",\"deadline_ns\":"<<i.deadlineNs
               <<",\"source_now_ns\":"<<i.nowNs<<",\"focused\":"<<i.focused<<",\"tracked\":["<<i.tracked[0]<<','<<i.tracked[1]
               <<"],\"released\":["<<i.released[0]<<','<<i.released[1]<<"]}";}
        out<<'}';
    }
    static void Gap(std::ostream& out,const ServerFireObservationGap& gap){
        if(!gap.present){out<<"null";return;}out<<"{\"begin_to_begin_qpc\":"<<gap.qpc<<",\"previous\":";Sample(out,gap.previous);
        out<<",\"current\":";Sample(out,gap.current);out<<'}';
    }
    std::array<ServerFireObservationCohort,capacity> cohorts_{};
    std::size_t count_=0;
    std::uint64_t recorded_=0,helperCallbacks_=0,invalidTiming_=0,evictedCohorts_=0,evictedReturns_=0;
    std::optional<ServerFireObservationSample> latest_,latestTimed_,worstSpan_;
    ServerFireObservationGap worstGap_{};
};
} // namespace fvr::bc2
