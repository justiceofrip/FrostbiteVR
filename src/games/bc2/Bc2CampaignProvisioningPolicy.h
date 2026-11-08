#pragma once
#include "Bc2CampaignProvisioningCatalog.h"
#include "Bc2CampaignProvisioningCode.h"
#include "Bc2CampaignProvisioningAssignment.h"
#include <algorithm>
#include <cmath>
#include <string_view>

namespace fvr::bc2::provisioning_candidate {
// Candidate test setup only. No game hook is installed by this component.
// Host serializes every method on its native seat callback, except requests
// which must arrive through the host's synchronized control queue.
struct Resource {
    std::uint32_t asset=0,data=0,firing=0,primary=0;
    std::array<char,64> name{};std::array<char,192> path{};
    std::array<unsigned char,0x8c> logic{};std::array<unsigned char,0x24> ammo{};
    bool operator==(const Resource&)const=default;
};
struct Selector {
    std::string_view name,path;
    std::int32_t capacity=0,fireLogic=0,reloadType=0;
    float boltDelay=0,boltTime=0;bool holdFire=false,holdZoom=false;
};
struct Owner {
    std::uint64_t session=0,actorGeneration=0,space=0;
    std::uint32_t player=0,actor=0,inventory=0,slot=0,item=0,data=0;
    std::array<std::uint32_t,3> firing{}; // client +3c, +40, exact server item
    bool operator==(const Owner&)const=default;
};
struct Boundary {
    Owner owner{};
    std::uint64_t sequence=0;
    std::int64_t nowNs=0,ownerObservedNs=0;
    std::uint32_t thread=0,returnAddress=0,seat=0,nativeFiringDepth=0,seatDepth=0;
    // Exact original seat-entry or owned 5-byte E9 patch, verified below.
    std::array<unsigned char,5> seatEntryBytes{};
};
struct Host {
    void* context=nullptr;
    // Must call existing ReadReloadServerOwner with a fresh published typed
    // client/server cohort and compare every Owner field. Never return true
    // from actor pointer equality alone. Called immediately before dispatch.
    bool (*owner)(void*,const Owner&,std::int64_t)=nullptr;
    // Test stub or x86 InvokeOriginalAssignment. No request may call directly.
    bool (*assign)(void*,std::uint32_t actor,std::uint32_t data,std::uint32_t slot,std::uint32_t& returnedItem)=nullptr;
};
namespace provision_detail {
template<class T,std::size_t N>T Value(const std::array<unsigned char,N>& b,std::size_t at)noexcept {
    T value{};if(at+sizeof(T)<=N)std::memcpy(&value,b.data()+at,sizeof(T));return value;
}
inline bool TextEqual(const char* actual,std::string_view wanted)noexcept {
    const std::string_view a(actual);if(a.size()!=wanted.size())return false;
    for(std::size_t n=0;n<a.size();++n){const auto lower=[](char c){return c>='A'&&c<='Z'?char(c+32):c;};
        if(lower(a[n])!=lower(wanted[n]))return false;}return true;
}
inline bool ResourceOnce(const Memory& m,std::uint32_t data,std::uint32_t asset,Resource& out)noexcept {
    out={};out.asset=asset;out.data=data;std::uint32_t table=0,name=0,path=0,object=0;
    if(!m.type||!detail::Get(m,data,table)||table!=0x145f098||!m.type(m.context,data,"SoldierWeaponData")||
       !detail::Get(m,std::uint64_t(data)+0xc,name)||!detail::Text(m,name,out.name)||
       !detail::Get(m,std::uint64_t(data)+0x40,path)||!detail::Text(m,path,out.path)||
       !detail::Get(m,std::uint64_t(data)+0x98,out.firing)||!m.type(m.context,out.firing,"WeaponFiringData")||
       !detail::Get(m,std::uint64_t(out.firing)+0x40,out.primary)||!m.type(m.context,out.primary,"FiringFunctionData")||
       !detail::Read(m,std::uint64_t(out.primary)+0xc,out.logic.data(),out.logic.size())||
       !detail::Read(m,std::uint64_t(out.primary)+0x170,out.ammo.data(),out.ammo.size()))return false;
    if(asset){std::array<char,192> assetPath{};
        if(!detail::Get(m,asset,table)||table!=0x1474554||!m.type(m.context,asset,"SoldierWeaponAsset")||
           !detail::Get(m,std::uint64_t(asset)+0x50,object)||object!=data||
           !detail::Get(m,std::uint64_t(asset)+0xc,path)||!detail::Text(m,path,assetPath)||
           !TextEqual(assetPath.data(),out.path.data()))return false;}
    return true;
}
inline bool Code(const Memory& m,const std::array<unsigned char,5>* installed)noexcept {
    std::array<unsigned char,4096> bytes{};
    for(const auto& slice:ProvisionCode){
        if(!detail::Read(m,slice.address,bytes.data(),slice.bytes.size()))return false;
        std::size_t skip=0;
        if(slice.address==0x6b0830&&installed){
            if(std::memcmp(bytes.data(),installed->data(),5))return false;
            if((*installed)[0]!=0xe9&&std::memcmp(installed->data(),SeatEntryBytes,5))return false;skip=5;}
        if(std::memcmp(bytes.data()+skip,slice.bytes.data()+skip,slice.bytes.size()-skip))return false;
    }return true;
}
inline bool SameActor(const Owner& a,const Owner& b)noexcept {
    return a.session==b.session&&a.actorGeneration==b.actorGeneration&&a.space==b.space&&
        a.player==b.player&&a.actor==b.actor&&a.inventory==b.inventory&&a.slot==b.slot;
}
inline bool Seat(const Memory& m,const Boundary& b)noexcept {
    std::uint32_t table=0,begin=0,end=0,seat=0,player=0,controlled=0,input=0,back=0;
    if(!b.owner.session||!b.owner.actorGeneration||!b.owner.space||b.seat!=0||b.owner.slot>1||
       !detail::Get(m,b.owner.actor,table)||table!=0x1424850||
       !detail::Get(m,std::uint64_t(b.owner.actor)+0x90,begin)||
       !detail::Get(m,std::uint64_t(b.owner.actor)+0x94,end)||end<=begin||end-begin>64*4||(end-begin)%4||
       !detail::Get(m,begin,seat)||!detail::Get(m,std::uint64_t(seat)+0xe8,player)||player!=b.owner.player||
       !detail::Get(m,std::uint64_t(player)+0xc3c,controlled)||controlled!=b.owner.actor||
       !detail::Get(m,std::uint64_t(player)+0xc6c,controlled)||controlled!=b.owner.actor||
       !detail::Get(m,std::uint64_t(b.owner.actor)+0x220,back)||back!=player||
       !detail::Get(m,std::uint64_t(player)+0xc38,input)||input<0x10000)return false;
    return true;
}
inline bool Idle(const Memory& m,const Owner& o)noexcept {
    if(!o.firing[0]||!o.firing[1]||!o.firing[2]||o.firing[0]==o.firing[1]||o.firing[0]==o.firing[2]||o.firing[1]==o.firing[2])return false;
    for(auto f:o.firing){std::uint32_t current=0,next=0;
        if(!detail::Get(m,std::uint64_t(f)+0x3c,current)||current!=2||!detail::Get(m,std::uint64_t(f)+0x44,next)||next!=2)return false;}
    return true;
}
}
inline bool ValidateOriginalProvisioningCode(const Memory& m)noexcept{return provision_detail::Code(m,nullptr);}
inline std::optional<Resource> ReadProvisioningResource(const Memory& m,std::uint32_t data,std::uint32_t asset=0)noexcept {
    Resource a{},b{};if(!provision_detail::ResourceOnce(m,data,asset,a)||!provision_detail::ResourceOnce(m,data,asset,b)||a!=b)return {};return a;
}
inline std::optional<Resource> SelectProvisioningResource(const Memory& m,std::uint32_t data,std::uint32_t asset,const Selector& s)noexcept {
    using provision_detail::Value;const auto r=ReadProvisioningResource(m,data,asset);
    if(!r||!asset||!provision_detail::TextEqual(r->path.data(),s.path)||std::string_view(r->name.data())!=s.name||
       Value<std::int32_t>(r->ammo,0x14)!=s.capacity||Value<std::int32_t>(r->logic,0x18)!=s.fireLogic||
       Value<std::int32_t>(r->logic,0x14)!=s.reloadType||Value<float>(r->logic,0x48)!=s.boltDelay||
       Value<float>(r->logic,0x44)!=s.boltTime||r->logic[0x4d]!=std::uint8_t(s.holdFire)||r->logic[0x4c]!=std::uint8_t(s.holdZoom))return {};
    return r;
}
enum class BoundaryFailure:std::uint8_t {None,ThreadConflict,Owner,FreshOwner,Thread,ServerAge,Actor,Caller,NativeDepth,EntryDepth,Code,Seat,Idle,Count};
enum class ProvisionPhase:std::uint8_t {Preflight,Armed,AwaitingReplication,Equipped,RestoreArmed,AwaitingRestore,Restored,Indeterminate,Cancelled};
struct ProvisionReceipt {
    ProvisionPhase phase=ProvisionPhase::Preflight;
    std::uint32_t nativeCalls=0,returnedItem=0,thread=0;
    std::uint64_t dispatchSequence=0;std::int64_t dispatchNs=0;
    bool serverItemObserved=false,clientServerConverged=false;
};
class WeaponProvisioner {
public:
    // Observe only from the actual original server firing Update boundary with
    // an owner-qualified server actor. This records thread affinity; it never
    // dispatches a grant. Seat preflight requires a preceding observation.
    void ObserveServerUpdate(const Owner& o,std::uint32_t thread,std::uint64_t sequence,std::int64_t now)noexcept {
        if(!thread||!sequence||now<=0)return;
        if(serverThread_&&(serverThread_!=thread||!provision_detail::SameActor(observed_,o))){threadConflict_=true;return;}
        if(sequence<=serverSequence_)return;
        observed_=o;serverThread_=thread;serverSequence_=sequence;serverNs_=now;
    }
    bool Preflight(const Memory& m,const Host& host,const Boundary& b)noexcept {
        if(receipt_.phase!=ProvisionPhase::Preflight||!Valid(m,host,b)||b.sequence<=lastSeat_)return false;
        if(preflightCount_&&(!provision_detail::SameActor(preflightOwner_,b.owner)||b.owner.data!=preflightOwner_.data))return false;
        preflightOwner_=b.owner;lastSeat_=b.sequence;
        // Distinct seat invocations also need distinct actual server Updates.
        if(serverSequence_<=preflightServer_)return false;
        preflightServer_=serverSequence_;++preflightCount_;return true;
    }
    bool Arm(const Memory& m,const Host& host,const Boundary& b,const Resource& target,std::int64_t deadline)noexcept {
        if(receipt_.phase!=ProvisionPhase::Preflight||preflightCount_<2||!Valid(m,host,b)||
           !provision_detail::SameActor(preflightOwner_,b.owner)||b.owner.data!=preflightOwner_.data||
           deadline<=b.nowNs||deadline-b.nowNs>2000000000ll||!target.asset||target.data==b.owner.data)return false;
        const auto current=ReadProvisioningResource(m,b.owner.data),again=ReadProvisioningResource(m,target.data,target.asset);
        if(!current||!again||*again!=target)return false;
        original_=*current;target_=target;originalOwner_=b.owner;expectedOwner_=b.owner;
        deadline_=deadline;receipt_.phase=ProvisionPhase::Armed;return true;
    }
    bool RequestRestore(const Boundary& b,std::int64_t deadline)noexcept {
        if(receipt_.phase!=ProvisionPhase::Equipped||!provision_detail::SameActor(originalOwner_,b.owner)||
           b.owner.data!=target_.data||deadline<=b.nowNs||deadline-b.nowNs>2000000000ll)return false;
        expectedOwner_=b.owner;deadline_=deadline;receipt_.phase=ProvisionPhase::RestoreArmed;return true;
    }
    // Call at hook entry, before original 6b0830. Host ALWAYS calls original,
    // irrespective of this result. One failed/ambiguous invocation never retries.
    bool AtSeatEntry(const Memory& m,const Host& host,const Boundary& b)noexcept {
        const bool restore=receipt_.phase==ProvisionPhase::RestoreArmed;
        if(receipt_.phase!=ProvisionPhase::Armed&&!restore)return false;
        if(b.nowNs>deadline_){receipt_.phase=restore?ProvisionPhase::Equipped:ProvisionPhase::Cancelled;return false;}
        if(!Valid(m,host,b)||b.owner!=expectedOwner_||!host.assign)return false;
        const Resource& desired=restore?original_:target_;
        const auto target=ReadProvisioningResource(m,desired.data,desired.asset);
        const auto old=ReadProvisioningResource(m,b.owner.data,restore?target_.asset:original_.asset);
        if(!target||*target!=desired||!old||*old!=(restore?target_:original_))return false;
        // Disarm BEFORE destructive native dispatch, including reentrancy.
        receipt_.phase=restore?ProvisionPhase::AwaitingRestore:ProvisionPhase::AwaitingReplication;
        receipt_.dispatchSequence=b.sequence;receipt_.dispatchNs=b.nowNs;receipt_.thread=b.thread;
        receipt_.serverItemObserved=false;receipt_.clientServerConverged=false;++receipt_.nativeCalls;
        if(!host.assign(host.context,b.owner.actor,desired.data,b.owner.slot,receipt_.returnedItem)){
            receipt_.phase=ProvisionPhase::Indeterminate;return true;}
        std::uint32_t begin=0,end=0,item=0,data=0,actor=0,selected=0;
        if(!detail::Get(m,std::uint64_t(b.owner.actor)+0x2b8,begin)||!detail::Get(m,std::uint64_t(b.owner.actor)+0x2bc,end)||
           end<=begin||end-begin>256||(end-begin)%4||b.owner.slot>=(end-begin)/4||
           !detail::Get(m,std::uint64_t(begin)+b.owner.slot*4,item)||item!=receipt_.returnedItem||
           !detail::Get(m,std::uint64_t(item)+4,data)||data!=desired.data||
           !detail::Get(m,std::uint64_t(item)+8,actor)||actor!=b.owner.actor||
           !detail::Get(m,std::uint64_t(b.owner.inventory)+0x14c,selected)||selected!=b.owner.slot){
            receipt_.phase=ProvisionPhase::Indeterminate;return true;}
        receipt_.serverItemObserved=true;return true;
    }
    // Fresh canonical all-three client/server mapping after replication. An old
    // published lease, elapsed timeout or matching weapon name is insufficient.
    bool ObserveConvergence(const Memory& m,const Host& host,const Boundary& b)noexcept {
        const bool restore=receipt_.phase==ProvisionPhase::AwaitingRestore;
        if(receipt_.phase!=ProvisionPhase::AwaitingReplication&&!restore)return false;
        const Resource& wanted=restore?original_:target_;
        if(!receipt_.serverItemObserved||!Valid(m,host,b)||b.sequence<=receipt_.dispatchSequence||
           b.ownerObservedNs<=receipt_.dispatchNs||!provision_detail::SameActor(b.owner,originalOwner_)||
           b.owner.data!=wanted.data||b.owner.item!=receipt_.returnedItem)return false;
        const auto actual=ReadProvisioningResource(m,wanted.data,wanted.asset);
        if(!actual||*actual!=wanted)return false;
        receipt_.clientServerConverged=true;receipt_.phase=restore?ProvisionPhase::Restored:ProvisionPhase::Equipped;return true;
    }
    void CancelUnsent()noexcept {if(receipt_.phase==ProvisionPhase::Armed)receipt_.phase=ProvisionPhase::Cancelled;
        else if(receipt_.phase==ProvisionPhase::RestoreArmed)receipt_.phase=ProvisionPhase::Equipped;}
    const ProvisionReceipt& Receipt()const noexcept{return receipt_;}
    const Resource& Original()const noexcept{return original_;}
public:
    BoundaryFailure Diagnose(const Memory& m,const Host& host,const Boundary& b)const noexcept {
        if(threadConflict_)return BoundaryFailure::ThreadConflict;
        if(!host.owner||!host.owner(host.context,b.owner,b.nowNs))return BoundaryFailure::Owner;
        if(!b.sequence||b.nowNs<=0||b.ownerObservedNs<=0||b.nowNs<b.ownerObservedNs||b.nowNs-b.ownerObservedNs>200000000ll)return BoundaryFailure::FreshOwner;
        if(!b.thread||b.thread!=serverThread_)return BoundaryFailure::Thread;
        if(b.nowNs<serverNs_||b.nowNs-serverNs_>200000000ll)return BoundaryFailure::ServerAge;
        if(!provision_detail::SameActor(observed_,b.owner))return BoundaryFailure::Actor;
        if(b.returnAddress!=0x697b44)return BoundaryFailure::Caller;
        if(b.nativeFiringDepth)return BoundaryFailure::NativeDepth;
        if(b.seatDepth!=1)return BoundaryFailure::EntryDepth;
        if(!provision_detail::Code(m,&b.seatEntryBytes))return BoundaryFailure::Code;
        if(!provision_detail::Seat(m,b))return BoundaryFailure::Seat;
        if(!provision_detail::Idle(m,b.owner))return BoundaryFailure::Idle;
        return BoundaryFailure::None;
    }
private:
    bool Valid(const Memory& m,const Host& host,const Boundary& b)const noexcept {
        return Diagnose(m,host,b)==BoundaryFailure::None;
    }
    ProvisionReceipt receipt_{};Resource original_{},target_{};Owner originalOwner_{},expectedOwner_{},preflightOwner_{},observed_{};
    std::uint32_t serverThread_=0,preflightCount_=0;std::uint64_t serverSequence_=0,preflightServer_=0,lastSeat_=0;
    std::int64_t serverNs_=0,deadline_=0;bool threadConflict_=false;
};
// Implemented only for x86. Install no hooks, open no processes, mutate no shared
// resource fields. Called only as Host.assign after all service checks pass.
} // namespace fvr::bc2::provisioning_candidate
