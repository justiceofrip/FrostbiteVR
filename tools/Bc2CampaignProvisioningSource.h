#pragma once
#include "Bc2CampaignProvisioning.h"
#include <algorithm>
#include <limits>

namespace fvr::bc2::campaignProvisioning::command {
inline constexpr std::int64_t SourceAgeNs=100000000,SourceWaitNs=500000000,ArmNs=2000000000;
inline constexpr unsigned SourcePollMs=5,SourceReadLimit=101;
enum class SourceResult {Fresh,Invalid,Changed,TimedOut};
inline bool PublishedSource(const Request& r,std::uint64_t pid)noexcept {
    return r.magic==0x31504342&&r.version==1&&r.bytes==sizeof(Request)&&!r.reserved&&
        r.operation==Operation::Observe&&!r.job&&!r.nonce&&!r.issuedNs&&!r.deadlineNs&&
        r.process.pid==pid&&pid&&!r.process.reserved&&r.process.creation&&r.process.session&&
        r.process.executable[0]&&r.process.executable.back()==0&&
        std::any_of(r.process.image.begin(),r.process.image.end(),[](auto b){return b!=0;})&&
        r.sourceSequence&&r.sourceObservedNs>0&&r.source.session==r.process.session&&
        r.client.player&&r.client.soldier&&r.client.weak&&r.client.weapon&&
        r.client.actorGeneration&&r.client.equipGeneration&&r.client.space&&
        r.source.player&&r.source.actor&&r.source.inventory&&r.source.item&&r.source.data&&
        std::all_of(r.source.firing.begin(),r.source.firing.end(),[](auto p){return p!=0;})&&
        r.source.actorGeneration==r.client.actorGeneration&&r.source.space==r.client.space;
}
inline bool FreshSource(const Request& r,std::int64_t now)noexcept {
    return r.sourceObservedNs>0&&now>=r.sourceObservedNs&&now-r.sourceObservedNs<=SourceAgeNs;
}
// Only a stale, otherwise valid publication may wait. Read/shape refusal or
// replacement ends this attempt; the pinned identity is never reacquired.
// Clock/read/wait are injected so offline tests execute this production path.
template<class Read,class Clock,class Wait>
SourceResult WaitForFreshSource(std::uint64_t pid,Request& out,Read read,Clock clock,Wait wait) {
    const auto started=clock();if(started<=0)return SourceResult::Invalid;
    auto previousNow=started;Request pinned{},previous{};bool haveSource=false;
    for(unsigned attempt=0;attempt<SourceReadLimit;++attempt){
        const auto before=clock();
        if(before<previousNow)return SourceResult::Invalid;
        if(before-started>=SourceWaitNs)return SourceResult::TimedOut;
        Request next{};if(!read(next))return SourceResult::Invalid;
        const auto now=clock();
        if(now<before)return SourceResult::Invalid;
        if(now-started>=SourceWaitNs)return SourceResult::TimedOut;
        if(!PublishedSource(next,pid)||next.sourceObservedNs>now)return SourceResult::Invalid;
        if(haveSource){
            if(next.process!=pinned.process||next.client!=pinned.client||next.source!=pinned.source)
                return SourceResult::Changed;
            // A repeated file is allowed while stale; a new observation must
            // advance both fields, never synthesize/renew either lease field.
            const bool repeated=next.sourceSequence==previous.sourceSequence&&
                next.sourceObservedNs==previous.sourceObservedNs;
            if(!repeated&&(next.sourceSequence<=previous.sourceSequence||
                           next.sourceObservedNs<=previous.sourceObservedNs))return SourceResult::Invalid;
        }else {pinned=next;haveSource=true;}
        if(FreshSource(next,now)){out=next;return SourceResult::Fresh;}
        previous=next;previousNow=now;
        if(attempt+1<SourceReadLimit)wait(SourcePollMs);
    }
    return SourceResult::TimedOut;
}
inline bool ArmRequest(Request& r,std::int64_t now)noexcept {
    if(r.issuedNs||r.deadlineNs||!FreshSource(r,now)||now>std::numeric_limits<std::int64_t>::max()-ArmNs)return false;
    r.issuedNs=now;r.deadlineNs=now+ArmNs;return true;
}
inline bool CanPublishRequest(const Request& r,std::int64_t now)noexcept {
    return r.issuedNs>0&&now>=r.issuedNs&&now<r.deadlineNs&&FreshSource(r,now);
}
}
