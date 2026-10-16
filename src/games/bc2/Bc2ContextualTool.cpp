#include "Bc2ContextualTool.h"
#include <algorithm>
#include <cstring>

namespace fvr::bc2 {namespace {
struct Reader {
    const WeaponModeMemory& m;const ContextualToolBinding& binding;
    bool Read(std::uint64_t at,void* p,std::size_t n)const noexcept {
        return at>=0x10000&&n&&n<=4096&&at+n<=UINT32_MAX&&m.read&&m.read(m.context,unsigned(at),p,n);
    }
    template<class T>bool Get(std::uint64_t at,T& out)const noexcept{return Read(at,&out,sizeof(out));}
    bool Image(std::uint64_t at,std::size_t n)const noexcept {
        return binding.imageBase&&at>=binding.imageBase&&at+n<=std::uint64_t(binding.imageBase)+binding.imageSize;
    }
    bool Name(unsigned at,const char* expected)const noexcept {
        std::array<char,96> text{};const auto n=std::strlen(expected)+1;
        return n<=text.size()&&Image(at,n)&&Read(at,text.data(),n)&&!std::memcmp(text.data(),expected,n);
    }
    bool Field(unsigned object,const char* type,unsigned size,const char* name,unsigned offset,unsigned* enumInfo=nullptr)const noexcept {
        unsigned table=0,getter=0,info=0,meta=0,fields=0;std::array<unsigned char,6> code{};std::array<unsigned,7> words{};
        if(!m.type||!m.type(m.context,object,type)||!Get(object,table)||!Image(table,12)||!Get(std::uint64_t(table)+8,getter)||
           !Image(getter,6)||!Read(getter,code.data(),6)||code[0]!=0xb8||code[5]!=0xc3)return false;
        std::memcpy(&info,code.data()+1,4);
        if(!Image(info,40)||!Get(std::uint64_t(info)+4,meta)||!Image(meta,28)||!Read(meta,words.data(),28)||
           !Name(words[0],type)||(words[1]&65535)!=0x35||(size&&(words[1]>>16)!=size)||
           (words[1]>>16)<=offset||(words[1]>>16)>4096||!Get(std::uint64_t(info)+36,fields))return false;
        const unsigned count=(words[3]>>8)&255;if(!count||count>64||!Image(fields,count*24))return false;
        unsigned matches=0;
        for(unsigned n=0;n<count;++n){std::array<unsigned,6> f{};
            if(!Read(std::uint64_t(fields)+n*24,f.data(),24))return false;
            if(Name(f[0],name)){if(++matches!=1||f[4]!=offset)return false;if(enumInfo)*enumInfo=f[2];}}
        return matches==1;
    }
    bool ToolClass(unsigned data)const noexcept {
        unsigned info=0,meta=0;std::array<unsigned,7> words{};
        if(!Field(data,"SoldierWeaponData",0x130,"WeaponClass",0x84,&info)||!Image(info,8)||
           !Get(std::uint64_t(info)+4,meta)||!Image(meta,28)||!Read(meta,words.data(),28)||
           !Name(words[0],"WeaponClassEnum")||words[1]!=(4u<<16|0x179))return false;
        const unsigned count=(words[3]>>8)&255;if(!count||count>64||!Image(words[6],count*24))return false;
        unsigned matches=0;
        for(unsigned n=0;n<count;++n){std::array<unsigned,6> f{};
            if(!Read(std::uint64_t(words[6])+n*24,f.data(),24))return false;
            if(Name(f[0],"wcLaserDesignator")){if(++matches!=1||f[4]!=12)return false;}}
        return matches==1;
    }
    bool Data(const BodyNativeItem& item,ContextualToolData& out,bool tool)const noexcept {
        out={};out.item=item;unsigned path=0,booleanInfo=0,meta=0;std::uint8_t fireOnDeploy=1;std::array<unsigned,7> words{};
        if(!item.weapon||!item.data||(tool?(item.category!=12||item.slot!=4||!ToolClass(item.data)):!BodyLongGunCategory(item.category))||
           !Field(item.data,"SoldierWeaponData",0x130,"WeaponAssetName",0x40)||
           !Field(item.data,"SoldierWeaponData",0x130,"WeaponFiring",0x98)||
           !Get(std::uint64_t(item.data)+0x40,path)||!Read(path,out.asset.data(),out.asset.size())||
           !Get(std::uint64_t(item.data)+0x98,out.firing)||
           !Field(out.firing,"WeaponFiringData",0,"FireOnDeploy",0x48,&booleanInfo)||
           !Image(booleanInfo,8)||!Get(std::uint64_t(booleanInfo)+4,meta)||!Image(meta,28)||!Read(meta,words.data(),28)||
           !Name(words[0],"Boolean")||(words[1]>>16)!=1||
           !Get(std::uint64_t(out.firing)+0x48,fireOnDeploy)||fireOnDeploy)return false;
        const auto end=std::find(out.asset.begin(),out.asset.end(),'\0');
        if(end==out.asset.begin()||end==out.asset.end())return false;
        for(auto p=out.asset.begin();p!=end;++p)if(static_cast<unsigned char>(*p)<32||static_cast<unsigned char>(*p)>126)return false;
        // Keep the complete native asset identity; no display-name matching or
        // assumed firearm persistence is used to manufacture a tool category.
        std::fill(end,out.asset.end(),'\0');return true;
    }
};
const BodyNativeRoute* Route(const BodyNativeInventory& i,unsigned action)noexcept {
    if(i.count>i.items.size()||i.routeCount>i.routes.size()||i.selected>=i.count)return nullptr;
    const BodyNativeRoute* found=nullptr;
    for(unsigned n=0;n<i.routeCount;++n){const auto& r=i.routes[n];if(r.from!=i.selected||r.action!=action)continue;
        if(found||!r.count||r.count>r.targets.size())return nullptr;found=&r;}
    return found;
}
bool SelectedTool(const ContextualToolObservation& o)noexcept {
    const auto& i=o.inventory;return i.count<=i.items.size()&&i.selected<i.count&&i.selected==4&&
        i.items[i.selected]==o.tool.item&&o.tool.item.category==12&&i.owner.weapon==o.tool.item.weapon;
}
}
std::optional<ContextualToolBinding> VerifyContextualToolBinding(const WeaponModeMemory& m,unsigned base,unsigned size)noexcept {
    // Exact installed executable, complete selector body. This proves current
    // slot+14c, last-primary+154, native eligibility callback and edge consumer.
    if(base!=0x400000||size!=0x1913000)return {};
    ContextualToolBinding out{base,size,false};Reader r{m,out};std::array<std::byte,433> code{};
    if(!r.Read(std::uint64_t(base)+0x2ef590,code.data(),code.size()))return {};
    std::uint64_t hash=14695981039346656037ull;for(auto b:code){hash^=std::to_integer<unsigned char>(b);hash*=1099511628211ull;}
    if(hash!=0x0071c4bf9465827eull)return {};
    for(const auto [action,name]:{std::pair{14u,"EiaZoom"},std::pair{34u,"EiaStaticGadget"},std::pair{7u,"EiaSwitchPrimaryWeapon"}}){
        std::array<unsigned,6> record{};
        if(!r.Read(std::uint64_t(base)+0x1827c58+action*24,record.data(),24)||record[4]!=action||!r.Name(record[0],name))return {};
    }
    out.verified=true;return out;
}
std::optional<ContextualToolObservation> ReadContextualTool(const WeaponModeMemory& m,const ContextualToolBinding& binding,const ReloadStateOwner& owner)noexcept {
    if(!binding.verified)return {};
    Reader r{m,binding};unsigned attached=1;
    if(!r.Get(std::uint64_t(owner.player)+0xc60,attached)||attached)return {};
    const auto first=ReadBodyInventory(m,owner,true);if(!first.snapshot)return {};
    const auto& i=*first.snapshot;if(i.count<=4||i.items[4].category!=12)return {};
    ContextualToolObservation out;out.inventory=i;
    if(!r.Data(i.items[4],out.tool,true)||!r.Get(std::uint64_t(i.inventory)+0x154,out.lastPrimary)||out.lastPrimary>1||
       out.lastPrimary>=i.count||!r.Data(i.items[out.lastPrimary],out.primary,false))return {};
    const auto second=ReadBodyInventory(m,owner,true);ContextualToolData again,primaryAgain;unsigned primary=UINT32_MAX;
    if(!second.snapshot||*second.snapshot!=i||!r.Data(second.snapshot->items[4],again,true)||again!=out.tool||
       !r.Get(std::uint64_t(i.inventory)+0x154,primary)||primary!=out.lastPrimary||
       !r.Data(second.snapshot->items[primary],primaryAgain,false)||primaryAgain!=out.primary||
       !r.Get(std::uint64_t(owner.player)+0xc60,attached)||attached)return {};
    return out;
}
std::optional<WeaponModeCommand> ResolveStaticToolEquip(const ContextualToolObservation& o)noexcept {
    const auto& i=o.inventory;const auto* route=Route(i,34);
    if(!route||i.selected>1||!BodyLongGunCategory(i.items[i.selected].category)||i.items[i.selected].weapon!=i.owner.weapon||
       o.tool.item.slot!=4||o.tool.item.category!=12||i.count<=4||i.items[4]!=o.tool.item||!o.tool.item.weapon||route->targets[0]!=4)return {};
    for(unsigned n=0;n<route->count;++n)if(route->targets[n]>10)return {};
    return WeaponModeCommand{34,o.tool.item.weapon,4,i.inventory,o.tool.item.persistence};
}
std::optional<WeaponModeCommand> ResolveStaticToolReturn(const ContextualToolObservation& o,const BodyNativeItem& original)noexcept {
    const auto& i=o.inventory;const auto* route=Route(i,7);
    if(!SelectedTool(o)||!route||!original.weapon||original.slot>1||o.lastPrimary!=original.slot||
       i.items[original.slot]!=original||o.primary.item!=original||!BodyLongGunCategory(original.category)||route->targets[0]!=9)return {};
    for(unsigned n=0;n<route->count;++n)if(route->targets[n]>10)return {};
    // Native may reject lastPrimary and choose another authored fallback. Only
    // a later exact selected-item observation can acknowledge this return.
    return WeaponModeCommand{7,original.weapon,original.slot,i.inventory,original.persistence};
}
bool ContextualToolSecondary::Update(const ContextualToolObservation& o,const interaction::InputFrame& in,
    const interaction::HandInteractionSample& s,const interaction::HandClaimToken& hold,const interaction::HandInteraction& hands)noexcept {
    using namespace interaction;const auto current=hands.Current(InteractionHand::Right);
    const auto deny=[&](){armed_=false;identity_.reset();return false;};
    const auto& owner=o.inventory.owner;const auto& left=in.hands[0];
    if(!SelectedTool(o)||!ValidInput(in)||!in.focused||!in.headValid||!s.focused||!s.tracked[0]||!s.tracked[1]||
       !left.gripTracked||!(left.active&Trigger)||!in.hands[1].gripTracked||in.generation!=s.sequence||in.spaceGeneration!=s.owner.space||
       s.owner.actor!=((std::uint64_t(owner.weak)<<32)|owner.soldier)||s.owner.actorGeneration!=owner.actorGeneration||s.owner.space!=owner.space||
       s.observedNs<=0||s.observedNs>s.nowNs||s.deadlineNs<=s.nowNs||s.deadlineNs-s.observedNs>100000000||
       !current||current->token!=hold||hold.owner!=s.owner||hold.hand!=InteractionHand::Right||hold.kind!=HandClaimKind::GunHold||
       hold.item.id!=o.tool.item.weapon||!hold.item.generation||current->deadlineNs<=s.nowNs||current->inputSequence!=s.sequence||
       !hands.HasRecordedEvidence(s,InteractionHand::Left)||!hands.HasRecordedEvidence(s,InteractionHand::Right))return deny();
    const Identity identity{owner,o.tool,hold};
    if(!identity_||*identity_!=identity){
        // First observation of the selected tool/claim is a boundary, never a
        // neutral rearm inherited from before selection or a failed packet.
        if(s.sequence<=lastSequence_)return deny();
        armed_=false;identity_=identity;lastSequence_=s.sequence;lastObserved_=s.observedNs;
        lastDeadline_=s.deadlineNs;lastPredicted_=in.predictedNs;lastTrigger_=left.trigger;return false;
    }
    if(s.sequence<lastSequence_||s.observedNs<lastObserved_||
       (s.sequence==lastSequence_&&(s.observedNs!=lastObserved_||s.deadlineNs!=lastDeadline_||
        in.predictedNs!=lastPredicted_||left.trigger!=lastTrigger_)))return deny();
    if(s.sequence>lastSequence_){
        if(lastObserved_&&s.observedNs-lastObserved_>100000000)armed_=false;
        lastSequence_=s.sequence;lastObserved_=s.observedNs;lastDeadline_=s.deadlineNs;lastPredicted_=in.predictedNs;lastTrigger_=left.trigger;
        if(left.trigger<=.1f)armed_=true;
    }
    return armed_&&left.trigger>=.75f;
}
}
