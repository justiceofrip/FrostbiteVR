#include "Bc2M95ShotPartCapture.h"
#include "Bc2ReloadConfigDescriptor.h"
#include "Test.h"
#include <memory>
#include <sstream>
#include <cstring>
#include <cstdio>
using namespace fvr;using namespace fvr::bc2;
namespace {
math::Matrix4 I(float z=0){math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;m.values[3][2]=z;return m;}
struct Fixture {
    std::unique_ptr<M95ShotPartCapture> capture=std::make_unique<M95ShotPartCapture>();
    reloadFlowRuntime::PumpPartNativeSample before{},after{};RigSnapshot rig{};SelectedMeshesSnapshot selected{};
    std::int64_t now=1000010000;float units=1;unsigned phase=1;std::uint64_t sequence=1;
    Fixture(){auto& n=before.identity;n.owner={0x10000,0x20000,0x30000,0x40000,1,2,3};n.firing={0x50000,0x60000,0x70000};
        n.serverPlayer=0x80000;n.serverSoldier=0x90000;n.serverItem=0xa0000;
        auto& c=before.config;const std::string asset="M95_sp",path="Objects/Weapons/Handheld/BU_sni_M95/SP_sni_M95";
        std::memcpy(c.assetName.data(),asset.data(),asset.size());std::memcpy(c.assetPath.data(),path.data(),path.size());
        c.weaponData=0xb0000;c.firingData=0xc0000;c.primaryFire=0xd0000;c.ammoAddress=c.primaryFire+0x170;
        c.fireLogicType=1;c.reloadType=1;c.fireInputAction=8;c.reloadInputAction=29;c.baseCapacity=c.numberOfMagazines=5;
        c.reloadTime=6.9f;c.reloadThreshold=.67f;c.boltTime=2.3f;c.holdBoltUntilFireRelease=true;
        before.observedNs=1000000000;before.completedNs=1000001000;before.deadlineNs=1100000000;before.holdPhase=2;
        for(unsigned k=0;k<3;++k){auto& b=before.branches[k];b.owner=n.owner;b.branch=std::uint8_t(k);b.firing=n.firing[k];b.current=8;b.previous=7;b.next=1;b.loaded=4;b.reserve=12;}
        after=before;after.observedNs=1000002000;after.completedNs=1000003000;
        selected.owner=n.owner;selected.weaponData=c.weaponData;selected.observedNs=999999000;selected.deadlineNs=1100000000;selected.sequence=1;
        selected.stateCount=1;selected.soleConfiguredArray=0x1234;selected.states[0].count=1;
        auto& mesh=selected.states[0].meshes[0];mesh.kind=SelectedMeshKind::Spas12;mesh.address=0x2345;
        const std::string meshPath="Objects/Weapons/Handheld/BU_sni_M95/BU_sni_M95_Mesh";std::memcpy(mesh.assetPath.data(),meshPath.data(),meshPath.size());
        selected.configurationPathVerified=true;std::memcpy(selected.configurationPath.data(),path.data(),path.size());
        rig.names={"root","jntWpn_1","jntWpn_3","RightHand"};rig.parents={-1,0,1,0};rig.weaponBone=1;rig.right.wrist=3;
        rig.identity.soldier=n.owner.soldier;rig.identity.weak=n.owner.weak;rig.identity.count=4;
        rig.inverseBind.assign(4,I());rig.evaluatedWorld={I(),I(2),I(1.2f),I(1.25f)};
        for(auto digit:{"Thumb","Index","Middle","Ring","Pinky"})for(unsigned joint=1;joint<=3;++joint){
            rig.names.push_back(std::string("RightHand")+digit+std::to_string(joint));
            rig.parents.push_back(joint==1?3:int(rig.names.size()-2));rig.inverseBind.push_back(I());rig.evaluatedWorld.push_back(I(1.3f));}
        rig.identity.count=unsigned(rig.names.size());
    }
    bool Run(){return capture->Observe(before,after,rig,selected,sequence,selected.observedNs,selected.deadlineNs,units,now,phase);}
    void Next(){constexpr auto dt=21000000;now+=dt;++sequence;before.observedNs+=dt;before.completedNs+=dt;before.deadlineNs+=dt;
        after.observedNs+=dt;after.completedNs+=dt;after.deadlineNs+=dt;selected.observedNs+=dt;selected.deadlineNs+=dt;}
};
int OriginalPoseAndLease(){Fixture f;CHECK(f.Run());const auto& row=f.capture->Row(0);CHECK(row.stableStateBracket);
    CHECK(Near(row.partFromWeapon.values[3][2],-.8f));CHECK(Near(row.wristFromWeapon.values[3][2],-.75f));
    CHECK(row.before.deadlineNs==1100000000&&row.selectedObservedNs==999999000);
    std::ostringstream text;f.capture->Report(text);CHECK(text.str().find("\"runtime_authority\":false")!=std::string::npos);return 0;}
int TransitionIsAmbiguousNotDiscarded(){Fixture f;f.after.branches[1].current=1;CHECK(f.Run());CHECK(!f.capture->Row(0).stableStateBracket);return 0;}
int ExpiryAndProvenance(){for(unsigned k=0;k<8;++k){Fixture f;switch(k){case 0:f.now=f.before.deadlineNs;break;
    case 1:++f.after.identity.owner.equipGeneration;break;case 2:++f.selected.weaponData;break;case 3:f.selected.deadlineNs=f.now;break;
    case 4:f.before.branches[0].firing++;break;case 5:f.rig.identity.soldier++;break;case 6:f.after.observedNs=f.before.completedNs-1;break;
    case 7:f.selected.stateCount=2;break;}CHECK(!f.Run());}return 0;}
int UnitsAndNoDeadlineRenewal(){Fixture f;f.units=100;for(auto& m:f.rig.evaluatedWorld)for(unsigned k=0;k<3;++k)m.values[3][k]*=100;
    CHECK(f.Run());CHECK(Near(f.capture->Row(0).partFromWeapon.values[3][2],-.8f));
    f.after.deadlineNs+=10000000;f.now=f.before.deadlineNs;CHECK(!f.Run());return 0;}
int FrozenFiredWindowAndBound(){Fixture f;for(unsigned n=0;n<100;++n){CHECK(f.Run());f.Next();}CHECK(f.capture->Count()==32);
    const auto oldest=f.before.observedNs-32*21000000;f.phase=2;CHECK(f.Run());CHECK(f.capture->Row(0).before.observedNs==oldest);
    for(unsigned n=1;n<320;++n){f.Next();f.phase=3;CHECK(f.Run());}CHECK(f.capture->Count()==352);
    const auto first=f.capture->Row(32).before.observedNs;f.Next();CHECK(!f.Run());CHECK(f.capture->Row(32).before.observedNs==first);
    f.phase=4;CHECK(!f.Run());return 0;}
int RightFingerAndRevisionEvidence(){Fixture f;CHECK(f.Run());CHECK(Near(f.capture->Row(0).fingersFromWrist[0].values[3][2],.05f));
    f.Next();++f.after.callbackRevision;CHECK(f.Run());CHECK(!f.capture->Row(1).stableStateBracket);
    Fixture missing;missing.rig.names[4]="missing";CHECK(!missing.Run());Fixture parent;parent.rig.parents[4]=1;CHECK(!parent.Run());return 0;}
int ExactVariantAndNoPostTerminal(){for(unsigned n=0;n<5;++n){Fixture f;switch(n){case 0:f.selected.configurationPath[0]='x';break;
    case 1:f.selected.states[0].meshes[0].assetPath[0]='x';break;case 2:f.phase=0;break;case 3:f.phase=4;break;case 4:f.before.config.boltTime=2.2f;break;}CHECK(!f.Run());}return 0;}

}
int main(){if(OriginalPoseAndLease()||TransitionIsAmbiguousNotDiscarded()||ExpiryAndProvenance()||UnitsAndNoDeadlineRenewal()||FrozenFiredWindowAndBound()||RightFingerAndRevisionEvidence()||ExactVariantAndNoPostTerminal())return 1;
    std::puts("7 M95 bounded shot/part/right-hand capture groups passed; observation only.");}
