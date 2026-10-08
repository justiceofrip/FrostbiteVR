#include "Bc2ReloadPalette.h"
#include "Test.h"
#include <algorithm>
#include <cstring>
#include <iostream>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
auto Pose(float x=0,float y=0,float z=0){auto m=reload_insertion_detail::Identity();m.values[3]={x,y,z,1};return m;}
struct Fixture {
    RigSnapshot rig;Bc2ReloadPresentationBinding binding;Bc2ReloadTargets target;
    Bc2ReloadPaletteSample sample;std::vector<BoneWrite> base;
    Fixture(){
        rig.names={"root","jntWpn_1","jntWpn_7","LeftArm","LeftForeArm","LeftHand","RightArm","RightForeArm","RightHand"};
        rig.parents={-1,0,1,0,3,4,0,6,7};rig.left={3,4,5};rig.right={6,7,8};rig.weaponBone=1;
        for(const char* finger:{"Thumb","Index","Middle","Ring","Pinky"})for(unsigned j=1;j<=3;++j){rig.parents.push_back(j==1?5:std::int32_t(rig.names.size()-1));rig.names.push_back(std::string("LeftHand")+finger+std::to_string(j));}
        rig.names.push_back("unrelated");rig.parents.push_back(0);const auto n=unsigned(rig.names.size());
        rig.identity.count=n;rig.identity.soldier=1;rig.identity.evaluatedMatrices=0x10000;
        rig.inverseBind.assign(n,Pose());rig.evaluatedWorld.assign(n,Pose());rig.nativeEvaluated.resize(n);
        for(auto& bytes:rig.nativeEvaluated)bytes.fill(std::byte{0x91});
        rig.evaluatedWorld[4]=Pose(.3f,0,0);rig.evaluatedWorld[5]=Pose(.6f,0,0);
        rig.evaluatedWorld[6]=Pose(0,-.4f,0);rig.evaluatedWorld[7]=Pose(.3f,-.4f,0);rig.evaluatedWorld[8]=Pose(.6f,-.4f,0);
        for(unsigned i=9;i<n-1;++i)rig.evaluatedWorld[i]=Pose(.6f,0,0);
        binding=*bc2_reload_detail::DerivePresentationBinding(rig);
        target.identity={{1,2,3,4},{5,6},{7,8},9};target.inputSequence=10;target.nativeCycle=11;target.observedNs=1000000000ll;target.deadlineNs=1100000000ll;
        target.shellClaim={12,target.identity.owner,InteractionHand::Left,HandClaimKind::AmmoObject,target.identity.item,{20,1},0};
        target.weaponClaim={13,target.identity.owner,InteractionHand::Right,HandClaimKind::GunHold,target.identity.weapon,{21,1},0};
        target.weaponFromLeftWristMeters=Pose(.45f,.1f,0);
        target.weaponFromShellCenterMeters=Multiply(*InverseRigid(SpasReloadInsertionProfile().itemFromHand),target.weaponFromLeftWristMeters);
        auto& s=sample.presentation;s.enabled=s.selectedMeshIdentityVerified=s.sectionOwnershipVerified=s.shellSectionVisible=true;
        s.assetName=SpasReloadAsset;s.meshPath=SpasReloadMesh;s.identity=target.identity;s.inputSequence=10;s.nativeCycle=11;s.nowNs=1000000000ll;s.placedWeaponWorld=Pose();
        base={{1,Pose()},{2,Pose(10,10,10)},{8,Pose(.61f,-.41f,0)},{n-1,Pose(8,9,10)}};
        sample.enabled=true;sample.baseWrites=base;sample.armNativeWorld=rig.evaluatedWorld;sample.leftPoleDirection={0,1,0};
    }
    Bc2ReloadPalettePlan Run(){sample.baseWrites=base;sample.armNativeWorld=rig.evaluatedWorld;return BuildBc2ReloadPalette(rig,binding,sample,target);}
};
int CoherentOverlay(){Fixture f;const auto original=f.rig.nativeEvaluated;const auto base=f.base;const auto plan=f.Run();CHECK(plan.palette&&plan.reason==Bc2ReloadPaletteReason::None);CHECK(f.rig.nativeEvaluated==original);
    for(const auto& w:plan.writes)CHECK(std::count_if(plan.writes.begin(),plan.writes.end(),[&](const auto& q){return q.index==w.index;})==1);
    const auto at=[&](unsigned index)->const math::Matrix4&{return std::find_if(plan.writes.begin(),plan.writes.end(),[&](const auto& w){return w.index==index;})->transform;};
    CHECK(at(8).values==base[2].transform.values);CHECK(at(unsigned(f.rig.names.size()-1)).values==base[3].transform.values);CHECK(at(1).values==base[0].transform.values);
    CHECK(at(2).values!=base[1].transform.values);CHECK(at(5).values==f.target.weaponFromLeftWristMeters.values);
    for(const auto& e:plan.palette->edits){CHECK(e.before==original[e.index]);for(unsigned row=0;row<4;++row)for(unsigned c=12;c<16;++c)CHECK(e.after[row*16+c]==std::byte{0x91});}return 0;}
