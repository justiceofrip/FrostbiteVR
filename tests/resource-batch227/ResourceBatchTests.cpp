#include "Test.h"
#include "Bc2MagazineResourceReload.h"
#include "Bc2MagazineResourceProbe.h"
#include "ResourceMagazineProbeInput.h"
#include "Bc2PhysicalReload.h"
#include "fvr/interaction/BodyAnchors.h"
#include "fvr/interaction/ReloadGrip.h"
#include "fvr/interaction/TrackedRig.h"
#include <cstring>
#include <iostream>
#include <sstream>
#include "fvr/interaction/SupportGrip.h"
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;using namespace fvr::interaction::reload_insertion_detail;
namespace {
constexpr std::int64_t Ms=1000000;
auto Pose(float x=0,float y=0,float z=0){auto p=Identity();p.values[3]={x,y,z,1};return p;}
auto Matrix(const math::Pose& p){return *InverseRigid(*math::MakeLhViewFromOpenXRPose(p));}
std::string Report(const Bc2MagazineResourceProbe& p){std::ostringstream out;p.Report(out);return out.str();}
template<class Adapter> MagazineResourceProbeState PublishedProbeState(Adapter& adapter,const MagazinePhysicalResult& result,std::int64_t now){
    if constexpr(requires {adapter.ResourceProbeState(now);})return adapter.ResourceProbeState(now);
    // Only the isolated frozen222 baseline lacks root's new read-only accessor.
    else return {result,result.tracking.resource?std::optional<AmmoResourceView>(result.tracking.resource->resource):std::nullopt};
}
InputFrame Input(std::uint64_t sequence){InputFrame in;in.generation=sequence;in.spaceGeneration=7;in.predictedNs=1000*Ms+std::int64_t(sequence)*10*Ms;
 in.focused=in.headValid=true;for(auto& h:in.hands){h.gripTracked=h.aimTracked=true;h.active=Components;}
 in.hands[0].grip.position={-.2f,-.4f,-.3f};in.hands[1].grip.position={0,-.25f,-.45f};return in;}
struct Fixture {
    AmmoResourceService service;AmmoResourceChannel channel;MagazineResourceApi api;
    std::optional<Bc2MagazinePhysicalReload> adapter;
    HandInteraction hands;std::optional<HandClaim> gun;MagazinePhysicalSample s;MagazinePhysicalResult result;
    ReloadHoldIdentity native{{0x10000,0x20000,0x30000,0x40000,5,3,7},{0x50000,0x60000,0x70000},0x80000,0x90000,0xa0000};
    AmmoResourceBinding binding;AmmunitionCounts counts{27,83,30};std::int64_t now=1000*Ms;
    std::uint64_t sequence=0,intent=0,nativeSequence=0,invocation=0;unsigned calls=0;
    std::shared_ptr<SelectedMeshesSnapshot> meshes=std::make_shared<SelectedMeshesSnapshot>();
    const MagazineEquipmentProfile* profile=&Xm8MagazineEquipment();bool completeNative=true,publishBetweenCallbacks=false,skipSelectOnce=false;
    std::optional<AmmoResourceNativeCall> outstanding;
    Fixture(const MagazineEquipmentProfile& equipment=Xm8MagazineEquipment()):profile(&equipment){
        binding={native.owner,{{native.owner.soldier,5,native.owner.weapon,45},3,7},0xd0000,0x130000,0x140000,0x150000,now,now+100*Ms};
        s.nativeOwner=native.owner;s.weapon={0xb0000,17};s.trackingEpoch=7;
        s.input={{(std::uint64_t(0x30000)<<32)|0x20000,5,17,7},0,now,now+100*Ms,now,true,{true,true},{true,false}};
        s.asset=profile->geometry->asset;s.meshes=meshes;s.bodyFromHand=Pose(2);
        s.family.binding={native.owner,s.weapon,binding.inventory,0xc0000,2,profile};
        if(profile->native->identityRoute!=MagazineIdentityRoute::LinkedLauncherAlias){
            s.weapon.id=native.owner.weapon;s.family.binding.weapon=s.weapon;s.family.binding.launcher=s.family.binding.launcherSlot=0;
            auto& identity=s.family.binding.equipment;identity.weapon=native.owner.weapon;identity.data=binding.data;identity.persistence=binding.persistence;
            std::memcpy(identity.asset.data(),s.asset.data(),s.asset.size());
        }
        meshes->owner=native.owner;meshes->stateCount=1;meshes->soleConfiguredArray=0x110000;meshes->states[0].count=1;
        meshes->weaponData=binding.data;
        auto& mesh=meshes->states[0].meshes[0];mesh.kind=profile->geometry->meshKind;mesh.address=0x120000;
        std::memcpy(mesh.assetPath.data(),profile->geometry->mesh.data(),profile->geometry->mesh.size());
        s.raw.valid=true;s.raw.owner=s.nativeOwner;s.raw.rigFingerprint=profile->geometry->rigFingerprint;s.raw.weaponWorldMeters=Pose();s.raw.nativeMagazineAttached=true;
        api.context=this;api.read=[](void* p,const ReloadStateOwner& o,std::int64_t now)noexcept{return static_cast<Fixture*>(p)->channel.Read(o,now);};
        api.submit=[](void* p,const AmmoResourceRequest& r)noexcept{return static_cast<Fixture*>(p)->channel.Submit(r);};
        MagazinePhysicalApi playerApi;playerApi.context=this;playerApi.resourceRead=api.read;playerApi.resourceSubmit=api.submit;
        auto pouch=ChestAmmoSupply();const auto ids=Bc2MagazinePhysicalReload::DefaultPouch();pouch.itemNamespace=ids.itemNamespace;pouch.pouch=ids.pouch;
        adapter.emplace(true,playerApi,pouch);adapter->EnableBodyAmmo();
    }
    AmmunitionSnapshot Snapshot(){return {binding.context,++nativeSequence,now,now+100*Ms,counts,true};}
    void Select(){binding.observedNs=now;binding.deadlineNs=now+100*Ms;service.Select(binding,native,Snapshot(),now);channel.Publish(service.View());}
    AmmoResourceOwnUpdate Row(unsigned branch,const AmmoResourceNativeCall& c){AmmoResourceOwnUpdate r;
        r.beforeOwner=r.afterOwner=c.identity.owner;r.firing=c.identity.firing[branch];r.serverPlayer=c.identity.serverPlayer;r.serverSoldier=c.identity.serverSoldier;r.serverItem=c.identity.serverItem;
        r.invocation=c.invocation+(branch==2?0:branch+1);r.beginNs=c.beginNs-1;r.endNs=c.endNs+10+branch;r.branch=branch;r.depth=1;
        r.loadedBefore=c.before.loaded;r.reserveBefore=c.before.reserve;r.loadedAfter=c.after.loaded;r.reserveAfter=c.after.reserve;
        r.current=r.next=2;r.finished=r.identityRetained=r.neutralContext=true;return r;}
    void Drain(){if(const auto r=channel.Take())if(const auto c=service.Accept(*r,now)){
        const auto adjacent=Snapshot();if(service.Dispatch(adjacent,now)){
            ++calls;outstanding=AmmoResourceNativeCall{native,c->id,(invocation+=10),now+1,now+2,c->before,c->after,true,true,true};counts=c->after;
            service.Call(*outstanding);
        }}
        if(outstanding&&completeNative){
            // The helper-owning server Update is retained at its actual time.
            // Later independent idle Updates provide fresh convergence counts.
            service.Observe(Row(2,*outstanding),now+100);
            for(unsigned branch:{0,1,2}){auto r=Row(branch,*outstanding);r.beginNs=now;r.endNs=now+10+branch;
                r.invocation=outstanding->invocation+100+branch;r.loadedBefore=r.loadedAfter;r.reserveBefore=r.reserveAfter;
                service.Observe(r,now+100);}
            if(publishBetweenCallbacks){channel.Publish(service.View(),service.Outcome());skipSelectOnce=true;}
            outstanding.reset();}
    }

