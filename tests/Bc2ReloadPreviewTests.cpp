#include "Bc2ReloadPreview.h"
#include "Bc2WeaponVisibility.h"
#include "Test.h"
#include <cstring>
#include <iostream>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
constexpr std::int64_t Now=1000000000ll;
auto Pose(float x=0,float y=0,float z=0){auto m=reload_insertion_detail::Identity();m.values[3]={x,y,z,1};return m;}
bool Same(const math::Matrix4& a,const math::Matrix4& b){for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)if(!Near(a.values[i][j],b.values[i][j],1e-5f))return false;return true;}
struct Fixture {
    ReloadTracking tracking;ReloadPreview preview;ReloadRawContact contact;
    std::shared_ptr<SelectedMeshesSnapshot> meshes=std::make_shared<SelectedMeshesSnapshot>();
    Fixture(){
        tracking.enabled=true;tracking.owner={0x10000,0x20000,0x30000,0x40000,17,23,31};
        // Physical hand equip73 deliberately differs from native equip23.
        tracking.inputEvidence={{(std::uint64_t(0x30000)<<32)|0x20000,17,73,31},100,Now,Now+100000000,Now,true,{true,true}};
        meshes->owner=tracking.owner;meshes->sequence=71;meshes->observedNs=Now;meshes->deadlineNs=Now+200000000;
        meshes->stateCount=1;meshes->soleConfiguredArray=0x110000;meshes->states[0].count=1;
        auto& mesh=meshes->states[0].meshes[0];mesh.kind=SelectedMeshKind::Spas12;mesh.address=0x120000;
        std::memcpy(mesh.assetPath.data(),SpasReloadMesh.data(),SpasReloadMesh.size());preview.selectedMeshes=meshes;
        ReloadHoldIdentity native;native.owner=tracking.owner;native.serverPlayer=0x50000;native.serverSoldier=0x60000;native.serverItem=0x70000;
        native.firing={0x80000,0x90000,0xa0000};
        preview.reserve={native,81,Now,Now+150000000,4,12,8,true};
        preview.shellClaim={{501,tracking.inputEvidence.owner,InteractionHand::Left,HandClaimKind::AmmoObject,{601,1},{701,1},502},Now+100000000,99};
        preview.weaponClaim={{502,tracking.inputEvidence.owner,InteractionHand::Right,HandClaimKind::GunHold,{0x40000,73},{702,1},0},Now+100000000,100};
        contact.valid=true;contact.nativeShellVisible=true;contact.owner=tracking.owner;contact.rigFingerprint=SpasReloadRig;
        contact.inputEvidence=tracking.inputEvidence;contact.rawLeftWristWorldMeters=Pose(2.1f,3.2f,4.3f);contact.weaponWorldMeters=Pose(2,3,4);
    }
    void Guided(){
        preview.phase=ReloadPreviewPhase::Guided;preview.native={preview.reserve.identity,41,82,Now,Now+100000000,4,12,8,true,true};
        auto& t=preview.targets;t.identity={tracking.inputEvidence.owner,preview.weaponClaim.token.item,preview.shellClaim.token.item,31};
        t.shellClaim=preview.shellClaim.token;t.weaponClaim=preview.weaponClaim.token;t.inputSequence=99;t.nativeCycle=41;
        t.observedNs=Now-1000000;t.deadlineNs=Now+99000000;t.weaponFromShellCenterMeters=Pose(.1f,.2f,.3f);
        t.weaponFromLeftWristMeters=Multiply(SpasReloadInsertionProfile().itemFromHand,t.weaponFromShellCenterMeters);
    }
    void Publish(){tracking.preview=std::make_shared<const ReloadPreview>(preview);}
};
int CarriedHasRealZeroCycle(){Fixture f;CHECK(ReloadTrackingFresh(f.tracking,Now));CHECK(ReloadPreviewFresh(f.preview,f.tracking,Now));
    const auto t=ResolveReloadPreviewTargets(f.preview,f.tracking,f.contact,Now);CHECK(t&&!t->nativeCycle);CHECK(t->inputSequence==100);
    CHECK(Same(Multiply(t->weaponFromLeftWristMeters,f.contact.weaponWorldMeters),f.contact.rawLeftWristWorldMeters));
    CHECK(Same(Multiply(SpasReloadInsertionProfile().itemFromHand,t->weaponFromShellCenterMeters),t->weaponFromLeftWristMeters));
    CHECK(t->observedNs==f.tracking.inputEvidence.observedNs&&t->deadlineNs==Now+100000000);return 0;}
