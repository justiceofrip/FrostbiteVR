#include "Bc2ChestKnifeNative.h"
#include "KnifeNativeCode.h"
#include "Bc2KnifeAttackGate.h"
#include "Bc2KnifeCopyReceipt.h"
#include "Bc2KnifeAcceptedOwnership.h"
#include "Bc2KnifeSessionClose.h"
#include "Bc2KnifeSessionCloseProof.h"
#include <atomic>
#include <mutex>
#include <cmath>
#if defined(_WIN32) && defined(_M_IX86) && defined(FVR_BC2_CHEST_KNIFE_DRAW_RETURN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <MinHook.h>
#include <intrin.h>
namespace fvr::bc2::chestKnifeNative { namespace {
using namespace interaction;namespace pk=private_knife;
#include "KnifeNativeBooleanFields.inc"
struct Service {
    std::mutex gate;std::atomic<bool> enabled=false;std::atomic<unsigned> active=0,gaps=0;std::atomic<bool> ownedObservationGap=false;
    std::atomic<std::uint64_t> sourceEpoch=1;
    // Session lifecycle only: no input sequence/lease and no attack capability.
    std::atomic<bool> closing=false;std::atomic<std::int64_t> closeRequestedNs=0;
    pk::KnifeSessionClose close;unsigned closeReadGaps=0;
    struct CloseProof {
        bool snapshot=false;unsigned stage=0,branch=2,reason=0,phase=0,mask=0;
        std::int64_t observedNs=0;std::uint64_t lifetime=0;
        unsigned actor=0,weak=0,original=0,knife=0;
        std::array<unsigned,3> selected{},previous{},knifeState{},knifeNext{},originalState{},originalNext{};
        std::array<int,3> knifeLoaded{},knifeReserve{},originalLoaded{},originalReserve{};
        bool operator==(const CloseProof&)const=default;
    };
    std::array<CloseProof,16> closeProofs{};
    std::array<CloseProof,4> closeFirstRejected{};
    std::array<bool,4> closeRejectedRecorded{};
    std::array<CloseProof,2> closeOffered{};
    std::array<bool,2> closeOfferRecorded{};
    CloseProof closeLast{};unsigned closeProofCount=0,closeProofDropped=0;bool closeLastKnown=false;
    ReloadStateMemory memory{};ReloadStateBinding state{};ReloadServerBinding server{};ReloadFlowBinding flow{};unsigned base=0;
    std::int64_t frequency=0;bool installed=false,drained=false,owned=false,returnReady=false,motionEnabled=false;std::atomic<bool> bad=false;
    std::optional<pk::Anchor> anchor;std::shared_ptr<const pk::Anchor> stagedGuard;
    std::atomic<std::shared_ptr<const pk::Anchor>> deployGuard;pk::DrawReturnGate commands;pk::KnifeAttackGate attacks;KnifeSample source{};
    pk::OriginalKnifeSource sourceIdentity;
    std::uint64_t stagedEpoch=0,bodyOperation=0,nativeLifetime=0,observationSequence=0;
    std::optional<KnifeCommand> outstanding;
    std::uint64_t lastAttackSequence=0,lastAttackSerial=0;std::int64_t lastAttackObservedNs=0,lastAttackReceiptNs=0;
    std::array<std::atomic<std::uint64_t>,3> returnedCopySerial{};unsigned attackUnknown=0;
    std::array<unsigned,3> updates{},states{};std::array<std::atomic<unsigned>,3> deploy{};unsigned chooserOverrides=0;std::atomic<unsigned> deployMisses=0,foreignGuards=0,returnSuppressed=0;unsigned attackCopies=0,attackReceipts=0;
    bool fireSeen=false,attackSeen=false,sentinelChanged=false;
    unsigned completedInputMask=0,completedObservationMask=0;
    struct CallbackWitness {
        bool recorded=false;std::int64_t observedNs=0;
        unsigned reason=0,branch=0,firing=0,context=0,caller=0,argument=0,before=0,current=0,next=0,consumed=0,observed=0;
        std::uint64_t tokenLifetime=0,currentLifetime=0,tokenSerial=0,pendingSerial=0;
        int loadedBefore=0,reserveBefore=0,loadedAfter=0,reserveAfter=0;
        std::array<unsigned,3> selected{};
    } firstRejectedCallback,firstPredictionActivity;
    // Three bounded receipts. The before hash travels in this call's token,
    // so coherent lifetime retirement cannot replace its comparison source.
    std::array<std::atomic<std::uint64_t>,3> copySerial{},copyInput{},copyObservedNs{},copyContext{},copyCaller{};
    std::array<std::atomic<std::uint64_t>,3> copyReturnedNs{},copyOriginalUnchanged{},copyPostReadKnown{},copyHashBefore{},copyHashAfter{};
} service;
using ChooseFn=int(__thiscall*)(void*,void*);using ReadyFn=bool(__thiscall*)(void*);
using SetterFn=void(__thiscall*)(void*,unsigned,bool);
ChooseFn originalChoose=nullptr;ReadyFn originalReady=nullptr;SetterFn originalSetter=nullptr;std::array<void*,3> hooks{};
struct Active {Active(){service.active.fetch_add(1,std::memory_order_acq_rel);}~Active(){service.active.fetch_sub(1,std::memory_order_acq_rel);}};
std::int64_t Now()noexcept {LARGE_INTEGER q{};if(!service.frequency||!QueryPerformanceCounter(&q))return 0;
    return q.QuadPart/service.frequency*1000000000ll+(q.QuadPart%service.frequency)*1000000000ll/service.frequency;}
template<class T>T Word(const std::byte* b,unsigned at)noexcept{T value{};std::memcpy(&value,b+at,sizeof(value));return value;}
bool SameActor(const ReloadStateOwner& a,const ReloadStateOwner& b)noexcept{return a.player==b.player&&a.soldier==b.soldier&&a.weak==b.weak&&a.actorGeneration==b.actorGeneration;}
bool SameCommandSource(const KnifeCommand& c,const KnifeSample& s)noexcept {
    const auto& a=c.source;const auto& b=s.source;
    return a.owner==b.owner&&a.sequence==b.sequence&&a.observedNs==b.observedNs&&a.deadlineNs==b.deadlineNs&&
        c.knife==s.knife&&s.onFoot&&KnifeCommandInputCurrent(c,s)&&s.fireSuppressed&&
        GameplayOwnsInput(s.menu,b.nowNs)&&s.menu.deadlineNs>b.nowNs;
}
bool SourceCurrent(const KnifeCommand& c,std::int64_t now)noexcept {
    if(service.closing.load(std::memory_order_acquire)||service.stagedEpoch!=service.sourceEpoch.load(std::memory_order_acquire)||
       !pk::DrawReturnGate::Fresh(c,now)||!SameCommandSource(c,service.source)||
       !GameplayOwnsInput(service.source.menu,now))return false;
    const auto h=unsigned(c.hand);
    return c.kind==KnifeCommandKind::Return?!service.source.gripHeld[h]&&c.source.released[h]:
        service.source.gripHeld[h]&&!c.source.released[h];
}
bool Flags(const pk::Anchor& a)noexcept {
    Reader r{service.memory,service.base,service.state.imageSize};
    if(!r.Field(a.original.client.config.firingData,"WeaponFiringData",0,"FireOnDeploy",0x48)||
       !r.Field(a.knifeFiringData,"WeaponFiringData",0,"FireOnDeploy",0x48))return false;
    constexpr const char* names[]{"HideWhenOutOfAmmo","SwitchToPrimaryWhenOutOfAmmo","SwitchToPreviousWeaponAfterFire","AllowSwitchingToWeaponOutOfAmmo"};
    for(unsigned n=0;n<4;++n)if(!r.Field(a.knifeData,"SoldierWeaponData",0x130,names[n],0x121+n))return false;
    std::uint8_t original=1,originalAgain=1,fire=0,fireAgain=0;std::array<std::uint8_t,4> flags{},again{};
    return r.Get(std::uint64_t(a.original.client.config.firingData)+0x48,original)&&!original&&
        r.Get(std::uint64_t(a.knifeFiringData)+0x48,fire)&&fire==1&&r.Read(std::uint64_t(a.knifeData)+0x121,flags.data(),4)&&
        flags==std::array<std::uint8_t,4>{0,0,1,1}&&
        r.Get(std::uint64_t(a.original.client.config.firingData)+0x48,originalAgain)&&originalAgain==original&&
        r.Get(std::uint64_t(a.knifeFiringData)+0x48,fireAgain)&&fireAgain==fire&&
        r.Read(std::uint64_t(a.knifeData)+0x121,again.data(),4)&&again==flags;
}
// Derive ACTUAL current selected item from the native inventory. This local
// identity is used only for hook-side native coherence, never published as an
// input/pose/equipment lease. Source authority remains the original command.
bool NativeSelection(const pk::Anchor& a,pk::Selection& out)noexcept {
    pk::detail::Reader r{service.memory};
    unsigned slot=0;if(!r.Get(std::uint64_t(a.original.links.clientInventory)+0x14c,slot)||slot>=a.original.links.clientItems.size())return false;
    auto actual=a.original.client.owner;actual.weapon=a.original.links.clientItems[slot];
    pk::Selection before,after;
    if(!pk::detail::Actor(r,service.server,service.base,a,actual,before)||
       !pk::detail::Actor(r,service.server,service.base,a,actual,after)||before!=after)return false;
    out=before;return true;
}
bool NativeSelection(pk::Selection& out)noexcept {return service.anchor&&NativeSelection(*service.anchor,out);}
bool SameAnchor(const pk::Anchor& a,const pk::Anchor& b)noexcept {
    return a.original.client.owner==b.original.client.owner&&a.original.links==b.original.links&&
        a.original.client.config==b.original.client.config&&a.knifeWeapon==b.knifeWeapon&&a.knifeSlot==b.knifeSlot&&
        a.knifeData==b.knifeData&&a.knifeFiringData==b.knifeFiringData&&a.knifeFiring==b.knifeFiring&&
        a.knifeClientEffects==b.knifeClientEffects&&a.knifeServerEffects==b.knifeServerEffects&&
        pk::detail::SameState(a.original.state,b.original.state)&&
        pk::detail::SameState(a.original.client.branches[0],b.original.client.branches[0])&&
        pk::detail::SameState(a.original.client.branches[1],b.original.client.branches[1]);
}
bool Ammo(const pk::Snapshot& v)noexcept {
    if(!service.anchor||!pk::KnifeSentinel(v))return false;const auto& a=service.anchor->original;
    for(unsigned n=0;n<3;++n){const auto& old=n<2?a.client.branches[n]:a.state;
        const auto& current=n<2?v.original.client.branches[n]:v.original.server;
        if(old.loaded!=current.loaded||old.reserve!=current.reserve)return false;}
    return true;
}
bool Ready(const pk::Item& v)noexcept {
    for(const auto* s:{&v.client.branches[0],&v.client.branches[1],&v.server})
        if(s->currentState!=2||s->nextState!=2||!std::isfinite(s->phaseTimer)||s->phaseTimer<0||s->phaseTimer>1000000||(s->flagsA8&8))return false;
    return true;
}

// Fresh native facts only. Never turn retained controller metadata into close
// authority. Unknown reads preserve the accepted operation and its suppression.
enum CloseReason:unsigned {CloseOkay,CloseOwnership,CloseSelection,CloseRead,CloseFlags,CloseAmmo,CloseClock,
    CloseNotIdle,CloseWrongSelected,CloseWrongPrevious,CloseBoundaryRejected,CloseNotOriginal,CloseNotDrained};
void RecordClose(unsigned stage,unsigned branch,unsigned reason,const pk::Snapshot* snapshot=nullptr)noexcept {
    Service::CloseProof row;row.stage=stage;row.branch=branch;row.reason=reason;
    row.phase=unsigned(service.close.Phase());row.mask=service.close.Mask();row.lifetime=service.nativeLifetime;
    if(service.anchor){row.actor=service.anchor->original.client.owner.soldier;row.weak=service.anchor->original.client.owner.weak;
        row.original=service.anchor->original.client.owner.weapon;row.knife=service.anchor->knifeWeapon;}
    if(snapshot){row.snapshot=true;row.selected=snapshot->selected.current;row.previous=snapshot->selected.previous;
        for(unsigned n=0;n<3;++n){const auto& k=n<2?snapshot->knife.client.branches[n]:snapshot->knife.server;
            const auto& o=n<2?snapshot->original.client.branches[n]:snapshot->original.server;
            row.knifeState[n]=k.currentState;row.knifeNext[n]=k.nextState;row.originalState[n]=o.currentState;row.originalNext[n]=o.nextState;
            row.knifeLoaded[n]=k.loaded;row.knifeReserve[n]=k.reserve;row.originalLoaded[n]=o.loaded;row.originalReserve[n]=o.reserve;}}
    // Keep transitions and the first rejected predicate at each boundary. No native reads,
    // allocation or I/O here; the existing service gate owns this bounded observation.
    if(service.closeLastKnown){auto prior=service.closeLast;prior.observedNs=0;if(prior==row)return;}
    row.observedNs=Now();service.closeLast=row;service.closeLastKnown=true;
    if(stage==1&&reason==CloseOkay&&branch<service.closeOffered.size()&&!service.closeOfferRecorded[branch]){
        service.closeOffered[branch]=row;service.closeOfferRecorded[branch]=true;}
    if(service.closeProofCount<service.closeProofs.size())service.closeProofs[service.closeProofCount++]=row;
    else ++service.closeProofDropped;
    if(reason&&stage<service.closeFirstRejected.size()&&!service.closeRejectedRecorded[stage]){
        service.closeFirstRejected[stage]=row;service.closeRejectedRecorded[stage]=true;}
}
std::optional<pk::Snapshot> CloseSnapshot(unsigned& reason)noexcept {
    reason=CloseOkay;const auto began=Now();
    if(!service.anchor||pk::ReadAcceptedKnifeOwnership(service.memory,*service.anchor,
        service.base+service.state.firingVtableRva)!=pk::AcceptedKnifeOwnership::Same){reason=CloseOwnership;return {};}
    pk::Selection selected;if(!NativeSelection(selected)){reason=CloseSelection;return {};}
    auto current=service.anchor->original.client.owner;
    current.weapon=service.anchor->original.links.clientItems[selected.current[0]];
    const auto snapshot=pk::Read(service.memory,service.state,service.server,service.base,*service.anchor,
        current,++service.observationSequence,Now());
    if(!snapshot||snapshot->selected!=selected){reason=CloseRead;return {};}
    if(!Flags(*service.anchor)){reason=CloseFlags;return {};}
    if(!Ammo(*snapshot)){service.bad=true;reason=CloseAmmo;return {};}
    const auto ended=Now();if(began<=0||ended<began||ended-began>=100000000){reason=CloseClock;return {};}
    return snapshot;
}
bool CloseIdle(const pk::Snapshot& s,bool returned=false)noexcept {return Ready(returned?s.original:s.knife);}
unsigned Branch(unsigned firing)noexcept {
    if(service.anchor)for(unsigned n=0;n<3;++n)if(service.anchor->knifeFiring[n]==firing)return n+1;return 0;
}
bool State(unsigned firing,std::array<std::byte,0xb0>& bytes)noexcept {
    pk::detail::Reader r{service.memory};return service.anchor&&r.Read(firing,bytes.data(),bytes.size())&&
        Word<unsigned>(bytes.data(),0)==service.base+service.state.firingVtableRva&&
        Word<unsigned>(bytes.data(),8)==service.anchor->knifeFiringData&&
        Word<unsigned>(bytes.data(),0x3c)<=15&&Word<unsigned>(bytes.data(),0x44)<=15;
}
void RecordCallback(Service::CallbackWitness& receipt,unsigned reason,unsigned branch,const OriginalFiringUpdate& call,
    const OriginalFiringToken& token,const std::array<std::byte,0xb0>& state,const pk::Selection& selected)noexcept {
    if(receipt.recorded)return;
    receipt.recorded=true;receipt.observedNs=Now();receipt.reason=reason;receipt.branch=branch;
    receipt.firing=call.firing;receipt.context=call.context;receipt.caller=call.caller;receipt.argument=call.argument;
    receipt.before=unsigned(token.words[2]);receipt.current=Word<unsigned>(state.data(),0x3c);receipt.next=Word<unsigned>(state.data(),0x44);
    receipt.tokenLifetime=token.words[0];receipt.currentLifetime=service.nativeLifetime;receipt.tokenSerial=token.words[6];
    receipt.pendingSerial=service.attacks.Pending()?service.attacks.Pending()->serial:0;
    receipt.consumed=service.attacks.ConsumedMask();receipt.observed=service.attacks.AttackMask();
    receipt.loadedBefore=int(std::uint32_t(token.words[4]));receipt.reserveBefore=int(std::uint32_t(token.words[5]));
    receipt.loadedAfter=Word<int>(state.data(),0x7c);receipt.reserveAfter=Word<int>(state.data(),0x80);receipt.selected=selected.current;
}
OriginalFiringToken Enter(void*,const OriginalFiringUpdate& call)noexcept {
    OriginalFiringToken token;service.active.fetch_add(1,std::memory_order_acq_rel);token.words[7]=1;
    if(!service.enabled.load(std::memory_order_acquire))return token;
    std::unique_lock lock(service.gate,std::try_to_lock);
    if(!lock.owns_lock()){service.gaps.fetch_add(1);
        const auto guard=service.deployGuard.load(std::memory_order_acquire);
        if(guard&&std::find(guard->knifeFiring.begin(),guard->knifeFiring.end(),call.firing)!=guard->knifeFiring.end())service.ownedObservationGap=true;
        return token;}
    const auto branch=service.owned?Branch(call.firing):0;if(!branch)return token;
    pk::Selection selected;std::array<std::byte,0xb0> state{};std::array<std::byte,0x30> context{};
    const pk::detail::Reader r{service.memory};
    if(!NativeSelection(selected)||!State(call.firing,state)||!r.Read(call.context,context.data(),context.size())){++service.gaps;service.ownedObservationGap=true;return token;}
    const auto input=DecodeReloadUpdateContext(context);if(!input){++service.gaps;service.ownedObservationGap=true;return token;}
    token.words[0]=service.nativeLifetime;token.words[1]=branch;token.words[2]=Word<unsigned>(state.data(),0x3c);
    token.words[3]=input->fireRequested?1:0;
    if(service.attacks.Pending())token.words[6]=service.attacks.Pending()->serial;
    token.words[4]=std::uint32_t(Word<int>(state.data(),0x7c));token.words[5]=std::uint32_t(Word<int>(state.data(),0x80));return token;
}
void Leave(void*,const OriginalFiringUpdate& call,const OriginalFiringToken& token)noexcept {
    // Published only after this exact copied original Update has returned.
    // Observation lock/read loss cannot mistake an in-flight copy for idle.
    if((token.words[3]&2)&&token.words[1]>=1&&token.words[1]<=3){
        const auto branch=unsigned(token.words[1]-1);std::array<std::byte,0x30> after{};
        const pk::detail::Reader reader{service.memory};const bool known=reader.Read(call.context,after.data(),after.size());
        const auto before=pk::KnifeContextHash(token);
        const auto afterHash=known?pk::KnifeContextHash(after):0;
        service.copyPostReadKnown[branch].store(known,std::memory_order_relaxed);
        service.copyHashBefore[branch]=before;service.copyHashAfter[branch]=afterHash;
        service.copyOriginalUnchanged[branch].store(known&&before==afterHash,std::memory_order_relaxed);
        service.copyReturnedNs[branch].store(std::uint64_t(Now()),std::memory_order_relaxed);
        // Publish only after both the real original call and the untouched
        // source-context observation. Missing proof cannot be called restored.
        service.returnedCopySerial[branch].store(token.words[6],std::memory_order_release);
    }
    if(token.words[1]&&service.enabled.load(std::memory_order_acquire)){
        std::unique_lock lock(service.gate,std::try_to_lock);
        if(!lock.owns_lock()){++service.gaps;service.ownedObservationGap=true;}
        else if(service.owned&&token.words[0]==service.nativeLifetime){
            std::array<std::byte,0xb0> state{};pk::Selection selected;
            if(!State(call.firing,state)||!NativeSelection(selected)){++service.gaps;service.ownedObservationGap=true;}
            else {const auto branch=unsigned(token.words[1]-1),current=Word<unsigned>(state.data(),0x3c),next=Word<unsigned>(state.data(),0x44);
                ++service.updates[branch];service.states[branch]=current;
                service.fireSeen|=(token.words[3]&1)!=0;service.attackSeen|=token.words[2]==5||token.words[2]==6||current==5||current==6||next==5||next==6;
                service.sentinelChanged|=std::uint32_t(token.words[4])!=UINT32_MAX||std::uint32_t(token.words[5])!=UINT32_MAX||
                    Word<int>(state.data(),0x7c)!=-1||Word<int>(state.data(),0x80)!=-1;
                const bool expected=service.attacks.ExpectedObservation(branch,token.words[6]);
                if(expected)service.attacks.Observe(branch,unsigned(token.words[2]),current,next);
                const bool busy=token.words[2]==5||token.words[2]==6||current==5||current==6||next==5||next==6;
                const unsigned rejected=(service.fireSeen?1u:0u)|((busy&&!expected)?2u:0u)|(service.sentinelChanged?4u:0u);
                if(rejected){RecordCallback(service.firstRejectedCallback,rejected,branch,call,token,state,selected);service.bad.store(true);}
                else if(busy&&branch==0&&expected)RecordCallback(service.firstPredictionActivity,0,branch,call,token,state,selected);
            }
        }
    }
    if(token.words[7])service.active.fetch_sub(1,std::memory_order_acq_rel);
}
int __fastcall Choose(void* self,void*,void* input){
    Active active;const auto caller=unsigned(_ReturnAddress());const int original=originalChoose(self,input);
    if(original!=-1||caller!=service.base+pk::ChooseCallerRva||!service.enabled.load(std::memory_order_acquire))return original;
    std::unique_lock lock(service.gate,std::try_to_lock);if(!lock.owns_lock()||service.bad)return original;
    if(service.closing.load(std::memory_order_acquire)){
        if(!service.anchor||service.close.Phase()!=pk::KnifeClosePhase::Returning)return original;
        const auto& anchor=*service.anchor;
        const std::array<unsigned,2> inventories{anchor.original.links.clientInventory,anchor.original.links.inventory};
        const unsigned branch=unsigned(self)==inventories[0]?0:unsigned(self)==inventories[1]?1:2;
        if(branch>=2)return original;
        unsigned reason=CloseOkay;const auto snapshot=CloseSnapshot(reason);
        if(!snapshot){++service.closeReadGaps;RecordClose(1,branch,reason);return original;}
        reason=!pk::KnifeCloseConsumerReady(*snapshot,branch)?CloseNotIdle:
            snapshot->selected.current[branch]!=anchor.knifeSlot?CloseWrongSelected:
            snapshot->selected.previous[branch]!=anchor.original.client.selectedSlot?CloseWrongPrevious:CloseOkay;
        const bool chosen=service.close.Choose(branch,Now(),reason==CloseOkay);
        RecordClose(1,branch,reason?reason:chosen?CloseOkay:CloseBoundaryRejected,&*snapshot);
        if(!chosen)return original;
        ++service.chooserOverrides;return int(anchor.original.client.selectedSlot);
    }
    if(!service.commands.Pending())return original;
    const auto& a=*service.anchor;const std::array<unsigned,2> inventories{a.original.links.clientInventory,a.original.links.inventory};
    const unsigned branch=unsigned(self)==inventories[0]?0:unsigned(self)==inventories[1]?1:2;if(branch==2)return original;
    pk::Selection selected;if(!NativeSelection(selected)||!Flags(a))return original;
    const auto& command=*service.commands.Pending();const bool drawing=command.kind==KnifeCommandKind::Draw;
    const unsigned from=drawing?a.original.client.selectedSlot:a.knifeSlot,to=drawing?a.knifeSlot:a.original.client.selectedSlot;
    if(selected.current[branch]!=from||(!drawing&&selected.previous[branch]!=to))return original;
    const auto now=Now();if(!service.commands.Choose(branch,now,SourceCurrent(command,now)))return original;
    service.deployGuard.store(service.stagedGuard,std::memory_order_release);service.owned=true;++service.chooserOverrides;return int(to);
}
// Denial belongs to the already accepted native tool operation. An unknown
// read never grants an action. Only a coherent replacement may retire it.
bool RetainGuard(const std::shared_ptr<const pk::Anchor>& guard)noexcept {
    const auto ownership=pk::ReadAcceptedKnifeOwnership(service.memory,*guard,service.base+service.state.firingVtableRva);
    if(ownership==pk::AcceptedKnifeOwnership::Same)return true;
    service.ownedObservationGap=true;
    if(ownership==pk::AcceptedKnifeOwnership::Unknown){++service.deployMisses;return true;}
    service.bad=true;++service.foreignGuards;auto expected=guard;
    service.deployGuard.compare_exchange_strong(expected,{},std::memory_order_acq_rel);return false;
}
void __fastcall AutomaticReturn(void* self,void*,unsigned slot,bool eligibility){
    Active active;const auto caller=unsigned(_ReturnAddress());
    if(!eligibility&&service.enabled.load(std::memory_order_acquire)){
        const auto guard=service.deployGuard.load(std::memory_order_acquire);
        if(guard&&slot==guard->original.client.selectedSlot&&
           ((unsigned(self)==guard->original.links.clientInventory&&caller==service.base+0x482873)||
            (unsigned(self)==guard->original.links.inventory&&caller==service.base+0x29f504))){
            pk::detail::Reader reader{service.memory};std::array<unsigned,2> selected{},again{};
            if(!reader.Read(std::uint64_t(unsigned(self))+0x14c,selected.data(),sizeof(selected))||
               !reader.Read(std::uint64_t(unsigned(self))+0x14c,again.data(),sizeof(again))||selected!=again){
                service.ownedObservationGap=true;
            }else if(selected[0]==guard->knifeSlot&&selected[1]==slot&&RetainGuard(guard)){
            // Exact native automatic-return caller only. Explicit chooser
            // return follows its ordinary setter call and remains untouched.
                ++service.returnSuppressed;return;
            }
        }
    }
    originalSetter(self,slot,eligibility);
}
std::optional<std::array<std::byte,0x30>> AttackInput(void*,const OriginalFiringUpdate& call,OriginalFiringToken& token)noexcept {
    if(!service.enabled.load(std::memory_order_acquire)||!service.motionEnabled)return {};
    std::unique_lock lock(service.gate,std::try_to_lock);
    if(!lock.owns_lock()||service.closing.load(std::memory_order_acquire)||!service.owned||service.bad||!service.attacks.Pending())return {};
    const auto branch=Branch(call.firing);if(!branch||!pk::KnifeAttackGate::InputBranch(branch-1))return {};
    pk::Selection selected;std::array<std::byte,0xb0> state{};
    std::array<std::byte,0x30> copy{};const pk::detail::Reader reader{service.memory};
    ULONG_PTR low=0,high=0;GetCurrentThreadStackLimits(&low,&high);
    if(call.caller!=service.base+service.flow.code[1].rva+0x2f||call.context<low||
       std::uint64_t(call.context)+copy.size()>high||(call.context&3)||
       pk::ReadAcceptedKnifeOwnership(service.memory,*service.anchor,service.base+service.state.firingVtableRva)!=pk::AcceptedKnifeOwnership::Same||
       !NativeSelection(selected)||!Flags(*service.anchor)||!State(call.firing,state)||
       !reader.Read(call.context,copy.data(),copy.size()))return {};
    for(auto slot:selected.current)if(slot!=service.anchor->knifeSlot)return {};
    const auto nativeInput=pk::CopyKnifeAttackInput(copy);const float timer=Word<float>(state.data(),0x50);
    if(!nativeInput||Word<unsigned>(state.data(),0x3c)!=2||
       Word<unsigned>(state.data(),0x44)!=2||!std::isfinite(timer)||timer<0||timer>1000000||
       (Word<std::uint8_t>(state.data(),0xa8)&8)||Word<int>(state.data(),0x7c)!=-1||Word<int>(state.data(),0x80)!=-1)return {};
    const auto now=Now();
    if(!service.attacks.Consume(branch-1,now,SourceCurrent(*service.attacks.Pending(),now)))return {};
    token.words[0]=service.nativeLifetime;token.words[1]=branch;token.words[2]=2;token.words[3]=2;
    token.words[4]=token.words[5]=UINT32_MAX;token.words[6]=service.attacks.Pending()->serial;
    const auto beforeHash=pk::KnifeContextHash(copy);
    pk::SetKnifeContextHash(token,beforeHash);
    const auto& command=*service.attacks.Pending();
    service.copyInput[branch-1]=command.source.sequence;service.copyObservedNs[branch-1]=std::uint64_t(command.source.observedNs);
    service.copyContext[branch-1]=call.context;service.copyCaller[branch-1]=call.caller;
    service.copyReturnedNs[branch-1]=0;service.copyOriginalUnchanged[branch-1]=0;service.copyPostReadKnown[branch-1]=0;
    service.copySerial[branch-1].store(command.serial,std::memory_order_release);
    ++service.attackCopies;return nativeInput;
}
bool __fastcall DeployReady(void* self,void*){
    Active active;const auto caller=unsigned(_ReturnAddress());const bool ready=originalReady(self);
    if(!ready||caller!=service.base+pk::DeployReadyCallerRva||!service.enabled.load(std::memory_order_acquire))return ready;
    // No policy/gather lock: a busy observer must never turn a deliberately
    // selected knife's automatic deployment attack back on.
    const auto guard=service.deployGuard.load(std::memory_order_acquire);if(!guard)return ready;
    unsigned branch=0;for(unsigned n=0;n<3;++n)if(guard->knifeFiring[n]==unsigned(self))branch=n+1;
    if(!branch)return ready;
    if(!RetainGuard(guard))return ready;
    ++service.deploy[branch-1];return false;
}
}
bool Install(std::span<const std::byte> bytes,const engine::PeImage& pe,std::uintptr_t base,const ReloadStateMemory& memory,bool motionEnabled)noexcept {
    if(service.installed||base>UINT32_MAX||!memory.read||!memory.type)return false;
    const auto mode=DiscoverWeaponMode(bytes,pe);const auto state=DiscoverReloadState(bytes,pe);const auto server=DiscoverReloadServer(bytes,pe);const auto flow=DiscoverReloadFlow(bytes,pe);
    if(!mode||!state||!server||!flow||!ValidateReloadFlowLive(memory,*flow,unsigned(base))||!pk::VerifyAttackCode(memory,unsigned(base),state->imageSize)||!pk::VerifyDrawReturnCode(memory,unsigned(base),state->imageSize,*mode)||
       !ValidateReloadStateLive(memory,*state,unsigned(base))||!ValidateReloadServerLive(memory,*server,unsigned(base)))return false;
    LARGE_INTEGER f{};if(!QueryPerformanceFrequency(&f)||f.QuadPart<=0||f.QuadPart>1000000000)return false;
    service.memory=memory;service.state=*state;service.server=*server;service.flow=*flow;service.base=unsigned(base);service.frequency=f.QuadPart;
    service.motionEnabled=motionEnabled;
    hooks={reinterpret_cast<void*>(base+pk::DrawReturnCode[0].rva),reinterpret_cast<void*>(base+pk::DrawReturnCode[2].rva),reinterpret_cast<void*>(base+0x2d7c80)};
    if(MH_CreateHook(hooks[0],Choose,reinterpret_cast<void**>(&originalChoose))!=MH_OK){hooks={};return false;}
    if(MH_CreateHook(hooks[1],DeployReady,reinterpret_cast<void**>(&originalReady))!=MH_OK){MH_RemoveHook(hooks[0]);hooks={};return false;}
    if(MH_CreateHook(hooks[2],AutomaticReturn,reinterpret_cast<void**>(&originalSetter))!=MH_OK){
        MH_RemoveHook(hooks[1]);MH_RemoveHook(hooks[0]);hooks={};return false;}
    if(!reloadFlowRuntime::RegisterOriginalFiringObserver({&service,Enter,Leave,AttackInput})){
        MH_RemoveHook(hooks[2]);MH_RemoveHook(hooks[1]);MH_RemoveHook(hooks[0]);hooks={};return false;}
    service.installed=true;return true;
}
void Start()noexcept{if(service.installed)service.enabled.store(true,std::memory_order_release);}
std::optional<View> Inspect(const ReloadStateOwner& current,unsigned knifeWeapon,std::uint64_t sequence,std::int64_t now)noexcept {
    std::unique_lock lock(service.gate,std::try_to_lock);if(!lock.owns_lock()||!service.enabled.load())return {};
    auto anchor=service.anchor;if(!anchor)anchor=pk::MakeAnchor(service.memory,service.state,service.server,service.base,current,knifeWeapon,sequence,now);
    if(!anchor||anchor->knifeWeapon!=knifeWeapon)return {};
    const auto snapshot=pk::Read(service.memory,service.state,service.server,service.base,*anchor,current,sequence,now);
    if(!snapshot||!Flags(*anchor))return {};
    return View{*anchor,*snapshot,service.owned,!service.bad.load(),!service.ownedObservationGap.load(),service.motionEnabled};
}
void AcknowledgeNoEffectRehide(const Bc2BodyHolster& body)noexcept {
    std::unique_lock lock(service.gate,std::try_to_lock);
    if(lock.owns_lock()&&!service.owned&&!service.outstanding&&service.returnReady&&
       body.LastTemporaryToolReturn()==service.bodyOperation&&!body.TemporaryToolActive()){
        service.returnReady=false;service.anchor.reset();service.stagedGuard.reset();
    }
}
void SourceAdvanced(const HandInteractionSample& source)noexcept {
    if(service.sourceIdentity.Advance(source))service.sourceEpoch.fetch_add(1,std::memory_order_acq_rel);
}
bool BeforePrepare(const KnifeSample& source,ChestKnifeCustody& policy)noexcept {
    // Gather is the sole caller. Revoke first-boundary input authority before
    // waiting for the native gate; accepted native work is drained separately.
    SourceAdvanced(source.source);
    std::unique_lock lock(service.gate,std::try_to_lock);if(!lock.owns_lock()||service.closing.load(std::memory_order_acquire))return false;
    if(service.outstanding){const auto result=service.outstanding->kind==KnifeCommandKind::Attack?
        service.attacks.ResolveBeforeSource(source.source.sequence,source.source.observedNs,source.source.nowNs):
        service.commands.ResolveBeforeSource(source.source.sequence,source.source.observedNs,source.source.nowNs);
        if(result){policy.Committed(*service.outstanding,*result);if(*result==KnifeCommit::RejectedNoEffect){service.returnReady=service.outstanding->kind==KnifeCommandKind::Draw;service.outstanding.reset();}}}
    service.source=source;return true;
}
bool Stage(const KnifeCommand& command,const KnifeSample& source,const HandInteraction& hands,ChestKnifeCustody& policy,
    const pk::Anchor& anchor,const ReloadStateOwner& current,std::uint64_t bodyOperation)noexcept {
    std::unique_lock lock(service.gate,std::try_to_lock);
    const auto reject=[&](){policy.Committed(command,KnifeCommit::RejectedNoEffect);return false;};
    if(!lock.owns_lock()||service.closing.load(std::memory_order_acquire)||!service.enabled.load()||service.bad||!bodyOperation||
       (command.kind==KnifeCommandKind::Attack&&!service.motionEnabled)||
       !policy.CanCommit(command,source,hands)||!SameCommandSource(command,source)||!SameCommandSource(command,service.source)||command.hand!=InteractionHand::Right)return reject();
    const bool drawing=command.kind==KnifeCommandKind::Draw;
    if((drawing&&(service.owned||current.weapon!=anchor.original.client.owner.weapon))||
       (!drawing&&(!service.owned||!service.anchor||!SameAnchor(anchor,*service.anchor)||bodyOperation!=service.bodyOperation||current.weapon!=anchor.knifeWeapon)))return reject();
    const auto now=Now();const auto native=pk::Read(service.memory,service.state,service.server,service.base,anchor,current,++service.observationSequence,now);
    if(!native||!Flags(anchor)||!pk::KnifeSentinel(*native)||!Ready(drawing?native->original:native->knife))return reject();
    for(auto selected:native->selected.current)if(selected!=(drawing?anchor.original.client.selectedSlot:anchor.knifeSlot))return reject();
    if(drawing){service.anchor=anchor;service.bodyOperation=bodyOperation;++service.nativeLifetime;
        service.ownedObservationGap=false;service.updates={};for(auto& n:service.deploy)n.store(0);service.states={};service.fireSeen=service.attackSeen=service.sentinelChanged=false;}
    else if(!Ammo(*native))return reject();
    try{service.stagedGuard=std::make_shared<const pk::Anchor>(anchor);}catch(...){return reject();}
    if(command.kind==KnifeCommandKind::Attack? !service.attacks.Stage(command,now):!service.commands.Stage(command,now))return reject();
    if(command.kind==KnifeCommandKind::Attack){service.lastAttackSequence=command.source.sequence;
        service.lastAttackObservedNs=command.source.observedNs;service.lastAttackSerial=command.serial;}
    service.source=source;service.stagedEpoch=service.sourceEpoch.load(std::memory_order_acquire);
    service.outstanding=command;service.returnReady=false;return true;
}
std::optional<KnifeReceipt> Observe(const ReloadStateOwner& current,const Bc2BodyHolster& body,
    const std::optional<BodyFreeRightEvidence>& empty,std::int64_t now)noexcept {
    std::unique_lock lock(service.gate,std::try_to_lock);
    if(!lock.owns_lock()||!service.enabled.load()||!service.anchor||!service.outstanding)return {};
    const auto command=*service.outstanding;const bool drawing=command.kind==KnifeCommandKind::Draw,attacking=command.kind==KnifeCommandKind::Attack;
    if(attacking?service.attacks.ConsumedMask()!=pk::KnifeAttackGate::InputMask:service.commands.Mask()!=3)return {};
    if(attacking)for(unsigned branch=1;branch<3;++branch)
        if(service.returnedCopySerial[branch].load(std::memory_order_acquire)!=command.serial)return {};
    const auto v=pk::Read(service.memory,service.state,service.server,service.base,*service.anchor,current,++service.observationSequence,now);
    if(!v){++service.gaps;return {};}
    if(!Ammo(*v)){service.bad=true;return {};}
    const auto wanted=(drawing||attacking)?service.anchor->knifeSlot:service.anchor->original.client.selectedSlot;
    for(auto selected:v->selected.current)if(selected!=wanted)return {};
    if(!Ready((drawing||attacking)?v->knife:v->original)||service.bad)return {};
    if(drawing){for(auto n:service.updates)if(!n)return {};}
    else if(!attacking){service.returnReady=true;
        if(body.LastTemporaryToolReturn()!=service.bodyOperation||!empty||
           !BodyFreeRightEvidenceCurrent(*empty,now)||empty->visibility.nativeOwner!=current)return {};}
    bool attackUnobserved=false;
    if(attacking){
        for(unsigned branch=1;branch<3;++branch)
            if(service.returnedCopySerial[branch].load(std::memory_order_acquire)!=command.serial)return {};
        attackUnobserved=service.attacks.AttackMask()!=pk::KnifeAttackGate::ObservationMask;
        if(!service.attacks.Complete(command.serial,true,true,attackUnobserved))return {};
        service.completedInputMask=service.attacks.ConsumedMask();service.completedObservationMask=service.attacks.AttackMask();
        if(attackUnobserved){++service.attackUnknown;service.ownedObservationGap=true;}
    }else if(!service.commands.Complete(command.serial))return {};
    KnifeReceipt receipt;receipt.kind=command.kind;receipt.serial=command.serial;receipt.actor=command.source.owner.actor;
    receipt.actorGeneration=command.source.owner.actorGeneration;receipt.space=command.source.owner.space;receipt.knife=command.knife;
    receipt.observedNs=receipt.nowNs=now;receipt.deadlineNs=now+100000000;receipt.coherent=true;
    receipt.originalAmmoUnchanged=receipt.knifeSentinelUnchanged=true;
    receipt.selectedKnife=drawing||attacking;receipt.drawDidNotAttack=drawing;receipt.holdGatesOwned=drawing||attacking;
    receipt.attackObserved=attacking&&!attackUnobserved;receipt.attackSettled=attacking;receipt.attackQuiescentOnly=attackUnobserved;
    if(attacking){if(!attackUnobserved)++service.attackReceipts;service.lastAttackReceiptNs=now;}
    receipt.originalSelectedHidden=!drawing&&!attacking;receipt.fireSuppressed=!drawing&&!attacking;
    service.outstanding.reset();if(!drawing&&!attacking){service.owned=false;service.returnReady=false;service.anchor.reset();service.deployGuard.store({},std::memory_order_release);}return receipt;
}
std::optional<std::uint64_t> ReturnNeedsRehide()noexcept {
    std::unique_lock lock(service.gate,std::try_to_lock);return lock.owns_lock()&&service.returnReady?std::optional(service.bodyOperation):std::nullopt;
}
void ObserveLifetime(const ReloadStateOwner& current)noexcept {
    if(!current.player||!current.soldier||!current.weak||!current.actorGeneration)return;
    std::unique_lock lock(service.gate,std::try_to_lock);if(!lock.owns_lock()||!service.anchor||SameActor(current,service.anchor->original.client.owner))return;
    service.sourceEpoch.fetch_add(1);service.commands.LifetimeEnded();service.attacks.LifetimeEnded();service.outstanding.reset();service.anchor.reset();
    service.owned=service.returnReady=false;service.bad.store(false);service.deployGuard.store({},std::memory_order_release);++service.nativeLifetime;
}
void RequestSessionClose()noexcept {
    bool expected=false;
    if(service.closing.compare_exchange_strong(expected,true,std::memory_order_acq_rel)){
        service.closeRequestedNs.store(Now(),std::memory_order_release);
        service.sourceEpoch.fetch_add(1,std::memory_order_acq_rel);
    }
}
SessionCloseStatus PollSessionClose()noexcept {
    if(!service.installed)return SessionCloseStatus::Complete;
    if(!service.closing.load(std::memory_order_acquire))return SessionCloseStatus::Unresolved;
    std::unique_lock lock(service.gate,std::try_to_lock);if(!lock.owns_lock())return SessionCloseStatus::Pending;
    const auto now=Now();
    if(service.close.Phase()==pk::KnifeClosePhase::Unrequested){
        // Do not race an already-entered chooser/firing callback. No new action
        // may pass closing, but an earlier native call must actually return.
        if(service.active.load(std::memory_order_acquire))return SessionCloseStatus::Pending;
        const bool pending=service.owned?(bool(service.outstanding)||bool(service.commands.Pending())||bool(service.attacks.Pending())):
            ((service.commands.Pending()&&service.commands.Accepted())||(service.attacks.Pending()&&service.attacks.ConsumedMask()!=0));
        service.close.Begin(service.closeRequestedNs.load(std::memory_order_acquire),service.owned,pending||(service.owned&&service.bad.load()),now);
        if(!service.owned&&service.close.Phase()==pk::KnifeClosePhase::Complete){
            service.commands.LifetimeEnded();service.attacks.LifetimeEnded();service.outstanding.reset();
            service.anchor.reset();service.stagedGuard.reset();service.returnReady=false;
        }
    }
    service.close.CheckDeadline(now);
    if(service.close.Phase()==pk::KnifeClosePhase::Unresolved)return SessionCloseStatus::Unresolved;
    if(service.close.Phase()==pk::KnifeClosePhase::Complete)return SessionCloseStatus::Complete;
    if(service.bad){service.close.Reject();return SessionCloseStatus::Unresolved;}
    unsigned readReason=CloseOkay;const auto snapshot=CloseSnapshot(readReason);
    if(!snapshot){++service.closeReadGaps;RecordClose(0,2,readReason);return SessionCloseStatus::Pending;}
    const auto& anchor=*service.anchor;
    if(service.close.Phase()==pk::KnifeClosePhase::WaitingForProof){
        bool selectedKnife=true;for(auto slot:snapshot->selected.current)selectedKnife&=slot==anchor.knifeSlot;
        const bool previousOriginal=snapshot->selected.previous[0]==anchor.original.client.selectedSlot&&
            snapshot->selected.previous[1]==anchor.original.client.selectedSlot;
        const unsigned reason=!selectedKnife?CloseWrongSelected:!previousOriginal?CloseWrongPrevious:
            !CloseIdle(*snapshot)?CloseNotIdle:CloseOkay;
        const bool admitted=service.close.Admit(Now(),reason==CloseOkay);
        RecordClose(2,2,reason?reason:admitted?CloseOkay:CloseBoundaryRejected,&*snapshot);
    }else{
        bool original=true;for(auto slot:snapshot->selected.current)original&=slot==anchor.original.client.selectedSlot;
        const bool idle=CloseIdle(*snapshot,true),drained=!service.active.load(std::memory_order_acquire);
        const bool completed=service.close.Complete(Now(),original&&idle,drained);
        RecordClose(3,2,!original?CloseNotOriginal:!idle?CloseNotIdle:!drained?CloseNotDrained:
            completed?CloseOkay:CloseBoundaryRejected,&*snapshot);
        if(completed){
            // Desktop handback is NOT a paired-hide / empty-hand receipt.
            service.owned=service.returnReady=false;service.anchor.reset();service.stagedGuard.reset();
            service.deployGuard.store({},std::memory_order_release);
        }
    }
    service.close.CheckDeadline(Now());
    return service.close.Phase()==pk::KnifeClosePhase::Complete?SessionCloseStatus::Complete:
        service.close.Phase()==pk::KnifeClosePhase::Unresolved?SessionCloseStatus::Unresolved:SessionCloseStatus::Pending;
}
bool Stop()noexcept {
    {std::unique_lock lock(service.gate,std::try_to_lock);
        if(!lock.owns_lock()||service.owned||(service.commands.Pending()&&service.commands.Accepted()))return false;
        // Unconsumed stages can no longer reach a native chooser after stop.
        service.sourceEpoch.fetch_add(1,std::memory_order_acq_rel);service.commands.LifetimeEnded();service.attacks.LifetimeEnded();service.outstanding.reset();
        service.enabled.store(false,std::memory_order_release);
    }
    bool okay=true;
    for(auto hook:hooks)if(hook){const auto status=MH_DisableHook(hook);okay&=status==MH_OK||status==MH_ERROR_DISABLED;}
    const auto until=GetTickCount64()+2000;while(service.active.load(std::memory_order_acquire)&&GetTickCount64()<until)Sleep(1);
    service.drained=okay&&!service.active.load()&&reloadFlowRuntime::RemoveOriginalFiringObserver(&service);return service.drained;
}
void Report(std::ostream& out){std::unique_lock lock(service.gate,std::try_to_lock);
    if(!lock.owns_lock()){out<<"{\"available\":false}";return;}
    out<<"{\"private_motion_attack\":"<<service.motionEnabled<<",\"installed\":"<<service.installed<<",\"drained\":"<<service.drained
       <<",\"session_close_requested\":"<<service.closing.load()<<",\"session_close_phase\":"<<unsigned(service.close.Phase())
       <<",\"session_close_mask\":"<<service.close.Mask()<<",\"session_close_started_ns\":"<<service.close.StartedNs()
       <<",\"session_close_deadline_ns\":"<<service.close.DeadlineNs()<<",\"session_close_completed_ns\":"<<service.close.CompletedNs()
       <<",\"desktop_handback\":"<<service.close.DesktopHandback()<<",\"session_close_read_gaps\":"<<service.closeReadGaps
       <<",\"native_owned\":"<<service.owned<<",\"bad\":"<<service.bad.load()<<",\"chooser_overrides\":"<<service.chooserOverrides
       <<",\"observation_gaps\":"<<service.gaps.load()<<",\"deployment_unknowns\":"<<service.deployMisses.load()
       <<",\"foreign_guard_retirements\":"<<service.foreignGuards.load()<<",\"automatic_returns_suppressed\":"<<service.returnSuppressed.load()
       <<",\"attack_context_copies\":"<<service.attackCopies<<",\"attack_receipts\":"<<service.attackReceipts
       <<",\"attack_outcome_unknown\":"<<service.attackUnknown
       <<",\"attack_consumed_mask\":"<<service.attacks.ConsumedMask()<<",\"attack_observed_mask\":"<<service.attacks.AttackMask()
       <<",\"attack_required_input_mask\":"<<pk::KnifeAttackGate::InputMask<<",\"attack_required_observation_mask\":"<<pk::KnifeAttackGate::ObservationMask
       <<",\"attack_completed_input_mask\":"<<service.completedInputMask<<",\"attack_completed_observation_mask\":"<<service.completedObservationMask
       <<",\"last_attack_input_sequence\":"<<service.lastAttackSequence<<",\"last_attack_input_observed_ns\":"<<service.lastAttackObservedNs
       <<",\"last_attack_serial\":"<<service.lastAttackSerial<<",\"last_attack_receipt_ns\":"<<service.lastAttackReceiptNs
       <<",\"owned_observation_complete\":"<<!service.ownedObservationGap.load()
       <<",\"fire_seen\":"<<service.fireSeen<<",\"attack_seen\":"<<service.attackSeen<<",\"sentinel_changed\":"<<service.sentinelChanged
       <<",\"body_operation\":"<<service.bodyOperation<<",\"update_counts\":["<<service.updates[0]<<','<<service.updates[1]<<','<<service.updates[2]
       <<"],\"deploy_suppressed\":["<<service.deploy[0].load()<<','<<service.deploy[1].load()<<','<<service.deploy[2].load()<<"]"
       <<",\"input_context_writes\":0,\"copy_receipts\":[";
    for(unsigned n=0;n<3;++n){if(n)out<<',';
        out<<"{\"branch\":"<<n<<",\"serial\":"<<service.copySerial[n].load()<<",\"input\":"<<service.copyInput[n].load()
           <<",\"observed_ns\":"<<service.copyObservedNs[n].load()<<",\"context\":"<<service.copyContext[n].load()<<",\"caller\":"<<service.copyCaller[n].load()
           <<",\"returned_serial\":"<<service.returnedCopySerial[n].load()<<",\"returned_ns\":"<<service.copyReturnedNs[n].load()
           <<",\"original_read_known\":"<<service.copyPostReadKnown[n].load()<<",\"original_hash_match\":"<<service.copyOriginalUnchanged[n].load()<<",\"before_fnv64\":"<<service.copyHashBefore[n].load()<<",\"after_fnv64\":"<<service.copyHashAfter[n].load()<<'}';}
    out<<"]";
    const auto closeProof=[&](const Service::CloseProof& r){
        out<<"{\"snapshot\":"<<r.snapshot<<",\"stage\":"<<r.stage<<",\"branch\":"<<r.branch
           <<",\"reason\":"<<r.reason<<",\"phase\":"<<r.phase<<",\"mask\":"<<r.mask
           <<",\"observed_ns\":"<<r.observedNs<<",\"lifetime\":"<<r.lifetime
           <<",\"actor\":"<<r.actor<<",\"weak\":"<<r.weak<<",\"original\":"<<r.original<<",\"knife\":"<<r.knife;
        const auto values=[&](const char* key,const auto& a){out<<",\""<<key<<"\":[";
            for(unsigned n=0;n<a.size();++n){if(n)out<<',';out<<a[n];}out<<']';};
        values("selected",r.selected);values("previous",r.previous);values("knife_state",r.knifeState);values("knife_next",r.knifeNext);
        values("original_state",r.originalState);values("original_next",r.originalNext);
        values("knife_loaded",r.knifeLoaded);values("knife_reserve",r.knifeReserve);
        values("original_loaded",r.originalLoaded);values("original_reserve",r.originalReserve);out<<'}';
    };
    out<<",\"session_close_proof\":{\"capacity\":"<<service.closeProofs.size()<<",\"dropped\":"<<service.closeProofDropped
       <<",\"stage_names\":[\"snapshot\",\"chooser\",\"initial_admission\",\"completion\"]"
       <<",\"reason_names\":[\"okay\",\"ownership\",\"selection\",\"read\",\"flags\",\"ammo\",\"clock\",\"not_idle\",\"wrong_selected\",\"wrong_previous\",\"boundary_rejected\",\"not_original\",\"not_drained\"],\"rows\":[";
    for(unsigned n=0;n<service.closeProofCount;++n){if(n)out<<',';closeProof(service.closeProofs[n]);}
    out<<"],\"last\":";closeProof(service.closeLast);out<<",\"first_rejected\":[";
    for(unsigned n=0;n<service.closeFirstRejected.size();++n){if(n)out<<',';if(service.closeRejectedRecorded[n])closeProof(service.closeFirstRejected[n]);else out<<"null";}
    out<<"],\"chooser_offers\":[";
    for(unsigned n=0;n<service.closeOffered.size();++n){if(n)out<<',';if(service.closeOfferRecorded[n])closeProof(service.closeOffered[n]);else out<<"null";}
    out<<"]}";
    const auto witness=[&](const char* name,const Service::CallbackWitness& r){
        out<<",\""<<name<<"\":{\"recorded\":"<<r.recorded<<",\"observed_ns\":"<<r.observedNs<<",\"reason\":"<<r.reason
           <<",\"branch\":"<<r.branch<<",\"firing\":"<<r.firing<<",\"context\":"<<r.context<<",\"caller\":"<<r.caller<<",\"argument\":"<<r.argument
           <<",\"before\":"<<r.before<<",\"current\":"<<r.current<<",\"next\":"<<r.next
           <<",\"token_lifetime\":"<<r.tokenLifetime<<",\"current_lifetime\":"<<r.currentLifetime
           <<",\"token_serial\":"<<r.tokenSerial<<",\"pending_serial\":"<<r.pendingSerial
           <<",\"consumed_mask\":"<<r.consumed<<",\"observed_mask\":"<<r.observed
           <<",\"loaded_before\":"<<r.loadedBefore<<",\"reserve_before\":"<<r.reserveBefore
           <<",\"loaded_after\":"<<r.loadedAfter<<",\"reserve_after\":"<<r.reserveAfter
           <<",\"selected\":["<<r.selected[0]<<','<<r.selected[1]<<','<<r.selected[2]<<"]}";
    };
    witness("first_rejected_callback",service.firstRejectedCallback);witness("first_prediction_activity",service.firstPredictionActivity);out<<'}';
}
}
#else
namespace fvr::bc2::chestKnifeNative {
bool Install(std::span<const std::byte>,const engine::PeImage&,std::uintptr_t,const ReloadStateMemory&,bool)noexcept{return false;}
void Start()noexcept{}
std::optional<View> Inspect(const ReloadStateOwner&,unsigned,std::uint64_t,std::int64_t)noexcept{return {};}
void AcknowledgeNoEffectRehide(const Bc2BodyHolster&)noexcept{}

void SourceAdvanced(const interaction::HandInteractionSample&)noexcept{}
bool BeforePrepare(const interaction::KnifeSample&,interaction::ChestKnifeCustody&)noexcept{return false;}
bool Stage(const interaction::KnifeCommand&,const interaction::KnifeSample&,const interaction::HandInteraction&,interaction::ChestKnifeCustody&,
 const private_knife::Anchor&,const ReloadStateOwner&,std::uint64_t)noexcept{return false;}
std::optional<interaction::KnifeReceipt> Observe(const ReloadStateOwner&,const Bc2BodyHolster&,const std::optional<BodyFreeRightEvidence>&,std::int64_t)noexcept{return {};}
std::optional<std::uint64_t> ReturnNeedsRehide()noexcept{return {};}
void ObserveLifetime(const ReloadStateOwner&)noexcept{}
void RequestSessionClose()noexcept{}
SessionCloseStatus PollSessionClose()noexcept{return SessionCloseStatus::Complete;}
bool Stop()noexcept{return true;}
void Report(std::ostream& out){out<<"{\"enabled\":false}";}
}
#endif
