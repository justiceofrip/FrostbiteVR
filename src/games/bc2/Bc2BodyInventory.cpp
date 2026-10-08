#include "Bc2BodyInventory.h"
#include "Bc2BodyHolster.h"
#include <algorithm>
#include <cstring>
#include <limits>
namespace fvr::bc2 { namespace {
using namespace interaction;
struct Reader {
    const WeaponModeMemory& m;unsigned calls=0;BodyReadStatus status=BodyReadStatus::ReadFailure;
    bool Read(std::uint64_t at,void* dst,std::size_t n)noexcept {
        if(calls++>=16384||at<0x10000||!n||n>4096||at+n>UINT32_MAX||!m.read)return false;
        return m.read(m.context,std::uint32_t(at),dst,n);
    }
    template<class T>bool Get(std::uint64_t at,T& value)noexcept{return Read(at,&value,sizeof(value));}
    bool Type(unsigned at,const char* name)const noexcept{return m.type&&m.type(m.context,at,name);}
    bool Name(unsigned at,const char* wanted)noexcept {
        std::array<char,96> value{};const auto n=std::strlen(wanted)+1;
        return n<=value.size()&&Read(at,value.data(),n)&&!std::memcmp(value.data(),wanted,n);
    }
    bool Text(unsigned at,std::array<char,96>& value)noexcept {
        // Reflected image strings only. Reject unreadable or unterminated spans
        // rather than falling back to hundreds of bytewise process reads.
        if(!Read(at,value.data(),value.size()))return false;
        for(unsigned n=0;n<value.size();++n){const auto c=static_cast<unsigned char>(value[n]);if(!c)return n>0;if(c<32||c>126)return false;}
        return false;
    }
    bool Info(unsigned at,const char* expected,unsigned short flags,unsigned short size,unsigned& fields,unsigned char& count)noexcept {
        unsigned meta=0;std::array<unsigned,7> words{};
        if(!Get(std::uint64_t(at)+4,meta)||!Read(meta,words.data(),sizeof(words))||!Name(words[0],expected)||
            (words[1]&0xffff)!=flags||(words[1]>>16)!=size)return false;
        count=static_cast<unsigned char>((words[3]>>8)&0xff);
        if(!count||count>64)return false;
        if(flags==0x35)return Get(std::uint64_t(at)+36,fields);
        fields=words[6];return true;
    }
    bool ClassLayout(unsigned data)noexcept {
        unsigned table=0,getter=0,info=0,fields=0;unsigned char count=0;std::array<unsigned char,6> code{};
        if(!Type(data,"SoldierWeaponData")||!Get(data,table)||!Get(std::uint64_t(table)+8,getter)||!Read(getter,code.data(),6)||code[0]!=0xb8||code[5]!=0xc3)return false;
        std::memcpy(&info,code.data()+1,4);
        if(!Info(info,"SoldierWeaponData",0x35,0x130,fields,count))return false;
        unsigned found=0,enumInfo=0;std::array<std::array<unsigned,6>,64> descriptors{};
        if(!Read(fields,descriptors.data(),std::size_t(count)*24))return false;
        for(unsigned n=0;n<count;++n){const auto& f=descriptors[n];std::array<char,96> name{};
            if(!Text(f[0],name))return false;
            if(!std::strcmp(name.data(),"WeaponClass")){if(++found!=1||f[4]!=0x84)return false;enumInfo=f[2];}}
        if(found!=1||!Info(enumInfo,"WeaponClassEnum",0x179,4,fields,count))return false;
        constexpr const char* names[]={"wcAssault","wcShotgun","wcSmg","wcLmg","wcSniper","wcUgl"};
        unsigned mask=0;if(!Read(fields,descriptors.data(),std::size_t(count)*24))return false;
        for(unsigned n=0;n<count;++n){const auto& f=descriptors[n];std::array<char,96> name{};
            if(!Text(f[0],name))return false;
            for(unsigned k=0;k<6;++k)if(!std::strcmp(name.data(),names[k])){if(f[4]!=k||(mask&(1u<<k)))return false;mask|=1u<<k;}}
        return mask==63;
    }
    bool Snapshot(const ReloadStateOwner& owner,BodyNativeInventory& out)noexcept {
        unsigned weak=0,soldier=0,player=0,controlled=0,begin=0,end=0;unsigned char flags=0;
        std::array<std::byte,0x7a> playerFields{};std::array<unsigned,18> soldierFields{};
        std::array<unsigned,0x150/4> inventoryFields{};
        out={};out.owner=owner;
        if(!owner.player||!owner.soldier||!owner.weak||!owner.weapon||!owner.actorGeneration||!owner.equipGeneration||!owner.space||
           !Type(owner.soldier,"ClientSoldierEntity")||!Read(std::uint64_t(owner.player)+0xc54,playerFields.data(),playerFields.size())||
           !Read(std::uint64_t(owner.soldier)+0x220,soldierFields.data(),sizeof(soldierFields)))return false;
        std::memcpy(&weak,playerFields.data(),4);std::memcpy(&controlled,playerFields.data()+0x14,4);player=soldierFields[0];
        if(weak!=owner.weak||!Get(weak,soldier)||std::uint64_t(owner.soldier)+4!=soldier||player!=owner.player||
           !(std::to_integer<unsigned char>(playerFields[0x79])&8)||controlled!=owner.soldier||!Get(std::uint64_t(owner.soldier)+0x114,flags))return false;
        out.inventory=soldierFields[(flags&1)?11:10];begin=soldierFields[16];end=soldierFields[17];
        if(!Read(out.inventory,inventoryFields.data(),sizeof(inventoryFields)))return false;
        out.switching=inventoryFields[1];out.selected=inventoryFields[0x14c/4];
        if(!Type(out.switching,"WeaponSwitchingData")||end<=begin||end-begin>32*4||(end-begin)%4)return false;
        out.begin=begin;out.end=end;out.count=(end-begin)/4;if(out.selected>=out.count)return false;
        std::array<unsigned,32> raw{};if(!Read(begin,raw.data(),end-begin)||raw[out.selected]!=owner.weapon)return false;
        unsigned layoutData=0;
        for(unsigned n=0;n<out.count;++n){auto& item=out.items[n];item.weapon=raw[n];item.slot=n;if(!raw[n])continue;
            std::array<unsigned,9> itemFields{};
            if(std::count(raw.begin(),raw.end(),raw[n])!=1||!Get(std::uint64_t(raw[n])+4,item.data)||!Type(item.data,"SoldierWeaponData")||
               !Read(std::uint64_t(item.data)+0x64,itemFields.data(),sizeof(itemFields)))return false;
            item.persistence=itemFields[0];item.category=itemFields[8];if(item.category>20)return false;
            if(!layoutData){layoutData=item.data;if(!ClassLayout(layoutData)){status=BodyReadStatus::Layout;return false;}}
        }
        std::array<unsigned,2> mapRange{};
        if(!Read(std::uint64_t(out.switching)+0x18,mapRange.data(),sizeof(mapRange)))return false;
        const auto map=mapRange[0],last=mapRange[1];if(
            last<=map||last-map>128*24||(last-map)%24){status=BodyReadStatus::Bounds;return false;}
        out.mapBegin=map;out.mapEnd=last;out.routeCount=(last-map)/24;
        std::array<std::array<unsigned,6>,128> routes{};
        if(!Read(map,routes.data(),std::size_t(out.routeCount)*24))return false;
        for(unsigned n=0;n<out.routeCount;++n){const auto& r=routes[n];auto& route=out.routes[n];
            if(r[5]>10||r[4]>=49||r[3]<r[2]||r[3]-r[2]>44||(r[3]-r[2])%4)return false;
            route.from=r[5];route.action=r[4];route.count=(r[3]-r[2])/4;
            if(route.count&&!Read(r[2],route.targets.data(),route.count*4))return false;
            for(unsigned j=0;j<route.count;++j)if(route.targets[j]>10)return false;
        }
        return true;
    }
};
BodyInventoryOwner Owner(const ReloadStateOwner& o)noexcept{return {(std::uint64_t(o.weak)<<32)|o.soldier,o.actorGeneration,o.space};}
}
BodyInventoryRead ReadBodyInventory(const WeaponModeMemory& memory,const ReloadStateOwner& owner,bool enabled)noexcept {
    BodyInventoryRead out;if(!enabled)return out;Reader r{memory};BodyNativeInventory first,second;
    if(!r.Snapshot(owner,first)||!r.Snapshot(owner,second))out.status=r.status;
    else if(first!=second)out.status=BodyReadStatus::Changed;
    else {out.status=BodyReadStatus::Okay;out.snapshot=first;}
    out.reads=r.calls;return out;
}
bool BodyLongGunCategory(unsigned c)noexcept{return c<=4;}
std::optional<WeaponModeCommand> ResolveBodyDraw(const BodyNativeInventory& inventory,unsigned weapon)noexcept {
    if(!weapon||weapon==inventory.owner.weapon||inventory.count>32||inventory.routeCount>128||inventory.selected>=inventory.count)return {};
    const auto wanted=std::find_if(inventory.items.begin(),inventory.items.begin()+inventory.count,[&](const auto& i){return i.weapon==weapon&&BodyLongGunCategory(i.category);});
    if(wanted==inventory.items.begin()+inventory.count)return {};
    // Only established non-firing selection inputs. Native selector eligibility
    // may skip the first populated target; that is NOT an acknowledgement.
    for(unsigned action:{7u,36u,33u}){const BodyNativeRoute* match=nullptr;bool duplicate=false;
        for(unsigned n=0;n<inventory.routeCount;++n){const auto& r=inventory.routes[n];if(r.from!=inventory.selected||r.action!=action)continue;if(match)duplicate=true;match=&r;}
        if(!match||duplicate||!match->count||match->count>11)continue;
        bool malformed=false;unsigned first=0;
        for(unsigned n=0;n<match->count;++n){const auto slot=match->targets[n];if(slot>=9||slot>=inventory.count){malformed=true;break;}
            if(!first&&inventory.items[slot].weapon)first=inventory.items[slot].weapon;}
        if(!malformed&&first==weapon)return WeaponModeCommand{action,weapon,wanted->slot,inventory.inventory,wanted->persistence};
    }return {};
}
bool BodyInventoryDisplayFresh(const BodyInventoryDisplay& s,std::int64_t now)noexcept {
    if(!s.revision||!s.cohort||!s.sequence||!s.count||s.count>s.slots.size()||
       s.observedNs<=0||s.observedNs>now||s.deadlineNs<=now||s.deadlineNs-s.observedNs>100000000||
       !s.selectedOwner.player||!s.selectedOwner.soldier||!s.selectedOwner.weak||!s.selectedOwner.actorGeneration||
       !s.selectedOwner.equipGeneration||!s.selectedOwner.weapon||!s.physicalSelected.id||!s.physicalSelected.generation||
       s.physicalOwner.actor!=((std::uint64_t(s.selectedOwner.weak)<<32)|s.selectedOwner.soldier)||
       s.physicalOwner.actorGeneration!=s.selectedOwner.actorGeneration||!s.physicalOwner.equipGeneration||
       s.physicalOwner.space!=s.selectedOwner.space||!s.physicalOwner.space||
       !s.carried.inventory||!s.carried.switching||!s.carried.count||s.carried.count>32)return false;
    for(unsigned n=0;n<s.count;++n){const auto& v=s.slots[n];
        if(!v.assignment.slot||!v.assignment.item.generation||v.assignment.item.id!=v.native.weapon||
           v.native.slot>=s.carried.count||s.carried.items[v.native.slot]!=v.native||!BodyLongGunCategory(v.native.category))return false;
        for(unsigned k=0;k<n;++k)if(s.slots[k].assignment.slot==v.assignment.slot||s.slots[k].assignment.item.id==v.assignment.item.id)return false;
    }return true;
}
bool SameBodyInventoryDisplayCohort(const BodyInventoryDisplay& a,const BodyInventoryDisplay& b)noexcept {
    return a.selectedOwner==b.selectedOwner&&a.physicalOwner==b.physicalOwner&&a.carried==b.carried&&
        a.physicalSelected==b.physicalSelected&&a.revision==b.revision&&a.count==b.count&&a.slots==b.slots;
}
void Bc2BodyInventory::PublishDisplay(const BodyNativeInventory& n,const BodyDrawSample& s,BodyItemKey selected)noexcept {
    BodyInventoryDisplay out;out.selectedOwner=s.owner;out.physicalOwner=s.hand.owner;
    out.carried={n.inventory,n.switching,n.count,n.items};out.physicalSelected=selected;
    out.revision=revision_;out.sequence=s.hand.sequence;out.observedNs=s.hand.observedNs;
    out.deadlineNs=std::min(s.hand.deadlineNs,s.hand.observedNs+100000000);
    if(slots_.size()>out.slots.size()||s.cancel||s.reloadBusy||!s.hand.focused||!s.input.focused||!s.input.headValid){display_.reset();return;}
    for(const auto& slot:slots_){const auto item=std::find_if(life_.begin(),life_.end(),[&](const Life& l){return l.key==slot.item;});
        if(item==life_.end()){display_.reset();return;}out.slots[out.count++]={slot,item->native};}
    if(display_&&display_->selectedOwner==out.selectedOwner&&display_->physicalOwner==out.physicalOwner){
        if(out.sequence<display_->sequence){display_.reset();return;}
        if(out.sequence==display_->sequence){
            if(!SameBodyInventoryDisplayCohort(*display_,out)||out.observedNs!=display_->observedNs||out.deadlineNs!=display_->deadlineNs)display_.reset();
            return; // duplicate source cannot renew even while holster polling
        }
    }
    // One cohort generation changes on any actual selected/slot/item identity
    // change, including same-pointer data/persistence replacement. It is display
    // pairing identity only, not a new native equipment epoch.
    if(!display_||!SameBodyInventoryDisplayCohort(*display_,out)){
        if(displayCohort_==UINT64_MAX){display_.reset();return;}++displayCohort_;
    }
    out.cohort=displayCohort_;
    if(BodyInventoryDisplayFresh(out,s.hand.nowNs))display_=out;else display_.reset();
}
bool Bc2BodyInventory::Visible(const BodyDrawSample& s)const noexcept {
    if(!s.visible)return false;const auto& v=*s.visible;
    return v.owner==s.owner&&v.generation&&v.generation<=s.input.generation&&v.observedNs>0&&v.observedNs<=s.hand.nowNs&&
        v.deadlineNs>s.hand.nowNs&&v.deadlineNs-v.observedNs<=150000000;
}
void Bc2BodyInventory::ObserveNativeActor(unsigned manager,unsigned player,unsigned soldier,unsigned weak,bool foot)noexcept {
    if(!automaticEnabled_||manager<0x10000||player<0x10000||soldier<0x10000||weak<0x10000)return;
    const Actor next{manager,player,soldier,weak};
    const bool entering=foot&&(next!=automaticActor_||!automaticFoot_);
    if(next!=automaticActor_||!foot){automaticPending_=false;automaticNeutral_=false;automaticItem_.reset();}
    automaticActor_=next;automaticFoot_=foot;
    if(entering){automaticPending_=true;automaticNeutral_=false;automaticItem_.reset();++automaticEntries_;}
}
void Bc2BodyInventory::ObserveAutomaticSelection(const BodyNativeItem& item)noexcept {
    if(!automaticPending_)return;
    if(automaticItem_&&*automaticItem_!=item){automaticPending_=false;++automaticSuperseded_;}
    else automaticItem_=item;
}
void Bc2BodyInventory::Cancel()noexcept {
    display_.reset();
    heldApproach_.Reset();heldRelease_.Reset();
    if(pending_)++cancels_;pending_.reset();armed_=false;lastInput_=0;automaticNeutral_=false;
    policy_.Update({});
    // Loss of observation is a lifetime gap; a returning pointer is not assumed
    // to be the same physical object after death/pickup/reconnect.
    life_.clear();items_.clear();slots_.clear();owner_={};inventory_=switching_=0;nativeProofNs_=0;
}
void Bc2BodyInventory::SuspendInteraction()noexcept {
    display_.reset();heldApproach_.Reset();heldRelease_.Reset();
    if(pending_)++cancels_;pending_.reset();armed_=false;automaticNeutral_=false;
    // Retire portable pending selection/ack authority while keeping exact item
    // metadata. No presentation or hand claim is inferred from these slots.
    if(owner_.actor&&sequence_!=UINT64_MAX&&!items_.empty()){
        BodyInventorySample neutral;
        neutral.inventory={owner_,revision_,++sequence_,lastNow_,items_,std::nullopt,BodyPresentation::Unknown,false};
        neutral.nowNs=lastNow_;neutral.focused=false;neutral.handTracked=false;
        slots_=policy_.Update(neutral).slots;
    }
}
BodyDrawResult Bc2BodyInventory::ObserveSuspended(const WeaponModeMemory& memory,const BodyDrawSample& s)noexcept {
    BodyDrawResult out;out.cancelled=pending_?pending_->id:0;out.blockFire=true;
    // The clock originates from the current gather, not an expired controller
    // observation. Invalid/backward clocks cannot refresh a native lifetime.
    if(s.hand.nowNs<=0||s.hand.nowNs<lastNow_){Cancel();++unavailable_;return out;}
    const auto read=ReadBodyInventory(memory,s.owner,true);++readStatuses_[unsigned(read.status)];
    if(!read.snapshot||!Observe(*read.snapshot,s)){Cancel();++unavailable_;return out;}
    lastNow_=s.hand.nowNs;SuspendInteraction();lastInput_=s.input.generation;
    out.blockFire=out.cancelled!=0;++blocked_;return out;
}
bool Bc2BodyInventory::Observe(const BodyNativeInventory& native,const BodyDrawSample& s)noexcept {
    // Native metadata continuity is bounded independently of input/render leases.
    // A returning pointer after an unobserved interval receives a new lifetime.
    if(nativeProofNs_&&(s.hand.nowNs<nativeProofNs_||s.hand.nowNs-nativeProofNs_>=150000000))Cancel();
    nativeProofNs_=s.hand.nowNs;
    const auto owner=Owner(s.owner);bool changed=owner.actor!=owner_.actor||owner.generation!=owner_.generation||inventory_!=native.inventory||switching_!=native.switching;
    std::vector<Life> next;std::vector<BodyInventoryItem> items;
    for(unsigned n=0;n<native.count;++n){const auto& item=native.items[n];if(!item.weapon)continue;
        auto found=changed?life_.end():std::find_if(life_.begin(),life_.end(),[&](const Life& l){auto prior=l.native;prior.slot=item.slot;return prior==item;});
        BodyItemKey key{};
        if(found==life_.end()){if(generation_==UINT64_MAX)return false;key={item.weapon,++generation_};}else key=found->key;
        next.push_back({item,key});BodyInventoryItem policy;policy.key=key;
        if(BodyLongGunCategory(item.category)){const unsigned preferred=item.category==1?1u:0u;policy.preferredSlots={config_.shoulders[preferred].slot,config_.shoulders[1-preferred].slot,0,0};policy.preferenceCount=2;policy.priority=item.category==1?100:50;}
        items.push_back(policy);
    }
    changed=changed||items!=items_;
    if(changed||owner.space!=owner_.space){if(revision_==UINT64_MAX)return false;++revision_;armed_=false;heldApproach_.Reset();heldRelease_.Reset();}
    // Retained metadata names only the same proven native lifetime. Never
    // transfer an old shoulder to a replacement owner/container/item key.
    std::erase_if(slots_,[&](const BodySlotAssignment& slot){
        const auto item=std::find_if(items.begin(),items.end(),[&](const auto& current){return current.key==slot.item;});
        return item==items.end()||std::find(item->preferredSlots.begin(),item->preferredSlots.begin()+item->preferenceCount,slot.slot)==item->preferredSlots.begin()+item->preferenceCount;
    });
    owner_=owner;nativeOwner_=native.owner;inventory_=native.inventory;switching_=native.switching;life_=std::move(next);items_=std::move(items);return true;
}
std::optional<AmmoResourceBinding> Bc2BodyInventory::ResourceBinding(const ReloadStateOwner& owner,std::int64_t now)const noexcept {
    if(!enabled_||owner!=nativeOwner_||nativeProofNs_<=0||nativeProofNs_>INT64_MAX-100000000)return {};
    const auto item=std::find_if(life_.begin(),life_.end(),[&](const Life& l){return l.native.weapon==owner.weapon;});
    if(item==life_.end())return {};
    AmmoResourceBinding result{nativeOwner_,{{owner.soldier,owner.actorGeneration,item->key.id,item->key.generation},
        owner.equipGeneration,owner.space},inventory_,switching_,item->native.data,item->native.persistence,
        nativeProofNs_,nativeProofNs_+100000000};
    return AmmoResourceBindingFresh(result,owner,now)?std::optional(result):std::nullopt;
}
BodyDrawResult Bc2BodyInventory::Tick(const WeaponModeMemory& memory,const BodyDrawSample& s,HandInteraction& hands,std::uint64_t& sharedIntent)noexcept {
    return TickDraw(memory,s,hands,sharedIntent,std::nullopt);
}
BodyDrawResult Bc2BodyInventory::TickDraw(const WeaponModeMemory& memory,const BodyDrawSample& s,HandInteraction& hands,
    std::uint64_t& sharedIntent,const std::optional<DrawApproach>& approach)noexcept {
    BodyDrawResult out;if(!enabled_)return out;
    const bool fresh=ValidBodyAnchors(config_)&&ValidInput(s.input)&&s.hand.owner.actor==Owner(s.owner).actor&&
        s.hand.owner.actorGeneration==s.owner.actorGeneration&&s.hand.owner.space==s.owner.space&&s.input.spaceGeneration==s.owner.space&&
        s.hand.sequence==s.input.generation&&s.hand.observedNs>0&&s.hand.observedNs<=s.hand.nowNs&&s.hand.deadlineNs>s.hand.nowNs&&
        s.hand.deadlineNs-s.hand.observedNs<=150000000&&s.hand.nowNs>=lastNow_&&s.hand.focused&&s.hand.tracked[1]&&s.input.focused&&s.input.headValid;
    if(!fresh||s.cancel||s.reloadBusy||s.interaction==BodyInteractionState::Suspended)return ObserveSuspended(memory,s);
    lastNow_=s.hand.nowNs;
    if(s.input.generation==lastInput_){out.pending=bool(pending_);out.blockFire=out.pending;return out;}
    lastInput_=s.input.generation;
    const auto read=ReadBodyInventory(memory,s.owner,true);++readStatuses_[unsigned(read.status)];
    if(!read.snapshot||!Observe(*read.snapshot,s)||sequence_==UINT64_MAX){out.cancelled=pending_?pending_->id:0;Cancel();++unavailable_;return out;}
    const auto& native=*read.snapshot;
    unsigned physicalSelected=s.owner.weapon;
    if(s.verifiedFamily&&s.verifiedFamily->inventory==native.inventory&&s.verifiedFamily->persistent&&
        native.items[native.selected].category==5&&native.items[native.selected].persistence==s.verifiedFamily->persistent){
        const auto base=std::find_if(life_.begin(),life_.end(),[&](const Life& l){return l.native.weapon==s.verifiedFamily->targetWeapon&&
            BodyLongGunCategory(l.native.category)&&l.native.persistence==s.verifiedFamily->persistent;});
        if(base!=life_.end())physicalSelected=base->native.weapon;
    }
    const auto selected=std::find_if(life_.begin(),life_.end(),[&](const Life& l){return l.native.weapon==physicalSelected;});
    if(selected==life_.end()){Cancel();return out;}
    ObserveAutomaticSelection(selected->native);
    const bool visible=Visible(s);BodyInventorySample sample;
    sample.inventory={owner_,revision_,++sequence_,s.hand.nowNs,items_,selected->key,visible?BodyPresentation::WeaponVisible:BodyPresentation::Unknown,false};
    sample.nowNs=s.hand.nowNs;sample.focused=true;sample.handTracked=true;
    if(pending_&&pending_->item==selected->key&&visible)sample.acknowledgement=BodyInventoryAcknowledgement{pending_->id,pending_->revision,pending_->owner,pending_->operation,pending_->item,true};
    const bool squeezeAvailable=(s.input.hands[1].active&Squeeze)!=0;
    const bool pressed=squeezeAvailable&&s.input.hands[1].squeeze>=.75f&&armed_;
    if(!squeezeAvailable)armed_=false;else if(s.input.hands[1].squeeze<=.35f)armed_=true;else if(pressed)armed_=false;
    const auto held=hands.Current(InteractionHand::Right);
    const bool retainedApproach=approach&&squeezeAvailable&&s.input.hands[1].squeeze>.35f&&
        approach->revision==revision_&&approach->input==s.hand.sequence&&approach->observed==s.hand.observedNs&&
        approach->deadline>s.hand.nowNs&&approach->deadline<=s.hand.deadlineNs&&s.hand.tracked[0]&&
        held&&held->token==approach->hold&&held->inputSequence==s.hand.sequence&&held->deadlineNs>s.hand.nowNs&&
        (s.input.hands[0].active&Trigger)&&(s.input.hands[1].active&Trigger)&&
        s.input.hands[0].trigger<=.1f&&s.input.hands[1].trigger<=.1f;
    std::optional<WeaponModeCommand> proposed;
    if((pressed||retainedApproach)&&!pending_&&visible){
        const auto point=BodyAnchorHandPose(s.input,InteractionHand::Right);
        const auto left=hands.Current(InteractionHand::Left),right=hands.Current(InteractionHand::Right);
        // Do not steal support/sight/ammo ownership or silently retire a reload.
        if(point&&!left&&right&&right->token.kind==HandClaimKind::GunHold&&right->token.owner==s.hand.owner&&right->token.item==s.gun&&right->deadlineNs>s.hand.nowNs){
            for(const auto& anchor:config_.shoulders){if(!BodyAnchorContains(anchor,*point))continue;
                const auto assigned=std::find_if(slots_.begin(),slots_.end(),[&](const auto& a){return a.slot==anchor.slot;});
                if(assigned==slots_.end())continue;
                if(assigned->item==selected->key){
                    // Explicit rejected stow attempt on the ordinary selector.
                    // No hide permission, synthetic acknowledgement or ownership
                    // transfer is granted by touching the currently held slot.
                    EventRow(4,s,0,physicalSelected,anchor.slot);continue;
                }
                if(approach&&(!retainedApproach||*assigned!=approach->target))continue;
                proposed=ResolveBodyDraw(native,unsigned(assigned->item.id));if(!proposed)continue;
                // Reserve the actual gesture through the shared hand domain.
                // Native still owns the gun until selection is observed. Transfer
                // back immediately so no automatic GunHold renewal can mis-renew
                // a BodyInventory token on the next tick.
                const HandContactProof proof{{0x424332484f4c53ull+anchor.slot,revision_},s.hand.sequence,s.hand.deadlineNs,true};
                if(sharedIntent>UINT64_MAX-2){proposed.reset();break;}
                const HandClaimRequest claim{s.hand.owner,InteractionHand::Right,HandClaimKind::BodyInventory,
                    {assigned->item.id,assigned->item.generation},proof,++sharedIntent,0};
                const auto reserve=hands.Transfer(s.hand,right->token,claim);
                if(!reserve.accepted){proposed.reset();break;}
                const HandClaimRequest restore{s.hand.owner,InteractionHand::Right,HandClaimKind::GunHold,s.gun,
                    {{1,s.hand.owner.equipGeneration},s.hand.sequence,s.hand.deadlineNs,true},++sharedIntent,0};
                if(!hands.Transfer(s.hand,reserve.claim->token,restore).accepted){proposed.reset();break;}
                sample.intent={sharedIntent,BodyInventoryOperation::Draw,anchor.slot,assigned->item};break;
            }
        }
    }
    const auto result=policy_.Update(sample);slots_=result.slots;
    if(result.cancelledRequest){out.cancelled=result.cancelledRequest;EventRow(3,s,result.cancelledRequest);pending_.reset();++cancels_;}
    if(result.committedRequest){out.committed=result.committedRequest;EventRow(2,s,result.committedRequest,unsigned(selected->key.id));pending_.reset();++commits_;}
    if(result.request&&proposed){pending_=result.request;out.command=proposed;EventRow(1,s,result.request->id,proposed->targetWeapon,proposed->action);++requests_;}
    out.pending=bool(pending_);out.blockFire=out.pending;
    if(!out.pending)PublishDisplay(native,s,selected->key);else display_.reset();return out;
}
BodyHolsterResult Bc2BodyInventory::TickHolster(const WeaponModeMemory& memory,const BodyDrawSample& source,BodyHolsterSample sample,
    Bc2BodyHolster& holster,HandInteraction& hands,std::uint64_t& intent)noexcept {
    auto s=source;
    if(enabled_&&(source.interaction==BodyInteractionState::Suspended||source.cancel||source.reloadBusy||
        !ValidInput(source.input)||!source.hand.focused||!source.hand.tracked[1]||!source.input.focused||!source.input.headValid||
        source.hand.deadlineNs<=source.hand.nowNs)){
        const bool pendingOperation=bool(pending_)||holster.BlocksActions();
        const auto priorItem=std::find_if(life_.begin(),life_.end(),[&](const Life& item){return item.native.weapon==source.owner.weapon;});
        const auto priorKey=priorItem==life_.end()?BodyItemKey{}:priorItem->key;
        const auto gun=hands.Current(InteractionHand::Right);
        const bool existingHold=ValidInput(source.input)&&source.input.focused&&source.input.headValid&&
            source.hand.focused&&source.hand.tracked[1]&&source.hand.sequence==source.input.generation&&
            source.hand.owner.actor==Owner(source.owner).actor&&source.hand.owner.actorGeneration==source.owner.actorGeneration&&
            source.hand.owner.space==source.owner.space&&source.input.spaceGeneration==source.owner.space&&
            source.hand.observedNs>0&&source.hand.observedNs<=source.hand.nowNs&&source.hand.deadlineNs>source.hand.nowNs&&
            source.hand.deadlineNs-source.hand.observedNs<=150000000&&gun&&gun->token.kind==HandClaimKind::GunHold&&
            gun->token.owner==source.hand.owner&&gun->token.item==source.gun&&gun->deadlineNs>source.hand.nowNs;
        const auto metadata=ObserveSuspended(memory,source);BodyHolsterResult out;
        // Existing native holster cancellation remains authoritative for any
        // in-flight hide/draw. Metadata retention never acknowledges it.
        sample.cancel=true;sample.hand=source.hand;sample.nativeOwner=source.owner;
        if(holster.Phase()!=BodyHolsterPhase::Held)out=holster.Tick(policy_,sample,hands,intent);
        out.ordinaryDraw=metadata;out.phase=holster.Phase();const bool samePhysicalItem=priorKey.id&&std::any_of(life_.begin(),life_.end(),[&](const Life& item){return item.key==priorKey;});
        out.blockWeaponActions=pendingOperation||holster.BlocksActions()||!existingHold||!samePhysicalItem;out.allowAutomaticGunHold=false;
        return out;
    }
    if(s.cancel||s.reloadBusy)display_.reset();
    const auto ordinary=[&](std::optional<DrawApproach> approach=std::nullopt){heldApproach_.Reset();heldRelease_.Reset();BodyHolsterResult out;out.ordinaryDraw=TickDraw(memory,s,hands,intent,approach);out.blockWeaponActions=out.ordinaryDraw.blockFire;
        // An observed but unaccepted scoped XM8 stays on ordinary input. Do not
        // surprise-stow a later supported weapon because the entry was pending.
        if(automaticPending_&&!s.cancel&&!s.reloadBusy&&sample.selected&&sample.selected->owner==s.owner&&
            sample.selected->sequence&&sample.selected->observedNs>0&&sample.selected->observedNs<=s.hand.nowNs&&
            sample.selected->deadlineNs>s.hand.nowNs&&!holster.AcceptsProfile(s.owner,sample.selected,s.hand.nowNs)&&
            FindSelectedMesh(*sample.selected,s.owner,SelectedMeshKind::Xm8,s.hand.nowNs)&&
            FindSelectedMesh(*sample.selected,s.owner,SelectedMeshKind::Acog4x,s.hand.nowNs)){
            automaticPending_=false;++automaticUnsupported_;}
        return out;};
    if(!enabled_||(holster.Phase()==BodyHolsterPhase::Held&&
        (pending_||s.cancel||s.reloadBusy||!holster.AcceptsProfile(s.owner,sample.selected,s.hand.nowNs))))return ordinary();
    sample.nativeOwner=s.owner;sample.hand=s.hand;sample.gun=s.gun;sample.ordinary=s.visible;sample.carried.reset();
    sample.cancel=sample.cancel||s.cancel;sample.reloadBusy=s.reloadBusy;
    sample.triggerNeutral=ValidInput(s.input)&&s.input.generation==s.hand.sequence&&
        (s.input.hands[0].active&Trigger)&&(s.input.hands[1].active&Trigger)&&
        s.input.hands[0].trigger<=.1f&&s.input.hands[1].trigger<=.1f;
    const bool fresh=ValidBodyAnchors(config_)&&ValidInput(s.input)&&s.input.generation==s.hand.sequence&&
        s.input.spaceGeneration==s.owner.space&&s.hand.nowNs>=lastNow_;
    const auto read=ReadBodyInventory(memory,s.owner,fresh);++readStatuses_[unsigned(read.status)];
    if(!read.snapshot||!Observe(*read.snapshot,s)||sequence_==UINT64_MAX){display_.reset();heldApproach_.Reset();heldRelease_.Reset();sample.cancel=true;return holster.Tick(policy_,sample,hands,intent);}
    const auto selected=std::find_if(life_.begin(),life_.end(),[&](const auto& item){return item.native.weapon==s.owner.weapon;});
    if(selected==life_.end()){display_.reset();heldApproach_.Reset();heldRelease_.Reset();sample.cancel=true;return holster.Tick(policy_,sample,hands,intent);}
    ObserveAutomaticSelection(selected->native);
    const auto& native=*read.snapshot;
    sample.carried=BodyCarriedIdentity{native.inventory,native.switching,native.count,native.items};
    const bool newInput=s.input.generation!=lastHolsterInput_;
    const bool available=(s.input.hands[1].active&Squeeze)!=0;
    const bool wasArmed=armed_;
    const bool pressed=newInput&&available&&wasArmed&&s.input.hands[1].squeeze>=.75f;
    const auto phase=holster.Phase();
    const auto point=BodyAnchorHandPose(s.input,InteractionHand::Right);
    std::optional<BodySlotAssignment> observedContact;
    if(point)
        for(const auto& anchor:config_.shoulders)if(BodyAnchorContains(anchor,*point)){
            const auto found=std::find_if(slots_.begin(),slots_.end(),[&](const auto& slot){return slot.slot==anchor.slot;});
            if(found!=slots_.end())observedContact=*found;break;}
    // A fresh press may precede reaching either shoulder. Own-slot contact
    // stows the held gun; other-slot contact passes current evidence into the
    // existing ordinary native selector without creating a controller edge.
    const auto currentRight=hands.Current(InteractionHand::Right);
    const bool approachEligible=phase==BodyHolsterPhase::Held&&!sample.cancel&&!s.reloadBusy&&
        point&&Visible(s)&&!hands.Current(InteractionHand::Left)&&currentRight&&
        currentRight->token.kind==HandClaimKind::GunHold&&currentRight->token.owner==s.hand.owner&&
        currentRight->token.item==s.gun&&currentRight->inputSequence==s.hand.sequence&&currentRight->deadlineNs>s.hand.nowNs;
    const bool emptyApproachEligible=phase==BodyHolsterPhase::Empty&&!sample.cancel&&!s.reloadBusy&&
        point&&!hands.Current(InteractionHand::Left)&&!currentRight;
    const bool approach=heldApproach_.Update(s.hand,s.gun,revision_,currentRight?currentRight->token.id:0,pressed,
        available&&s.input.hands[1].squeeze>.35f,approachEligible||emptyApproachEligible,
        observedContact.has_value(),
        emptyApproachEligible?BodyGripApproachMode::Empty:BodyGripApproachMode::Held);
    // Actual neutral squeeze at a freshly contacted own slot can stow a gun
    // already held continuously. No lost pose/component or old contact becomes
    // a release; the current GunHold, slot, item and owner remain prerequisites.
    const auto release=heldRelease_.Update(s.hand,s.gun,revision_,currentRight?currentRight->token.id:0,
        available&&s.input.hands[1].squeeze>=.75f,available&&s.input.hands[1].squeeze<=.35f,
        available&&approachEligible&&sample.triggerNeutral,
        observedContact&&observedContact->item==selected->key?observedContact:std::nullopt);
    const auto contact=release?release:(pressed||approach)?observedContact:std::nullopt;
    Gesture gesture;gesture.input=s.input.generation;gesture.space=s.owner.space;
    gesture.observed=s.hand.observedNs;gesture.deadline=s.hand.deadlineNs;gesture.now=s.hand.nowNs;
    gesture.weapon=s.owner.weapon;gesture.phase=unsigned(phase);gesture.squeeze=s.input.hands[1].squeeze;
    gesture.flags=(available?1u:0u)|(wasArmed?2u:0u)|(pressed?4u:0u)|(point?8u:0u)|
        (s.hand.focused?16u:0u)|(s.hand.tracked[1]?32u:0u)|(s.reloadBusy?64u:0u)|(sample.cancel?128u:0u)|(approach?256u:0u)|(release?512u:0u);
    if(point)for(unsigned n=0;n<3;++n)gesture.point[n]=point->values[3][n];
    if(observedContact){gesture.slot=observedContact->slot;gesture.contactWeapon=unsigned(observedContact->item.id);}
    if(const auto claim=hands.Current(InteractionHand::Left))gesture.left=1u+unsigned(claim->token.kind);
    if(const auto claim=hands.Current(InteractionHand::Right))gesture.right=1u+unsigned(claim->token.kind);
    const auto record=[&](const BodyHolsterResult& out){if(newInput){auto row=gesture;row.nextPhase=unsigned(holster.Phase());
        row.request=out.inventory.request?out.inventory.request->id:0;row.reason=unsigned(out.inventory.reason);row.evaluation=unsigned(out.inventoryEvaluation);
        row.operation=unsigned(sample.body.intent.operation);GestureRow(row);}};
    // Existing held-gun shoulder switching remains the ordinary native path.
    // A consumed approach carries only this gather's exact contact and claim;
    // TickDraw must reread and validate them before using the native selector.
    if(holster.Phase()==BodyHolsterPhase::Held&&contact&&contact->item!=selected->key){
        if(automaticPending_){automaticPending_=false;++automaticSuperseded_;}
        const auto proof=approach&&approachEligible&&sample.triggerNeutral&&currentRight?
            std::optional(DrawApproach{*contact,currentRight->token,revision_,s.hand.sequence,s.hand.observedNs,
                std::min(s.hand.deadlineNs,currentRight->deadlineNs)}):std::nullopt;
        const auto out=ordinary(proof);record(out);return out;}
    lastNow_=s.hand.nowNs;lastHolsterInput_=s.input.generation;
    if(!available)armed_=false;else if(s.input.hands[1].squeeze<=.35f)armed_=true;else if(pressed)armed_=false;
    sample.body.inventory={owner_,revision_,++sequence_,s.hand.nowNs,items_,selected->key};
    sample.body.nowNs=s.hand.nowNs;sample.body.focused=s.hand.focused;sample.body.handTracked=s.hand.tracked[1];
    if(contact&&intent<UINT64_MAX){
        const auto operation=holster.Phase()==BodyHolsterPhase::Held?BodyInventoryOperation::Holster:
            holster.Phase()==BodyHolsterPhase::Empty?BodyInventoryOperation::Draw:BodyInventoryOperation::None;
        if(operation!=BodyInventoryOperation::None)sample.body.intent={++intent,operation,contact->slot,contact->item};
    }
    bool automaticIntent=false;
    if(automaticPending_&&automaticNeutral_&&newInput&&phase==BodyHolsterPhase::Held&&
        sample.body.intent.operation==BodyInventoryOperation::None&&!sample.cancel&&!sample.reloadBusy&&Visible(s)&&
        s.owner.player==automaticActor_.player&&s.owner.soldier==automaticActor_.soldier&&s.owner.weak==automaticActor_.weak&&automaticFoot_&&
        std::all_of(s.input.hands.begin(),s.input.hands.end(),[](const auto& hand){return hand.held==0&&hand.trigger<=.1f&&hand.squeeze<=.35f;})){
        const auto assigned=AssignedSlot(s.owner.weapon);
        if(assigned&&assigned->item==selected->key&&intent<UINT64_MAX){
            sample.body.intent={++intent,BodyInventoryOperation::Holster,assigned->slot,assigned->item};automaticIntent=true;}
    }
    auto out=holster.Tick(policy_,sample,hands,intent);
    if(out.inventoryEvaluation==BodyInventoryEvaluation::Evaluated)slots_=out.inventory.slots;
    if(newInput)automaticNeutral_=sample.body.intent.operation==BodyInventoryOperation::None;
    if(out.inventory.request&&automaticPending_){automaticPending_=false;
        if(automaticIntent)++automaticRequests_;else ++automaticSuperseded_;}
    if(out.inventory.request){++requests_;EventRow(1,s,out.inventory.request->id,unsigned(out.inventory.request->item.id));}
    if(out.inventory.committedRequest){++commits_;EventRow(2,s,out.inventory.committedRequest,unsigned(selected->key.id));}
    if(out.inventory.cancelledRequest){++cancels_;EventRow(3,s,out.inventory.cancelledRequest);}
    if(out.inventoryEvaluation==BodyInventoryEvaluation::Evaluated&&!sample.cancel&&!sample.reloadBusy&&(holster.Phase()==BodyHolsterPhase::Held||holster.Phase()==BodyHolsterPhase::Empty))
        PublishDisplay(native,s,selected->key);else display_.reset();
    record(out);return out;
}
void Bc2BodyInventory::GestureRow(const Gesture& row)noexcept {
    if(row.input==gestureInput_&&row.space==gestureSpace_)return;
    gestureInput_=row.input;gestureSpace_=row.space;
    if(lastGesture_){const auto& old=*lastGesture_;
        if(old.space==row.space&&old.weapon==row.weapon&&old.slot==row.slot&&old.contactWeapon==row.contactWeapon&&
            old.phase==row.phase&&old.nextPhase==row.nextPhase&&old.flags==row.flags&&old.left==row.left&&old.right==row.right&&
            old.operation==row.operation&&old.request==row.request&&old.reason==row.reason)return;}
    lastGesture_=row;gestures_[gestureNext_]=row;gestureNext_=(gestureNext_+1)%unsigned(gestures_.size());
    if(gestureCount_<gestures_.size())++gestureCount_;else ++gestureDropped_;
}
void Bc2BodyInventory::EventRow(unsigned kind,const BodyDrawSample& s,std::uint64_t request,unsigned to,unsigned action)noexcept {
    if(eventCount_==events_.size()){++eventDropped_;return;}events_[eventCount_++]={kind,request,s.input.generation,s.hand.nowNs,s.owner.weapon,to,action};
}
void Bc2BodyInventory::Report(std::ostream& out)const {
    out<<",\"body_inventory\":{\"enabled\":"<<(enabled_?"true":"false")<<",\"requests\":"<<requests_<<",\"committed\":"<<commits_
       <<",\"cancelled\":"<<cancels_<<",\"blocked\":"<<blocked_<<",\"unavailable\":"<<unavailable_<<",\"empty_hands_enabled\":false"
       <<",\"automatic_stow\":{\"enabled\":"<<(automaticEnabled_?"true":"false")<<",\"pending\":"<<(automaticPending_?"true":"false")
       <<",\"entries\":"<<automaticEntries_<<",\"requests\":"<<automaticRequests_<<",\"unsupported\":"<<automaticUnsupported_<<",\"superseded\":"<<automaticSuperseded_<<"},\"read_status\":[";
    for(unsigned n=0;n<readStatuses_.size();++n){if(n)out<<',';out<<readStatuses_[n];}
    out<<"],\"event_kind4\":\"own_slot_stow_unavailable\",\"event_kind4_action\":\"contact_slot\",\"events_dropped\":"<<eventDropped_<<",\"events\":[";
    for(unsigned n=0;n<eventCount_;++n){if(n)out<<',';const auto& e=events_[n];out<<"{\"kind\":"<<e.kind<<",\"request\":"<<e.request<<",\"input\":"<<e.input<<",\"now_ns\":"<<e.now<<",\"from\":"<<e.from<<",\"to\":"<<e.to<<",\"action\":"<<e.action<<'}';}
    out<<"],\"gesture_flags\":{\"available\":1,\"armed\":2,\"pressed\":4,\"point_valid\":8,\"focused\":16,\"right_tracked\":32,\"reload_busy\":64,\"cancelled\":128,\"held_approach\":256,\"grip_release\":512},"
       <<"\"gesture_claim_encoding\":\"0 none; otherwise HandClaimKind + 1\",\"gestures_dropped\":"<<gestureDropped_<<",\"gestures\":[";
    for(unsigned n=0;n<gestureCount_;++n){if(n)out<<',';const auto& e=gestures_[(gestureCount_==gestures_.size()?gestureNext_+n:n)%gestures_.size()];
        out<<"{\"input\":"<<e.input<<",\"space\":"<<e.space<<",\"observed_ns\":"<<e.observed<<",\"deadline_ns\":"<<e.deadline<<",\"now_ns\":"<<e.now
           <<",\"weapon\":"<<e.weapon<<",\"slot\":"<<e.slot<<",\"contact_weapon\":"<<e.contactWeapon<<",\"phase\":"<<e.phase<<",\"next_phase\":"<<e.nextPhase
           <<",\"flags\":"<<e.flags<<",\"left_claim\":"<<e.left<<",\"right_claim\":"<<e.right<<",\"operation\":"<<e.operation<<",\"reason\":"<<e.reason
           <<",\"inventory_evaluation\":"<<e.evaluation<<",\"request\":"<<e.request<<",\"squeeze\":"<<e.squeeze<<",\"point\":["<<e.point[0]<<','<<e.point[1]<<','<<e.point[2]<<"]}";}
    out<<"]}";
}
}




