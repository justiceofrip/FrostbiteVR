#include "Bc2NativeCycleEvidence.h"
#include "Bc2ReloadFlowRuntime.h"
#include <bit>
#include <cstring>

namespace fvr::bc2 {
namespace {
bool Identity(const ReloadHoldIdentity& i)noexcept {
    const auto& o=i.owner;
    if(o.player<0x10000||o.soldier<0x10000||o.soldier>UINT32_MAX-4||o.weak<0x10000||o.weapon<0x10000||
       !o.actorGeneration||!o.equipGeneration||!o.space||i.serverPlayer<0x10000||i.serverSoldier<0x10000||i.serverItem<0x10000)return false;
    for(unsigned n=0;n<3;++n){if(i.firing[n]<0x10000)return false;for(unsigned p=0;p<n;++p)if(i.firing[n]==i.firing[p])return false;}
    return true;
}
bool Boundary(const ReloadFlowBoundary& b,const ReloadHoldIdentity& i,unsigned branch)noexcept {
    if(branch>=3||b.owner!=i.owner||!b.snapshotSequence||b.branch!=branch||b.firing!=i.firing[branch]||
       b.wrapperOffset!=(branch==0?0x3cu:branch==1?0x40u:0x10u)||
       b.current>15||b.previous>15||b.next>15||!std::isfinite(b.timer)||b.timer<0||b.timer>3600||
       b.loaded<0||b.loaded>1000000||b.reserve<0||b.reserve>1000000||(b.flagsA8&(8|16)))return false;
    return branch==2?(b.serverPlayer==i.serverPlayer&&b.serverSoldier==i.serverSoldier&&b.serverItem==i.serverItem):
        (!b.serverPlayer&&!b.serverSoldier&&!b.serverItem);
}
bool Context(const ReloadFlowRecord& r,bool scratchAllowed)noexcept {
    const auto& a=r.entry;const auto& b=r.exit;
    if(a.context<0x10000||!a.contextCopied||!b.contextCopied)return false;
    const auto before=DecodeReloadUpdateContext(a.copiedContext),after=DecodeReloadUpdateContext(b.copiedContext);
    if(!before||!after||!ValidManualReloadDelta(before->deltaSeconds)||
       before->reloadTimeMultiplier<=0||before->reloadTimeMultiplier>4)return false;
    // Update stores its original firing-object scratch word at context+0x14.
    // No other original-context difference is accepted, including delta bytes.
    for(unsigned n=0;n<a.copiedContext.size();++n)
        if((!scratchAllowed||n<0x14||n>=0x18)&&a.copiedContext[n]!=b.copiedContext[n])return false;
    return true;
}
bool Basic(const ReloadFlowRecord& r,const ReloadHoldIdentity& identity,unsigned branch,bool update)noexcept {
    if(!Identity(identity)||branch>=3||!r.id||r.entry.nativeInvocation!=r.id||!r.finished||!r.identityRetained||
       !r.exit.boundary||!r.entry.thread||r.entry.thread!=r.exit.thread||r.entry.nowNs<=0||r.exit.nowNs<r.entry.nowNs||
       r.entry.caller<0x10000||!Boundary(r.entry.boundary,identity,branch)||!Boundary(*r.exit.boundary,identity,branch)||
       r.entry.boundary.snapshotSequence!=r.exit.boundary->snapshotSequence||
       r.entry.boundary.soldierFlags!=r.exit.boundary->soldierFlags||!Context(r,update))return false;
    return true;
}
bool Unheld(const ReloadFlowRecord& r)noexcept {
    const auto& h=r.exit.hold;
    return !r.exit.holdRequested&&!h.applied&&!h.restored&&!h.unexpectedNativeWrite&&!h.original&&!h.beforeRestore;
}
bool Neutral(const ReloadFlowRecord& r,bool requireFireReleased)noexcept {
    const auto c=DecodeReloadUpdateContext(r.entry.copiedContext);
    return c&&!c->orderRequested&&!c->reloadRequested&&!c->flags24Through28[2]&&!c->flags24Through28[4]&&
        (!requireFireReleased||(!c->fireRequested&&!c->flags24Through28[3]));
}
bool SameState(const ReloadFlowBoundary& a,const ReloadFlowBoundary& b)noexcept {
    return a.current==b.current&&a.previous==b.previous&&a.next==b.next&&
        std::bit_cast<std::uint32_t>(a.timer)==std::bit_cast<std::uint32_t>(b.timer)&&
        a.loaded==b.loaded&&a.reserve==b.reserve&&a.flagsA8==b.flagsA8;
}
}
bool ValidateCycleUpdate(const ReloadFlowRecord& r,const ReloadHoldIdentity& identity,unsigned branch)noexcept {
    return r.entry.kind==ReloadFlowEvent::Update&&r.entry.depth==1&&!r.entry.parent&&!r.entry.nativeParent&&
        r.entry.update==r.id&&r.entry.nativeUpdate==r.id&&Basic(r,identity,branch,true);
}
bool CycleConvergencePhase(unsigned current,unsigned next,float timer,bool m95,bool empty)noexcept {
    if(!std::isfinite(timer)||timer<0)return false;
    if(current==2)return next==2&&timer==0;
    if(current==1)return (next==1||next==2)&&timer==0;
    if(empty)return current==6&&next==1&&timer>0&&timer<=1;
    if(current==6)return next==7&&timer>0&&timer<=1;
    if(current==7)return (next==7||next==8)&&timer>=0&&timer<=.5f;
    return current==8&&next==1&&timer>=0&&timer<=(m95?2.3f:1.f);
}
bool CycleGuardedUpdate(const ReloadFlowRecord& r,const ReloadHoldIdentity& identity,unsigned branch,
    const NativeCycleInputGuard& guard,unsigned guardedSteps,unsigned rejectedSteps,bool m95,bool empty)noexcept {
    if(!ValidateCycleUpdate(r,identity,branch)||!Unheld(r)||!guard.Exact()||rejectedSteps)return false;
    std::array<std::uint32_t,2> original{};std::memcpy(original.data(),r.entry.copiedContext.data()+0x28,8);
    if(guard.original!=original||guard.effective!=std::array<std::uint32_t,2>{(original[0]&~255u)|1u,0})return false;
    const auto context=DecodeReloadUpdateContext(r.entry.copiedContext);
    const auto& before=r.entry.boundary;const auto& after=*r.exit.boundary;
    return context&&!context->flags24Through28[2]&&
        (!(before.current==2&&before.next==2&&after.current==2&&after.next==2)||guardedSteps)&&
        CycleConvergencePhase(before.current,before.next,before.timer,m95,empty)&&
        CycleConvergencePhase(after.current,after.next,after.timer,m95,empty)&&
        before.loaded==after.loaded&&before.reserve==after.reserve&&
        (after.flagsA8==before.flagsA8||after.flagsA8==(before.flagsA8&~2u));
}
bool CycleGuardedCommit(const ReloadFlowRecord& r,const ReloadHoldIdentity& identity,unsigned branch,std::uint64_t parent,bool m95,bool empty)noexcept {
    (void)m95;const auto& e=r.entry;
    if(!parent||r.id<=parent||e.kind!=ReloadFlowEvent::Commit||e.depth!=2||e.parent!=parent||e.nativeParent!=parent||
       e.update!=parent||e.nativeUpdate!=parent||!Basic(r,identity,branch,false)||!Unheld(r))return false;
    const auto c=DecodeReloadUpdateContext(e.copiedContext);const auto& a=e.boundary;const auto& z=*r.exit.boundary;
    return c&&!c->inputFlags&&!c->flags24Through28[2]&&c->flags24Through28[4]&&
        (a.timer==0)&&((a.current==7&&a.next==8&&!empty)||(a.current==8&&a.next==1&&!empty)||(a.current==6&&a.next==1&&empty)||(a.current==1&&a.next==2))&&
        z.previous==a.current&&z.current==a.next&&z.next==a.next&&z.timer==a.timer&&
        z.loaded==a.loaded&&z.reserve==a.reserve&&z.flagsA8==a.flagsA8;
}
bool CycleConvergenceRestore(const ReloadFlowRecord& r,const ReloadHoldIdentity& identity,unsigned branch,bool m95,bool empty)noexcept {
    const auto& a=r.entry;const auto& e=r.exit;
    if(!Identity(identity)||branch>=2||a.kind!=ReloadFlowEvent::Restore||!r.id||a.nativeInvocation!=r.id||
       a.depth!=1||a.parent||a.nativeParent||a.update||a.nativeUpdate||!r.finished||!r.identityRetained||
       !e.boundary||!a.thread||a.thread!=e.thread||a.nowNs<=0||e.nowNs<a.nowNs||a.caller<0x10000||a.context<0x10000||
       a.contextCopied||e.contextCopied||!Unheld(r)||!Boundary(a.boundary,identity,branch)||!Boundary(*e.boundary,identity,branch)||
       a.boundary.snapshotSequence!=e.boundary->snapshotSequence||a.boundary.soldierFlags!=e.boundary->soldierFlags||
       !ReloadRestoreMatched(r))return false;
    const auto& b=a.boundary;const auto& v=*e.boundary;
    return CycleConvergencePhase(b.current,b.next,b.timer,m95,empty)&&
        CycleConvergencePhase(v.current,v.next,v.timer,m95,empty)&&
        b.loaded==v.loaded&&b.reserve==v.reserve&&b.flagsA8==v.flagsA8;
}
bool CycleM95PreHoldRewind(const ReloadFlowRecord& r,const ReloadHoldIdentity& identity,unsigned branch)noexcept {
    const auto& a=r.entry;const auto& e=r.exit;
    if(!Identity(identity)||branch>=2||a.kind!=ReloadFlowEvent::Restore||!r.id||a.nativeInvocation!=r.id||
       a.depth!=1||a.parent||a.nativeParent||a.update||a.nativeUpdate||!r.finished||!r.identityRetained||
       !e.boundary||!a.thread||a.thread!=e.thread||a.nowNs<=0||e.nowNs<a.nowNs||a.caller<0x10000||a.context<0x10000||
       a.contextCopied||e.contextCopied||!Unheld(r)||!Boundary(a.boundary,identity,branch)||!Boundary(*e.boundary,identity,branch)||
       a.boundary.snapshotSequence!=e.boundary->snapshotSequence||a.boundary.soldierFlags!=e.boundary->soldierFlags||
       !ReloadRestoreMatched(r))return false;
    const auto& b=a.boundary;const auto& v=*e.boundary;
    return b.current==8&&(b.previous==7||b.previous==8)&&b.next==1&&b.timer>0&&b.timer<=2.3f&&
        v.current==6&&v.previous==8&&v.next==7&&v.timer>0&&v.timer<=.1f&&
        b.loaded>0&&v.loaded==b.loaded&&v.reserve==b.reserve&&v.flagsA8==b.flagsA8;
}
bool CyclePreHoldSnapshotProgression(const ReloadFlowRecord& r,const ReloadHoldIdentity& identity,unsigned branch,
    const NativeCycleHeldBoundary& held,float precedingMaximum)noexcept {
    const auto& a=r.entry;const auto& e=r.exit;
    if(!Identity(identity)||branch>=2||a.kind!=ReloadFlowEvent::Restore||!r.id||a.nativeInvocation!=r.id||
       a.depth!=1||a.parent||a.nativeParent||a.update||a.nativeUpdate||!r.finished||!r.identityRetained||
       !e.boundary||!a.thread||a.thread!=e.thread||a.nowNs<=0||e.nowNs<a.nowNs||a.caller<0x10000||a.context<0x10000||
       a.contextCopied||e.contextCopied||!Unheld(r)||!Boundary(a.boundary,identity,branch)||!Boundary(*e.boundary,identity,branch)||
       a.boundary.snapshotSequence!=e.boundary->snapshotSequence||a.boundary.soldierFlags!=e.boundary->soldierFlags||
       !ReloadRestoreMatched(r)||!std::isfinite(precedingMaximum)||precedingMaximum<=0||precedingMaximum>30||
       held.current<=6||held.current>15||held.next>15||held.emptyAmmunition||!std::isfinite(held.maxTimer)||held.maxTimer<=0)return false;
    const auto phase=[&](const ReloadFlowBoundary& b){
        return std::isfinite(b.timer)&&b.timer>0&&((b.current==6&&b.next==7&&b.timer<=precedingMaximum)||
            (b.current==held.current&&b.next==held.next&&b.timer<=held.maxTimer));};
    const auto& b=a.boundary;const auto& v=*e.boundary;
    return b.current!=v.current&&phase(b)&&phase(v)&&b.loaded>0&&v.loaded==b.loaded&&
        v.reserve==b.reserve&&v.flagsA8==b.flagsA8;
}
bool CycleM95ForwardReadyRestore(const ReloadFlowRecord& r,const ReloadHoldIdentity& identity)noexcept {
    constexpr unsigned branch=0;
    const auto& a=r.entry;const auto& e=r.exit;
    if(!Identity(identity)||branch>=2||a.kind!=ReloadFlowEvent::Restore||!r.id||a.nativeInvocation!=r.id||
       a.depth!=1||a.parent||a.nativeParent||a.update||a.nativeUpdate||!r.finished||!r.identityRetained||
       !e.boundary||!a.thread||a.thread!=e.thread||a.nowNs<=0||e.nowNs<a.nowNs||a.caller<0x10000||a.context<0x10000||
       a.contextCopied||e.contextCopied||!Unheld(r)||!Boundary(a.boundary,identity,branch)||!Boundary(*e.boundary,identity,branch)||
       a.boundary.snapshotSequence!=e.boundary->snapshotSequence||a.boundary.soldierFlags!=e.boundary->soldierFlags||
       !ReloadRestoreMatched(r))return false;
    const auto& b=a.boundary;const auto& v=*e.boundary;
    return b.current==8&&(b.previous==7||b.previous==8)&&b.next==1&&b.timer>0&&b.timer<=2.3f&&
        v.current==2&&v.previous==8&&v.next==2&&v.timer==0&&
        b.loaded>0&&v.loaded==b.loaded&&v.reserve==b.reserve&&v.flagsA8==b.flagsA8;
}
bool CycleM95ForwardIdleUpdate(const ReloadFlowRecord& r,const ReloadHoldIdentity& identity)noexcept {
    if(!ValidateCycleUpdate(r,identity,0)||!Unheld(r)||!Neutral(r,true))return false;
    const auto& b=r.entry.boundary;const auto& a=*r.exit.boundary;
    // Actual239 Update2752 clears only the stock per-update bit0x2 (130 to128).
    // Preserve every other state/count/flag bit; this is still its own completed Update.
    auto compared=a;compared.flagsA8=b.flagsA8;
    return b.current==2&&b.previous==8&&b.next==2&&b.timer==0&&SameState(b,compared)&&
        (a.flagsA8==b.flagsA8||a.flagsA8==(b.flagsA8&~2u));
}

bool CycleShot(const ReloadFlowRecord& r,const ReloadHoldIdentity& identity,unsigned branch)noexcept {
    if(!ValidateCycleUpdate(r,identity,branch)||!Unheld(r)||!Neutral(r,false))return false;
    const auto& before=r.entry.boundary;const auto& after=*r.exit.boundary;
    const bool manualPath=(after.current==6&&after.previous==5&&after.next==7)||
        (after.current==7&&after.previous==6&&(after.next==7||after.next==8))||
        (after.current==8&&after.previous==7&&after.next==1);
    // Last-round behavior is deliberately not admitted by a positive-load
    // manual-cycle candidate. It needs its own native empty/chamber contract.
    return manualPath&&before.loaded>=2&&after.loaded==before.loaded-1&&after.reserve==before.reserve;
}
bool CycleEmptyShot(const ReloadFlowRecord& r,const ReloadHoldIdentity& identity,unsigned branch)noexcept {
    if(!ValidateCycleUpdate(r,identity,branch)||!Unheld(r)||!Neutral(r,false))return false;
    const auto& before=r.entry.boundary;const auto& after=*r.exit.boundary;
    // Actual212 own shot receipts: the empty branch bypasses states7/8.
    return before.loaded==1&&after.loaded==0&&after.reserve==before.reserve&&
        after.current==6&&after.previous==5&&after.next==1&&after.timer>0&&after.timer<=1.f;
}
bool CycleHold(const ReloadFlowRecord& r,const ReloadHoldIdentity& identity,unsigned branch,
    const NativeCycleHeldBoundary& p,bool arming)noexcept {
    if(!ValidateCycleUpdate(r,identity,branch)||!Neutral(r,true)||p.current>15||p.previous>15||p.next>15||p.current==p.next||
       !std::isfinite(p.minArmTimer)||!std::isfinite(p.maxTimer)||p.minArmTimer<=0||p.maxTimer<p.minArmTimer||p.maxTimer>30)return false;
    const auto& before=r.entry.boundary;const auto& after=*r.exit.boundary;const auto& hold=r.exit.hold;
    std::uint32_t original=0;std::memcpy(&original,r.entry.copiedContext.data()+0x18,4);
    return r.exit.holdRequested&&hold.applied&&hold.restored&&!hold.unexpectedNativeWrite&&
        hold.original==original&&hold.beforeRestore==0&&
        before.current==p.current&&before.previous==p.previous&&before.next==p.next&&before.timer>0&&
        before.timer<=p.maxTimer&&(!arming||before.timer>=p.minArmTimer)&&
        (p.emptyAmmunition?before.loaded==0:before.loaded>0)&&SameState(before,after);
}
std::optional<NativeCycleCommitEvidence> CycleCommit(const ReloadFlowRecord& r,const ReloadHoldIdentity& identity,
    unsigned branch,std::uint64_t parentUpdateId)noexcept {
    const auto& a=r.entry;
    if(!parentUpdateId||r.id<=parentUpdateId||a.kind!=ReloadFlowEvent::Commit||a.depth!=2||
       a.parent!=parentUpdateId||a.nativeParent!=parentUpdateId||a.update!=parentUpdateId||a.nativeUpdate!=parentUpdateId||
       !Basic(r,identity,branch,false)||!Unheld(r)||!Neutral(r,true))return {};
    const auto& before=a.boundary;const auto& after=*r.exit.boundary;
    if(before.current==before.next||after.previous!=before.current||after.current!=before.next||after.next!=before.next||
       std::bit_cast<std::uint32_t>(before.timer)!=std::bit_cast<std::uint32_t>(after.timer)||
       before.loaded!=after.loaded||before.reserve!=after.reserve||before.flagsA8!=after.flagsA8)return {};
    return NativeCycleCommitEvidence{r.id,parentUpdateId,branch,before.current,before.previous,before.next,
        after.current,after.previous,after.next,after.loaded,after.reserve,after.timer,a.nowNs,r.exit.nowNs};
}
std::optional<NativeCycleRestoreEvidence> CycleRestore(const ReloadFlowRecord& r,const ReloadHoldIdentity& identity,
    unsigned branch,const NativeCycleHeldBoundary& p)noexcept {
    const auto proof=CyclePredictionRestore(r,identity,branch);
    if(!proof||(p.emptyAmmunition?proof->loaded!=0:proof->loaded<=0)||p.current>15||p.previous>15||p.next>15||p.current==p.next||
       !std::isfinite(p.maxTimer)||p.maxTimer<=0||p.maxTimer>30||
       proof->current!=p.current||proof->next!=p.next||
       (proof->beforePrevious!=p.previous&&proof->beforePrevious!=p.current)||
       proof->beforeTimer<=0||proof->beforeTimer>p.maxTimer||proof->timer<=0||proof->timer>p.maxTimer)return {};
    return proof;
}
std::optional<NativeCycleRestoreEvidence> CyclePredictionRestore(const ReloadFlowRecord& r,const ReloadHoldIdentity& identity,
    unsigned branch)noexcept {
    const auto& a=r.entry;const auto& e=r.exit;
    if(!Identity(identity)||branch>=2||a.kind!=ReloadFlowEvent::Restore||!r.id||a.nativeInvocation!=r.id||
       a.depth!=1||a.parent||a.nativeParent||a.update||a.nativeUpdate||!r.finished||!r.identityRetained||
       !e.boundary||!a.thread||a.thread!=e.thread||a.nowNs<=0||e.nowNs<a.nowNs||a.caller<0x10000||a.context<0x10000||
       a.contextCopied||e.contextCopied||!Unheld(r)||!Boundary(a.boundary,identity,branch)||!Boundary(*e.boundary,identity,branch)||
       a.boundary.snapshotSequence!=e.boundary->snapshotSequence||a.boundary.soldierFlags!=e.boundary->soldierFlags||
       !ReloadRestoreMatched(r))return {};
    const auto& b=a.boundary;const auto& v=*e.boundary;
    if(v.current!=b.current||v.previous!=b.current||v.next!=b.next||
       v.loaded!=b.loaded||v.reserve!=b.reserve||v.flagsA8!=b.flagsA8)return {};
    return NativeCycleRestoreEvidence{r.id,branch,b.previous,v.loaded,v.reserve,a.nowNs,e.nowNs,
        v.current,v.next,b.timer,v.timer};
}
}
