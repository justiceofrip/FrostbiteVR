#include "Bc2PumpCapture.h"
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
    std::unique_ptr<PumpPartCapture> capture=std::make_unique<PumpPartCapture>();
    reloadFlowRuntime::PumpPartNativeSample before{},after{};RigSnapshot rig{};SelectedMeshesSnapshot selected{};
    std::int64_t now=1000010000;float units=1;
    Fixture(){auto& n=before.identity;n.owner={0x10000,0x20000,0x30000,0x40000,1,2,3};n.firing={0x50000,0x60000,0x70000};
        n.serverPlayer=0x80000;n.serverSoldier=0x90000;n.serverItem=0xa0000;
        auto& c=before.config;const auto& d=SpasReloadDescriptor;std::memcpy(c.assetName.data(),d.assetName.data(),d.assetName.size());
        std::memcpy(c.assetPath.data(),d.assetPath.data(),d.assetPath.size());c.weaponData=0xb0000;c.firingData=0xc0000;c.primaryFire=0xd0000;c.ammoAddress=c.primaryFire+0x170;
        c.fireLogicType=1;c.reloadType=0;c.fireInputAction=8;c.reloadInputAction=29;c.baseCapacity=c.numberOfMagazines=4;
        c.reloadDelay=.06f;c.reloadTime=.72f;c.reloadThreshold=c.postReloadTime=1;c.boltDelay=.5f;
        before.observedNs=1000000000;before.completedNs=1000001000;before.deadlineNs=1100000000;before.holdPhase=2;
        for(unsigned k=0;k<3;++k){auto& b=before.branches[k];b.owner=n.owner;b.branch=std::uint8_t(k);b.firing=n.firing[k];b.current=7;b.previous=6;b.next=8;b.loaded=7;b.reserve=12;}
        after=before;after.observedNs=1000002000;after.completedNs=1000003000;
        selected.owner=n.owner;selected.weaponData=c.weaponData;selected.observedNs=999999000;selected.deadlineNs=1100000000;selected.sequence=1;
        selected.stateCount=1;selected.soleConfiguredArray=0x1234;selected.states[0].count=1;
        auto& mesh=selected.states[0].meshes[0];mesh.kind=SelectedMeshKind::Spas12;mesh.address=0x2345;
        std::memcpy(mesh.assetPath.data(),SpasReloadMesh.data(),SpasReloadMesh.size());
        rig.names={"root","jntWpn_1","jntWpn_4","LeftHand"};rig.parents={-1,0,1,0};rig.weaponBone=1;rig.left.wrist=3;
        rig.identity.soldier=n.owner.soldier;rig.identity.weak=n.owner.weak;rig.identity.count=4;
        rig.inverseBind.assign(4,I());rig.evaluatedWorld={I(),I(2),I(1.2f),I(1.25f)};
    }
    bool Run(){return capture->Observe(before,after,rig,selected,1,999999000,1100000000,units,now);}
};
int OriginalPoseAndLease(){Fixture f;CHECK(f.Run());const auto& row=f.capture->Row(0);CHECK(row.stableStateBracket);
    CHECK(Near(row.partFromWeapon.values[3][2],-.8f));CHECK(Near(row.wristFromWeapon.values[3][2],-.75f));
    CHECK(row.before.deadlineNs==1100000000&&row.selectedObservedNs==999999000);
    std::ostringstream text;f.capture->Report(text);CHECK(text.str().find("\"runtime_authority\":false")!=std::string::npos);return 0;}
int TransitionIsAmbiguousNotDiscarded(){Fixture f;f.after.branches[1].current=8;CHECK(f.Run());CHECK(!f.capture->Row(0).stableStateBracket);return 0;}
int ExpiryAndProvenance(){for(unsigned k=0;k<8;++k){Fixture f;switch(k){case 0:f.now=f.before.deadlineNs;break;
    case 1:++f.after.identity.owner.equipGeneration;break;case 2:++f.selected.weaponData;break;case 3:f.selected.deadlineNs=f.now;break;
    case 4:f.before.branches[0].firing++;break;case 5:f.rig.identity.soldier++;break;case 6:f.after.observedNs=f.before.completedNs-1;break;
    case 7:f.selected.stateCount=2;break;}CHECK(!f.Run());}return 0;}
int UnitsAndNoDeadlineRenewal(){Fixture f;f.units=100;for(auto& m:f.rig.evaluatedWorld)for(unsigned k=0;k<3;++k)m.values[3][k]*=100;
    CHECK(f.Run());CHECK(Near(f.capture->Row(0).partFromWeapon.values[3][2],-.8f));
    f.after.deadlineNs+=10000000;f.now=f.before.deadlineNs;CHECK(!f.Run());return 0;}
}
int main(){if(OriginalPoseAndLease()||TransitionIsAmbiguousNotDiscarded()||ExpiryAndProvenance()||UnitsAndNoDeadlineRenewal())return 1;
    std::puts("4 pump state/part capture groups passed; observation only.");}
