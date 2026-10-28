#include "Test.h"
#include "Bc2SelectedKnifeRendering.h"
#include "Bc2ChestKnifeRendering.h"
#include "Bc2SelectedKnifeNativeReader.h"
#include "Bc2SelectedKnifeMotion.h"
#include "Bc2SelectedKnifeAvailability.h"
#include "Bc2SelectedKnifeCode.h"
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
int SelectedOwnerDiagnosticsPreserveAdmission(){
    using selectedKnife::OwnerDiagnostic;using selectedKnife::OwnerPhase;
    for(const bool retained:{false,true}){
        const auto check=[&](NativeFixture& f,const selectedKnife::Native& n,OwnerDiagnostic* d=nullptr){
            return retained?selectedKnife::AcceptedOwnerRetained(f.Memory(),f.binding,f.state,f.base,n,d):
                selectedKnife::OwnerCurrent(f.Memory(),f.binding,f.state,f.base,n,d);
        };
        // Stable permitted source-flag changes remain permitted, with no stale
        // diagnostic or post-rejection inventory samples on a successful read.
        {NativeFixture f;const auto n=f.Read();CHECK(n);f.Put<std::uint8_t>(f.cs+0x114,0x19);
            OwnerDiagnostic d;d.phase=OwnerPhase::SourceLinks;d.inventoryReadMask=3;
            CHECK(check(f,*n)&&check(f,*n,&d));CHECK(d.phase==OwnerPhase::None&&d.inventoryReadMask==0);}
        for(unsigned fault=0;fault<4;++fault){
            NativeFixture f;const auto n=f.Read();CHECK(n);OwnerDiagnostic d;
            if(fault==0)f.Put(f.effects+0x128,f.sp+4); // A different native player backlink.
            if(fault==1)f.failRead=f.cs+0x24c;
            if(fault==2)f.failType=f.ss;
            if(fault==3){f.Put(f.cs+0x248,f.ci+0x80);f.Put<std::uint8_t>(f.cs+0x114,0);}
            const auto bytes=f.bytes;
            CHECK(!check(f,*n)&&!check(f,*n,&d)&&f.bytes==bytes);
            CHECK(d.phase==OwnerPhase::ScopeBefore&&!d.beforeKnown&&!d.afterKnown);
            CHECK(d.sourceSoldierFlags==1&&d.sourceInventory==f.ci&&d.sourceDifference.count==0);
            if(fault==0){CHECK(!std::strcmp(d.scope.stage,"server_effects_player"));
                CHECK(d.scope.address==f.effects+0x128&&d.scope.bytes==4&&d.scope.expected==f.sp&&d.scope.observed==f.sp+4);
                CHECK(!d.scope.readFailed&&!d.scope.typeFailed);}
            if(fault==1){CHECK(!std::strcmp(d.scope.stage,"client_inventory_read"));
                CHECK(d.scope.address==f.cs+0x24c&&d.scope.bytes==4&&d.scope.readFailed&&!d.scope.typeFailed);
                CHECK(d.inventoryReadMask==1);}
            if(fault==2){CHECK(!std::strcmp(d.scope.stage,"server_soldier_type"));
                CHECK(d.scope.address==f.ss&&d.scope.typeFailed&&!d.scope.readFailed);}
            if(fault==3){CHECK(!std::strcmp(d.scope.stage,"client_inventory"));
                CHECK(d.scope.expected==f.ci&&d.scope.observed==f.ci+0x80&&d.beforeSoldierFlags==0&&d.beforeInventory==f.ci+0x80);
                CHECK(d.inventoryReadMask==3&&d.inventory248==f.ci+0x80&&d.inventory24c==f.ci);
                // Later samples of both slots do not retry the rejected branch.
                CHECK(!check(f,*n));}
        }
        // Raw and normalized source deltas are distinct: only0x18 is tolerated.
        {NativeFixture f;const auto n=f.Read();CHECK(n);f.Put<std::uint8_t>(f.cs+0x114,0x39);OwnerDiagnostic d;
            CHECK(!check(f,*n)&&!check(f,*n,&d));CHECK(d.phase==OwnerPhase::SourceLinks&&d.beforeKnown&&d.afterKnown);
            CHECK(d.sourceSoldierFlags==1&&d.beforeSoldierFlags==0x39&&d.afterSoldierFlags==0x39);
            CHECK(d.bracketDifference.count==0&&d.sourceDifference.count==1&&d.normalizedDifference.count==1);
            CHECK(!std::strcmp(d.sourceDifference.first[0].field,"clientSoldierFlags"));
            CHECK(d.sourceDifference.first[0].expected==1&&d.sourceDifference.first[0].observed==0x39);
            CHECK(d.normalizedDifference.first[0].expected==1&&d.normalizedDifference.first[0].observed==0x21);}
        // Flags outside the measured context mask and inventory replacement
        // remain rejected between reads, with and without diagnostic output.
        for(const bool diagnose:{false,true})for(unsigned fault=0;fault<2;++fault){
            NativeFixture f;const auto n=f.Read();CHECK(n);OwnerDiagnostic d;
            f.Put(f.cs+0x248,f.ci+0x80);f.raceAt=f.cp+0xccd;f.raceTarget=f.cs+0x114;f.raceValue=fault?0:0x21;
            CHECK(!check(f,*n,diagnose?&d:nullptr));
            if(!diagnose)continue;
            CHECK(d.beforeKnown&&d.beforeSoldierFlags==1);
            if(!fault){CHECK(d.phase==OwnerPhase::BracketLinks&&d.afterKnown&&d.afterSoldierFlags==0x21);
                CHECK(d.bracketDifference.count==1&&!std::strcmp(d.bracketDifference.first[0].field,"clientSoldierFlags"));
                CHECK(d.bracketDifference.first[0].expected==1&&d.bracketDifference.first[0].observed==0x21&&d.normalizedDifference.count==0);
            }else{CHECK(d.phase==OwnerPhase::ScopeAfter&&!d.afterKnown&&!std::strcmp(d.scope.stage,"client_inventory"));
                CHECK(d.afterSoldierFlags==0&&d.afterInventory==f.ci+0x80&&d.inventory248==f.ci+0x80&&d.inventory24c==f.ci);}
        }
        {NativeFixture f;const auto n=f.Read();CHECK(n);auto replaced=*n;replaced.clientEffects+=4;OwnerDiagnostic d;
            CHECK(!check(f,replaced)&&!check(f,replaced,&d));CHECK(d.phase==OwnerPhase::SourceEffects&&d.beforeKnown&&d.afterKnown);
            CHECK(d.sourceEffects==f.clientEffects+4&&d.beforeEffects==f.clientEffects&&d.afterEffects==f.clientEffects);}
    }
    // The retained-native path still tolerates selection changes only; the
    // ordinary acquisition path retains its stricter currently-selected gate.
    NativeFixture f;const auto n=f.Read();CHECK(n);f.Put(f.ci+0x14c,1u);f.Put(f.si+0x14c,1u);
    OwnerDiagnostic current,retained;
    CHECK(!selectedKnife::OwnerCurrent(f.Memory(),f.binding,f.state,f.base,*n,&current));
    CHECK(current.phase==OwnerPhase::ScopeBefore&&!std::strcmp(current.scope.stage,"client_selected"));
    CHECK(selectedKnife::AcceptedOwnerRetained(f.Memory(),f.binding,f.state,f.base,*n,&retained)&&retained.phase==OwnerPhase::None);
    return 0;
}
int SelectedOwnerContextBracket(){
    using selectedKnife::OwnerDiagnostic;using selectedKnife::OwnerPhase;
    for(const bool retained:{false,true})for(const bool diagnose:{false,true}){
        const auto check=[&](NativeFixture& f,const selectedKnife::Native& n,OwnerDiagnostic& d){
            return retained?selectedKnife::AcceptedOwnerRetained(f.Memory(),f.binding,f.state,f.base,n,diagnose?&d:nullptr):
                selectedKnife::OwnerCurrent(f.Memory(),f.binding,f.state,f.base,n,diagnose?&d:nullptr);
        };
        // Reproduce motion42702's3->19 and both directions of every measured
        // context-bit combination. Exact actor, item, inventory and effects
        // remain the same through the production callback-time reader.
        for(unsigned before:{0u,8u,16u,24u})for(unsigned after:{0u,8u,16u,24u}){
            NativeFixture f;f.Put<std::uint8_t>(f.cs+0x114,3);f.client.soldierFlags=3;
            const auto n=f.Read();CHECK(n);f.Put<std::uint8_t>(f.cs+0x114,std::uint8_t(3|before));
            f.raceAt=f.cp+0xccd;f.raceTarget=f.cs+0x114;f.raceValue=3|after;OwnerDiagnostic d;
            CHECK(check(f,*n,d));CHECK(f.raceCount==2&&d.phase==OwnerPhase::None&&d.inventoryReadMask==0);
        }
        // Every other flag stays exact, including bit0 even if both inventory
        // pointers happen to be equal. Context normalization cannot hide it.
        for(unsigned changed:{1u,2u,4u,32u,64u,128u}){
            NativeFixture f;f.Put<std::uint8_t>(f.cs+0x114,3);f.client.soldierFlags=3;
            const auto n=f.Read();CHECK(n);f.Put(f.cs+0x248,f.ci);
            f.raceAt=f.cp+0xccd;f.raceTarget=f.cs+0x114;f.raceValue=3^changed^0x18u;OwnerDiagnostic d;
            CHECK(!check(f,*n,d));if(diagnose){CHECK(d.phase==OwnerPhase::BracketLinks);
                CHECK(d.bracketDifference.count==1&&!std::strcmp(d.bracketDifference.first[0].field,"clientSoldierFlags"));}
        }
        // Actual ownership changes still reject at the exact first check or
        // full bracket comparison; selection-completion does not excuse them.
        for(auto [at,value]:std::initializer_list<std::pair<unsigned,unsigned>>{
            {NativeFixture::weak,NativeFixture::cs+8},{NativeFixture::cs+0x24c,NativeFixture::ci+0x80},
            {NativeFixture::effects+0x128,NativeFixture::sp+4},{NativeFixture::ca,NativeFixture::cw+4},
            {NativeFixture::sa+4,NativeFixture::sw},{NativeFixture::effects+0x10,NativeFixture::fd+4},
            {NativeFixture::cw+0x40,NativeFixture::cf0},{NativeFixture::clientEffects+0x10,NativeFixture::fd+4},
            {NativeFixture::manager+4,63u},{NativeFixture::sa+4,NativeFixture::sw+0x100}}){
            NativeFixture f;const auto n=f.Read();CHECK(n);f.raceAt=f.cp+0xccd;f.raceTarget=at;f.raceValue=value;OwnerDiagnostic d;
            CHECK(!check(f,*n,d));if(diagnose)CHECK(d.phase==OwnerPhase::ScopeAfter||d.phase==OwnerPhase::BracketLinks);
        }
    }
    // New publication still requires its original exact snapshot bracket.
    NativeFixture publication;publication.raceAt=publication.cp+0xccd;
    publication.raceTarget=publication.cs+0x114;publication.raceValue=0x11;CHECK(!publication.Read());
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
int OrdinarySelectedAcquisitionBoundary(){
    // Exact ordinary Step ->6e1fa0 listener slot0 return. The old generic
    // notifier and FireLogic4's inline loop are different native paths.
    CHECK(selectedKnife::IsAcquireCaller(0x6e1fc8u+2));
    for(const auto caller:{0u,0x6dd770u,0x6e1d0au,0x6e1fc8u,0x6e1fc9u,0x6e1fcbu,0x6dd9e5u})
        CHECK(!selectedKnife::IsAcquireCaller(caller));
    bool stepLink=false,notificationBody=false;
    for(const auto& code:selectedKnife::Code){
        if(code.rva==0x2e64a6)stepLink=code.size==12&&code.fingerprint==0xbabaafa460ccf960ull;
        if(code.rva==0x2e1fa0)notificationBody=code.size==168&&code.fingerprint==0x5a79cdc6166428ffull;
    }
    CHECK(stepLink&&notificationBody);
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
int RepeatedGatherKeepsMeasuredStroke(){
    using namespace interaction;constexpr std::int64_t ms=1000000;
    for(const float controllerYaw:{.23f,.71f,1.13f}){
        Fixture f;const auto binding=f.Bind();CHECK(binding);
        InputFrame input;input.generation=1;input.spaceGeneration=14;input.predictedNs=1000*ms;
        input.focused=input.headValid=true;
        for(auto& hand:input.hands){hand.gripTracked=hand.aimTracked=true;hand.active=Components;}
        input.hands[0].grip.position={-.2f,-.3f,-.4f};input.hands[1].grip.position={.2f,-.3f,-.4f};
        input.hands[1].grip.orientation={0,std::sin(controllerYaw*.5f),0,std::cos(controllerYaw*.5f)};
        TrackedRig rig;const TrackedRigOwner rigOwner{10,12,15,22,13};
        const std::array<ArmAnchor,2> anchors{ArmAnchor{{-.2f,-.1f,0},{-.1f,-.2f,.1f}},
            ArmAnchor{{.2f,-.1f,0},{.1f,-.2f,.1f}}};
        SelectedMeleeAttack policy;HandInteraction hands;SelectedMeleeSample sample;
        sample.motion.source={{1,2,3,14},1,1000*ms,1100*ms,1000*ms,true,{true,true},{false,false}};
        sample.identity={sample.motion.source.owner,{42,1},{53,1},7};
        sample.selected=sample.geometryCurrent=sample.motion.attackAvailable=true;
        unsigned emitted=0,derivedChanges=0;MeleePoint axis{};std::optional<SelectedMeleeIntent> intent;
        for(unsigned frame=0;frame<28;++frame){
            if(frame>=20){
                // OpenXR +Z is reflected in the LH tracked geometry.
                input.hands[1].grip.position.x+=axis.x*.012f;
                input.hands[1].grip.position.y+=axis.y*.012f;
                input.hands[1].grip.position.z-=axis.z*.012f;
            }
            ++input.generation;input.predictedNs+=10*ms;
            auto& source=sample.motion.source;source.sequence=input.generation;
            source.observedNs=source.nowNs=input.predictedNs;source.deadlineNs=source.nowNs+100*ms;
            hands.Update(source);const HandContactProof contact{{11,1},source.sequence,source.deadlineNs,true};
            const auto held=hands.Current(InteractionHand::Right);
            const auto claim=held?hands.Renew(source,held->token,contact):
                hands.Acquire(source,{source.owner,InteractionHand::Right,HandClaimKind::GunHold,sample.identity.item,contact,1,0});
            CHECK(claim.claim);sample.motion.hold=claim.claim->token;
            MeleePoint firstTip{},firstDirection{};
            // Real renderer path: TrackedRig -> measured wrist calibration ->
            // selected receipt -> MapGeometry. Body rotation can change between
            // gathers while the complete OpenXR packet stays unchanged.
            for(unsigned gather=0;gather<3;++gather){
                math::Pose bodyPose;const float yaw=.31f+.017f*float(frame*3+gather);
                bodyPose.orientation={0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)};
                const auto bodyView=math::MakeLhViewFromOpenXRPose(bodyPose);CHECK(bodyView);
                const auto body=InverseRigid(*bodyView);CHECK(body);
                const auto targets=rig.Update(rigOwner,input,*body,At(-.2f,-.3f,.4f),At(.2f,-.3f,.4f),
                    At(.2f,-.3f,.7f),anchors,{}, {}, {},AuthoredGripAttachment{private_knife::RightHandInKnife});
                CHECK(targets&&targets->tracked[1]);
                const auto calibration=private_knife::KnifeWristCalibration(input,*body,targets->right);CHECK(calibration);
                const auto receipt=MakeSelectedKnifeRenderReceipt(*binding,*calibration);CHECK(receipt);
                const auto geometry=selectedKnife::MapGeometry(input,*receipt,*body);CHECK(geometry);
                sample.motion.tip=geometry->motion.tip;sample.motion.direction=geometry->motion.direction;
                if(!gather){firstTip=sample.motion.tip;firstDirection=sample.motion.direction;axis=firstDirection;}
                else if(sample.motion.tip!=firstTip||sample.motion.direction!=firstDirection)++derivedChanges;
                source.nowNs=source.observedNs+gather*ms;
                const auto result=policy.Update(sample,hands);
                if(result.intent){++emitted;intent=result.intent;CHECK(gather==0);}
                if(gather)CHECK(!result.intent&&result.reason==SelectedMeleeReason::Duplicate);
            }
        }
        CHECK(derivedChanges>0&&emitted==1&&intent&&intent->cause==SelectedMeleeCause::Stroke);
        CHECK(policy.Pending()->serial==intent->serial&&policy.Pending()->deadlineNs==intent->deadlineNs);
        std::cout<<"Measured repeated gather yaw "<<controllerYaw<<": "<<derivedChanges<<" geometry changes, one stroke\n";
    }
    return 0;
}
int PublishedInputSurvivesGatherUntilConsumed(){
    using namespace interaction;constexpr std::int64_t ms=1000000;
    SelectedMeleeAttack policy;HandInteraction hands;SelectedMeleeSample sample{};
    sample.motion.source={{1,2,3,4},1,1000*ms,1100*ms,1000*ms,true,{true,true},{false,false}};
    sample.identity={sample.motion.source.owner,{42,1},{53,1},7};
    sample.selected=sample.geometryCurrent=sample.motion.attackAvailable=true;
    const auto next=[&](float dz=0){
        auto& s=sample.motion.source;++s.sequence;s.observedNs+=10*ms;s.nowNs=s.observedNs;s.deadlineNs=s.nowNs+100*ms;
        sample.motion.tip.z+=dz;hands.Update(s);const HandContactProof contact{{11,1},s.sequence,s.deadlineNs,true};
        const auto held=hands.Current(InteractionHand::Right);
        const auto claim=held?hands.Renew(s,held->token,contact):
            hands.Acquire(s,{s.owner,InteractionHand::Right,HandClaimKind::GunHold,sample.identity.item,contact,1,0});
        if(claim.claim)sample.motion.hold=claim.claim->token;
        return policy.Update(sample,hands);
    };
    for(unsigned n=0;n<20;++n)CHECK(!next().intent);
    std::optional<SelectedMeleeIntent> intent;
    for(unsigned n=0;n<8&&!intent;++n)intent=next(.012f).intent;
    CHECK(intent&&intent->cause==SelectedMeleeCause::Stroke);
    const auto original=*intent;const auto firstSource=sample.motion.source;
    selectedKnife::InputDelivery delivery;
    selectedKnife::InputDeliveryAuthority authority{true,true,true,true};
    auto continuation=SelectedMeleeReason::Duplicate;
    const auto repeat=[&]{return delivery.CanRepeat(original,policy.PendingState(),continuation,sample.motion.source,
        sample.motion.hold,7,authority,sample.motion.source.nowNs);};
    std::array<std::byte,InputBytes> cache{};
    const auto gather=[&](bool fire){
        // Real native cache adapter after a fresh original Gather. A delayed
        // consumer reads only the LAST Gather, not the earlier transient bit.
        cache={};const unsigned unrelated=0x80000000u;std::memcpy(cache.data()+0x9c,&unrelated,4);
        ActionOutput out;out.active=true;out.owner=2;if(fire)out.held=out.pressed=Fire;
        InputOverride patch;
        if(!patch.Apply(cache,out,{},{},{},false,EntryAction::MeleeAttack))return false;
        patch.Commit();unsigned flags=0;std::memcpy(&flags,cache.data()+0x9c,4);
        return flags==(unrelated|(fire?(1u<<5):0u));
    };
    const auto fireInCache=[&]{unsigned flags=0;std::memcpy(&flags,cache.data()+0x9c,4);return (flags&(1u<<5))!=0;};
    CHECK(!repeat());delivery.Consumed(firstSource.nowNs);CHECK(!delivery.consumedNs);
    CHECK(gather(true));delivery.Published(firstSource.nowNs);
    // Reproduce the old loss using the actual policy's duplicate result.
    ++sample.motion.source.nowNs;
    const auto duplicate=policy.Update(sample,hands);CHECK(!duplicate.intent&&duplicate.reason==SelectedMeleeReason::Duplicate);
    CHECK(gather(bool(duplicate.intent))&&!fireInCache());
    // The correction republishes the SAME immutable command, with no new edge.
    for(unsigned n=0;n<3;++n){
        ++sample.motion.source.nowNs;CHECK(!policy.Update(sample,hands).intent);
        CHECK(repeat()&&gather(repeat())&&fireInCache());delivery.Published(sample.motion.source.nowNs);
    }
    CHECK(delivery.publications==4&&delivery.firstPublishedNs==firstSource.nowNs);
    // One predicted firing copy can leave idle before the server consumes its
    // queued input. Current native readiness cannot erase admitted delivery.
    sample.motion.attackAvailable=false;const auto advanced=next();CHECK(!advanced.intent);
    continuation=advanced.reason;CHECK(repeat()&&gather(repeat()));
    delivery.Published(sample.motion.source.nowNs);
    ++sample.motion.source.nowNs;const auto predicted=policy.Update(sample,hands);
    CHECK(!predicted.intent&&predicted.reason==SelectedMeleeReason::NativeUnavailable);
    continuation=predicted.reason;CHECK(repeat());
    CHECK(policy.Pending()->serial==original.serial&&policy.Pending()->sourceSequence==original.sourceSequence&&
        policy.Pending()->observedNs==original.observedNs&&policy.Pending()->deadlineNs==original.deadlineNs);
    const auto continuous=sample.motion.source;const auto held=sample.motion.hold;
    for(unsigned fault=0;fault<17;++fault){
        auto bad=continuous;auto token=held;auto auth=authority;auto epoch=std::uint64_t(7);
        auto now=bad.nowNs;auto pending=SelectedMeleePending::Staged;
        switch(fault){
        case 0:auth.actionsAvailable=false;break;
        case 1:auth.gameplayOwned=false;break;
        case 2:auth.ownerCurrent=false;break;
        case 3:auth.admissionCurrent=false;break;
        case 4:bad.focused=false;break;
        case 5:bad.tracked[1]=false;break;
        case 6:bad.released[1]=true;break;
        case 7:++bad.owner.equipGeneration;break;
        case 8:++bad.owner.space;break;
        case 9:bad.sequence=original.sourceSequence-1;break;
        case 10:++epoch;break;
        case 11:++token.id;break;
        case 12:now=original.deadlineNs;break;
        case 13:bad.deadlineNs=now;break;
        case 14:pending=SelectedMeleePending::Submitted;break;
        case 15:pending=SelectedMeleePending::Unknown;break;
        case 16:now=delivery.lastPublishedNs-1;break;
        }
        CHECK(!delivery.CanRepeat(original,pending,continuation,bad,token,epoch,auth,now));
    }
    // Newer source leases never renew the original deadline.
    auto later=continuous;later.observedNs=later.nowNs=original.deadlineNs;
    later.deadlineNs=later.nowNs+100*ms;++later.sequence;
    CHECK(selectedKnife::PublishedSourceContinues(original,later,7));
    CHECK(!delivery.CanRepeat(original,policy.PendingState(),continuation,later,held,7,authority,later.nowNs));
    for(const auto reason:{SelectedMeleeReason::Invalid,SelectedMeleeReason::Order,
        SelectedMeleeReason::IdentityChanged,SelectedMeleeReason::NeutralRequired,SelectedMeleeReason::Ready,
        SelectedMeleeReason::Intent,SelectedMeleeReason::SerialLimit})
        CHECK(!delivery.CanRepeat(original,policy.PendingState(),reason,continuous,held,7,authority,continuous.nowNs));
    // Policy validity is independently required even when continuity, identity
    // and the native ownership booleans still look valid.
    auto malformed=sample;malformed.trigger=std::numeric_limits<float>::quiet_NaN();
    const auto invalid=policy.Update(malformed,hands);
    CHECK(invalid.reason==SelectedMeleeReason::Invalid&&policy.PendingState()==SelectedMeleePending::Staged);
    CHECK(!delivery.CanRepeat(original,policy.PendingState(),invalid.reason,continuous,held,7,authority,continuous.nowNs));
    auto released=sample.motion.source;++released.sequence;++released.observedNs;++released.nowNs;released.released[1]=true;
    hands.Update(released);const auto lostHold=policy.Update(sample,hands);
    CHECK(lostHold.reason==SelectedMeleeReason::Invalid);
    CHECK(!delivery.CanRepeat(original,policy.PendingState(),lostHold.reason,continuous,held,7,authority,continuous.nowNs));
    delivery.Consumed(delivery.firstPublishedNs-1);CHECK(!delivery.consumedNs);
    CHECK(fireInCache());delivery.Consumed(continuous.nowNs+1);
    CHECK(delivery.consumedNs==continuous.nowNs+1&&!repeat());
    CHECK(gather(repeat())&&!fireInCache());
    CHECK(policy.PendingState()==SelectedMeleePending::Staged); // Consumption is not completion.
    delivery.Consumed(continuous.nowNs+2);CHECK(delivery.consumedNs==continuous.nowNs+1);
    // Once any cache publication succeeded, a later failed publication leaves
    // an ambiguous native outcome, not a claimed no-effect rejection.
    CHECK(policy.Resolve(original.serial,original.identity,SelectedMeleeOutcome::Unknown));
    CHECK(!policy.Resolve(original.serial,original.identity,SelectedMeleeOutcome::RejectedNoEffect));
    CHECK(!repeat());
    CHECK(policy.Resolve(original.serial,original.identity,SelectedMeleeOutcome::Completed));
    CHECK(!repeat());
    std::cout<<"Retained delivery: old duplicate Gather erases Fire; same-lease repeated Gather preserves it until consumption\n";
    return 0;
}
namespace gap_test {
using namespace fvr::interaction;
using namespace fvr::bc2::selectedKnife;
constexpr std::int64_t gapMs=1000000;
struct IncoherentFixture {
    NativeFixture native;
    unsigned fullReads=0,changeAddress=NativeFixture::firing+0x50,changeValue=0x3a83126f;
    bool racing=false;
    ReloadStateMemory Memory(){
        return {this,[](void* p,unsigned at,void* out,std::size_t size){
            auto& f=*static_cast<IncoherentFixture*>(p);
            if(f.racing&&at==NativeFixture::firing&&size==0xb0&&++f.fullReads==2)
                f.native.Put(f.changeAddress,f.changeValue);
            const auto raw=f.native.Memory();return raw.read(raw.context,at,out,size);
        },[](void* p,unsigned at,const char* name){
            auto& f=*static_cast<IncoherentFixture*>(p);const auto raw=f.native.Memory();return raw.type(raw.context,at,name);
        }};
    }
    std::optional<Native> Read(NativeReadDiagnostic* diagnostic=nullptr){
        fullReads=0;if(racing)native.Put(NativeFixture::firing+0x50,0.f);
        return ReadNative(Memory(),native.binding,native.state,native.base,native.client,diagnostic);
    }
};
int ClassifiedReadFailures(){
    IncoherentFixture f;CHECK(f.Read());f.racing=true;
    CHECK(!f.Read()); // Default API remains unavailable.
    NativeReadDiagnostic d;CHECK(!f.Read(&d));
    CHECK(d.failure==NativeReadFailure::FiringIncoherent&&d.recognitionOwner&&d.branch.readyPair);
    CHECK(d.branch.failure==private_knife::detail::BranchFailure::Incoherent&&d.branch.differenceOffset==0x50);
    CHECK(d.branch.before==0&&d.branch.after==f.changeValue);
    CHECK(!NativeRecognitionOwner::attackAuthority);
    for(unsigned at:{NativeFixture::firing,NativeFixture::firing+8,NativeFixture::firing+12}){
        IncoherentFixture bad;bad.racing=true;bad.changeAddress=at;bad.changeValue=0;
        CHECK(!bad.Read(&d)&&d.failure==NativeReadFailure::Branch&&!d.recognitionOwner);
        CHECK(d.branch.failure==private_knife::detail::BranchFailure::Identity);
    }
    IncoherentFixture malformed;malformed.racing=true;malformed.changeAddress=NativeFixture::firing+0x3c;malformed.changeValue=99;
    CHECK(!malformed.Read(&d)&&d.branch.failure==private_knife::detail::BranchFailure::Malformed&&!d.recognitionOwner);
    IncoherentFixture busy;busy.racing=true;busy.changeAddress=NativeFixture::firing+0x3c;busy.changeValue=5;
    CHECK(!busy.Read(&d)&&d.failure==NativeReadFailure::FiringIncoherent&&!d.branch.readyPair);
    for(unsigned at:{NativeFixture::effects+0x128,NativeFixture::primary+0x4c,NativeFixture::cw+0x40}){
        IncoherentFixture changed;changed.racing=true;changed.changeAddress=at;changed.changeValue=0;
        CHECK(!changed.Read(&d)&&d.failure==NativeReadFailure::ScopeAfter&&!d.recognitionOwner);
    }
    IncoherentFixture type;type.native.failType=NativeFixture::effects;
    CHECK(!type.Read(&d)&&d.failure==NativeReadFailure::ScopeBefore&&d.scope.typeFailed&&!d.recognitionOwner);
    IncoherentFixture unreadable;unreadable.native.failRead=NativeFixture::firing;
    CHECK(!unreadable.Read(&d)&&d.failure==NativeReadFailure::ScopeBefore&&d.scope.readFailed&&!d.recognitionOwner);
    // Optional classification must not change the result for coherent snapshots.
    IncoherentFixture coherent;const auto plain=coherent.Read();const auto diagnosed=coherent.Read(&d);
    CHECK(plain&&diagnosed&&d.failure==NativeReadFailure::None&&!d.recognitionOwner&&plain->links==diagnosed->links);
    return 0;
}
struct GapFixture {
    IncoherentFixture memory;
    SelectedMeleeAttack policy;
    HandInteraction hands;
    RecognitionGap gap;
    SelectedMeleeSample sample{};
    unsigned emitted=0,claims=0,held=0,repeated=0,recovered=0,rejected=0,policyCalls=0;
    bool oldReset=false,available=true;
    NativeReadDiagnostic diagnostic{};
    SelectedMeleeObservation observation{};
    std::optional<SelectedMeleeIntent> last;
    GapFixture(bool old=false):oldReset(old){
        const auto& n=memory.native.client;
        sample.motion.source={{(std::uint64_t(n.owner.weak)<<32)|n.owner.soldier,1,7,3},1,1000*gapMs,1100*gapMs,1000*gapMs,true,{true,true},{false,false}};
        sample.identity={sample.motion.source.owner,{n.owner.weapon,2},{n.config.weaponData,2},8};
        sample.selected=sample.geometryCurrent=sample.motion.attackAvailable=true;
        Refresh();Evaluate();
    }
    void Refresh(){
        const auto& s=sample.motion.source;
        memory.native.client.sequence=s.sequence;memory.native.client.observedNs=s.observedNs;
        hands.Update(s);const HandContactProof contact{{11,1},s.sequence,s.deadlineNs,true};
        const auto current=hands.Current(InteractionHand::Right);
        if(current){const auto r=hands.Renew(s,current->token,contact);if(r.claim)sample.motion.hold=r.claim->token;}
        else {const auto r=hands.Acquire(s,{s.owner,InteractionHand::Right,HandClaimKind::GunHold,sample.identity.item,contact,++claims,0});
            if(r.claim)sample.motion.hold=r.claim->token;}
    }
    bool Evaluate(){
        if(!gap.SourceContinues(sample.motion.source,sample.identity.inputEpoch)){gap.Reset();policy.InvalidateMotion();}
        const auto native=memory.Read(&diagnostic);
        sample.motion.attackAvailable=available&&native&&Ready(*native);
        if(!native){
            if(oldReset){gap.Reset();policy.InvalidateMotion();return false;}
            const auto result=gap.Skip(diagnostic,sample,hands,available,policy.PendingState());
            if(result==RecognitionGapResult::Held){++held;return false;}
            if(result==RecognitionGapResult::Repeated){++repeated;return false;}
            ++rejected;policy.InvalidateMotion();return false;
        }
        const auto recovery=gap.BeforeVerified(*native,sample,hands,available,policy.PendingState());
        if(recovery==RecognitionGapResult::Recovered)++recovered;
        if(recovery==RecognitionGapResult::Rejected){++rejected;policy.InvalidateMotion();}
        const auto result=policy.Update(sample,hands,&observation);++policyCalls;
        gap.Remember(*native,sample,hands,result,observation);
        if(result.intent){++emitted;last=result.intent;}
        return bool(result.intent);
    }
    bool Step(std::int64_t ms,float z,bool fail=false){
        ++sample.motion.source.sequence;sample.motion.source.observedNs+=ms*gapMs;
        sample.motion.source.nowNs=sample.motion.source.observedNs;
        sample.motion.source.deadlineNs=sample.motion.source.nowNs+100*gapMs;
        sample.motion.tip.z=z;memory.racing=fail;Refresh();return Evaluate();
    }
    void Rest(){for(unsigned n=0;n<20;++n)Step(10,0);}
};
int DeliveredStrokeWithOneIncoherentPacket(){
    // Exact second-stroke displacements/timing delivered in approach43107,
    // plus the stationary sample immediately before motion. The old read-gap
    // reset loses the entire gesture. No pose is fabricated in the skipped call.
    for(bool old:{true,false}){
        GapFixture f(old);f.Rest();CHECK(f.emitted==0);
        CHECK(!f.Step(19,.019f));
        const auto before=f.policyCalls;
        CHECK(!f.Step(15,.034f,true));
        const auto failedSource=f.sample.motion.source;
        CHECK(f.policyCalls==before&&!f.last);
        for(unsigned n=0;n<5;++n){f.sample.motion.source.nowNs+=gapMs;CHECK(!f.Evaluate());}
        CHECK(f.policyCalls==before&&!f.last); // Repeated Gathers add no samples.
        f.Step(16,.050f);f.Step(31,.081f);f.Step(16,.097f);f.Step(31,.120f);
        if(old){CHECK(f.emitted==0);continue;}
        CHECK(f.held==1&&f.repeated==5&&f.recovered==1&&f.rejected==0);
        CHECK(f.emitted==1&&f.last&&f.last->cause==SelectedMeleeCause::Stroke&&f.last->stroke);
        CHECK(f.last->sourceSequence>failedSource.sequence&&f.last->observedNs>failedSource.observedNs);
        CHECK(f.last->deadlineNs==f.last->observedNs+100*gapMs);
        CHECK(f.policy.PendingState()==SelectedMeleePending::Staged&&!f.gap.Active());
        // Recovery still requires the real fully current native owner check.
        const auto current=f.memory.Read();CHECK(current);
        CHECK(OwnerCurrent(f.memory.Memory(),f.memory.native.binding,f.memory.native.state,f.memory.native.base,*current));
        f.memory.native.Put(NativeFixture::effects+0x128,0u);
        CHECK(!OwnerCurrent(f.memory.Memory(),f.memory.native.binding,f.memory.native.state,f.memory.native.base,*current));
    }
    std::cout<<"Delivered 43107 stroke: old gap reset emits zero; classified one-packet gap emits exactly one after current native recovery\n";
    return 0;
}
int GapsCannotExtendAuthority(){
    for(unsigned fault=0;fault<15;++fault){
        GapFixture f;f.Rest();CHECK(!f.Step(19,.019f));CHECK(!f.Step(15,.034f,true)&&f.gap.Active());
        switch(fault){
        case 0: f.Step(16,.050f,true);break; // second distinct failed packet
        case 1: f.sample.motion.source.nowNs+=76*gapMs;f.Evaluate();break;
        case 2: f.sample.motion.source.deadlineNs=f.sample.motion.source.nowNs;f.Evaluate();break;
        case 3: f.sample.motion.source.focused=false;f.Evaluate();break;
        case 4: f.sample.motion.source.tracked[1]=false;f.Evaluate();break;
        case 5: f.sample.motion.source.released[1]=true;f.Evaluate();break;
        case 6: ++f.sample.identity.inputEpoch;f.Evaluate();break;
        case 7: f.available=false;f.Evaluate();break; // caller's menu/action gate
        case 8: f.sample.trigger=1;f.Evaluate();break; // no trigger edge queued across a gap
        case 9: f.sample.trigger=std::numeric_limits<float>::quiet_NaN();f.Evaluate();break;
        case 10: f.hands.Release(f.sample.motion.source,f.sample.motion.hold);f.Evaluate();break;
        case 11: f.memory.changeAddress=NativeFixture::firing+0x3c;f.memory.changeValue=5;f.Evaluate();break;
        case 12: f.memory.native.Put(NativeFixture::effects+0x128,0u);f.Evaluate();break;
        case 13: ++f.sample.motion.source.observedNs;f.Evaluate();break; // mutated duplicate packet
        case 14: f.sample.motion.source.nowNs+=2*gapMs;f.Evaluate();f.sample.motion.source.nowNs-=gapMs;f.Evaluate();break;
        }
        CHECK(!f.gap.Active()&&f.emitted==0&&f.policy.PendingState()==SelectedMeleePending::None);
    }
    // A fully read replacement firing cohort and a new native actor cannot
    // reconnect a pre-gap gesture even when both snapshots independently pass.
    for(unsigned fault=0;fault<6;++fault){
        GapFixture f;f.Rest();f.Step(19,.019f);f.Step(15,.034f,true);
        CHECK(f.gap.Active());
        if(fault==0){f.memory.native.client.branches[0].address=NativeFixture::cf1;
            f.memory.native.client.branches[1].address=NativeFixture::cf0;
            f.memory.native.Put(NativeFixture::cw+0x3c,NativeFixture::cf1);f.memory.native.Put(NativeFixture::cw+0x40,NativeFixture::cf0);}
        if(fault==1)++f.memory.native.client.owner.actorGeneration;
        if(fault==2)++f.memory.native.client.owner.equipGeneration;
        if(fault==3)f.sample.geometryCurrent=false;
        if(fault==4)f.memory.native.Put(NativeFixture::firing+0x3c,5u);
        if(fault==5)f.sample.motion.source.nowNs+=76*gapMs;
        f.memory.racing=false;
        if(fault!=5){++f.sample.motion.source.sequence;f.sample.motion.source.observedNs+=16*gapMs;
            f.sample.motion.source.nowNs=f.sample.motion.source.observedNs;f.sample.motion.source.deadlineNs=f.sample.motion.source.nowNs+100*gapMs;f.Refresh();}
        f.sample.motion.tip.z=.050f;CHECK(!f.Evaluate());
        CHECK(f.recovered==0&&!f.gap.Active()&&f.emitted==0);
    }
    return 0;
}
int PendingAndDuplicateLeasesStayOriginal(){
    GapFixture cached;cached.Rest();cached.sample.motion.source.nowNs+=2*gapMs;
    cached.Step(10,0);cached.sample.motion.source.nowNs+=2*gapMs;
    // A fresh sample can be processed by Prepare later than its cached
    // SourceAdvanced timestamp. Repeating that cache is not clock reversal.
    ++cached.sample.motion.source.sequence;cached.sample.motion.source.observedNs+=10*gapMs;
    cached.sample.motion.source.nowNs=cached.sample.motion.source.observedNs+2*gapMs;
    cached.sample.motion.source.deadlineNs=cached.sample.motion.source.observedNs+100*gapMs;
    cached.Refresh();cached.Evaluate();
    auto sourceAdvanced=cached.sample.motion.source;sourceAdvanced.nowNs=sourceAdvanced.observedNs;
    CHECK(cached.gap.SourceContinues(sourceAdvanced,cached.sample.identity.inputEpoch));
    GapFixture f;f.Rest();f.Step(19,.019f);f.Step(15,.034f,true);
    const auto anchor=f.gap.AnchorObservedNs();
    for(unsigned n=0;n<35;++n){f.sample.motion.source.nowNs+=gapMs;CHECK(!f.Evaluate());}
    CHECK(f.gap.Active()&&f.gap.AnchorObservedNs()==anchor);
    f.sample.motion.source.nowNs=anchor+76*gapMs;CHECK(!f.Evaluate()&&!f.gap.Active());
    GapFixture pending;pending.Rest();
    for(unsigned n=1;n<=8;++n)pending.Step(10,.012f*n);
    CHECK(pending.emitted==1&&pending.last);
    const auto intent=*pending.last;
    pending.Step(10,.11f,true);
    CHECK(!pending.gap.Active()&&pending.policy.PendingState()==SelectedMeleePending::Staged);
    CHECK(pending.policy.Pending()->serial==intent.serial&&pending.policy.Pending()->deadlineNs==intent.deadlineNs);
    // No restoration or completion is inferred from a later coherent read.
    pending.Step(10,.12f);CHECK(pending.emitted==1&&pending.policy.Pending()->serial==intent.serial);
    return 0;
}
}

}
namespace local434_test {
using namespace interaction;
constexpr std::int64_t ms=1000000;
struct MotionFixture {
    Fixture geometry;
    std::optional<SelectedKnifeRenderReceipt> receipt;
    InputFrame input{};
    SelectedMeleeSample sample{};
    HandInteraction hands;
    SelectedMeleeAttack policy;
    std::optional<selectedKnife::Geometry> mapped;
    std::optional<SelectedMeleeIntent> intent;
    SelectedMeleeObservation observation{};
    unsigned emitted=0;
    std::uint64_t claims=0;
    explicit MotionFixture(math::Vec3 origin={}){
        if(const auto binding=geometry.Bind())receipt=MakeSelectedKnifeRenderReceipt(*binding,At());
        input.generation=1;input.spaceGeneration=14;input.predictedNs=1000*ms;
        input.focused=input.headValid=true;input.referenceHead.position=input.head.position=origin;
        auto& right=input.hands[1];right.gripTracked=right.aimTracked=true;right.active=Components;
        right.grip.position={origin.x+.2f,origin.y-.3f,origin.z-.4f};right.aim=right.grip;
        sample.motion.source={{1,2,3,14},1,1000*ms,1100*ms,1000*ms,true,{true,true},{false,false}};
        sample.identity={sample.motion.source.owner,{42,1},{53,1},7};
        sample.selected=sample.geometryCurrent=sample.motion.attackAvailable=true;
    }
    bool Step(){
        ++input.generation;input.predictedNs+=10*ms;
        auto& source=sample.motion.source;source.sequence=input.generation;
        source.observedNs=source.nowNs=input.predictedNs;source.deadlineNs=source.nowNs+100*ms;
        hands.Update(source);const HandContactProof contact{{11,1},source.sequence,source.deadlineNs,true};
        const auto held=hands.Current(InteractionHand::Right);
        const auto claim=held?hands.Renew(source,held->token,contact):
            hands.Acquire(source,{source.owner,InteractionHand::Right,HandClaimKind::GunHold,sample.identity.item,contact,++claims,0});
        if(!claim.claim||!receipt)return false;sample.motion.hold=claim.claim->token;
        mapped=selectedKnife::MapGeometry(input,*receipt,At(100,20,-30));
        if(!mapped)return false;
        sample.motion.tip=mapped->motion.tip;sample.motion.direction=mapped->motion.direction;
        const auto result=policy.Update(sample,hands,&observation);
        if(result.intent){++emitted;intent=result.intent;}
        return true;
    }
    void Move(math::Vec3 movement,bool head){
        auto& grip=input.hands[1].grip.position;
        grip.x+=movement.x;grip.y+=movement.y;grip.z+=movement.z;
        if(head){input.head.position.x+=movement.x;input.head.position.y+=movement.y;input.head.position.z+=movement.z;}
    }
};
int TranslatedOriginsKeepSameJab(){
    std::optional<SelectedMeleeIntent> baseline;
    std::optional<math::Matrix4> native;
    for(const auto origin:{math::Vec3{},math::Vec3{4,2,-6},math::Vec3{-7,-3,5}}){
        MotionFixture f(origin);for(unsigned n=0;n<20;++n)CHECK(f.Step());
        CHECK(!f.emitted&&f.mapped);
        const auto axis=f.mapped->motion.direction;
        for(unsigned n=0;n<8;++n){f.Move({axis.x*.013f,axis.y*.013f,-axis.z*.013f},false);CHECK(f.Step());}
        CHECK(f.emitted==1&&f.intent&&f.intent->cause==SelectedMeleeCause::Stroke&&f.intent->stroke);
        if(!baseline){baseline=f.intent;native=f.mapped->nativeAcquisition;}
        else {
            CHECK(f.intent->sourceSequence==baseline->sourceSequence&&f.intent->observedNs==baseline->observedNs);
            CHECK(f.intent->deadlineNs==baseline->deadlineNs&&Near(f.mapped->nativeAcquisition,*native));
            const auto& a=*f.intent->stroke;const auto& b=*baseline->stroke;
            CHECK(std::abs((a.end.x-a.start.x)-(b.end.x-b.start.x))<.00001f);
            CHECK(std::abs((a.end.y-a.start.y)-(b.end.y-b.start.y))<.00001f);
            CHECK(std::abs((a.end.z-a.start.z)-(b.end.z-b.start.z))<.00001f);
            CHECK(a.direction==b.direction);
        }
    }
    std::cout<<"434 origin: same physical jab/lease/native aim at three LOCAL origins\n";return 0;
}
int PhysicalWalkAndMovingHeadKeepFixedFrame(){
    MotionFixture f;for(unsigned n=0;n<20;++n)CHECK(f.Step());
    const auto reference=f.input.referenceHead;
    // Four metres of real head+hand translation after reference capture. The
    // .5m/s walk is below jab speed, and the hand stays beside the current head.
    for(unsigned n=0;n<800;++n){f.Move({.005f,0,0},true);CHECK(f.Step());CHECK(!f.emitted);}
    CHECK(f.input.referenceHead.position.x==reference.position.x&&f.input.head.position.x>3.9f);
    for(unsigned n=0;n<20;++n)CHECK(f.Step());
    const auto still=f.mapped->motion.tip;
    // Head motion alone must not create a reverse hand stroke. Recognition
    // retains raw LOCAL coordinates, not current-head-relative coordinates.
    const auto axis=f.mapped->motion.direction;
    for(unsigned n=0;n<8;++n){
        f.input.head.position.x-=axis.x*.012f;f.input.head.position.y-=axis.y*.012f;
        f.input.head.position.z+=axis.z*.012f;CHECK(f.Step());
        CHECK(f.mapped->motion.tip==still&&!f.emitted);
    }
    for(unsigned n=0;n<8;++n){f.Move({axis.x*.013f,axis.y*.013f,-axis.z*.013f},false);CHECK(f.Step());}
    CHECK(f.emitted==1&&f.intent&&f.intent->stroke);
    std::cout<<"434 walk: 4m after capture, moving head silent, subsequent jab accepted\n";return 0;
}
int CurrentReachGeometryAndFreshnessStillGate(){
    for(const auto origin:{math::Vec3{},math::Vec3{4,2,-6}}){
        MotionFixture f(origin);CHECK(f.Step()&&f.receipt);
        // Same helper is consumed by chest custody before it can set native
        // attack readiness; raw geometry alone cannot supply a reach proof.
        CHECK(private_knife::KnifeMotionFromTrackedGrip(f.input,f.receipt->wristInGrip));
        for(unsigned fault=0;fault<7;++fault){auto input=f.input;auto body=At();auto receipt=*f.receipt;
            if(fault==0)input.hands[1].grip.position={origin.x+1.51f,origin.y,origin.z};
            if(fault==1)input.hands[1].grip.position.x=std::numeric_limits<float>::quiet_NaN();
            if(fault==2)input.head.position.z=std::numeric_limits<float>::infinity();
            if(fault==3)input.hands[1].gripTracked=false;
            if(fault==4)input.headValid=false;
            if(fault==5)body.values[0][0]=2;
            if(fault==6)receipt.wristInGrip.values[3][0]=.1f;
            CHECK(!selectedKnife::MapGeometry(input,receipt,body));
            if(fault!=5)CHECK(!private_knife::KnifeMotionFromTrackedGrip(input,receipt.wristInGrip));
        }
        CHECK(!SelectedKnifeRenderCurrent(*f.receipt,f.geometry.source,f.geometry.mesh.get(),f.geometry.rig,
            Fixture::fingerprint,f.geometry.source.input.deadlineNs));
        ChestKnifeRenderSource chest{f.geometry.source.native.owner,f.geometry.source.input,{15,1},1};
        CHECK(ChestKnifeRenderCurrent(chest,chest.owner,chest.input,Fixture::now));
        CHECK(!ChestKnifeRenderCurrent(chest,chest.owner,chest.input,chest.input.deadlineNs));
        for(unsigned n=0;n<20;++n)CHECK(f.Step());
        f.sample.motion.source.nowNs=f.sample.motion.source.deadlineNs;
        CHECK(f.policy.Update(f.sample,f.hands).reason==SelectedMeleeReason::Invalid&&!f.emitted);
        f.input.predictedNs=f.sample.motion.source.nowNs;
        // Expired input destroyed history; a moving packet cannot restore it.
        const auto axis=f.mapped->motion.direction;
        for(unsigned n=0;n<8;++n){f.Move({axis.x*.012f,axis.y*.012f,-axis.z*.012f},false);CHECK(f.Step());}
        CHECK(!f.emitted);
    }
    std::cout<<"434 limits: reach/pose/calibration/render expiry/input expiry still reject\n";return 0;
}
}
int main(){if(local434_test::TranslatedOriginsKeepSameJab()||local434_test::PhysicalWalkAndMovingHeadKeepFixedFrame()||
    local434_test::CurrentReachGeometryAndFreshnessStillGate()||
    ExactSelectedGeometry()||RejectLookalikesAndMissingEvidence()||LifetimeCannotRenewOldCalibration()||CalibratedBladeUsesOriginalGrip()||
    BladeWorldAndNativeForward()||AcquisitionCopyKeepsSourcesPaddingAndRange()||SelectedNativeOwnsMeleeWithoutFirearmAnchor()||
    SelectedNativeRejectsLookalikesAndChangedOwners()||SelectedOwnerDiagnosticsPreserveAdmission()||SelectedOwnerContextBracket()||SelectedTargetRetainsExactWeakIdentity()||OrdinarySelectedAcquisitionBoundary()||PublishedContinuityDelayedMissAndNextJab()||RepeatedGatherKeepsMeasuredStroke()||PublishedInputSurvivesGatherUntilConsumed()||
    gap_test::ClassifiedReadFailures()||gap_test::DeliveredStrokeWithOneIncoherentPacket()||
    gap_test::GapsCannotExtendAuthority()||gap_test::PendingAndDuplicateLeasesStayOriginal())return 1;
    std::cout<<"Twenty-two selected-knife geometry, ownership, delivery, native-argument, classified-gap and LOCAL-origin groups passed; mocked native memory, no game hit claim\n";}