    Bc2MagazineResourceProbe probe{true};MagazinePackCounters packs{};TrackedRig rig;MagazineRawContact raw{};
    bool pairs=true,hideReceipt=false,stealSupport=false;unsigned delay=1,corruptSeat=0;std::array<MagazineRawContact,4> rawHistory{};
    unsigned releasedPackets=0;InputFrame lastInput{};
    SupportGrip support;std::optional<float> supportSqueeze;std::optional<HandClaim> supportClaim;
    bool supportHolding=false;
    MagazineResourceProbeState State(){
        MagazineResourceProbeState state=PublishedProbeState(*adapter,result,now);
        if(hideReceipt&&state.resource)state.resource->receipt.reset();
        if(corruptSeat&&state.resource&&state.resource->receipt&&state.resource->receipt->command.operation!=AmmunitionOperation::RemoveMagazine){
            auto& r=*state.resource->receipt;
            if(corruptSeat==1)r.authorityVerified=false;if(corruptSeat==2)r.copiesVerified=false;
            if(corruptSeat==3)r.authorityInvocation=0;if(corruptSeat==4)++r.command.context.resource.weapon;
            if(corruptSeat==5)r.completedNs=now+1;if(corruptSeat==6)++r.after.loaded;
            if(corruptSeat==7)r.command.id=1;if(corruptSeat==8)r.command.requestedNs=1;
            if(corruptSeat==9)state.resource->receipt.reset();
        }
        return state;
    }
    void Tick(unsigned fault=0){
        now+=10*Ms;++sequence;auto in=Input(sequence);
        if(fault==1)in.hands[0].gripTracked=false;
        if(fault==2)in.focused=false;
        if(fault==3)in.spaceGeneration++;
        if(fault==4)in.generation--;
        probe.Prepare(in,native.owner,s.asset,raw,State(),now,now+100*Ms,now);
        if(supportSqueeze){in.hands[0].squeeze=*supportSqueeze;in.hands[0].grip.position=in.hands[1].grip.position;
            in.hands[0].grip.position.z-=.25f;in.hands[0].aim=in.hands[0].grip;}
        lastInput=in;if(probe.Current()==Bc2MagazineResourceProbe::Phase::WaitReceipt&&in.hands[0].squeeze==0)++releasedPackets;
        s.input.sequence=sequence;s.input.observedNs=s.input.nowNs=now;s.input.deadlineNs=now+100*Ms;
        s.input.focused=in.focused;s.input.tracked={in.hands[0].gripTracked,in.hands[1].gripTracked};
        s.input.released[0]=in.hands[0].squeeze<=.35f;
        hands.Update(s.input);HandContactProof proof{{1002,1},sequence,s.input.deadlineNs,true};
        if(gun)gun=hands.Renew(s.input,gun->token,proof).claim;
        if(!gun)gun=hands.Acquire(s.input,{s.input.owner,InteractionHand::Right,HandClaimKind::GunHold,s.weapon,proof,++intent,0}).claim;
        s.gripPressed=ReloadGripActive(in.hands[0].squeeze,s.input,hands.Current(InteractionHand::Left));s.ejectPressed=false;
        s.geometrySequence=sequence;s.bodyFromHand=BodyAnchorHandPose(in,InteractionHand::Left).value_or(Pose(2));
        s.family.observedNs=now;s.family.deadlineNs=now+100*Ms;s.family.verified=true;
        if(s.weapon.id==native.owner.weapon)s.family.carried=SelectedCarriedWeaponEvidence{native.owner,s.input.owner,s.weapon,
            s.family.binding.equipment,binding.inventory,binding.switching,0,0,sequence,now,now+100*Ms,true};
        meshes->sequence=sequence;meshes->observedNs=now;meshes->deadlineNs=now+100*Ms;
        s.raw=raw;if(raw.valid)s.originalHandEvidence=raw.inputEvidence;
        if(skipSelectOnce)skipSelectOnce=false;else Select();result=adapter->Tick(s,hands,intent);
        if(pairs&&result.tracking.target&&MagazineTargetFresh(result.tracking,now)){
            ++packs.pairs;packs.copies+=2;const auto role=unsigned(result.tracking.target->role);++packs.rolePairs[role];packs.roleCopies[role]+=2;
        }
        auto state=State();if(stealSupport)state.result.ownsLeftHand=true;
        probe.Observe(state,packs,now);Drain();
        // Same ordering as gameplay: ordinary magazine consumer, then support
        // policy and the shared hand arbiter. Contact is a mock measured input.
        const auto supported=support.Update({s.input.owner.actor,s.input.owner.actorGeneration,native.owner.weapon},in,
            {true,0,{}},!in.focused,MagazineBlocksSupport(result,now));
        supportHolding=supported.holding;
        if(supported.holding){HandContactProof contact{{2002,1},sequence,s.input.deadlineNs,true};
            if(supportClaim)supportClaim=hands.Renew(s.input,supportClaim->token,contact).claim;
            else supportClaim=hands.Acquire(s.input,{s.input.owner,InteractionHand::Left,HandClaimKind::WeaponSupport,s.weapon,contact,++intent,gun->token.id}).claim;
        }else if(supportClaim){hands.Release(s.input,supportClaim->token);supportClaim.reset();}
        const auto body=Pose(5,2,3),left=Multiply(Pose(-.2f,-.4f,.3f),body),right=Multiply(Pose(0,-.25f,.45f),body);
        std::array<ArmAnchor,2> arms{{{{4.8f,1.8f,3},{0,1,0}},{{5.2f,1.8f,3},{0,1,0}}}};
        const auto posed=rig.Update({1,2,3,4},in,body,left,right,right,arms);
        if(posed){raw.valid=true;raw.owner=native.owner;raw.rigFingerprint=profile->geometry->rigFingerprint;raw.inputEvidence=s.input;
            raw.weaponWorldMeters=posed->weapon;raw.trackingBodyWorldMeters=body;raw.nativeMagazineAttached=!service.View()->wellEmpty;
            auto attachment=Pose();attachment.values[1][1]=attachment.values[2][2]=-1;
            auto wrist=Multiply(attachment,Multiply(Matrix(in.hands[0].grip),body));wrist.values[3]=posed->left.values[3];raw.rawLeftWristWorldMeters=wrist;
            rawHistory[sequence%rawHistory.size()]=raw;raw=rawHistory[(sequence+rawHistory.size()-(delay-1))%rawHistory.size()];
        }
    }
    bool Run(){for(unsigned n=0;n<3100&&!probe.CancelConsumer();++n)Tick();
        if(!probe.Completed())std::cerr<<Report(probe)<<"\n";return probe.Completed();}
};

int RealConsumerProfiles(){unsigned profiles=0;for(const auto* profile:{&Xm8MagazineEquipment(),FindMagazineEquipment("AEK971_sp")}){
    CHECK(profile&&profile->Ready());++profiles;
    for(int loaded:{0,27,30})for(bool original:{false,true})for(unsigned delay:{1u,3u}){
        Fixture f(*profile);f.counts.loaded=loaded;f.delay=delay;f.probe=Bc2MagazineResourceProbe(true,original);
        CHECK(f.Run());CHECK(f.calls==2);CHECK(!f.adapter->BlocksEquipment());
        CHECK(f.counts.loaded==(original?loaded:30));CHECK(f.counts.reserve==(original?83:53));
        CHECK(Report(f.probe).find("\"support_released\":1")!=std::string::npos);
    }}CHECK(profiles==2);return 0;}
int CombinedPersistent(){Fixture f;f.probe=Bc2MagazineResourceProbe(true,false,true);CHECK(f.Run());
    CHECK(f.calls==4&&f.counts.loaded==30&&f.counts.reserve==53);
    CHECK(Report(f.probe).find("\"completed_cycles\":2")!=std::string::npos);return 0;}
int NoReceiptOrRenderer(){for(unsigned fault=0;fault<2;++fault){Fixture f;f.hideReceipt=fault==0;f.pairs=fault!=1;
    CHECK(!f.Run());CHECK(f.calls==1);CHECK(!f.probe.Completed());}return 0;}
int DelayedReceiptRelease(){Fixture f;for(unsigned n=0;n<3000&&f.probe.Current()!=Bc2MagazineResourceProbe::Phase::Stroke;++n)f.Tick();
    CHECK(f.probe.Current()==Bc2MagazineResourceProbe::Phase::Stroke);f.completeNative=false;
    for(unsigned n=0;n<1000&&f.probe.Current()!=Bc2MagazineResourceProbe::Phase::WaitReceipt;++n)f.Tick();
    CHECK(f.probe.Current()==Bc2MagazineResourceProbe::Phase::WaitReceipt);CHECK(f.counts.loaded==30&&f.calls==2);
    for(unsigned n=0;n<40;++n)f.Tick();CHECK(!f.probe.Completed()&&f.releasedPackets>=39&&f.calls==2);
    f.completeNative=true;CHECK(f.Run());CHECK(f.calls==2);return 0;}
int InterruptedInputs(){for(auto phase:{Bc2MagazineResourceProbe::Phase::Grip,Bc2MagazineResourceProbe::Phase::Pull,
    Bc2MagazineResourceProbe::Phase::GrabReplacement,Bc2MagazineResourceProbe::Phase::Stroke})for(unsigned fault:{1u,2u,3u}){
    Fixture f;for(unsigned n=0;n<3000&&!f.probe.CancelConsumer()&&f.probe.Current()!=phase;++n)f.Tick();CHECK(f.probe.Current()==phase);
    f.Tick(fault);CHECK(f.probe.CancelConsumer()&&!f.probe.Completed());CHECK(f.lastInput.hands[0].squeeze==0);
    CHECK(!f.hands.Current(InteractionHand::Left));
}return 0;}
int SupportEvidenceRequired(){Fixture f;f.stealSupport=true;CHECK(!f.Run());CHECK(f.calls==2);return 0;}
int ExactSeatReceiptRequired(){for(unsigned bad=1;bad<=9;++bad){Fixture f;f.corruptSeat=bad;CHECK(!f.Run());
    CHECK(f.calls==2&&f.counts.loaded==30&&f.counts.reserve==53);CHECK(!f.probe.Completed());}return 0;}
int ExistingEvidenceCannotBeReplayed(){Fixture f;CHECK(f.Run());const auto before=f.calls;const auto saved=Report(f.probe);
    auto in=Input(++f.sequence);f.probe.Prepare(in,f.native.owner,f.s.asset,f.raw,f.State(),f.now,f.now+100*Ms,f.now);
    CHECK(in.hands[0].squeeze==0&&f.calls==before&&Report(f.probe)==saved);
    f.probe=Bc2MagazineResourceProbe(true,true);CHECK(f.Run());CHECK(f.calls==4);return 0;}
int StartupWaitsForActualResource(){Fixture f;auto owner=f.native.owner;++owner.weapon;
    for(unsigned n=0;n<200;++n){f.now+=10*Ms;auto in=Input(++f.sequence);
        f.probe.Prepare(in,owner,"SPAS12",{},{},f.now,f.now+100*Ms,f.now);
        CHECK(!f.probe.CancelConsumer()&&f.probe.Current()==Bc2MagazineResourceProbe::Phase::Warmup&&in.hands[0].squeeze==0);}
    CHECK(f.Run());CHECK(f.calls==2);return 0;}
int SetupReceiverHasOnlyFiniteActions(){for(bool exit:{false,true})for(float direction:{-1.f,1.f}){
    unsigned use=0,select=0;
    for(unsigned ms=0;ms<30000;++ms){auto in=Input(ms+1);probe::ResourceMagazineInput(in,ms,exit,direction);
        CHECK(in.hands[0].held==(exit&&ms>=400&&ms<500?Primary:0));
        const bool selected=(ms>=1000&&ms<1800)||(exit&&ms>=3000&&ms<3300);
        CHECK(in.hands[1].stickY==(selected?direction:0));
        CHECK(!in.hands[0].trigger&&!in.hands[1].trigger&&!in.hands[0].squeeze&&!in.hands[1].squeeze&&!in.hands[1].held);
        use+=in.hands[0].held!=0;select+=in.hands[1].stickY!=0;
    }CHECK(use==(exit?100:0)&&select==(exit?1100:800));
}return 0;}
int DoneShutdownDoesNotCreateNewWork(){Fixture f;f.probe=Bc2MagazineResourceProbe(true,false,true);CHECK(f.Run());
    CHECK(f.calls==4&&f.counts.loaded==30&&f.counts.reserve==53);const auto report=Report(f.probe);
    // Match Gameplay's Observe(Done)->Cancel, then sample.cancel on every later
    // gather packet while the finite monitor continues recording.
    f.adapter->Cancel(f.s.input,f.hands);f.s.cancel=true;
    for(unsigned n=0;n<2000;++n)f.Tick();
    CHECK(f.probe.Completed()&&Report(f.probe)==report&&f.calls==4);
    CHECK(f.counts.loaded==30&&f.counts.reserve==53&&!f.service.Active());
    CHECK(!f.adapter->BlocksEquipment()&&!f.hands.Current(InteractionHand::Left));
    std::ostringstream consumer;f.adapter->Report(consumer);
    CHECK(consumer.str().find("\"phase\":9,\"pending\":0,\"cycle\":0")!=std::string::npos);
    return 0;}
#ifdef FVR_RESOURCE_PROBE_VARIANTS
int ExactPublishedVariants(){
    CHECK(!FindMagazineEquipment("registry_rifle"));
    unsigned variants=0;
    for(const auto& row:RegisteredMagazineNativeProfiles()){
        if(!row.enabled||row.profile->configuration.assetName!="registry_rifle")continue;
        const auto p=FindMagazineEquipment(row.id);CHECK(p&&p->Ready());++variants;
        for(bool original:{false,true}){Fixture f(*p);f.counts.capacity=p->native->configuration.values.baseCapacity;
            f.counts.loaded=f.counts.capacity-3;const auto loaded=f.counts.loaded;
            f.probe=Bc2MagazineResourceProbe(true,original);CHECK(f.Run());CHECK(f.calls==2);
            CHECK(f.counts.loaded==(original?loaded:f.counts.capacity));
            CHECK(f.counts.reserve==(original?83:83-f.counts.capacity));
        }
        Fixture f(*p);for(unsigned n=0;n<900&&f.probe.Current()!=Bc2MagazineResourceProbe::Phase::ApproachMagazine;++n)f.Tick();
        CHECK(f.probe.Current()==Bc2MagazineResourceProbe::Phase::ApproachMagazine);
        const auto calls=f.calls;auto state=f.State();
        for(const auto& other:RegisteredMagazineNativeProfiles())if(other.enabled&&other.id!=row.id&&other.profile->configuration.assetName=="registry_rifle")
            state.result.tracking.family.binding.profile=FindMagazineEquipment(other.id);
        auto in=Input(++f.sequence);f.now+=10*Ms;
        f.probe.Prepare(in,f.native.owner,f.s.asset,f.raw,state,f.now,f.now+100*Ms,f.now);
        CHECK(f.probe.CancelConsumer()&&!f.probe.Completed()&&in.hands[0].squeeze==0&&f.calls==calls);
    }
    CHECK(variants==2);
    // A copied, unregistered profile is not the published immutable join.
    Fixture f;for(unsigned n=0;n<5;++n)f.Tick();auto state=f.State();auto copy=*state.result.tracking.family.binding.profile;
    state.result.tracking.family.binding.profile=&copy;auto in=Input(++f.sequence);f.now+=10*Ms;
    f.probe.Prepare(in,f.native.owner,f.s.asset,f.raw,state,f.now,f.now+100*Ms,f.now);
    CHECK(f.probe.CancelConsumer()&&!f.probe.Completed()&&f.calls==0);return 0;
}
#endif
int CompletionPublicationBetweenCallbacks(){
    for(int loaded:{0,22,30}){Fixture f;f.counts.loaded=loaded;f.publishBetweenCallbacks=true;
        f.probe=Bc2MagazineResourceProbe(true,false,true);CHECK(f.Run());CHECK(f.calls==4);
        CHECK(f.counts.loaded==30&&f.counts.reserve==53&&!f.adapter->BlocksEquipment());
    }return 0;
}
int DiagnosticFirstActiveCause(){Fixture f;
    for(unsigned n=0;n<900&&f.probe.Current()!=Bc2MagazineResourceProbe::Phase::Pull;++n)f.Tick();
    CHECK(f.probe.Current()==Bc2MagazineResourceProbe::Phase::Pull);
    f.Tick(2);std::ostringstream first;f.adapter->Report(first);
    const auto pos=first.str().find("\"first_active_cancel\":{");CHECK(pos!=std::string::npos);
    CHECK(first.str().find("\"first_active_cancel\":{\"cause\":1")!=std::string::npos);
    const auto event=first.str().substr(pos);
    for(unsigned n=0;n<10;++n)f.Tick(2);
    std::ostringstream later;f.adapter->Report(later);CHECK(later.str().substr(later.str().find("\"first_active_cancel\":"))==event);
    return 0;
}
int PreparedBatchResourceConsumers(){unsigned profiles=0,cycles=0;
    for(const auto& row:RegisteredMagazineNativeProfiles()){
        if(std::uint64_t(row.id)<2)continue;
        CHECK(row.enabled);const auto p=FindMagazineEquipment(row.id);CHECK(p&&p->Ready());++profiles;
        unsigned sameName=0;for(const auto& candidate:RegisteredMagazineNativeProfiles())
            if(candidate.enabled&&candidate.profile->configuration.assetName==p->geometry->asset)++sameName;
        CHECK(sameName>1?!FindMagazineEquipment(p->geometry->asset):FindMagazineEquipment(p->geometry->asset)==p);
        CHECK(p->geometry->configurationPath==p->native->configuration.assetPath);
        const auto capacity=p->native->configuration.values.baseCapacity;
        for(int loaded:{0,capacity-3,capacity})for(bool original:{false,true})for(unsigned lag:{1u,3u}){
            Fixture f(*p);f.counts={loaded,191,capacity};f.delay=lag;f.publishBetweenCallbacks=true;
            f.probe=Bc2MagazineResourceProbe(true,original);CHECK(f.Run());CHECK(f.calls==2);++cycles;
            CHECK(f.counts.loaded==(original?loaded:capacity));CHECK(f.counts.reserve==(original?191:191-capacity));
            CHECK(!f.adapter->BlocksEquipment()&&!f.hands.Current(InteractionHand::Left));
            const auto calls=f.calls;const auto gun=f.hands.Current(InteractionHand::Right)->token;
            f.supportSqueeze=0;for(unsigned n=0;n<3;++n)f.Tick();
            f.supportSqueeze=1;f.Tick();CHECK(f.supportHolding&&f.supportClaim);
            CHECK(f.hands.Current(InteractionHand::Left)->token.kind==HandClaimKind::WeaponSupport);
            const auto support=f.supportClaim->token;for(unsigned n=0;n<5;++n)f.Tick();
            CHECK(f.supportHolding&&f.supportClaim->token==support&&f.hands.Current(InteractionHand::Right)->token==gun);
            CHECK(f.calls==calls&&!f.adapter->BlocksEquipment());f.supportSqueeze=0;f.Tick();
            CHECK(!f.supportHolding&&!f.hands.Current(InteractionHand::Left));
        }
        std::cout<<"{\"asset\":\""<<p->geometry->asset<<"\",\"path\":\""<<p->geometry->configurationPath
                 <<"\",\"resource_cycles\":12,\"mock_native\":true,\"runtime_admission\":false}\n";
    }
#ifndef FVR_EXPECTED_RESOURCE_PROFILE_COUNT
#define FVR_EXPECTED_RESOURCE_PROFILE_COUNT 12
#endif
    CHECK(profiles==FVR_EXPECTED_RESOURCE_PROFILE_COUNT&&cycles==12*FVR_EXPECTED_RESOURCE_PROFILE_COUNT);return 0;
}
int Disabled(){Bc2MagazineResourceProbe p;auto in=Input(1);const auto before=in;p.Prepare(in,{},"",{},{},0,0,0);
    CHECK(in.hands[0].grip.position.x==before.hands[0].grip.position.x&&!p.CancelConsumer());return 0;}
}
int main(){
#ifdef FVR_RESOURCE_PROBE_VARIANTS
    if(ExactPublishedVariants())return 1;
#endif
    if(DiagnosticFirstActiveCause()||PreparedBatchResourceConsumers()||CompletionPublicationBetweenCallbacks()||DoneShutdownDoesNotCreateNewWork()||Disabled()||RealConsumerProfiles()||CombinedPersistent()||NoReceiptOrRenderer()||DelayedReceiptRelease()||InterruptedInputs()||SupportEvidenceRequired()||ExactSeatReceiptRequired()||ExistingEvidenceCannotBeReplayed()||StartupWaitsForActualResource()||SetupReceiverHasOnlyFiniteActions())return 1;
    std::cout<<"Resource controller driver: XM8/AEK, zero/partial/full originals, return/discard/refill, delayed receipt, hand/support release, N-3 geometry, interruptions passed (mock native Updates and renderer pairs).\n";}