// Regression for pickup changing a correctly mapped free wrist to the old
// animation-calibrated support wrist. The native call site must supply freeLeft;
// this exercises the downstream composition through moving weapon/hand frames.
int CarriedPreservesAnatomicalWristAcrossPickup(){
    for(float yaw:{-.9f,0.f,1.7f}){
        Fixture f;math::Pose gripPose;gripPose.orientation={0,std::sin(yaw/2),0,std::cos(yaw/2)};
        const auto grip=*InverseRigid(*math::MakeLhViewFromOpenXRPose(gripPose));
        auto anatomicalAttachment=Pose();anatomicalAttachment.values[1][1]=anatomicalAttachment.values[2][2]=-1;
        auto freeWrist=Multiply(anatomicalAttachment,grip);freeWrist.values[3]={2.1f,3.2f,4.3f,1};
        auto oldSupportWrist=grip;oldSupportWrist.values[3]=freeWrist.values[3];
        CHECK(reload_insertion_detail::Angle(oldSupportWrist,freeWrist)>3.14f);
        for(unsigned step=0;step<5;++step){
            freeWrist.values[3][0]+=.01f;f.contact.rawLeftWristWorldMeters=freeWrist;
            f.contact.weaponWorldMeters=Multiply(grip,Pose(2+step*.02f,3,4));
            const auto picked=ResolveReloadPreviewTargets(f.preview,f.tracking,f.contact,Now);CHECK(picked);
            const auto carried=Multiply(picked->weaponFromLeftWristMeters,f.contact.weaponWorldMeters);
            CHECK(Same(carried,freeWrist));
            CHECK(reload_insertion_detail::Angle(carried,oldSupportWrist)>3.14f);
            // The measured shell/finger grasp is unchanged; only its upstream
            // tracked wrist is anatomical. Release returns to this same wrist.
            CHECK(Same(Multiply(SpasReloadInsertionProfile().itemFromHand,picked->weaponFromShellCenterMeters),picked->weaponFromLeftWristMeters));
            const auto afterRelease=freeWrist;CHECK(Same(carried,afterRelease));
        }
    }return 0;
}
int NativePhysicalEquipAreDistinct(){Fixture f;CHECK(f.tracking.owner.equipGeneration!=f.tracking.inputEvidence.owner.equipGeneration);
    CHECK(ReloadPreviewFresh(f.preview,f.tracking,Now));++f.preview.weaponClaim.token.item.generation;CHECK(!ReloadPreviewFresh(f.preview,f.tracking,Now));return 0;}
int GuidedKeepsOriginalGeometry(){Fixture f;f.Guided();CHECK(ReloadPreviewFresh(f.preview,f.tracking,Now));const auto t=ResolveReloadPreviewTargets(f.preview,f.tracking,f.contact,Now);
    CHECK(t&&t->inputSequence==99&&t->observedNs==Now-1000000);CHECK(Same(t->weaponFromLeftWristMeters,f.preview.targets.weaponFromLeftWristMeters));
    CHECK(!Same(t->weaponFromLeftWristMeters,Pose(.1f,.2f,.3f)));return 0;}
int OwnerMismatchFails(){for(unsigned k=0;k<9;++k){Fixture f;
    if(k==0)++f.tracking.owner.player;if(k==1)++f.tracking.owner.soldier;if(k==2)++f.tracking.owner.weak;
    if(k==3)++f.tracking.owner.weapon;if(k==4)++f.tracking.owner.actorGeneration;if(k==5)++f.tracking.owner.equipGeneration;if(k==6)++f.tracking.owner.space;
    if(k==7)++f.tracking.inputEvidence.owner.actor;if(k==8)++f.tracking.inputEvidence.owner.space;
    CHECK(!ReloadPreviewFresh(f.preview,f.tracking,Now));}return 0;}
int MeshLeaseRejects(){for(unsigned k=0;k<7;++k){Fixture f;
    if(k==0)f.preview.selectedMeshes.reset();if(k==1)f.meshes->stateCount=2;if(k==2)f.meshes->deadlineNs=Now;
    if(k==3)f.meshes->states[0].meshes[0].assetPath[0]='X';if(k==4){f.meshes->states[0].count=2;f.meshes->states[0].meshes[1]=f.meshes->states[0].meshes[0];}
    if(k==5)++f.meshes->owner.equipGeneration;if(k==6)f.meshes->deadlineNs=Now+250000001;
    CHECK(!ReloadPreviewFresh(f.preview,f.tracking,Now));}return 0;}
