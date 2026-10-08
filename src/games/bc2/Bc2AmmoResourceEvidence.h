#pragma once
#include "Bc2ReloadHold.h"
#include "Bc2NativeAmmoRefill.h"
#include "fvr/interaction/AmmunitionLedger.h"

namespace fvr::bc2 {
// Values minted at the actual native helper boundary, after full code/owner
// admission and exact whole-object/context postchecks. Not a client count read.
struct AmmoResourceNativeCall {
    ReloadHoldIdentity identity{};
    std::uint64_t command=0,invocation=0;
    std::int64_t beginNs=0,endNs=0;
    interaction::AmmunitionCounts before{},after{};
    bool bindingVerified=false,exactPostcondition=false,contextUnchanged=false;
    bool operator==(const AmmoResourceNativeCall&)const=default;
};
// Each row is from that branch's own completed original Update. Server links
// belong only to branch 2; reading sibling state does not produce another row.
struct AmmoResourceOwnUpdate {
    ReloadStateOwner beforeOwner{},afterOwner{};
    std::uint32_t firing=0,serverPlayer=0,serverSoldier=0,serverItem=0;
    std::uint64_t invocation=0;
    std::int64_t beginNs=0,endNs=0;
    unsigned branch=3,depth=0;
    int loadedBefore=0,reserveBefore=0,loadedAfter=0,reserveAfter=0;
    unsigned current=0,next=0;float timer=0;
    bool finished=false,identityRetained=false,neutralContext=false,animationHold=false;
    bool operator==(const AmmoResourceOwnUpdate&)const=default;
};
// BC2-specific completion observer for the engine-independent ledger. The
// observer authorizes no native call and cannot turn a function return into an
// ammunition acknowledgement without all three genuine completed Updates.
class AmmoResourceCompletion {
public:
    bool Begin(const interaction::AmmunitionCommand& command,const ReloadHoldIdentity& identity)noexcept {
        if(active_||!ValidCommand(command)||!ValidIdentity(identity)||
           command.context.resource.actor!=identity.owner.soldier||
           command.context.resource.actorGeneration!=identity.owner.actorGeneration||
           command.context.resource.weapon!=identity.owner.weapon||
           command.context.equipGeneration!=identity.owner.equipGeneration||command.context.space!=identity.owner.space)return false;
        command_=command;identity_=identity;active_=true;failed_=false;server_=false;mask_=0;seen_={};invocations_={};call_.reset();return true;
    }
    bool Call(const AmmoResourceNativeCall& c)noexcept {
        if(!active_||failed_||c.command!=command_.id)return false;
        if(call_)return c==*call_?true:Fail();
        if(c.identity!=identity_||!c.invocation||
           !c.bindingVerified||!c.exactPostcondition||!c.contextUnchanged||
           c.beginNs<command_.requestedNs||c.beginNs>=command_.admissionDeadlineNs||c.endNs<c.beginNs||
           c.endNs>=command_.deadlineNs||c.before!=command_.before||c.after!=command_.after)return Fail();
        call_=c;return true;
    }
    bool Observe(const AmmoResourceOwnUpdate& r)noexcept {
        if(!active_||failed_||!call_||r.branch>=3||r.firing!=identity_.firing[r.branch]||
           r.endNs<call_->endNs)return false;
        if(r.beforeOwner!=identity_.owner||r.afterOwner!=identity_.owner||!r.finished||!r.identityRetained||
           !r.invocation||r.depth!=1||r.beginNs<=0||r.endNs<r.beginNs||r.endNs>=command_.deadlineNs||
           !r.neutralContext||r.animationHold)return Fail();
        if(r.branch==2&&(r.serverPlayer!=identity_.serverPlayer||r.serverSoldier!=identity_.serverSoldier||r.serverItem!=identity_.serverItem))return Fail();
        if(r.branch==2&&r.invocation==call_->invocation){
            if(r.beginNs>call_->beginNs||r.endNs<call_->endNs||
               r.loadedBefore!=command_.before.loaded||r.reserveBefore!=command_.before.reserve||
               r.loadedAfter!=command_.after.loaded||r.reserveAfter!=command_.after.reserve||
               r.current!=2||r.next!=2||r.timer!=0)return Fail();
            server_=true;
        }
        // The retained helper-owning Update can arrive after a later server
        // observation. Its provenance still matters; it must not renew counts.
        if(r.endNs<seen_[r.branch])return false;
        const bool expected=r.loadedAfter==command_.after.loaded&&r.reserveAfter==command_.after.reserve;
        const bool baseline=r.loadedAfter==command_.before.loaded&&r.reserveAfter==command_.before.reserve;
        if(!expected){if(!baseline||(mask_&(1u<<r.branch)))return Fail();return false;}
        if(r.current!=2||r.next!=2||r.timer!=0)return Fail();
        for(unsigned n=0;n<3;++n)if(n!=r.branch&&invocations_[n]==r.invocation)return Fail();
        seen_[r.branch]=r.endNs;invocations_[r.branch]=r.invocation;mask_|=1u<<r.branch;return true;
    }
    std::optional<interaction::AmmunitionReceipt> Receipt(std::int64_t now)const noexcept {
        if(!active_||failed_||!call_||!server_||mask_!=7||now>=command_.deadlineNs)return {};
        for(auto t:seen_)if(t<=0||now<t||now-t>=100000000)return {};
        return interaction::AmmunitionReceipt{command_,call_->invocation,call_->beginNs,
            *std::max_element(seen_.begin(),seen_.end()),command_.before,command_.after,true,true};
    }
    bool Failed()const noexcept{return failed_;}
    unsigned Mask()const noexcept{return mask_;}
private:
    static bool ValidIdentity(const ReloadHoldIdentity& i)noexcept {
        const auto& o=i.owner;
        if(o.player<0x10000||o.soldier<0x10000||o.weak<0x10000||o.weapon<0x10000||!o.actorGeneration||!o.equipGeneration||!o.space||
           i.serverPlayer<0x10000||i.serverSoldier<0x10000||i.serverItem<0x10000)return false;
        for(unsigned n=0;n<3;++n){if(i.firing[n]<0x10000)return false;for(unsigned p=0;p<n;++p)if(i.firing[n]==i.firing[p])return false;}return true;
    }
    static bool ValidCommand(const interaction::AmmunitionCommand& c)noexcept {
        using namespace interaction;
        const auto& b=c.before;const auto& a=c.after;
        if(!c.id||!c.sourceSequence||!c.context.resource.weaponGeneration||c.requestedNs<=0||c.admissionDeadlineNs<=c.requestedNs||
           c.admissionDeadlineNs-c.requestedNs>100000000||c.deadlineNs<c.admissionDeadlineNs||
           c.deadlineNs-c.requestedNs>2000000000ll||a.capacity!=b.capacity||b.capacity<=0||b.capacity>1000000||
           b.loaded<0||b.loaded>b.capacity||b.reserve<0||b.reserve>1000000||c.mutationRequired!=(b!=a))return false;
        switch(c.operation){
        case AmmunitionOperation::RemoveMagazine:return !c.original&&a.loaded==0&&a.reserve==b.reserve;
        case AmmunitionOperation::ReturnMagazine:return c.original&&c.original->id&&c.original->owner==c.context.resource&&
            c.original->capacity==b.capacity&&c.original->rounds>=0&&c.original->rounds<=b.capacity&&
            b.loaded==0&&a.loaded==c.original->rounds&&a.reserve==b.reserve;
        case AmmunitionOperation::RefillMagazine:case AmmunitionOperation::InsertRound:{
            if(c.original||(c.operation==AmmunitionOperation::RefillMagazine&&b.loaded!=0))return false;
            const auto p=PlanNativeAmmoRefill(b.loaded,b.reserve,b.capacity,c.operation==AmmunitionOperation::RefillMagazine?1:0);
            return p&&a.loaded==p->expectedLoaded&&a.reserve==p->expectedReserve;
        }
        default:return false;
        }
    }
    bool Fail()noexcept{failed_=true;return false;}
    interaction::AmmunitionCommand command_{};ReloadHoldIdentity identity_{};
    std::optional<AmmoResourceNativeCall> call_;
    std::array<std::int64_t,3> seen_{};std::array<std::uint64_t,3> invocations_{};
    unsigned mask_=0;bool active_=false,failed_=false,server_=false;
};
}