int GatesRemainClosed(){for(unsigned k=0;k<8;++k){Fixture f;
    if(k==0)f.sample.enabled=false;if(k==1)f.sample.presentation.enabled=false;if(k==2)f.sample.presentation.sectionOwnershipVerified=false;
    if(k==3)f.sample.presentation.shellSectionVisible=false;if(k==4)f.sample.presentation.selectedMeshIdentityVerified=false;
    if(k==5)f.rig.nativeHiddenLeaves={2};if(k==6)++f.sample.presentation.nativeCycle;if(k==7)f.sample.presentation.nowNs=f.target.deadlineNs;
    auto p=f.Run();CHECK(!p.palette&&p.writes.empty());if(k==5)CHECK(p.presentationReason==Bc2ReloadPresentationReason::NativeShellHidden);}return 0;}
int UnreachableWristCancels(){Fixture f;f.target.weaponFromLeftWristMeters=Pose(5,0,0);auto p=f.Run();CHECK(!p.palette&&p.writes.empty());CHECK(p.reason==Bc2ReloadPaletteReason::WristUnreachable);return 0;}
int InvalidBaseRejected(){for(unsigned k=0;k<4;++k){Fixture f;if(k==0)f.base.push_back(f.base[0]);if(k==1)f.base[0].index=999;if(k==2)f.base.clear();if(k==3)f.base[0].transform={};
    const auto p=f.Run();CHECK(!p.palette&&p.reason==Bc2ReloadPaletteReason::InvalidBasePose);}return 0;}
int ArmMismatchRejected(){Fixture f;f.rig.left.wrist=4;const auto p=f.Run();CHECK(!p.palette&&p.reason==Bc2ReloadPaletteReason::ArmRejected);
    Fixture malformed;malformed.rig.evaluatedWorld[4]={};const auto invalid=malformed.Run();CHECK(!invalid.palette&&invalid.reason==Bc2ReloadPaletteReason::ArmRejected);return 0;}
int UnitsPreserveGrasp(){Fixture f;const auto first=f.Run();CHECK(first.palette);
    f.sample.presentation.unitsPerMetre=100;for(auto& m:f.rig.evaluatedWorld)for(unsigned a=0;a<3;++a)m.values[3][a]*=100;
    for(auto& w:f.base)for(unsigned a=0;a<3;++a)w.transform.values[3][a]*=100;
    const auto scaled=f.Run();CHECK(scaled.palette&&first.writes.size()==scaled.writes.size());
    for(unsigned i=0;i<first.writes.size();++i)for(unsigned a=0;a<3;++a)CHECK(Near(scaled.writes[i].transform.values[3][a],100*first.writes[i].transform.values[3][a],.001f));return 0;}
int CarriedZeroCycleIsExplicit(){Fixture f;f.target.nativeCycle=0;f.sample.presentation.nativeCycle=0;
    CHECK(!f.Run().palette);f.sample.presentation.carried=true;CHECK(f.Run().palette);
    f.target.nativeCycle=1;CHECK(!f.Run().palette);f.target.nativeCycle=0;
    f.rig.nativeHiddenLeaves={2};f.sample.presentation.shellSectionVisible=false;const auto p=f.Run();
    CHECK(!p.palette&&p.presentationReason==Bc2ReloadPresentationReason::NativeShellHidden);return 0;}
