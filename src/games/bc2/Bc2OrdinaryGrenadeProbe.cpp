#include "Bc2OrdinaryGrenadeProbe.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace fvr::bc2::private_grenade { namespace {
static_assert(sizeof(Probe)<=32768); // Heap-owned in Gameplay; never an x86 stack journal.
struct Reader {
    const ReloadStateMemory& m;unsigned base,size;
    bool Read(std::uint64_t at,void* p,std::size_t n)const noexcept {
        return at>=0x10000&&n&&n<=4096&&at+n<=UINT32_MAX&&m.read&&m.read(m.context,unsigned(at),p,n);
    }
    template<class T>bool Get(std::uint64_t at,T& out)const noexcept{return Read(at,&out,sizeof(out));}
    bool Image(unsigned at,std::size_t n)const noexcept{return at>=base&&std::uint64_t(at)+n<=std::uint64_t(base)+size;}
    bool Name(unsigned at,const char* text)const noexcept {
        std::array<char,96> value{};const auto n=std::strlen(text)+1;
        return n<=value.size()&&Image(at,n)&&Read(at,value.data(),n)&&!std::memcmp(value.data(),text,n);
    }
    bool Field(unsigned object,const char* type,unsigned sizeWanted,const char* name,unsigned offset,bool grenadeEnum=false)const noexcept {
        unsigned table=0,getter=0,info=0,meta=0,fields=0;std::array<unsigned char,6> code{};std::array<unsigned,7> words{};
        if(!m.type||!m.type(m.context,object,type)||!Get(object,table)||!Image(table,12)||!Get(std::uint64_t(table)+8,getter)||
           !Image(getter,6)||!Read(getter,code.data(),6)||code[0]!=0xb8||code[5]!=0xc3)return false;
        std::memcpy(&info,code.data()+1,4);
        if(!Image(info,40)||!Get(std::uint64_t(info)+4,meta)||!Image(meta,28)||!Read(meta,words.data(),28)||
           !Name(words[0],type)||(words[1]&0xffff)!=0x35||(sizeWanted&&(words[1]>>16)!=sizeWanted)||
           (words[1]>>16)<=offset||(words[1]>>16)>4096||!Get(std::uint64_t(info)+36,fields))return false;
        const unsigned count=(words[3]>>8)&255;if(!count||count>64||!Image(fields,count*24))return false;
        unsigned matches=0;
        for(unsigned n=0;n<count;++n){std::array<unsigned,6> f{};if(!Read(std::uint64_t(fields)+n*24,f.data(),24))return false;
            if(!Name(f[0],name))continue;
            unsigned booleanMeta=0;std::array<unsigned,7> b{};
            if(++matches!=1||f[4]!=offset||!Image(f[2],8)||!Get(std::uint64_t(f[2])+4,booleanMeta)||
               !Image(booleanMeta,28)||!Read(booleanMeta,b.data(),28))return false;
            if(grenadeEnum){
                const unsigned enums=(b[3]>>8)&255;
                if(!Name(b[0],"WeaponClassEnum")||b[1]!=(4u<<16|0x179)||!enums||enums>64||!Image(b[6],enums*24))return false;
                unsigned found=0;for(unsigned k=0;k<enums;++k){std::array<unsigned,6> entry{};
                    if(!Read(std::uint64_t(b[6])+k*24,entry.data(),24))return false;
                    if(Name(entry[0],"wcHgr")){if(++found!=1||entry[4]!=6)return false;}}
                if(found!=1)return false;
            }else if(!Name(b[0],"Boolean")||(b[1]>>16)!=1)return false;
        }return matches==1;
    }
    bool SelectorAt(unsigned at,Selector& s)const noexcept {
        s.inventory=at;return Get(std::uint64_t(at)+4,s.switching)&&Get(std::uint64_t(at)+0x148,s.callback)&&
            Get(std::uint64_t(at)+0x14c,s.current)&&Get(std::uint64_t(at)+0x150,s.previous)&&Get(std::uint64_t(at)+0x154,s.primary)&&
            s.current<32&&s.previous<32&&s.primary<32;
    }
};
bool StockReturnFirearmClass(unsigned category)noexcept {
    // ReadBodyInventory verifies these exact WeaponClassEnum names/values.
    // Native return0x8827a0 uses the previous slot, not the original class.
    // Keep UGL/grenade/tools out of this personal-firearm return contract;
    // configuration, selected-owner, route and all three firing copies below
    // still have to agree before this read grants any action eligibility.
    switch(category){
    case 0: // wcAssault
    case 1: // wcShotgun
    case 2: // wcSmg
    case 3: // wcLmg
    case 4: // wcSniper
        return true;
    default:return false;
    }
}
bool AmmoEqual(const ReloadServerInventoryItem& s)noexcept {
    const auto& a=s.client.branches[0];const auto& b=s.client.branches[1];const auto& c=s.server;
    return a.loaded>=0&&a.reserve>=0&&!(a.flagsA8&8)&&!(b.flagsA8&8)&&!(c.flagsA8&8)&&
        a.loaded==b.loaded&&a.loaded==c.loaded&&a.reserve==b.reserve&&a.reserve==c.reserve;
}
bool StockActionReady(const ReloadServerInventoryItem& s)noexcept {
    // Native step0x6e62d0 copies remaining update time to +0x50 BEFORE
    // dispatching ready state2. The neutral ready path leaves that time for
    // timed-step0x6e69e0 to consume. Zero is an internal update boundary, not
    // stock action eligibility or proof that a returned rifle stays ready.
    for(const auto* state:{&s.client.branches[0],&s.client.branches[1],&s.server})
        if(state->currentState!=2||state->nextState!=2||!std::isfinite(state->phaseTimer)||
           state->phaseTimer<0||state->phaseTimer>1000000)return false; // Existing reader bound.
    return true;
}
bool SameItem(const ReloadServerInventoryItem& a,const ReloadServerInventoryItem& b)noexcept {
    return a.client.target==b.client.target&&a.client.slot==b.client.slot&&a.client.inventory==b.client.inventory&&
        a.client.config==b.client.config&&a.client.branches[0].address==b.client.branches[0].address&&
        a.client.branches[1].address==b.client.branches[1].address&&a.server.address==b.server.address&&
        a.serverItem==b.serverItem&&a.effects==b.effects&&a.callback==b.callback;
}
}
bool StockReady(const ReloadServerInventoryItem& s)noexcept{return StockActionReady(s);}
bool StockAmmoEqual(const ReloadServerInventoryItem& s)noexcept{return AmmoEqual(s);}
bool StockItemSame(const ReloadServerInventoryItem& a,const ReloadServerInventoryItem& b)noexcept{return SameItem(a,b);}
bool VerifyStockReturn(const ReloadStateMemory& m,unsigned base)noexcept {
    if(base!=0x400000)return false;
    // Complete exact body from stock-return-audit.json, not callable addresses.
    constexpr std::array<ReloadCodeProof,4> proof{{{0x4827a0,214,0x74cd67e736d5721aull},
        {0x2ce960,7,0xc605056b535108f3ull},{0x2ce970,7,0x62e42cc59ea1661full},{0x2ce980,7,0x959f396f93de362bull}}};
    std::array<std::byte,214> bytes{};
    for(const auto& p:proof){if(!m.read||!m.read(m.context,base+p.rva,bytes.data(),p.size))return false;
        std::uint64_t h=14695981039346656037ull;for(unsigned i=0;i<p.size;++i){h^=std::to_integer<unsigned char>(bytes[i]);h*=1099511628211ull;}
        if(h!=p.fingerprint)return false;}return true;
}
std::optional<Native> ReadNative(const ReloadStateMemory& m,const ReloadStateBinding& state,
    const ReloadServerBinding& server,unsigned base,const ReloadStateOwner& owner,std::uint64_t sequence,
    std::int64_t now,unsigned original,unsigned target)noexcept {
    Reader r{m,base,state.imageSize};unsigned attached=0;
    if(!r.Get(std::uint64_t(owner.player)+0xc60,attached)||attached)return {};
    const WeaponModeMemory mode{m.context,m.read,m.type};const auto body=ReadBodyInventory(mode,owner,true);
    const auto current=ReadReloadState(m,state,base,owner,sequence,now);
    if(!body.snapshot||!current.snapshot)return {};
    const auto selected=ReadReloadServerState(m,server,state,base,*current.snapshot);if(!selected)return {};
    const auto& inv=*body.snapshot;Native out;out.owner=owner;out.carried={inv.inventory,inv.switching,inv.count,inv.items};
    if(!original)original=owner.weapon;
    const auto orig=std::find_if(inv.items.begin(),inv.items.begin()+inv.count,[&](const auto& i){return i.weapon==original;});
    if(orig==inv.items.begin()+inv.count||!StockReturnFirearmClass(orig->category))return {};out.originalSlot=orig->slot;
    const BodyNativeRoute* route=nullptr;
    for(unsigned n=0;n<inv.routeCount;++n)if(inv.routes[n].from==orig->slot&&inv.routes[n].action==38){if(route)return {};route=&inv.routes[n];}
    if(!route||route->count!=1||route->targets[0]>=inv.count)return {};out.targetSlot=route->targets[0];
    const auto& item=inv.items[out.targetSlot];if(!item.weapon||item.category!=6||(target&&item.weapon!=target))return {};
    const auto a=ReadReloadServerInventoryItem(m,server,state,base,*selected,original);
    const auto b=ReadReloadServerInventoryItem(m,server,state,base,*selected,item.weapon);
    if(!a||!b||a->client.config.fireLogicType!=2||a->client.config.fireInputAction!=8||
       b->client.config.fireLogicType!=3||b->client.config.fireInputAction!=8||b->client.config.weaponData!=item.data)return {};
    out.original=*a;out.target=*b;
    if(!r.Field(item.data,"SoldierWeaponData",0x130,"WeaponClass",0x84,true))return {};
    // The returned firearm itself must not fire on deployment. Trigger-neutral
    // input alone cannot establish this property of the native stock return.
    std::uint8_t originalDeploy=1,originalAgain=1;
    if(!r.Field(a->client.config.firingData,"WeaponFiringData",0,"FireOnDeploy",0x48)||
       !r.Get(std::uint64_t(a->client.config.firingData)+0x48,originalDeploy)||originalDeploy)return {};
    // Reflection verifies the exact Boolean names/types and offsets before use.
    if(!r.Field(b->client.config.firingData,"WeaponFiringData",0,"FireOnDeploy",0x48))return {};
    constexpr const char* flags[]{"HideWhenOutOfAmmo","SwitchToPrimaryWhenOutOfAmmo","SwitchToPreviousWeaponAfterFire","AllowSwitchingToWeaponOutOfAmmo"};
    for(unsigned n=0;n<4;++n)if(!r.Field(item.data,"SoldierWeaponData",0x130,flags[n],0x121+n))return {};
    if(!r.Get(std::uint64_t(b->client.config.firingData)+0x48,out.targetFlags[0])||
       !r.Read(std::uint64_t(item.data)+0x121,out.targetFlags.data()+1,4))return {};
    if(out.targetFlags!=std::array<std::uint8_t,5>{1,1,1,1,0})return {};
    unsigned independent=0;if(!r.Get(std::uint64_t(owner.soldier)+0x248,independent)||
        !r.SelectorAt(inv.inventory,out.selectors[0])||!r.SelectorAt(selected->links.inventory,out.selectors[1])||
        !r.SelectorAt(independent,out.selectors[2]))return {};
    std::array<Selector,3> again{};std::array<std::uint8_t,5> againFlags{};
    if(out.selectors[0].current!=inv.selected||out.selectors[1].current!=inv.selected||
       out.selectors[0].switching!=inv.switching||out.selectors[1].switching!=inv.switching||
       !r.SelectorAt(inv.inventory,again[0])||!r.SelectorAt(selected->links.inventory,again[1])||
       !r.SelectorAt(independent,again[2])||again!=out.selectors||
       !r.Get(std::uint64_t(b->client.config.firingData)+0x48,againFlags[0])||
       !r.Read(std::uint64_t(item.data)+0x121,againFlags.data()+1,4)||againFlags!=out.targetFlags||
       !r.Get(std::uint64_t(a->client.config.firingData)+0x48,originalAgain)||originalAgain!=originalDeploy)return {};
    const auto bodyAgain=ReadBodyInventory(mode,owner,true);
    const auto serverAgain=ReadReloadServerState(m,server,state,base,*current.snapshot);
    if(!bodyAgain.snapshot||*bodyAgain.snapshot!=inv||!serverAgain||serverAgain->links!=selected->links||
       !r.Get(std::uint64_t(owner.player)+0xc60,attached)||attached)return {};
    return out;
}
Sample StockSample(const Native& n,const Facts& f)noexcept {
    auto s=f.source;auto& a=s.admission;
    const auto& item=n.original.client;const BodyInventoryDisplaySlot* slot=nullptr;
    if(f.display&&f.display->count<=f.display->slots.size())for(unsigned i=0;i<f.display->count;++i)
        if(f.display->slots[i].native.weapon==item.target){slot=&f.display->slots[i];break;}
    const bool assigned=slot!=nullptr;
    s.identity={n.owner.soldier,n.owner.weak,n.owner.actorGeneration,n.owner.equipGeneration,n.owner.space,
        item.target,assigned?slot->assignment.item.generation:0,f.display?f.display->revision:0,
        n.carried.inventory,n.carried.switching,n.target.client.target,n.target.client.config.weaponData,
        n.target.client.config.firingData,f.right?f.right->token.id:0};
    a.routeCurrent=n.owner.weapon==item.target&&n.selectors[0].current==n.originalSlot&&n.selectors[1].current==n.originalSlot;
    a.configCurrent=true;a.ammoAvailable=AmmoEqual(n.original)&&StockActionReady(n.original)&&AmmoEqual(n.target)&&
        n.target.client.branches[0].loaded+n.target.client.branches[0].reserve>0;
    a.visibleHeld=f.visible&&f.holsterPhase==1&&f.display&&f.display->selectedOwner==n.owner&&f.display->carried==n.carried;
    a.currentGunHold=f.right&&f.right->token.kind==interaction::HandClaimKind::GunHold&&f.right->token.owner==f.hand.owner&&
        f.right->token.item==f.handItem&&f.right->inputSequence==s.input.generation&&f.right->deadlineNs>s.nowNs;
    a.leftFree=!f.left;return s;
}
Sample Probe::Fill(const Native& n,const Facts& f)const noexcept {return StockSample(n,f);}
bool Probe::SamePlan(const Native& n)const noexcept {
    if(!baseline_)return false;const auto& b=*baseline_;
    return b.owner==n.owner&&b.carried==n.carried&&SameItem(b.original,n.original)&&SameItem(b.target,n.target)&&b.targetFlags==n.targetFlags;
}
std::optional<Request> Probe::Prepare(const std::optional<Native>& n,Facts f)noexcept {
    if(complete_)return {};
    if(!n){Missing(f.source.nowNs);return {};}
    f.source=Fill(*n,f);const auto before=semantic_.Current();const auto request=semantic_.Observe(f.source);
    if(before!=semantic_.Current())Add(Gesture,f,&*n,&f.source);
    if(semantic_.Current()==Phase::Pressed&&!baseline_){baseline_=*n;body_=f.display;originalGun_=f.right->token;Add(Baseline,f,&*n,&f.source);}
    if(request){if(!SamePlan(*n)){semantic_.Reject(Reason::IdentityChanged);Add(Rejected,f,&*n,&f.source);return {};}
        Add(RequestIssued,f,&*n,&f.source);}return request;
}
bool Probe::CanStage(const Request& request,const std::optional<Native>& n,Facts f)noexcept {
    const bool okay=n&&SamePlan(*n)&&semantic_.CanStage(request,Fill(*n,f));
    if(!okay){semantic_.Reject(Reason::FinalValidation);const auto evaluated=n?std::optional<Sample>{Fill(*n,f)}:std::nullopt;
        Add(Rejected,f,n?&*n:nullptr,evaluated?&*evaluated:nullptr);return false;}
    staged_=Fill(*n,f);Add(Staged,f,&*n,&staged_);return true;
}
void Probe::Commit(const Request& request,Facts f,bool bit38)noexcept {
    // Preserve the exactly staged immutable identity/admission; only refresh
    // final processing time and immediate owner/cache checks in Gameplay.
    const auto now=f.source.nowNs;const bool ownerCurrent=f.source.admission.ownerCurrent;
    f.source=staged_;f.source.nowNs=now;f.source.admission.ownerCurrent=ownerCurrent;
    const bool accepted=semantic_.Committed(request,f.source,bit38);
    if(bit38)commitNs_=f.source.nowNs; // Observe an actual edge even after failed final acceptance.
    if(!accepted)failed_=true;Add(Committed,f,baseline_?&*baseline_:nullptr,&f.source);
}
void Probe::Missing(std::int64_t now)noexcept {
    Sample missing;missing.nowNs=now;semantic_.Observe(missing);
    if(commitNs_&&!complete_){Facts f;f.source.nowNs=now;if(!gap_)Add(ReadGap,f);gap_=true;failed_=true;
        if(now>=commitNs_+8000000000ll){complete_=true;Add(Finished,f);}}
}
void Probe::Observe(const std::optional<Native>& n,const Facts& f)noexcept {
    if(!commitNs_||complete_)return;
    if(!n){Missing(f.source.nowNs);return;}
    const auto& b=*baseline_;const auto& v=*n;
    if((lastGoodNs_&&f.source.nowNs-lastGoodNs_>150000000)||f.source.nowNs<commitNs_)failed_=true;
    lastGoodNs_=f.source.nowNs;gap_=false;
    const bool stable=v.owner.player==b.owner.player&&v.owner.soldier==b.owner.soldier&&v.owner.weak==b.owner.weak&&
        v.owner.actorGeneration==b.owner.actorGeneration&&v.owner.space==b.owner.space&&v.carried==b.carried&&
        SameItem(v.original,b.original)&&SameItem(v.target,b.target)&&v.targetFlags==b.targetFlags;
    if(!stable||!f.cacheQuiet)failed_=true;
    if(v.owner.weapon==b.target.client.target&&v.selectors[0].previous==b.originalSlot&&v.selectors[1].previous==b.originalSlot)selectedGrenade_=true;
    const bool currentOriginal=v.owner.weapon==b.original.client.target&&v.selectors[0].current==b.originalSlot&&v.selectors[1].current==b.originalSlot&&
        v.selectors[0].previous==b.targetSlot&&v.selectors[1].previous==b.targetSlot;
    if(selectedGrenade_&&currentOriginal)selectedOriginal_=true;
    for(unsigned k=0;k<3;++k){const auto& old=k<2?b.original.client.branches[k]:b.original.server;
        const auto& cur=k<2?v.original.client.branches[k]:v.original.server;if(cur.loaded!=old.loaded||cur.reserve!=old.reserve)failed_=true;}
    const auto total=std::int64_t(v.target.client.branches[0].loaded)+v.target.client.branches[0].reserve;
    const auto previous=std::int64_t(b.target.client.branches[0].loaded)+b.target.client.branches[0].reserve;
    ammoConsumed_=AmmoEqual(v.target)&&total==previous-1;
    // Allow initial asynchronous baseline/-1 convergence, but never erase an
    // observed extra loss or gain in any replica when the replicas later agree.
    for(unsigned k=0;k<3;++k){const auto& old=k<2?b.target.client.branches[k]:b.target.server;
        const auto& cur=k<2?v.target.client.branches[k]:v.target.server;
        const auto before=std::int64_t(old.loaded)+old.reserve,after=std::int64_t(cur.loaded)+cur.reserve;
        if(cur.loaded<0||cur.reserve<0||after<before-1||after>before)failed_=true;}
    if(returnSettled_&&(v.owner!=returnedOwner_||v.selectors!=returnedSelectors_||!ammoConsumed_||!StockActionReady(v.original)))failed_=true;
    bool bodySame=f.display&&body_&&f.display->revision==body_->revision&&f.display->carried==body_->carried&&
        f.display->count==body_->count&&f.display->slots==body_->slots;
    if(f.custodyObserved) returnedHeld_=selectedOriginal_&&currentOriginal&&bodySame&&f.display->selectedOwner==v.owner&&
        f.display->physicalSelected==body_->physicalSelected&&f.visible&&f.holsterPhase==1&&f.right&&!f.left&&f.hand.focused&&f.hand.tracked[1]&&
        f.right->token.kind==interaction::HandClaimKind::GunHold&&f.right->token.item.id==originalGun_.item.id&&
        f.right->token.item==f.handItem&&f.right->token.owner.actor==originalGun_.owner.actor&&
        f.right->token.owner.actorGeneration==originalGun_.owner.actorGeneration&&f.right->token.owner.space==originalGun_.owner.space&&
        f.right->token.id!=originalGun_.id&&f.right->token.owner.equipGeneration!=originalGun_.owner.equipGeneration&&
        f.right->token.owner==f.hand.owner&&f.right->inputSequence==f.source.input.generation&&f.right->deadlineNs>f.source.nowNs;
    if(f.custodyObserved){
        // Pin the first proven settled return. A later body/custody break or
        // replacement token remains failed even after a fresh stable interval.
        if(returnSettled_&&(!returnedHeld_||f.right->token!=returnedGun_))failed_=true;
        if(!returnSettled_&&returnedHeld_&&ammoConsumed_&&StockActionReady(v.original)){
            returnSettled_=true;returnNs_=f.source.nowNs;returnedGun_=f.right->token;
            returnedOwner_=v.owner;returnedSelectors_=v.selectors;}}
    Add(NativeChange,f,&v);
    if(f.custodyObserved&&f.source.nowNs>=commitNs_+8000000000ll){complete_=true;pass_=!failed_&&!dropped_&&semantic_.ObservedNativeEdges()==1&&
        semantic_.Commits()==1&&selectedGrenade_&&selectedOriginal_&&returnedHeld_&&ammoConsumed_&&returnNs_&&f.source.nowNs-returnNs_>=500000000;
        Add(Finished,f,&v);}
}
void Probe::Add(unsigned kind,const Facts& f,const Native* n,const Sample* evaluated)noexcept {
    Event e;e.kind=kind;e.phase=unsigned(semantic_.Current());e.reason=unsigned(semantic_.Rejection());
    e.input=f.source.input.generation;e.now=f.source.nowNs;e.observed=f.source.observedNs;e.deadline=f.source.deadlineNs;
    e.holster=f.holsterPhase;e.visible=f.visible;e.cacheQuiet=f.cacheQuiet;e.revision=f.display?f.display->revision:0;
    e.custodyObserved=f.custodyObserved;
    e.admissionEvaluated=evaluated!=nullptr;
    if(evaluated){e.admissionFailures=evaluated->admission.FailureMask();e.bodySettledFailures=f.bodySettledFailures;}
    if(f.right){e.rightInput=f.right->inputSequence;e.rightDeadline=f.right->deadlineNs;}
    if(f.right){e.right=f.right->token.id;e.rightItem=f.right->token.item.id;e.rightGeneration=f.right->token.owner.equipGeneration;}
    if(f.left)e.left=f.left->token.id;
    if(n){e.selected=n->owner.weapon;e.selectors=n->selectors;for(unsigned i=0;i<6;++i){const auto& item=i<3?n->original:n->target;
        const auto& state=i%3<2?item.client.branches[i%3]:item.server;e.ammo[i*2]=state.loaded;e.ammo[i*2+1]=state.reserve;e.firing[i]=state.currentState;
        e.firingFlags[i]=state.flagsA8;
        if(i<3){e.originalNext[i]=state.nextState;e.originalTimers[i]=state.phaseTimer;}}}
    if(kind==NativeChange&&lastNative_){const auto& p=*lastNative_;
        if(e.selected==p.selected&&e.selectors==p.selectors&&e.ammo==p.ammo&&e.firing==p.firing&&e.cacheQuiet==p.cacheQuiet&&
           (!f.custodyObserved||(e.holster==p.holster&&e.right==p.right&&e.left==p.left&&e.revision==p.revision&&e.visible==p.visible)))return;}
    if(kind==NativeChange)lastNative_=e;
    if(count_==events_.size()){++dropped_;failed_=true;return;}events_[count_++]=e;
}
void Probe::Report(std::ostream& out)const {
    out<<"{\"enabled\":true,\"complete\":"<<complete_<<",\"passed\":"<<pass_<<",\"failed\":"<<failed_
       <<",\"semantic_phase\":"<<unsigned(semantic_.Current())<<",\"reason\":"<<unsigned(semantic_.Rejection())
       <<",\"commits\":"<<semantic_.Commits()<<",\"observed_native_edges\":"<<semantic_.ObservedNativeEdges()
       <<",\"selected_grenade\":"<<selectedGrenade_<<",\"selected_original\":"<<selectedOriginal_
       <<",\"returned_held\":"<<returnedHeld_<<",\"return_settled\":"<<returnSettled_<<",\"ammo_consumed\":"<<ammoConsumed_<<",\"dropped\":"<<dropped_<<",\"baseline\":";
    if(!baseline_)out<<"null";
    else {const auto& b=*baseline_;out<<"{\"owner\":["<<b.owner.player<<','<<b.owner.soldier<<','<<b.owner.weak<<','<<b.owner.weapon<<','
        <<b.owner.actorGeneration<<','<<b.owner.equipGeneration<<','<<b.owner.space<<"],\"original_slot\":"<<b.originalSlot<<",\"target_slot\":"<<b.targetSlot
        <<",\"inventory\":"<<b.carried.inventory<<",\"switching\":"<<b.carried.switching<<",\"items\":[";
        for(unsigned k=0;k<b.carried.count;++k){const auto& i=b.carried.items[k];if(k)out<<',';out<<'['<<i.weapon<<','<<i.data<<','<<i.persistence<<','<<i.category<<','<<i.slot<<']';}
        out<<"],\"firing_identity\":[";unsigned k=0;for(const auto* i:{&b.original,&b.target}){if(k++)out<<',';out<<'['<<i->client.target<<','
            <<i->client.config.weaponData<<','<<i->client.config.firingData<<','<<i->client.config.primaryFire<<','<<i->client.config.ammoAddress<<','
            <<i->client.branches[0].address<<','<<i->client.branches[1].address<<','<<i->serverItem<<','<<i->server.address<<']';}
        out<<"],\"body_slots\":[";if(body_)for(unsigned i=0;i<body_->count;++i){const auto& s=body_->slots[i];if(i)out<<',';out<<'['<<s.assignment.slot<<','<<s.assignment.item.id<<','<<s.assignment.item.generation<<']';}
        out<<"],\"original_gun\":["<<originalGun_.id<<','<<originalGun_.item.id<<','<<originalGun_.item.generation<<"]}";}
    out<<",\"admission_failure_names\":[";unsigned nameIndex=0;
    for(const auto* name:Admission::Names){if(nameIndex++)out<<',';out<<'"'<<name<<'"';}
    out<<"],\"body_settled_failure_names\":[\"inventory_missing\",\"holster_missing\",\"inventory_pending\",\"automatic_stow_pending\",\"holster_blocks\",\"draw_pending\",\"draw_blocks_fire\",\"draw_command\",\"queued_draw\",\"holster_equipment\",\"weapon_mode\",\"cycle\"]";
    out<<",\"events\":[";
    for(unsigned i=0;i<count_;++i){const auto& e=events_[i];if(i)out<<',';
        out<<"{\"kind\":"<<e.kind<<",\"phase\":"<<e.phase<<",\"reason\":"<<e.reason<<",\"input\":"<<e.input
           <<",\"now\":"<<e.now<<",\"observed\":"<<e.observed<<",\"deadline\":"<<e.deadline<<",\"selected\":"<<e.selected
           <<",\"holster\":"<<e.holster<<",\"right\":"<<e.right<<",\"right_item\":"<<e.rightItem<<",\"right_generation\":"<<e.rightGeneration
           <<",\"left\":"<<e.left<<",\"revision\":"<<e.revision<<",\"visible\":"<<e.visible<<",\"custody_observed\":"<<e.custodyObserved<<",\"cache_quiet\":"<<e.cacheQuiet<<",\"selectors\":[";
        for(unsigned k=0;k<3;++k){const auto& s=e.selectors[k];if(k)out<<',';out<<'['<<s.inventory<<','<<s.switching<<','<<s.callback<<','<<s.current<<','<<s.previous<<','<<s.primary<<']';}
        out<<"],\"ammo\":[";for(unsigned k=0;k<12;++k){if(k)out<<',';out<<e.ammo[k];}out<<"],\"firing\":[";
        for(unsigned k=0;k<6;++k){if(k)out<<',';out<<e.firing[k];}
        out<<"],\"admission_evaluated\":"<<e.admissionEvaluated<<",\"admission_failures\":"<<e.admissionFailures
           <<",\"body_settled_failures\":"<<e.bodySettledFailures<<",\"right_input\":"<<e.rightInput<<",\"right_deadline\":"<<e.rightDeadline
           <<",\"original_next\":[";
        for(unsigned k=0;k<3;++k){if(k)out<<',';out<<e.originalNext[k];}out<<"],\"original_timers\":[";
        for(unsigned k=0;k<3;++k){if(k)out<<',';out<<e.originalTimers[k];}out<<"],\"firing_flags_a8\":[";
        for(unsigned k=0;k<6;++k){if(k)out<<',';out<<unsigned(e.firingFlags[k]);}out<<"]}";}out<<"]}";
}
}