int ClaimsRejectLoss(){for(unsigned k=0;k<9;++k){Fixture f;
    if(k==0)f.preview.shellClaim.token.kind=HandClaimKind::WeaponSupport;if(k==1)f.preview.shellClaim.token.hand=InteractionHand::Right;
    if(k==2)f.preview.shellClaim.deadlineNs=Now;if(k==3)f.preview.weaponClaim.token.id=0;if(k==4)++f.preview.shellClaim.token.prerequisiteClaim;
    if(k==5)++f.preview.shellClaim.token.owner.equipGeneration;if(k==6)f.preview.shellClaim.inputSequence=101;
    if(k==7)f.tracking.inputEvidence.released[0]=true;if(k==8)f.preview.weaponClaim.token.item.id++;
    CHECK(!ReloadPreviewFresh(f.preview,f.tracking,Now));}return 0;}
int NativeLeaseAndCyclesReject(){for(unsigned k=0;k<9;++k){Fixture f;f.Guided();
    if(k==0)f.preview.native.allThreeHeld=false;if(k==1)f.preview.native.nativeBindingVerified=false;if(k==2)f.preview.native.cycle=0;
    if(k==3)++f.preview.targets.nativeCycle;if(k==4)f.preview.native.identity.firing[2]=f.preview.native.identity.firing[1];
    if(k==5)f.preview.native.deadlineNs=Now;if(k==6)++f.preview.targets.shellClaim.id;if(k==7)f.preview.targets.observedNs=Now+1;
    if(k==8)f.preview.targets.identity.trackingEpoch=0;
    CHECK(!ReloadPreviewFresh(f.preview,f.tracking,Now));}return 0;}
int CarriedNeverInventsHeldCycle(){for(unsigned k=0;k<6;++k){Fixture f;
    if(k==0)f.preview.native.cycle=1;if(k==1)f.preview.targets.nativeCycle=1;if(k==2)f.preview.reserve.verified=false;
    if(k==3)f.preview.reserve.reserve=0;if(k==4)f.preview.reserve.loaded=8;if(k==5)f.preview.reserve.deadlineNs=Now;
    CHECK(!ReloadPreviewFresh(f.preview,f.tracking,Now));}return 0;}
int ImmutablePreviewRetainsOriginalLease(){Fixture f;f.Publish();const auto old=*f.tracking.preview;CHECK(ReloadPreviewRetained(old,f.tracking,Now));
    ++f.tracking.inputEvidence.sequence;f.tracking.inputEvidence.observedNs+=10000000;f.tracking.inputEvidence.deadlineNs+=10000000;
    f.preview.shellClaim.inputSequence=101;f.preview.weaponClaim.inputSequence=101;f.preview.shellClaim.deadlineNs+=10000000;f.preview.weaponClaim.deadlineNs+=10000000;f.Publish();
    CHECK(ReloadPreviewRetained(old,f.tracking,Now+10000000));CHECK(!ReloadPreviewRetained(old,f.tracking,Now+100000000));
    ++f.preview.shellClaim.token.id;f.Publish();CHECK(!ReloadPreviewRetained(old,f.tracking,Now+10000000));return 0;}
int PendingDoesNotFabricateHold(){Fixture f;f.Guided();f.preview.phase=ReloadPreviewPhase::Pending;f.Publish();const auto old=*f.tracking.preview;
    CHECK(ReloadPreviewRetained(old,f.tracking,Now));++f.preview.targets.identity.trackingEpoch;f.Publish();CHECK(!ReloadPreviewRetained(old,f.tracking,Now));
    --f.preview.targets.identity.trackingEpoch;f.preview.native.allThreeHeld=false;f.Publish();CHECK(!ReloadPreviewRetained(old,f.tracking,Now));
    f.preview.native.allThreeHeld=true;++f.preview.native.cycle;++f.preview.targets.nativeCycle;f.Publish();CHECK(!ReloadPreviewRetained(old,f.tracking,Now));return 0;}
int RawContactIdentityAndTime(){for(unsigned k=0;k<8;++k){Fixture f;
    if(k==0)f.contact.valid=false;if(k==1)++f.contact.owner.equipGeneration;if(k==2)++f.contact.inputEvidence.sequence;
    if(k==3)++f.contact.inputEvidence.observedNs;if(k==4)++f.contact.inputEvidence.deadlineNs;if(k==5)++f.contact.rigFingerprint;
    if(k==6)f.contact.rawLeftWristWorldMeters={};if(k==7)f.contact.weaponWorldMeters={};
    CHECK(!ResolveReloadPreviewTargets(f.preview,f.tracking,f.contact,Now));}return 0;}
