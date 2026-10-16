#include "Bc2ContextualToolLifetime.h"
#include <cstdio>
using namespace fvr::bc2;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(false)
namespace {
ContextualToolObservation Original(){
    ContextualToolObservation o;o.inventory.owner={0x10000,0x11000,0x12000,0x13000,1,2,3};
    auto& i=o.inventory;i.inventory=0x16000;i.switching=0x17000;i.count=5;i.selected=0;
    i.items[0]={0x13000,0x14000,0x15000,0,0};i.items[4]={0x18000,0x19000,0,12,4};
    i.routeCount=1;i.routes[0].action=34;i.routes[0].from=0;i.routes[0].count=1;i.routes[0].targets[0]=4;
    o.tool.item=i.items[4];o.tool.firing=0x20000;o.tool.asset[0]='a';
    o.primary.item=i.items[0];o.primary.firing=0x21000;o.primary.asset[0]='b';return o;
}
ContextualToolObservation Selected(ContextualToolObservation o){
    o.inventory.selected=4;o.inventory.owner.weapon=o.tool.item.weapon;++o.inventory.owner.equipGeneration;return o;
}
int NewOperationCannotReplaceOwned(){
    const auto o=Original();ContextualToolLifetime l;CHECK(l.Begin(17,o));
    CHECK(!l.Begin(18,o)&&l.Operation()==17&&l.Anchor()==o);
    CHECK(!l.ObserveBody(17)&&l.Operation()==17);
    CHECK(l.ObserveBody(0)&&!l.Operation()&&!l.Anchor());CHECK(l.Begin(18,o));
    for(unsigned bad=0;bad<5;++bad){ContextualToolLifetime invalid;auto x=o;
        if(bad==0)x.inventory.selected=4;if(bad==1)x.tool.item.category=0;
        if(bad==2)x.inventory.owner.weapon++;if(bad==3)x.inventory.count=99;
        CHECK(!invalid.Begin(bad==4?0:17,x)&&!invalid.Operation());
    }return 0;
}
int MetadataInvalidationKeepsOriginalOperation(){
    for(unsigned kind=0;kind<5;++kind){const auto original=Original();ContextualToolLifetime l;
        CHECK(l.Begin(29,original));auto current=Selected(original);
        CHECK(l.Observe(current.inventory.owner,current)==ContextualToolSelection::Tool);
        if(kind==0)current.inventory.switching+=4;
        if(kind==1)current.inventory.routes[0].targets[0]=1;
        if(kind==2)current.tool.firing+=4;
        if(kind==3)current.tool.asset[0]='x';
        if(kind==4)current.inventory.routeCount=0;
        CHECK(l.Observe(current.inventory.owner,current)==ContextualToolSelection::Tool);
        CHECK(l.Revoked()&&l.Operation()==29&&l.Anchor()==original&&!l.ConfigurationCurrent(current));
        // A later matching metadata read cannot revive revoked action authority.
        current=Selected(original);CHECK(l.Observe(current.inventory.owner,current)==ContextualToolSelection::Tool);
        CHECK(l.Revoked()&&!l.ConfigurationCurrent(current)&&!l.ObserveBody(29));
    }return 0;
}
int OriginalReturnStillNeedsBodyCompletion(){
    const auto original=Original();ContextualToolLifetime l;CHECK(l.Begin(31,original));
    auto current=Selected(original);current.tool.firing+=4;l.Observe(current.inventory.owner,current);
    CHECK(l.Revoked()&&l.Operation()==31);
    current.inventory.selected=0;current.inventory.owner.weapon=original.inventory.owner.weapon;
    CHECK(l.Observe(current.inventory.owner,current)==ContextualToolSelection::Original);
    CHECK(l.Operation()==31&&l.Revoked()&&!l.ObserveBody(31));
    // Merely observing the original gun does not assert a new paired hide.
    CHECK(l.ObserveBody(0)&&!l.Operation());return 0;
}
int UnknownAndReplacementDoNotInventCompletion(){
    for(unsigned kind=0;kind<5;++kind){const auto original=Original();ContextualToolLifetime l;
        CHECK(l.Begin(37,original));auto current=Selected(original);
        if(kind==0)current.inventory.owner.actorGeneration++;
        if(kind==1)current.inventory.inventory+=4;
        if(kind==2)current.inventory.items[0].persistence+=4;
        if(kind==3)current.inventory.items[4].weapon+=4;
        if(kind==4){current.inventory.selected=1;current.inventory.owner.weapon=0x22000;}
        CHECK(l.Observe(current.inventory.owner,current)==ContextualToolSelection::Unknown);
        CHECK(l.Revoked()&&l.Operation()==37&&l.Anchor()==original&&!l.ObserveBody(37));
        // An actual body-domain replacement retires our bookkeeping without
        // adopting or mutating the other consumer's operation.
        CHECK(l.ObserveBody(38)&&!l.Operation()&&!l.Anchor());
    }return 0;
}
int MissingReadAndSpaceDoNotExtendSourceAuthority(){
    const auto original=Original();ContextualToolLifetime l;CHECK(l.Begin(41,original));
    for(unsigned n=0;n<100;++n)CHECK(l.Observe(original.inventory.owner,{})==ContextualToolSelection::Unknown);
    CHECK(l.Operation()==41&&!l.Revoked());
    auto current=Selected(original);current.inventory.owner.space++;
    CHECK(l.Observe(current.inventory.owner,current)==ContextualToolSelection::Tool);
    CHECK(l.Operation()==41&&l.Anchor()==original);
    // This object contains no input lease or hand claim: fresh source and
    // ownership must still pass the adapter's normal write checks.
    CHECK(l.ConfigurationCurrent(current));return 0;
}
int OriginalCommittedEdgesSurviveWithoutReplay(){
    using namespace fvr::interaction;
    const auto original=Original();ContextualToolLifetime l;CHECK(l.Begin(43,original));
    std::int64_t now=1000000000;std::array<std::byte,InputBytes> cache{};
    ToolInputRequest request;request.operation=43;request.serial=47;request.kind=ToolInputKind::Equip;
    request.scope={original.inventory.owner,
        {{(std::uint64_t(0x12000)<<32)|0x11000,1,6,3},53,now,now+100000000,now,true,{true,true},{false,false}},59,61,0x17000};
    request.from=original.inventory.items[0];request.target=original.tool.item;
    request.custody={67,request.scope.input.owner,InteractionHand::Right,HandClaimKind::BodyInventory,{request.target.weapon,2},{71,1},0};
    const ToolInputValidation validation{&now,[](void* p,const ToolInputRequest& r)noexcept{
        const auto time=*static_cast<std::int64_t*>(p);return time>=r.scope.input.observedNs&&time<r.scope.input.deadlineNs;
    }};
    ContextualToolInputOverride equip;CHECK(equip.Apply(cache,request,validation));const auto committed=equip.Commit();
    CHECK(committed&&l.RememberCommit(*committed));CHECK(!l.RememberCommit(*committed));
    auto wrong=*committed;wrong.request.operation++;CHECK(!l.RememberCommit(wrong));
    now+=5000000000;auto current=Selected(original);current.tool.firing+=4;
    CHECK(l.Observe(current.inventory.owner,current)==ContextualToolSelection::Tool&&l.Revoked());
    for(unsigned n=0;n<100;++n)l.Observe(current.inventory.owner,{});
    CHECK(l.EquipCommit()&&l.EquipCommit()->request.scope.input.sequence==53);
    CHECK(l.EquipCommit()->request.scope.input.observedNs==1000000000&&l.EquipCommit()->request.scope.input.deadlineNs==1100000000);
    CHECK(l.EquipCommit()->request.scope.input.nowNs==1000000000&&l.EquipCommit()->request.scope.cache==0x17000);
    CHECK(l.EquipCommit()->request.scope.nativeTick==61&&l.EquipCommit()->request.scope.request==59&&l.EquipCommit()->request.custody.id==67);
    // Retained diagnostic source cannot authorize a stale write after the gap.
    ContextualToolInputOverride stale;CHECK(!stale.Apply(cache,l.EquipCommit()->request,validation));
    request.kind=ToolInputKind::Return;request.serial=73;std::swap(request.from,request.target);request.custody={};
    request.scope.owner=current.inventory.owner;request.scope.input.owner.equipGeneration++;
    request.scope.input.sequence=79;request.scope.input.observedNs=request.scope.input.nowNs=now;request.scope.input.deadlineNs=now+100000000;
    request.scope.nativeTick=83;request.scope.request=89;request.scope.cache=0x1a000;
    ContextualToolInputOverride ret;CHECK(ret.Apply(cache,request,validation));const auto returnCommit=ret.Commit();
    CHECK(returnCommit&&l.RememberCommit(*returnCommit)&&!l.RememberCommit(*returnCommit));
    CHECK(l.EquipCommit()->request.serial==47&&l.ReturnCommit()->request.serial==73&&l.ReturnCommit()->request.scope.input.sequence==79);
    current.inventory.selected=0;current.inventory.owner.weapon=original.inventory.owner.weapon;
    CHECK(l.Observe(current.inventory.owner,current)==ContextualToolSelection::Original&&l.Operation()==43);
    CHECK(l.EquipCommit()&&l.ReturnCommit()&&!l.ObserveBody(43));
    CHECK(l.ObserveBody(0)&&!l.EquipCommit()&&!l.ReturnCommit());return 0;
}
}
int main(){CHECK(!NewOperationCannotReplaceOwned());CHECK(!MetadataInvalidationKeepsOriginalOperation());
    CHECK(!OriginalReturnStillNeedsBodyCompletion());CHECK(!UnknownAndReplacementDoNotInventCompletion());
    CHECK(!MissingReadAndSpaceDoNotExtendSourceAuthority());
    CHECK(!OriginalCommittedEdgesSurviveWithoutReplay());
    std::puts("Six contextual lifetime groups PASS: metadata quarantine retains body operation and original commits without extending source authority.");}
