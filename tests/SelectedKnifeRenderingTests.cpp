#include "Test.h"
#include "Bc2SelectedKnifeRendering.h"
#include "Bc2ChestKnifeRendering.h"
#include "Bc2SelectedKnifeNativeReader.h"
#include "Bc2SelectedKnifeMotion.h"
#include "fvr/interaction/TrackedRig.h"
#include <iostream>
#include <limits>
using namespace fvr;using namespace fvr::bc2;
namespace {
template<std::size_t N> void Text(std::array<char,N>& out,std::string_view text){out={};std::copy(text.begin(),text.end(),out.begin());}
math::Matrix4 At(float x=0,float y=0,float z=0){math::Matrix4 m;for(unsigned n=0;n<4;++n)m.values[n][n]=1;m.values[3]={x,y,z,1};return m;}
bool Near(const math::Matrix4& a,const math::Matrix4& b){for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)if(std::abs(a.values[r][c]-b.values[r][c])>.0001f)return false;return true;}
struct Fixture {
    static constexpr std::int64_t now=1000000000;
    static constexpr std::uint64_t fingerprint=0xa7f219a1426216abull;
    SelectedKnifeRenderSource source{};
    std::shared_ptr<SelectedMeshesSnapshot> mesh=std::make_shared<SelectedMeshesSnapshot>();
    RigIdentity rig{10,11,21,22,23,24,25,26,27,147,true};
    Fixture(){
        auto& n=source.native;n.owner={9,10,11,15,12,18,14};n.sequence=20;n.observedNs=now-1000;n.inventory=30;n.selectedSlot=0;
        n.config.weaponData=16;n.config.firingData=40;n.config.primaryFire=41;n.config.fireInputAction=37;
        n.config.fireLogicType=0;n.config.baseCapacity=n.config.numberOfMagazines=-1;
        Text(n.config.assetName,"KNV-1");Text(n.config.assetPath,"Objects/Weapons/Handheld/Knife_Equipable/Knife_Equipable");
        n.branches[0].wrapperOffset=0x3c;n.branches[1].wrapperOffset=0x40;n.branches[0].address=50;n.branches[1].address=51;
        source.equipment={15,16,17,{}};Text(source.equipment.asset,"KNV-1");
        source.input={{(std::uint64_t(11)<<32)|10,12,13,14},20,now-10000,now+90000000,now,true,{true,true}};source.gameplayOwned=true;
        mesh->owner=n.owner;mesh->sequence=19;mesh->observedNs=now-10000000;mesh->deadlineNs=now+190000000;
        mesh->weaponData=16;mesh->stateTypeInfo=1;mesh->meshTypeInfo=2;mesh->inventory=30;mesh->selectedSlot=0;
        Text(mesh->weaponName,"KNV-1");mesh->configurationPathVerified=true;mesh->configurationPathPointer=31;Text(mesh->configurationPath,n.config.assetPath.data());
        mesh->stateCount=1;mesh->soleConfiguredArray=5;mesh->states[0].array=5;mesh->states[0].state=6;mesh->states[0].count=1;
        auto& m=mesh->states[0].meshes[0];m.address=7;m.typeInfo=2;m.namePointer=8;Text(m.assetPath,private_knife::KnifeMesh);
    }
    auto Bind(){return BindSelectedKnifeGrip(source,mesh,rig,fingerprint,now);}
};
int ExactSelectedGeometry(){
    Fixture f;auto b=f.Bind();CHECK(b&&b->grip.profile==&SelectedKnifeGripProfiles()[0]);
    CHECK(b->grip.profile!=&ChestKnifeGripProfiles()[0]);CHECK(!AuthoredRifleSupport(b->grip));
    CHECK(b->grip.profile->rightInWeapon.values==private_knife::RightHandInKnife.values);
    CHECK(!SelectedKnifeRenderBinding::attackCapability&&!SelectedKnifeRenderBinding::damageCapability);
    CHECK(SelectedKnifeBindingCurrent(*b,f.source,f.mesh.get(),f.rig,Fixture::fingerprint,Fixture::now));
    return 0;
}
int RejectLookalikesAndMissingEvidence(){
    for(unsigned fault=0;fault<15;++fault){Fixture f;
        if(fault==0)f.source.native.config.fireInputAction=8;
        if(fault==1)Text(f.source.native.config.assetPath,"Objects/Weapons/Handheld/Knife/Knife");
        if(fault==2)f.source.native.selectedSlot=7;
        if(fault==3)f.mesh->configurationPathVerified=false;
        if(fault==4)f.mesh->configurationPathPointer=0;
        if(fault==5)Text(f.mesh->configurationPath,"Objects/Weapons/Handheld/Knife/Knife");
        if(fault==6)Text(f.mesh->states[0].meshes[0].assetPath,"Objects/Weapons/Other/Knife_Mesh");
        if(fault==7){f.mesh->states[0].count=2;f.mesh->states[0].meshes[1]=f.mesh->states[0].meshes[0];}
        if(fault==8)++f.mesh->owner.equipGeneration;
        if(fault==9)++f.mesh->inventory;
        if(fault==10)++f.source.equipment.data;
        if(fault==11)f.source.native.branches[1].address=f.source.native.branches[0].address;
        if(fault==12)f.source.gameplayOwned=false;
        if(fault==13)f.source.input.tracked[1]=false;
        if(fault==14)f.source.input.released[1]=true;
        CHECK(!f.Bind());
    }
    Fixture f;CHECK(!BindSelectedKnifeGrip(f.source,f.mesh,f.rig,Fixture::fingerprint+1,Fixture::now));return 0;
}
int LifetimeCannotRenewOldCalibration(){
    Fixture f;const auto b=f.Bind();CHECK(b);const auto receipt=MakeSelectedKnifeRenderReceipt(*b,At());CHECK(receipt);
    auto current=f.source;current.input.sequence++;current.native.sequence++;current.input.observedNs+=20000;current.native.observedNs+=20000;
    current.input.nowNs+=20000;current.input.deadlineNs+=20000;
    CHECK(SelectedKnifeRenderCurrent(*receipt,current,f.mesh.get(),f.rig,Fixture::fingerprint,Fixture::now+20000));
    auto currentMesh=*f.mesh;
    currentMesh.configurationPathVerified=false;
    CHECK(!SelectedKnifeRenderCurrent(*receipt,current,&currentMesh,f.rig,Fixture::fingerprint,Fixture::now+20000));
    currentMesh=*f.mesh;
    Text(currentMesh.configurationPath,"Objects/Weapons/Handheld/Knife/Knife");
    CHECK(!SelectedKnifeRenderCurrent(*receipt,current,&currentMesh,f.rig,Fixture::fingerprint,Fixture::now+20000));
    for(unsigned fault=0;fault<10;++fault){auto now=current;auto rig=f.rig;
        if(fault==0)++now.input.owner.equipGeneration;
        if(fault==1)++now.native.owner.equipGeneration;
        if(fault==2){++now.input.owner.actorGeneration;++now.native.owner.actorGeneration;}
        if(fault==3){++now.input.owner.space;++now.native.owner.space;}
        if(fault==4)++rig.pose;
        if(fault==5)++rig.evaluatedMatrices;
        if(fault==6)++now.native.branches[0].address;
        if(fault==7)now.gameplayOwned=false;
        if(fault==8)now.input.focused=false;
        if(fault==9)now.input.sequence=now.native.sequence=19;
        CHECK(!SelectedKnifeRenderCurrent(*receipt,now,f.mesh.get(),rig,Fixture::fingerprint,Fixture::now+20000));
    }
    // Original input/source lease cannot be refreshed with a later packet.
    current.input.observedNs=Fixture::now+80000000;current.native.observedNs=current.input.observedNs;
    current.input.nowNs=current.input.observedNs;current.input.deadlineNs=Fixture::now+170000000;
    CHECK(!SelectedKnifeRenderCurrent(*receipt,current,f.mesh.get(),f.rig,Fixture::fingerprint,f.source.input.deadlineNs));
    return 0;
}
int CalibratedBladeUsesOriginalGrip(){
    Fixture f;const auto b=f.Bind();CHECK(b);auto calibration=At();
    const auto receipt=MakeSelectedKnifeRenderReceipt(*b,calibration);CHECK(receipt);
    math::Pose grip{};grip.position={.23f,1.1f,-.42f};
    const auto result=private_knife::KnifeMotionFromGrip(grip,receipt->wristInGrip);CHECK(result);
    const auto view=math::MakeLhViewFromOpenXRPose(grip);CHECK(view);const auto pose=interaction::InverseRigid(*view);CHECK(pose);
    const auto knife=interaction::Multiply(*interaction::InverseRigid(private_knife::RightHandInKnife),*pose);
    CHECK(Near(interaction::Multiply(private_knife::RightHandInKnife,knife),*pose));
    const auto p=private_knife::BladeTipInKnife;
    const float z=p.x*knife.values[0][2]+p.y*knife.values[1][2]+p.z*knife.values[2][2]+knife.values[3][2];
    CHECK(std::abs(result->tip.z-z)<.00001f);
    calibration.values[3][0]=.1f;CHECK(!MakeSelectedKnifeRenderReceipt(*b,calibration));
    calibration=At();calibration.values[0][0]=2;CHECK(!MakeSelectedKnifeRenderReceipt(*b,calibration));
    // Ordinary chest profile pointer cannot be smuggled into selected receipt.
    auto other=*b;other.grip.profile=&ChestKnifeGripProfiles()[0];CHECK(!MakeSelectedKnifeRenderReceipt(other,At()));
    return 0;
}
int BladeWorldAndNativeForward(){
    Fixture f;const auto binding=f.Bind();CHECK(binding);
    const auto receipt=MakeSelectedKnifeRenderReceipt(*binding,At());CHECK(receipt);
    interaction::InputFrame input;input.generation=20;input.spaceGeneration=14;input.predictedNs=Fixture::now;
    input.focused=input.headValid=input.hands[1].gripTracked=true;
    input.hands[1].grip.position={.23f,-.2f,-.42f};
    const auto mapped=selectedKnife::MapGeometry(input,*receipt,At(100,20,-30));CHECK(mapped);
    // Independent physical-vector expectation: native world reflects Z once;
    // its search cone uses +basis row2, not the reflected camera's backwards row.
    CHECK(std::abs(mapped->nativeAcquisition.values[3][0]-(100+mapped->motion.tip.x))<.00002f);
    CHECK(std::abs(mapped->nativeAcquisition.values[3][1]-(20+mapped->motion.tip.y))<.00002f);
    CHECK(std::abs(mapped->nativeAcquisition.values[3][2]-(-(-30+mapped->motion.tip.z)))<.00002f);
    CHECK(std::abs(mapped->nativeAcquisition.values[2][0]-mapped->motion.direction.x)<.00002f);
    CHECK(std::abs(mapped->nativeAcquisition.values[2][1]-mapped->motion.direction.y)<.00002f);
    CHECK(std::abs(mapped->nativeAcquisition.values[2][2]+mapped->motion.direction.z)<.00002f);
    input.head.orientation={0,std::sin(.4f),0,std::cos(.4f)};
    const auto turned=selectedKnife::MapGeometry(input,*receipt,At(110,20,-40));CHECK(turned);
    CHECK(turned->motion.tip==mapped->motion.tip&&turned->motion.direction==mapped->motion.direction);
    input.worldUnitsPerMeter=2;
    const auto scaled=selectedKnife::MapGeometry(input,*receipt,At());CHECK(scaled);
    CHECK(scaled->motion.tip==mapped->motion.tip);
    CHECK(std::abs(scaled->nativeAcquisition.values[3][0]-2*mapped->motion.tip.x)<.00002f);
    CHECK(std::abs(scaled->nativeAcquisition.values[3][2]+2*mapped->motion.tip.z)<.00002f);
    input.hands[1].gripTracked=false;CHECK(!selectedKnife::MapGeometry(input,*receipt,At()));
    return 0;
}
int AcquisitionCopyKeepsSourcesPaddingAndRange(){
    auto original=At(1,2,3),blade=At(1.2f,2.1f,3.1f);std::array<std::byte,64> bytes{};
    std::memcpy(bytes.data(),&original,64);
    for(unsigned r=0;r<4;++r){const unsigned pad=0xdead0000u+r;std::memcpy(bytes.data()+r*16+12,&pad,4);}
    const auto before=bytes;const auto copy=selectedKnife::AcquisitionCopy(bytes,blade,1);CHECK(copy&&bytes==before);
    for(unsigned r=0;r<4;++r){CHECK(!std::memcmp(copy->data()+r*16+12,before.data()+r*16+12,4));
        CHECK(!std::memcmp(copy->data()+r*16,blade.values[r].data(),12));}
    blade.values[3][0]=4;CHECK(!selectedKnife::AcquisitionCopy(bytes,blade,1));
    CHECK(!selectedKnife::AcquisitionCopy(std::span(bytes).first(63),At(),1));
    CHECK(!selectedKnife::AcquisitionCopy(bytes,At(),0));
    // Exact SP_01 authored and repeated live Shot values. Direction is a
    // forward vector used to construct a basis, not three zero Euler angles.
    CHECK(selectedKnife::NeutralShotOffsets({0,0,1},{0,0,0}));
    CHECK(!selectedKnife::NeutralShotOffsets({0,0,0},{0,0,0}));
    CHECK(!selectedKnife::NeutralShotOffsets({0,0,-1},{0,0,0}));
    CHECK(!selectedKnife::NeutralShotOffsets({0,0,2},{0,0,0}));
    for(unsigned n=0;n<3;++n){
        std::array<float,3> direction{0,0,1},position{};
        direction[n]+=.1f;CHECK(!selectedKnife::NeutralShotOffsets(direction,position));
        direction={0,0,1};position[n]=.1f;CHECK(!selectedKnife::NeutralShotOffsets(direction,position));
        for(const float invalid:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
            direction={0,0,1};position={};direction[n]=invalid;
            CHECK(!selectedKnife::NeutralShotOffsets(direction,position));
            direction={0,0,1};position[n]=invalid;
            CHECK(!selectedKnife::NeutralShotOffsets(direction,position));
        }
    }
    return 0;
}
struct NativeFixture {
    static constexpr unsigned base=0x400000,cp=0x10000,cs=0x12000,weak=0x13000,ci=0x14000,ca=0x15000,cw=0x16000,
        data=0x17000,fd=0x18000,primary=0x19000,cf0=0x1a000,cf1=0x1b000,clientEffects=0x1c000,manager=0x20000,players=0x21000,
        sp=0x22000,ss=0x24000,si=0x26000,sa=0x27000,sw=0x28000,effects=0x29000,firing=0x2b000,
        soldierData=0x2d000,inventoryData=0x2e000,target=0x30000,targetWeak=0x31000;
    std::vector<std::byte> bytes=std::vector<std::byte>(0x30000),image=std::vector<std::byte>(0x10000);
    ReloadServerBinding binding{};ReloadStateBinding state{};ReloadStateSnapshot client{};
    unsigned failType=0,failRead=0,raceAt=0,raceCount=0,raceTarget=0,raceValue=0;
    std::byte* At(unsigned at){return at>=base?image.data()+at-base:bytes.data()+at-0x10000;}
    template<class T>void Put(unsigned at,T value){std::memcpy(At(at),&value,sizeof(value));}
    NativeFixture(){
        binding.preferredBase=base;binding.imageSize=0x1913000;binding.contextRva=0x100;binding.managerVtableRva=0x200;binding.controlledGetterRva=0x300;
        state.preferredBase=base;state.imageSize=0x1913000;state.firingVtableRva=0x500;
        client.owner={cp,cs,weak,cw,1,2,3};client.sequence=4;client.observedNs=100;client.inventory=ci;client.selectedSlot=0;client.soldierFlags=1;
        client.config.weaponData=data;client.config.firingData=fd;client.config.primaryFire=primary;client.config.ammoAddress=primary+0x170;
        client.config.fireInputAction=37;client.config.fireLogicType=0;client.config.baseCapacity=client.config.numberOfMagazines=-1;
        Text(client.config.assetName,"KNV-1");Text(client.config.assetPath,"Objects/Weapons/Handheld/Knife_Equipable/Knife_Equipable");
        client.branches[0].address=cf0;client.branches[1].address=cf1;client.branches[0].wrapperOffset=0x3c;client.branches[1].wrapperOffset=0x40;
        for(auto& b:client.branches){b.currentState=b.nextState=2;b.loaded=b.reserve=-1;}
        Put<std::uint8_t>(cp+0xccd,8);Put(cp+0xc54,weak);Put(weak,cs+4);Put(cs+0x220,cp);Put(cp+0xc68,cs);Put<std::uint8_t>(cs+0x114,1);
        Put(cs+0x24c,ci);Put(cs+0x260,ca);Put(cs+0x264,ca+8);Put(ca,cw);Put(ci+0x14c,0u);Put(cw+4,data);Put(data+0x98,fd);Put(fd+0x40,primary);Put(primary+0x4c,37u);
        Put(cw+0x38,clientEffects);Put(clientEffects+0x10,fd);Put(cw+0x3c,cf0);Put(cw+0x40,cf1);
        Put(base+0x108,manager);Put(manager,base+0x200);Put(manager+4,64u);Put(manager+0x6c,players);Put(players,sp);Put(cp+0x154,0u);Put(sp+0x154,0u);
        Put(sp,base+0x400);Put(base+0x430,base+0x300);Put(sp+0xc3c,ss);Put(sp+0xc6c,ss);Put(ss,0x1424850u);Put(ss+0xc,soldierData);Put(cs+0xc,soldierData);Put(ss+0x220,sp);
        Put(ss+0x2b4,si);Put(si+4,inventoryData);Put(ss+0x2b8,sa);Put(ss+0x2bc,sa+8);Put(sa,sw);Put(si+0x14c,0u);
        Put(sw+4,data);Put(sw+0xc,effects);Put(sw+0x10,firing);Put(effects,0x1417cd0u);Put(effects+0x10,fd);Put(effects+0x128,sp);Put(effects+0xd0,0x1417c70u);
        for(auto f:{cf0,cf1,firing}){Put(f,base+0x500);Put(f+8,fd);Put(f+12,primary+0x170);Put(f+0x3c,2u);Put(f+0x40,2u);Put(f+0x44,2u);Put(f+0x50,0.f);Put(f+0x7c,-1);Put(f+0x80,-1);}
        Put(target,0x1424850u);Put(targetWeak,target+4);
    }
    ReloadStateMemory Memory(){return {this,[](void* context,unsigned at,void* out,std::size_t size){auto& f=*static_cast<NativeFixture*>(context);
        if(at==f.failRead||!size||!((at>=0x10000&&std::uint64_t(at)+size<=0x40000)||(at>=base&&std::uint64_t(at)+size<=base+0x10000)))return false;
        if(at==f.raceAt&&++f.raceCount==2)f.Put(f.raceTarget,f.raceValue);
        std::memcpy(out,f.At(at),size);return true;
    },[](void* context,unsigned at,const char* name){const auto& f=*static_cast<NativeFixture*>(context);if(at==f.failType)return false;
        return ((at==ss||at==target)&&!std::strcmp(name,"ServerSoldierEntity"))||(at==inventoryData&&!std::strcmp(name,"WeaponSwitchingData"))||
            (at==clientEffects&&!std::strcmp(name,"ClientMeleeFiringEffects"))||(at==effects&&!std::strcmp(name,"ServerMeleeFiringEffects"));}};}
    auto Read(){return selectedKnife::ReadNative(Memory(),binding,state,base,client);}
};
int SelectedNativeOwnsMeleeWithoutFirearmAnchor(){
    NativeFixture f;const auto bytes=f.bytes;const auto native=f.Read();CHECK(native&&f.bytes==bytes&&selectedKnife::Ready(*native));
    CHECK(native->links.effects==f.effects&&native->links.soldier==f.ss&&native->clientEffects==f.clientEffects&&native->links.callback==0);
    CHECK(selectedKnife::OwnerCurrent(f.Memory(),f.binding,f.state,f.base,*native));
    f.Put<std::uint8_t>(f.cs+0x114,0x19);CHECK(selectedKnife::OwnerCurrent(f.Memory(),f.binding,f.state,f.base,*native));
    f.Put(f.firing+0x3c,5u);const auto busy=f.Read();CHECK(busy&&!selectedKnife::Ready(*busy));
    // Source freshness does not become a native-outcome deadline. Exact owned
    // callbacks can finish after the original input expired.
    auto old=*native;old.client.observedNs=1;CHECK(selectedKnife::OwnerCurrent(f.Memory(),f.binding,f.state,f.base,old));
    f.Put(f.ci+0x14c,1u);f.Put(f.si+0x14c,1u);
    CHECK(!selectedKnife::OwnerCurrent(f.Memory(),f.binding,f.state,f.base,*native));
    CHECK(selectedKnife::AcceptedOwnerRetained(f.Memory(),f.binding,f.state,f.base,*native));
    f.Put(f.effects+0x128,f.sp+4);CHECK(!selectedKnife::AcceptedOwnerRetained(f.Memory(),f.binding,f.state,f.base,*native));
    return 0;
}
int SelectedNativeRejectsLookalikesAndChangedOwners(){
    for(auto [at,value]:std::initializer_list<std::pair<unsigned,unsigned>>{{NativeFixture::cp+0xc60,1},
        {NativeFixture::sp+0xc6c,NativeFixture::ss+4},{NativeFixture::ss+0xc,1},{NativeFixture::si+0x14c,7},
        {NativeFixture::effects,0x1417cd4},{NativeFixture::effects+0xd0,0x1417c74},{NativeFixture::effects+0x128,NativeFixture::sp+4},
        {NativeFixture::effects+0x10,NativeFixture::fd+4},{NativeFixture::clientEffects+0x10,NativeFixture::fd+4},
        {NativeFixture::cw+0x40,NativeFixture::cf0},{NativeFixture::cf1+8,0},{NativeFixture::primary+0x4c,8},
        {NativeFixture::sa+4,NativeFixture::sw},{NativeFixture::ca+4,NativeFixture::cw},{NativeFixture::manager+4,257}}){
        NativeFixture f;const auto native=f.Read();CHECK(native);f.Put(at,value);CHECK(!f.Read());
        CHECK(!selectedKnife::OwnerCurrent(f.Memory(),f.binding,f.state,f.base,*native));}
    NativeFixture ordinary;ordinary.client.config.fireInputAction=8;CHECK(!ordinary.Read());
    NativeFixture path;Text(path.client.config.assetPath,"Objects/Weapons/Handheld/Knife/Knife");CHECK(!path.Read());
    NativeFixture race;race.raceAt=race.base+0x108;race.raceTarget=race.sa+4;race.raceValue=race.sw;CHECK(!race.Read());
    return 0;
}
int SelectedTargetRetainsExactWeakIdentity(){
    NativeFixture f;const auto absent=selectedKnife::ReadTarget(f.Memory(),f.effects);CHECK(absent&&!absent->handle&&!absent->soldier);
    f.Put(f.effects+0x130,f.targetWeak);const auto target=selectedKnife::ReadTarget(f.Memory(),f.effects);
    CHECK(target&&target->handle==f.targetWeak&&target->soldier==f.target);
    f.Put(f.targetWeak,0u);const auto retired=selectedKnife::ReadTarget(f.Memory(),f.effects);CHECK(retired&&retired->handle==f.targetWeak&&!retired->soldier);
    f.Put(f.targetWeak,f.target+4);f.raceAt=f.targetWeak;f.raceTarget=f.targetWeak;f.raceValue=0;CHECK(!selectedKnife::ReadTarget(f.Memory(),f.effects));
    return 0;
}
int PublishedContinuityDelayedMissAndNextJab(){
    using namespace interaction;constexpr std::int64_t ms=1000000;
    SelectedMeleeAttack policy;HandInteraction hands;SelectedMeleeSample sample{};
    sample.motion.source={{1,2,3,4},1,1000*ms,1100*ms,1000*ms,true,{true,true},{false,false}};
    sample.identity={sample.motion.source.owner,{42,1},{53,1},7};
    sample.selected=sample.geometryCurrent=sample.motion.attackAvailable=true;
    unsigned emitted=0;std::optional<SelectedMeleeIntent> intent;
    const auto step=[&](float dz=0){
        auto& s=sample.motion.source;++s.sequence;s.observedNs+=10*ms;s.nowNs=s.observedNs;s.deadlineNs=s.nowNs+100*ms;
        sample.motion.tip.z+=dz;hands.Update(s);const HandContactProof contact{{11,1},s.sequence,s.deadlineNs,true};
        if(const auto held=hands.Current(InteractionHand::Right)){const auto renewed=hands.Renew(s,held->token,contact);
            if(renewed.claim)sample.motion.hold=renewed.claim->token;
        }else {const auto acquired=hands.Acquire(s,{s.owner,InteractionHand::Right,HandClaimKind::GunHold,sample.identity.item,contact,1,0});
            if(acquired.claim)sample.motion.hold=acquired.claim->token;}
        const auto result=policy.Update(sample,hands);if(result.intent){++emitted;intent=result.intent;}
    };
    for(unsigned n=0;n<20;++n)step();for(unsigned n=0;n<8;++n)step(.012f);
    CHECK(emitted==1&&intent&&intent->cause==SelectedMeleeCause::Stroke);
    const auto original=*intent;
    for(unsigned n=0;n<30;++n){step();CHECK(selectedKnife::PublishedSourceContinues(original,sample.motion.source,7));}
    CHECK(sample.motion.source.nowNs>original.deadlineNs&&policy.PendingState()==SelectedMeleePending::Staged);
    CHECK(policy.Pending()->sourceSequence==original.sourceSequence&&policy.Pending()->deadlineNs==original.deadlineNs);
    CHECK(policy.Resolve(original.serial,original.identity,SelectedMeleeOutcome::Submitted));
    // A native acquisition can validly return no weak target. Its later exact
    // attack return completes a miss, and must not leave the next jab latched.
    NativeFixture native;const auto missed=selectedKnife::ReadTarget(native.Memory(),native.effects);
    CHECK(missed&&!missed->handle&&!missed->soldier);
    CHECK(selectedKnife::ReadTarget(native.Memory(),native.effects)==missed);
    CHECK(policy.Resolve(original.serial,original.identity,SelectedMeleeOutcome::Completed));
    for(unsigned n=0;n<20;++n)step();for(unsigned n=0;n<8;++n)step(.012f);
    CHECK(emitted==2&&intent->serial!=original.serial);
    for(unsigned fault=0;fault<8;++fault){auto interrupted=sample.motion.source;auto epoch=std::uint64_t(7);
        if(fault==0)interrupted.focused=false;
        if(fault==1)interrupted.tracked[1]=false;
        if(fault==2)interrupted.released[1]=true;
        if(fault==3)++interrupted.owner.equipGeneration;
        if(fault==4)++interrupted.owner.space;
        if(fault==5)interrupted.nowNs=interrupted.deadlineNs;
        if(fault==6)++epoch;
        if(fault==7)interrupted.sequence=intent->sourceSequence-1;
        CHECK(!selectedKnife::PublishedSourceContinues(*intent,interrupted,epoch));}
    // Interruption revokes mapping; it cannot manufacture no-effect or complete
    // native work. A subsequently matched native return can still settle it.
    CHECK(policy.Resolve(intent->serial,intent->identity,SelectedMeleeOutcome::Unknown));
    CHECK(!policy.Resolve(intent->serial,intent->identity,SelectedMeleeOutcome::RejectedNoEffect));
    CHECK(policy.Resolve(intent->serial,intent->identity,SelectedMeleeOutcome::Completed));
    return 0;
}
}
int main(){if(ExactSelectedGeometry()||RejectLookalikesAndMissingEvidence()||LifetimeCannotRenewOldCalibration()||CalibratedBladeUsesOriginalGrip()||
    BladeWorldAndNativeForward()||AcquisitionCopyKeepsSourcesPaddingAndRange()||SelectedNativeOwnsMeleeWithoutFirearmAnchor()||
    SelectedNativeRejectsLookalikesAndChangedOwners()||SelectedTargetRetainsExactWeakIdentity()||PublishedContinuityDelayedMissAndNextJab())return 1;
    std::cout<<"Ten selected-knife geometry, ownership and native-argument groups passed; mocked native memory, no game hit claim\n";}
