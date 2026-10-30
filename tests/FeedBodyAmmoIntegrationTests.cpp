#include "Bc2BodyAmmo.h"
#include "Bc2BodyAmmoHost.h"
#include "Test.h"
#include <cstdio>
#include <cstring>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
constexpr std::int64_t Now=1000000000;
math::Matrix4 Identity(){return reload_insertion_detail::Identity();}
template<std::size_t N> void Text(std::array<char,N>& out,std::string_view value){
    out={};std::memcpy(out.data(),value.data(),(std::min)(value.size(),N-1));
}
struct Fixture {
    Bc2FeedBodyAmmo body{};HandInteractionSample input{};HandInteraction hands;
    std::uint64_t intent=0;Bc2FeedAmmoSupply supply;
    math::Matrix4 chest=Identity();
    explicit Fixture(const Bc2FeedRegistration& row){
        auto& n=body.native;n.identity={{0x10000,0x20000,0x30000,0x40000,1,2,3},
            {0x50000,0x60000,0x70000},0x80000,0x90000,0xa0000};
        n.binding={n.identity.owner,{{0x20000,1,0x40000,19},2,3},0xb0000,0xc0000,0xd0000,0xe0000,Now-1,Now+99999999};
        const auto& v=row.config.values;auto& c=n.config;
        c.weaponData=0xd0000;c.firingData=0xf0000;c.primaryFire=0x100000;c.ammoAddress=c.primaryFire+0x170;
        Text(c.assetName,row.geometry.asset);Text(c.assetPath,row.geometry.configurationPath);
        c.fireLogicType=v.fireLogicType;c.reloadType=v.reloadType;c.fireInputAction=v.fireInputAction;c.reloadInputAction=v.reloadInputAction;
        c.baseCapacity=v.baseCapacity;c.numberOfMagazines=v.numberOfMagazines;c.reloadDelay=v.reloadDelay;c.reloadTime=v.reloadTime;
        c.reloadThreshold=v.reloadThreshold;c.postReloadTime=v.postReloadTime;c.boltDelay=v.boltDelay;c.boltTime=v.boltTime;
        c.holdBoltUntilFireRelease=v.holdBoltUntilFireRelease;c.holdBoltUntilZoomRelease=v.holdBoltUntilZoomRelease;
        n.counts={20,130,v.baseCapacity};n.sequence=1;n.observedNs=Now-1;n.deadlineNs=n.observedNs+row.mechanism.maxAgeNs;
        body.registrationKey=row.key;body.lifetime=10;body.supply={{0xd0000,19},{0x40000,19}};
        input.owner={(std::uint64_t(0x30000)<<32)|0x20000,1,7,3};input.sequence=1;
        input.observedNs=Now;input.nowNs=Now+1;input.deadlineNs=Now+100000000;input.focused=true;input.tracked={true,true};
        const auto config=Bc2FeedSupplyConfig();for(unsigned i=0;i<3;++i)chest.values[3][i]=config.pouchCenterMeters[i];
    }
    bool Step(bool grip,bool acquire=true,const std::optional<math::Matrix4>& pose=std::nullopt){
        ++input.sequence;input.observedNs+=10000000;input.nowNs=input.observedNs+1;input.deadlineNs=input.observedNs+100000000;
        input.released[0]=!grip;++body.native.sequence;body.native.observedNs=input.observedNs-1;
        body.native.deadlineNs=body.native.observedNs+FindBc2FeedRegistration(body.registrationKey)->mechanism.maxAgeNs;body.native.binding.observedNs=body.native.observedNs;
        body.native.binding.deadlineNs=body.native.deadlineNs;
        const HandContactProof proof{{1,7},input.sequence,input.deadlineNs,true};
        const auto gun=hands.Current(InteractionHand::Right);
        if(gun)(void)hands.UpdateAndRenewGunHold(input,gun->token,proof);
        else {(void)hands.Update(input);(void)hands.Acquire(input,{input.owner,InteractionHand::Right,HandClaimKind::GunHold,
            {body.native.identity.owner.weapon,input.owner.equipGeneration},proof,++intent,0});}
        return supply.Update(body,input,pose?pose:std::optional<math::Matrix4>{chest},grip,acquire,hands,intent);
    }
    FeedAmmunitionCommand Command()const {
        FeedAmmunitionCommand c;c.request.id=3;c.request.lifetime=body.lifetime;c.request.requestedNs=input.nowNs;
        c.authority.effect=FeedAmmunitionEffect::RefillLoaded;c.authority.operation=FeedPhysicalOperation::AttachContainer;
        c.before.owner=c.authority.owner=FeedEquipmentOwner(body.native.identity.owner);c.before.lifetime=c.authority.lifetime=body.lifetime;
        c.authority.supply=body.supply;c.authority.rounds=int(supply.Custody()->object.units);
        c.before.sequence=body.native.sequence;c.before.counts=body.native.counts;c.after=c.before.counts;
        c.after.loaded+=c.authority.rounds;c.after.reserve-=c.authority.rounds;c.loadedSupplyAfter=body.supply;c.mutationRequired=true;return c;
    }
};
int EveryEnrolledPouchUsesExactSourceAndAuthoredGrasp(){
    unsigned count=0;for(const auto& row:RegisteredBc2FeedProfiles()){
        Fixture f(row);f.Step(false);const auto source=Bc2FeedBodySource(f.body,f.input,f.input.nowNs);CHECK(source);
        BodyAmmoTracking body;body.feed=f.body;const auto config=Bc2FeedSupplyConfig();
        body.visual={true,false,*source,f.input,*f.hands.Current(InteractionHand::Right),
            {config.pouchCenterMeters,config.pouchRadiusMeters},SupplyAnchorFrame::RecenteredBody};
        CHECK(BodyAmmoFresh(body,f.input.nowNs));
        InputFrame input;input.generation=f.input.sequence;input.spaceGeneration=f.input.owner.space;input.predictedNs=f.input.observedNs;
        input.focused=input.headValid=true;input.referenceHead.orientation.w=input.head.orientation.w=1;
        input.referenceHead.position=input.head.position={0,1.7f,0};
        for(auto& h:input.hands){h.gripTracked=h.aimTracked=true;h.grip.orientation.w=h.aim.orientation.w=1;}
        for(float units:{1.f,100.f}){input.worldUnitsPerMeter=units;const auto pose=BuildBodyAmmoPose(body,input,Identity(),f.input.nowNs);CHECK(pose);
            CHECK(pose->asset==row.geometry.asset&&pose->mesh==row.geometry.meshPath&&pose->part==row.geometry.pouchBone.bone);
            CHECK(pose->rigFingerprint==row.geometry.rigFingerprint);
            auto grip=row.geometry.poses[unsigned(Bc2FeedPoseId::PouchWithdrawn)].sourceFromWrist;
            // partWorld already scales local metre coordinates into game units.
            const auto located=interaction::Multiply(grip,pose->partWorld);
            const auto anchor=AmmoSupplyAnchorWorld(input,Identity(),body.visual);CHECK(anchor);
            for(unsigned n=0;n<3;++n)CHECK(Near(located.values[3][n],anchor->values[3][n],.0003f));
            BodyAmmoRenderSource render;render.prop={body.visual,pose->partWorld};render.asset=pose->asset;render.mesh=pose->mesh;
            render.part=pose->part;render.rigFingerprint=pose->rigFingerprint;render.authority=std::make_shared<const BodyAmmoTracking>(body);
            render.geometry=MakeBodyAmmoGeometryKey(render.asset,render.mesh,render.part,render.rigFingerprint);
            CHECK(BodyAmmoSourceRetained(render,render,f.input.nowNs));
            const auto host=BodyAmmoHostInstance(render,render,f.input.nowNs);CHECK(host);
            CHECK(host->deadlineNs==f.body.native.deadlineNs);CHECK(!BodyAmmoHostInstance(render,render,host->deadlineNs));
            auto wrong=render;wrong.part="unregistered_pouch";CHECK(!BodyAmmoSourceRetained(wrong,wrong,f.input.nowNs));
            wrong=render;wrong.authority.reset();CHECK(!BodyAmmoHostInstance(wrong,wrong,f.input.nowNs));
        }
        auto bad=body;bad.feed->registrationKey=0;CHECK(!BodyAmmoFresh(bad,f.input.nowNs));
        bad=body;bad.feed->native.config.assetPath[0]='!';CHECK(!BodyAmmoFresh(bad,f.input.nowNs));
        bad=body;++bad.feed->native.binding.context.resource.weaponGeneration;CHECK(!BodyAmmoFresh(bad,f.input.nowNs));
        CHECK(!BodyAmmoFresh(body,body.visual.source.deadlineNs));
        auto current=body;++current.feed->native.sequence;++current.visual.source.sequence;
        CHECK(BodyAmmoRetained(body,current,f.input.nowNs));
        ++current.feed->native.counts.reserve;CHECK(!BodyAmmoRetained(body,current,f.input.nowNs));
        ++count;
    }
    CHECK(count>0);std::printf("Feed body source/geometry rows: %u; deterministic only\n",count);return 0;
}
int PickupClaimsCustodyAndOnlyExactReceiptConsumes(){
    Fixture f(RegisteredBc2FeedProfiles().front());const auto counts=f.body.native.counts;
    CHECK(!f.Step(true));CHECK(!f.supply.Custody());CHECK(!f.Step(false));CHECK(f.Step(true));
    CHECK(f.supply.Custody()&&f.supply.Held());CHECK(f.supply.Held()->claim.token.kind==HandClaimKind::AmmoObject);
    CHECK(f.body.native.counts==counts);const auto item=f.supply.Custody()->object.item;
    f.supply.Suspend(f.input,f.hands);CHECK(!f.supply.Held()&&f.supply.Custody()->object.item==item);
    CHECK(!f.Step(false));CHECK(f.supply.Ready(f.body,f.input));
    auto command=f.Command();auto bad=command;++bad.authority.rounds;CHECK(!f.supply.Bind(bad,f.body,f.input,f.hands));
    CHECK(f.supply.Bind(command,f.body,f.input,f.hands));CHECK(!f.supply.Ready(f.body,f.input));
    CHECK(!f.Step(false));CHECK(!f.Step(true));CHECK(f.supply.Custody()->pending==command);
    FeedAmmunitionReceipt receipt;receipt.command=command;receipt.acknowledgement.request=command.request;
    receipt.acknowledgement.status=FeedReloadAcknowledgement::Applied;receipt.authorityInvocation=99;
    receipt.completedNs=command.request.requestedNs+1;receipt.post.counts=command.after;
    CHECK(!f.supply.Complete(receipt));receipt.authorityVerified=receipt.copiesVerified=true;
    auto wrong=receipt;++wrong.post.counts.reserve;CHECK(!f.supply.Complete(wrong));
    CHECK(f.supply.Complete(receipt));CHECK(!f.supply.Custody());CHECK(!f.supply.Complete(receipt));return 0;
}
int ExhaustionMissingGeometryAndForeignHandsCannotAcquire(){
    Fixture f(RegisteredBc2FeedProfiles().front());f.Step(false);
    auto far=Identity();far.values[3][0]=5;CHECK(!f.Step(true,true,far));CHECK(!f.supply.Custody());
    f.Step(false);f.body.native.counts.reserve=0;CHECK(!f.Step(true));CHECK(!f.supply.Custody());
    f.body.native.counts.reserve=130;f.Step(false);
    CHECK(!f.supply.Update(f.body,f.input,std::nullopt,true,true,f.hands,f.intent));CHECK(!f.supply.Custody());
    const auto request=HandClaimRequest{f.input.owner,InteractionHand::Left,HandClaimKind::BodyInventory,{888,1},
        {{999,1},f.input.sequence,f.input.deadlineNs,true},++f.intent,0};
    // The current release packet cannot acquire. A fresh held packet first owns
    // the unrelated claim, so the supply provider must leave it alone.
    auto input=f.input;++input.sequence;input.observedNs+=1000000;input.nowNs=input.observedNs+1;input.deadlineNs=input.observedNs+100000000;input.released[0]=false;
    (void)f.hands.Update(input);auto occupied=request;occupied.contact.inputSequence=input.sequence;occupied.contact.deadlineNs=input.deadlineNs;
    const auto claim=f.hands.Acquire(input,occupied);CHECK(claim.claim);
    CHECK(!f.supply.Update(f.body,input,f.chest,true,true,f.hands,f.intent));
    CHECK(f.hands.Current(InteractionHand::Left)->token==claim.claim->token);return 0;
}
int NoEntryAndVerifiedContextMigrationKeepTheExactPickup(){
    Fixture f(RegisteredBc2FeedProfiles().front());f.Step(false);CHECK(f.Step(true));
    const auto item=f.supply.Custody()->object.item;const auto command=f.Command();
    CHECK(f.supply.Bind(command,f.body,f.input,f.hands));
    FeedAmmunitionReceipt receipt;receipt.command=command;receipt.acknowledgement.request=command.request;
    receipt.acknowledgement.status=FeedReloadAcknowledgement::Rejected;receipt.authorityInvocation=98;
    receipt.authorityVerified=receipt.copiesVerified=true;receipt.completedNs=command.request.requestedNs+1;
    receipt.post.counts=command.before.counts;CHECK(!f.supply.Complete(receipt));
    receipt.noTransferEntry=true;CHECK(f.supply.Complete(receipt));CHECK(f.supply.Custody()->object.item==item);
    const auto previous=f.body.native;f.Step(false);
    ++f.body.native.identity.owner.equipGeneration;++f.body.native.identity.owner.space;
    f.body.native.binding.owner=f.body.native.identity.owner;
    ++f.body.native.binding.context.equipGeneration;++f.body.native.binding.context.space;
    ++f.input.owner.equipGeneration;++f.input.owner.space;
    const auto snapshot=[](const FeedEquipmentNativeView& v){return AmmunitionSnapshot{v.binding.context,v.sequence,v.observedNs,v.deadlineNs,
        {v.counts.loaded,v.counts.reserve,v.counts.capacity},true};};
    const FeedContextMigration migration{snapshot(previous),snapshot(f.body.native)};
    CHECK(!f.supply.Ready(f.body,f.input));
    auto wrong=migration;++wrong.after.counts.reserve;CHECK(!f.supply.Migrate(wrong,f.body,f.input));
    CHECK(f.supply.Migrate(migration,f.body,f.input));CHECK(f.supply.Ready(f.body,f.input));
    CHECK(f.supply.Custody()->object.item==item&&f.supply.Custody()->origin.native.identity==previous.identity);return 0;
}
}
int main(){
    CHECK(EveryEnrolledPouchUsesExactSourceAndAuthoredGrasp()==0);
    CHECK(PickupClaimsCustodyAndOnlyExactReceiptConsumes()==0);
    CHECK(ExhaustionMissingGeometryAndForeignHandsCannotAcquire()==0);
    CHECK(NoEntryAndVerifiedContextMigrationKeepTheExactPickup()==0);
}