void HideShell(Fixture& f){
    f.rig.nativeHiddenLeaves={2};f.rig.nativeWorld=f.rig.nativeEvaluated;
    auto hidden=Pose(.02f,.03f,.04f);for(unsigned i=0;i<3;++i)hidden.values[i][i]=.0001f;
    f.rig.evaluatedWorld[2]=hidden;
    for(unsigned row=0;row<4;++row)for(unsigned col=0;col<3;++col){
        std::memcpy(f.rig.nativeWorld[2].data()+row*16+col*4,&hidden.values[row][col],4);
        std::memcpy(f.rig.nativeEvaluated[2].data()+row*16+col*4,&hidden.values[row][col],4);
    }
    std::erase_if(f.base,[](const auto& w){return w.index==2;});
    f.sample.presentation.shellSectionVisible=false;
}
int OwnedHiddenShellChangesPrivateCopyOnly(){Fixture f;HideShell(f);const auto original=f.rig;
    CHECK(!f.Run().palette);f.sample.presentation.allowOwnedShellVisibility=true;const auto p=f.Run();
    CHECK(p.palette&&p.ownedShellVisibility==2u);CHECK(f.rig.identity==original.identity&&f.rig.nativeEvaluated==original.nativeEvaluated&&
        f.rig.nativeWorld==original.nativeWorld&&f.rig.nativeHiddenLeaves==original.nativeHiddenLeaves&&f.rig.parents==original.parents&&f.rig.names==original.names);
    for(unsigned n=0;n<f.rig.evaluatedWorld.size();++n){CHECK(f.rig.evaluatedWorld[n].values==original.evaluatedWorld[n].values);CHECK(f.rig.inverseBind[n].values==original.inverseBind[n].values);}
    const auto e=std::find_if(p.palette->edits.begin(),p.palette->edits.end(),[](const auto& q){return q.index==2;});
    CHECK(e!=p.palette->edits.end()&&e->before==original.nativeEvaluated[2]&&e->after!=e->before);
    for(unsigned row=0;row<4;++row)for(unsigned col=12;col<16;++col)CHECK(e->after[row*16+col]==e->before[row*16+col]);
    const std::array<BoneWrite,1> write{{{2,Pose()}}};CHECK(!BuildRigPosePlan(f.rig,write));return 0;
}
int UnrelatedHiddenLeafRemainsForbidden(){Fixture f;HideShell(f);f.sample.presentation.allowOwnedShellVisibility=true;
    const auto unrelated=unsigned(f.rig.names.size()-1);f.rig.nativeHiddenLeaves.push_back(unrelated);
    CHECK(!f.Run().palette);std::erase_if(f.base,[&](const auto& w){return w.index==unrelated;});
    const auto p=f.Run();CHECK(p.palette&&p.ownedShellVisibility==2u);
    for(const auto& e:p.palette->edits)CHECK(e.index!=unrelated);return 0;
}
int VisibilityPermissionNeedsExactKnownLeaf(){for(unsigned k=0;k<8;++k){Fixture f;HideShell(f);f.sample.presentation.allowOwnedShellVisibility=true;
    if(k==0)f.sample.presentation.allowOwnedShellVisibility=false;if(k==1)f.sample.presentation.assetName="XM8_sp_s";
    if(k==2)f.target.shellClaim.owner.actor++;if(k==3)f.sample.presentation.nativeCycle++;
    if(k==4)f.rig.parents[2]=0;if(k==5)f.rig.nativeWorld.clear();
    if(k==6){float v=.001f;std::memcpy(f.rig.nativeEvaluated[2].data(),&v,4);}if(k==7)f.sample.presentation.sectionOwnershipVerified=false;
    CHECK(!f.Run().palette);}return 0;
}
}
int main(){if(CoherentOverlay()||GatesRemainClosed()||UnreachableWristCancels()||InvalidBaseRejected()||ArmMismatchRejected()||UnitsPreserveGrasp()||CarriedZeroCycleIsExplicit()||
    OwnedHiddenShellChangesPrivateCopyOnly()||UnrelatedHiddenLeafRemainsForbidden()||VisibilityPermissionNeedsExactKnownLeaf())return 1;std::cout<<"Bc2ReloadPalette: 10 cases passed\n";}