int DefaultAndProductionRigGate(){Fixture f;f.tracking.enabled=false;CHECK(!ReloadPreviewFresh(f.preview,f.tracking,Now));
    f.tracking.enabled=true;RigSnapshot rig;rig.identity.soldier=f.tracking.owner.soldier;rig.identity.weak=f.tracking.owner.weak;
    CHECK(!BuildReloadRawContact(f.tracking,rig,SpasReloadAsset,Pose(),Pose(),1,Now).valid);
    CHECK(!BuildReloadRawContact(f.tracking,rig,"XM8_sp_s",Pose(),Pose(),1,Now).valid);return 0;}
void Control(Fixture& f){
    ReloadShellControl c;c.cycle=41;c.reserve=f.preview.reserve;c.weaponClaim=f.preview.weaponClaim;c.selectedMeshes=f.meshes;
    f.tracking.preview.reset();f.tracking.shellControl=std::make_shared<const ReloadShellControl>(c);
}
ReloadShellHidePlan HidePlan(const Fixture& f){
    ReloadShellHidePlan p;p.source=f.tracking;p.shell=2;p.ordinary.resize(4);
    for(auto& row:p.ordinary){row.fill(std::byte{0x91});for(unsigned r=0;r<4;++r)for(unsigned c=0;c<3;++c){
        const float value=float(1+r*3+c)*.1f;std::memcpy(row.data()+r*16+c*4,&value,4);}}
    const std::array<std::uint32_t,1> indices{p.shell};p.hidden=*weapon_visibility_detail::CollapseWeightedPalette(p.ordinary,indices);return p;
}
int CycleControlDoesNotInventAnAmmoClaim(){Fixture f;Control(f);
    CHECK(ReloadShellControlFresh(f.tracking,Now));CHECK(!f.tracking.preview);
    auto control=*f.tracking.shellControl;control.reserve.loaded=8;control.reserve.reserve=0;
    f.tracking.shellControl=std::make_shared<const ReloadShellControl>(control);
    // A full or pending native cycle has no allThreeHeld/item-claim authority.
    // Visibility only does not grant insertion authority or manufacture an item.
    CHECK(ReloadShellControlFresh(f.tracking,Now));f.tracking.inputEvidence.released[0]=true;
    CHECK(ReloadShellControlFresh(f.tracking,Now));f.tracking.inputEvidence.released[1]=true;
    CHECK(!ReloadShellControlFresh(f.tracking,Now));return 0;
}
int CycleHidePreservesAllOtherBytes(){Fixture f;Control(f);const auto p=HidePlan(f);const auto original=p.ordinary;
    const auto packed=SelectReloadShellPackedPalette(p,&f.tracking,Now,true);CHECK(!packed.fallback&&packed.bytes.data()==p.hidden.data());
    for(unsigned n=0;n<p.ordinary.size();++n)for(unsigned r=0;r<4;++r)for(unsigned c=0;c<16;++c){
        if(n==p.shell&&c<12)CHECK(packed.bytes[n][r*16+c]==std::byte{});
        else CHECK(packed.bytes[n][r*16+c]==original[n][r*16+c]);}
    CHECK(p.ordinary==original);return 0;
}
int CycleCancelBetweenPackCallbacks(){Fixture f;Control(f);const auto p=HidePlan(f);
    const auto first=SelectReloadShellPackedPalette(p,&f.tracking,Now,true);CHECK(!first.fallback);
    f.tracking.shellControl.reset();const auto second=SelectReloadShellPackedPalette(p,&f.tracking,Now+1,true);
    CHECK(second.fallback&&std::equal(second.bytes.begin(),second.bytes.end(),p.ordinary.begin()));
    // A later cycle cannot revive either eye's stale private palette.
    Control(f);auto changed=*f.tracking.shellControl;++changed.cycle;
    f.tracking.shellControl=std::make_shared<const ReloadShellControl>(changed);
    CHECK(SelectReloadShellPackedPalette(p,&f.tracking,Now+2,true).fallback);return 0;
}
int CyclePackRejectsExpiryOwnerMeshAndPreview(){for(unsigned k=0;k<13;++k){Fixture f;Control(f);const auto p=HidePlan(f);
    auto changed=*f.tracking.shellControl;auto now=Now;bool coherent=true;
    if(k==0)f.tracking.enabled=false;if(k==1)f.tracking.inputEvidence.focused=false;if(k==2)++f.tracking.owner.equipGeneration;
    if(k==3)++changed.reserve.identity.firing[2];if(k==4)++changed.weaponClaim.token.id;if(k==5)now=Now+100000000;
    if(k==6){auto mesh=std::make_shared<SelectedMeshesSnapshot>(*f.meshes);++mesh->states[0].meshes[0].address;changed.selectedMeshes=mesh;}
    if(k==7)f.tracking.preview=std::make_shared<const ReloadPreview>(f.preview);
    if(k==8)changed.reserve.verified=false;if(k==9)coherent=false;if(k==10)++f.tracking.inputEvidence.observedNs;
    if(k==11)f.tracking.inputEvidence.tracked[0]=false;if(k==12)changed.selectedMeshes.reset();
    f.tracking.shellControl=std::make_shared<const ReloadShellControl>(changed);
    const auto q=SelectReloadShellPackedPalette(p,&f.tracking,now,coherent);CHECK(q.fallback&&q.bytes.data()==p.ordinary.data());
}return 0;}
int CycleRefreshDoesNotExtendOriginalLease(){Fixture f;Control(f);const auto p=HidePlan(f);
    ++f.tracking.inputEvidence.sequence;f.tracking.inputEvidence.observedNs+=10000000;f.tracking.inputEvidence.deadlineNs+=10000000;
    auto updated=*f.tracking.shellControl;++updated.weaponClaim.inputSequence;updated.weaponClaim.deadlineNs+=10000000;
    ++updated.reserve.sequence;updated.reserve.observedNs+=10000000;updated.reserve.deadlineNs+=10000000;
    f.tracking.shellControl=std::make_shared<const ReloadShellControl>(updated);
    CHECK(!SelectReloadShellPackedPalette(p,&f.tracking,Now+10000000,true).fallback);
    CHECK(SelectReloadShellPackedPalette(p,&f.tracking,Now+100000000,true).fallback);
    CHECK(SelectReloadShellPackedPalette(p,nullptr,Now,true).fallback);
    RigSnapshot unsupported;CHECK(!BuildReloadShellHidePalette(f.tracking,unsupported,p.ordinary,SpasReloadAsset,Now+10000000));return 0;
}
int PackReleaseRestoresExactHiddenBase(){Fixture f;f.Publish();const auto preview=*f.tracking.preview;
    std::vector<std::array<std::byte,64>> hidden(3),posed(3);for(auto& row:hidden)row.fill(std::byte{0x91});posed=hidden;
    // The base retains actual collapsed xyz plus opaque SIMD padding.
    for(unsigned row=0;row<4;++row)for(unsigned col=0;col<3;++col){const float v=row==col&&row<3?.0001f:0.f;
        std::memcpy(hidden[2].data()+row*16+col*4,&v,4);}
    posed=hidden;const float visible=1;std::memcpy(posed[2].data(),&visible,4);
    const auto selected=SelectReloadPackedPalette(preview,&f.tracking,Now,Now+100000000,posed,hidden,true);
    CHECK(!selected.fallback&&selected.bytes.data()==posed.data());
    f.tracking.inputEvidence.released[0]=true;
    const auto dropped=SelectReloadPackedPalette(preview,&f.tracking,Now,Now+100000000,posed,hidden,true);
    CHECK(dropped.fallback&&dropped.bytes.size()==hidden.size()&&std::equal(dropped.bytes.begin(),dropped.bytes.end(),hidden.begin()));
    f.tracking.inputEvidence.released[0]=false;
    CHECK(SelectReloadPackedPalette(preview,&f.tracking,Now,Now,posed,hidden,true).fallback);
    CHECK(SelectReloadPackedPalette(preview,&f.tracking,Now,Now+100000000,posed,hidden,false).fallback);
    const auto malformed=SelectReloadPackedPalette(preview,nullptr,Now,Now+100000000,posed,{},false);CHECK(malformed.fallback&&malformed.bytes.empty());
    return 0;}
}
int main(){if(CycleControlDoesNotInventAnAmmoClaim()||CycleHidePreservesAllOtherBytes()||CycleCancelBetweenPackCallbacks()||CyclePackRejectsExpiryOwnerMeshAndPreview()||CycleRefreshDoesNotExtendOriginalLease()||CarriedHasRealZeroCycle()||CarriedPreservesAnatomicalWristAcrossPickup()||NativePhysicalEquipAreDistinct()||GuidedKeepsOriginalGeometry()||OwnerMismatchFails()||MeshLeaseRejects()||
    ClaimsRejectLoss()||NativeLeaseAndCyclesReject()||CarriedNeverInventsHeldCycle()||ImmutablePreviewRetainsOriginalLease()||
    PendingDoesNotFabricateHold()||RawContactIdentityAndTime()||DefaultAndProductionRigGate()||PackReleaseRestoresExactHiddenBase())return 1;
    std::cout<<"Bc2ReloadPreview: 19 cases passed\n";}
