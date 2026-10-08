#include "Bc2BeltAmmo.h"
#include "Bc2PhysicalReload.h"
#include "Test.h"
#include <cstring>
#include <iostream>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
auto Pose(float x=0,float y=0,float z=0){auto m=reload_insertion_detail::Identity();m.values[3]={x,y,z,1};return m;}
bool Same(const math::Matrix4&a,const math::Matrix4&b,float epsilon=.0001f){for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)if(!Near(a.values[r][c],b.values[r][c],epsilon))return false;return true;}
struct Evidence {
    std::int64_t now=1000000000;BeltAmmoTracking t;InputFrame input{};
    Evidence(){
        t.owner={0x10000,0x20000,0x30000,0x40000,5,3,7};
        auto&r=t.reserve;r.identity.owner=t.owner;r.identity.firing={0x50000,0x60000,0x70000};
        r.identity.serverPlayer=0x80000;r.identity.serverSoldier=0x90000;r.identity.serverItem=0xa0000;
        r.sequence=11;r.observedNs=now;r.deadlineNs=now+200000000;r.loaded=2;r.capacity=8;r.reserve=8;r.verified=r.allThreeIdle=true;
        auto&v=t.visual;v.enabled=true;v.input={{(std::uint64_t(0x30000)<<32)|0x20000,5,17,7},13,now,now+100000000,now,true,{true,true},{true,false}};
        const HandInteractionKey weapon{t.owner.weapon,17};v.gun={{19,v.input.owner,InteractionHand::Right,HandClaimKind::GunHold,weapon,{123,1},0},v.input.deadlineNs,13};
        Bc2ReloadInteractionSample sample;sample.insertion.identity={v.input.owner,weapon,{},7};sample.insertion.nowNs=now;
        sample.assetName=SpasReloadAsset;sample.meshPath=SpasReloadMesh;sample.rigFingerprint=SpasReloadRig;sample.selectedMeshIdentityVerified=true;
        v.source=*SpasAmmoSupplySource(sample,r);const auto pouch=Bc2PhysicalReload::DefaultPouch();v.contact={pouch.pouchCenterMeters,pouch.pouchRadiusMeters};
        auto mesh=std::make_shared<SelectedMeshesSnapshot>();mesh->owner=t.owner;mesh->sequence=20;mesh->observedNs=now;mesh->deadlineNs=now+200000000;
        mesh->stateCount=1;mesh->soleConfiguredArray=0xb0000;mesh->states[0].count=1;
        auto&m=mesh->states[0].meshes[0];m.kind=SelectedMeshKind::Spas12;m.address=0xc0000;std::memcpy(m.assetPath.data(),SpasReloadMesh.data(),SpasReloadMesh.size());t.meshes=mesh;
        input.generation=13;input.spaceGeneration=7;input.predictedNs=now;input.focused=input.headValid=true;
        input.referenceHead.orientation.w=input.head.orientation.w=1;input.referenceHead.position={0,1.7f,0};input.head.position=input.referenceHead.position;
        for(auto&h:input.hands){h.gripTracked=h.aimTracked=true;h.grip.orientation.w=h.aim.orientation.w=1;}
    }
};
struct Palette {
    RigSnapshot rig;Bc2ReloadPresentationBinding binding;
    Palette(){
        rig.names={"root","jntWpn_1","jntWpn_7","LeftHand"};rig.parents={-1,0,1,0};rig.weaponBone=1;
        for(const char*f:{"Thumb","Index","Middle","Ring","Pinky"})for(unsigned j=1;j<=3;++j){rig.parents.push_back(j==1?3:std::int32_t(rig.names.size()-1));rig.names.push_back(std::string("LeftHand")+f+std::to_string(j));}
        rig.names.push_back("OtherHidden");rig.parents.push_back(0);
        const auto n=unsigned(rig.names.size());rig.identity.count=n;rig.identity.evaluatedMatrices=0x10000;rig.identity.soldier=0x20000;rig.identity.weak=0x30000;
        rig.inverseBind.assign(n,Pose());rig.evaluatedWorld.assign(n,Pose());rig.nativeWorld.resize(n);rig.nativeEvaluated.resize(n);
        for(auto&b:rig.nativeEvaluated)b.fill(std::byte{0x91});binding=*bc2_reload_detail::DerivePresentationBinding(rig);
        auto collapsed=Pose();for(unsigned r=0;r<3;++r)collapsed.values[r][r]=.0001f;
        for(auto index:{binding.shell,n-1}){rig.nativeHiddenLeaves.push_back(index);
            for(auto*list:{&rig.nativeWorld,&rig.nativeEvaluated}){auto&b=(*list)[index];b.fill(std::byte{0xa5});for(unsigned row=0;row<4;++row)std::memcpy(b.data()+row*16,collapsed.values[row].data(),12);}}
    }
};
void Held(Evidence& e){const auto& r=e.t.reserve;e.t.reserve.allThreeIdle=false;
    e.t.heldCycle=ReloadRoundLease{r.identity,7,21,e.now,e.now+100000000,r.loaded,r.reserve,r.capacity,true,true};}
ReloadTracking Control(const Evidence& e){ReloadTracking t;t.enabled=true;t.owner=e.t.owner;t.inputEvidence=e.t.visual.input;
    t.belt=std::make_shared<const BeltAmmoTracking>(e.t);
    if(e.t.heldCycle)t.shellControl=std::make_shared<const ReloadShellControl>(ReloadShellControl{e.t.heldCycle->cycle,e.t.reserve,e.t.visual.gun,e.t.meshes});
    return t;}
int IdleSecondPackDoesNotRetainPriorVisibleBuffer(){
    for(unsigned mode=0;mode<3;++mode){Evidence e;Palette p;
        BeltAmmoPalette plan{e.t,p.rig.identity,p.binding.shell,p.rig.nativeEvaluated,p.rig.nativeEvaluated};
        plan.posed[p.binding.shell][0]=std::byte{0x34};auto scopeBuffer=plan.ordinary;
        const auto first=SelectSpasBeltOverlay(plan,&e.t,e.now,true,scopeBuffer,false);CHECK(!first.fallback);
        scopeBuffer.assign(first.bytes.begin(),first.bytes.end());CHECK(scopeBuffer==plan.posed);
        auto current=e.t;auto now=e.now;
        if(mode==0)current.visual.occupied=true;if(mode==1)now=current.visual.input.deadlineNs;
        const auto second=SelectSpasBeltOverlay(plan,mode==2?nullptr:&current,now,true,scopeBuffer,false);
        CHECK(second.fallback);if(second.bytes.data()!=scopeBuffer.data())scopeBuffer.assign(second.bytes.begin(),second.bytes.end());
        CHECK(scopeBuffer==plan.ordinary&&scopeBuffer!=plan.posed);
    }return 0;
}
int HeldCycleRequiresActualFreshMatchingNativeLease(){
    Evidence good;Held(good);CHECK(SpasBeltAmmoFresh(good.t,good.now));CHECK(ReloadBeltCompatible(Control(good),good.now));
    for(unsigned k=0;k<10;++k){auto e=good;
        if(k==0)e.t.heldCycle->allThreeHeld=false;if(k==1)e.t.heldCycle->nativeBindingVerified=false;
        if(k==2)e.t.heldCycle->cycle=0;if(k==3)e.t.heldCycle->sequence=0;if(k==4)++e.t.heldCycle->identity.firing[2];
        if(k==5)--e.t.heldCycle->loaded;if(k==6)--e.t.heldCycle->reserve;if(k==7)e.t.heldCycle->deadlineNs=e.now;
        if(k==8)e.t.heldCycle->observedNs=e.now+1;if(k==9)e.t.heldCycle->deadlineNs=e.t.heldCycle->observedNs+200000001;
        CHECK(!SpasBeltAmmoFresh(e.t,e.now));CHECK(!ReloadBeltCompatible(Control(e),e.now));}
    auto changed=good.t;++changed.heldCycle->cycle;CHECK(!SpasBeltAmmoRetained(good.t,changed,good.now));
    changed=good.t;++changed.heldCycle->deadlineNs;CHECK(!SpasBeltAmmoRetained(good.t,changed,good.now));
    changed=good.t;changed.heldCycle.reset();changed.reserve.allThreeIdle=true;CHECK(!SpasBeltAmmoRetained(good.t,changed,good.now));
    return 0;
}
int CycleControlAndOwnedItemWinOverBelt(){
    Evidence e;Held(e);const auto original=Control(e);CHECK(ReloadBeltCompatible(original,e.now));
    for(unsigned k=0;k<8;++k){auto t=original;auto control=std::make_shared<ReloadShellControl>(*t.shellControl);
        if(k==0)t.shellControl.reset();if(k==1)++control->cycle;if(k==2)--control->reserve.reserve;
        if(k==3)++control->weaponClaim.token.id;if(k==4)t.preview=std::make_shared<const ReloadPreview>();
        if(k==5)++t.inputEvidence.sequence;if(k==6)t.inputEvidence.focused=false;if(k==7)++control->reserve.identity.owner.weapon;
        if(k!=0)t.shellControl=control;CHECK(!ReloadBeltCompatible(t,e.now));}
    Evidence idle;CHECK(ReloadBeltCompatible(Control(idle),idle.now));auto bad=Control(idle);bad.shellControl=original.shellControl;
    CHECK(!ReloadBeltCompatible(bad,idle.now));return 0;
}
int SecondPackFallbackPreservesCurrentSuppressionChoice(){
    Evidence e;Held(e);Palette p;BeltAmmoPalette plan{e.t,p.rig.identity,p.binding.shell,p.rig.nativeEvaluated,p.rig.nativeEvaluated};
    plan.posed[p.binding.shell][0]=std::byte{0x34};auto hidden=plan.ordinary;hidden[p.binding.shell][0]=std::byte{0x56};
    CHECK(!SelectSpasBeltOverlay(plan,&e.t,e.now,true,hidden,true).fallback);
    const auto fallback=[&](const BeltAmmoTracking* current,std::int64_t now,bool coherent,bool hiddenCurrent,
                            const std::vector<std::array<std::byte,64>>& base){
        const auto chosen=SelectSpasBeltOverlay(plan,current,now,coherent,base,hiddenCurrent);
        return chosen.fallback&&std::equal(chosen.bytes.begin(),chosen.bytes.end(),base.begin(),base.end());};
    auto current=e.t;current.visual.occupied=true;CHECK(fallback(&current,e.now,true,true,hidden));
    current=e.t;++current.heldCycle->cycle;CHECK(fallback(&current,e.now,true,true,hidden));
    CHECK(fallback(nullptr,e.now,true,true,hidden));CHECK(fallback(&e.t,e.now,false,true,hidden));
    CHECK(fallback(&e.t,e.t.heldCycle->deadlineNs,true,true,hidden));
    CHECK(fallback(&e.t,e.now,true,false,plan.ordinary));
    CHECK(!SelectSpasBeltOverlay(plan,&e.t,e.now,true,{},true).bytes.size());
    float one=1;std::memcpy(p.rig.nativeWorld[p.binding.shell].data(),&one,4);
    CHECK(!belt_ammo_detail::PlaceIdleShell(p.rig,p.binding,p.rig.nativeEvaluated,Pose(),1));return 0;
}

int AvailabilityUsesRealSourceWithoutMintingItems(){
    Evidence f;const auto before=f.t.reserve;CHECK(SpasBeltAmmoFresh(f.t,f.now));
    CHECK(f.t.visual.source.reserveUnits==8&&f.t.visual.source.objectUnits==1);
    f.t.visual.source.family=ReloadInsertionFamily::Magazine;CHECK(AmmoSupplyVisualFresh(f.t.visual,f.now));CHECK(!SpasBeltAmmoFresh(f.t,f.now));
    CHECK(!IndependentBeltMagazineInstance);CHECK(f.t.reserve.loaded==before.loaded&&f.t.reserve.reserve==before.reserve);return 0;
}
int FailClosedSourceOwnerAndPriority(){
    for(unsigned k=0;k<16;++k){Evidence f;
        if(k==0)f.t.visual.enabled=false;if(k==1)f.t.visual.occupied=true;if(k==2)f.t.visual.source.reserveUnits=0;
        if(k==3)f.t.reserve.verified=false;if(k==4)f.t.reserve.allThreeIdle=false;if(k==5)++f.t.reserve.identity.owner.weapon;
        if(k==6)++f.t.visual.gun.token.id,f.t.visual.gun.token.item.id++;
        if(k==7)f.t.visual.input.focused=false;if(k==8)f.t.visual.input.tracked[0]=false;if(k==9)f.t.visual.input.released[1]=true;
        if(k==10)f.now=f.t.visual.input.deadlineNs;if(k==11)++f.t.visual.source.identity.owner.space;
        if(k==12)f.t.meshes.reset();if(k==13)f.t.visual.contact.radiusMeters=0;if(k==14)f.t.visual.source.observedNs=f.now+1;
        if(k==15)f.t.visual.gun.deadlineNs=f.now;CHECK(!SpasBeltAmmoFresh(f.t,f.now));
    }return 0;
}
int OldPaletteCannotRenewOrBorrowAChangedPool(){
    Evidence f;const auto old=f.t;CHECK(SpasBeltAmmoRetained(old,f.t,f.now));
    ++f.t.visual.input.sequence;++f.t.visual.gun.inputSequence;f.t.visual.input.observedNs+=1000000;f.t.visual.input.deadlineNs+=1000000;f.t.visual.gun.deadlineNs+=1000000;
    CHECK(SpasBeltAmmoRetained(old,f.t,f.now+1000000));CHECK(!SpasBeltAmmoRetained(old,f.t,old.visual.input.deadlineNs));
    auto changed=f.t;++changed.visual.gun.token.id;CHECK(!SpasBeltAmmoRetained(old,changed,f.now+1000000));
    changed=f.t;--changed.reserve.reserve;--changed.visual.source.reserveUnits;CHECK(!SpasBeltAmmoRetained(old,changed,f.now+1000000));
    changed=old;changed.visual.input.deadlineNs++;changed.visual.gun.deadlineNs++;CHECK(!SpasBeltAmmoRetained(old,changed,f.now));return 0;
}
int BeltIsTheActualContactInBothAnchorFrames(){
    for(const auto frame:{SupplyAnchorFrame::HeadYaw,SupplyAnchorFrame::RecenteredBody})for(float units:{1.f,100.f}){
        Evidence f;f.t.visual.frame=frame;f.input.worldUnitsPerMeter=units;
        f.input.referenceHead.orientation={0,std::sin(.3f),0,std::cos(.3f)};f.input.head.orientation={0,std::sin(.9f),0,std::cos(.9f)};
        f.input.head.position={.15f,1.3f,-.18f};const auto eye=Pose(20,-10,30);
        auto anchor=AmmoSupplyAnchorWorld(f.input,eye,f.t.visual);CHECK(anchor);
        // Use the real pickup readers, with a physical controller pose offset
        // from a rotated/crouched head, then project that actual contact back.
        f.input.hands[0].grip.position={-.24f,.85f,-.27f};
        const auto contact=frame==SupplyAnchorFrame::RecenteredBody?BodyAnchorHandPose(f.input,InteractionHand::Left):PhysicalReloadPouchPose(f.input);CHECK(contact);
        for(unsigned n=0;n<3;++n)f.t.visual.contact.centerMeters[n]=contact->values[3][n];
        anchor=AmmoSupplyAnchorWorld(f.input,eye,f.t.visual);CHECK(anchor);
        auto grip=math::MakeRelativePose(f.input.referenceHead,f.input.hands[0].grip);CHECK(grip);
        grip->position.x*=units;grip->position.y*=units;grip->position.z*=units;
        const auto view=math::MakeLhViewFromOpenXRPose(*grip);CHECK(view);const auto expected=Multiply(*InverseRigid(*view),eye);
        for(unsigned n=0;n<3;++n)CHECK(Near(anchor->values[3][n],expected.values[3][n],.0001f));
    }return 0;
}
int RecenteredBeltDoesNotOrbitWithHeadYaw(){
    Evidence f;f.t.visual.frame=SupplyAnchorFrame::RecenteredBody;auto a=AmmoSupplyAnchorWorld(f.input,Pose(),f.t.visual);CHECK(a);
    f.input.head.orientation={0,std::sin(.7f),0,std::cos(.7f)};auto b=AmmoSupplyAnchorWorld(f.input,Pose(),f.t.visual);CHECK(b&&Same(*a,*b));
    f.t.visual.frame=SupplyAnchorFrame::HeadYaw;b=AmmoSupplyAnchorWorld(f.input,Pose(),f.t.visual);CHECK(b&&!Same(*a,*b));
    ++f.input.spaceGeneration;CHECK(!AmmoSupplyAnchorWorld(f.input,Pose(),f.t.visual));return 0;
}
int PrivatePaletteMovesOnlyProvenIdleShell(){
    Palette f;const auto before=f.rig.nativeEvaluated;auto ordinary=before;ordinary[1][0]=std::byte{0x42};
    const auto center=Pose(-.23f,-.55f,.02f);const auto out=belt_ammo_detail::PlaceIdleShell(f.rig,f.binding,ordinary,center,1);CHECK(out);
    for(unsigned i=0;i<out->size();++i)if(i!=f.binding.shell)CHECK((*out)[i]==ordinary[i]);
    for(unsigned row=0;row<4;++row)CHECK(std::memcmp((*out)[f.binding.shell].data()+row*16+12,before[f.binding.shell].data()+row*16+12,4)==0);
    CHECK((*out)[f.binding.shell]!=before[f.binding.shell]&&f.rig.nativeEvaluated==before);
    const auto bone=SpasShellBoneAtCenter(center,1);CHECK(bone);CHECK(Same(Multiply(Pose(0,.00033545875f,.000945806466f),*bone),center));
    // A synthetic rig is useful for isolation tests but grants no production binding.
    Evidence e;CHECK(!BuildSpasBeltAmmoPalette(e.t,f.rig,ordinary,e.input,Pose(),SpasReloadAsset,e.now));
    f.rig.nativeHiddenLeaves.erase(f.rig.nativeHiddenLeaves.begin());CHECK(!belt_ammo_detail::PlaceIdleShell(f.rig,f.binding,ordinary,center,1));return 0;
}
int NativeVisibleMalformedAndScaleGuards(){
    for(unsigned k=0;k<5;++k){Palette f;auto ordinary=f.rig.nativeEvaluated;auto center=Pose();float scale=1;
        if(k==0){float x=1;std::memcpy(f.rig.nativeEvaluated[f.binding.shell].data(),&x,4);}
        if(k==1)++f.binding.fingerprint;if(k==2)ordinary.pop_back();if(k==3)scale=0;if(k==4)center.values[0][0]=2;
        CHECK(!belt_ammo_detail::PlaceIdleShell(f.rig,f.binding,ordinary,center,scale));
    }return 0;
}
int EachPackRevalidatesAndFallsBackToOrdinary(){
    Evidence e;Palette p;BeltAmmoPalette plan{e.t,p.rig.identity,p.binding.shell,p.rig.nativeEvaluated,p.rig.nativeEvaluated};plan.posed[p.binding.shell][0]=std::byte{1};
    CHECK(!SelectSpasBeltAmmoPalette(plan,&e.t,e.now,true).fallback);
    auto current=e.t;current.visual.occupied=true;const auto second=SelectSpasBeltAmmoPalette(plan,&current,e.now,true);CHECK(second.fallback&&std::equal(second.bytes.begin(),second.bytes.end(),plan.ordinary.begin()));
    CHECK(SelectSpasBeltAmmoPalette(plan,&e.t,e.now,false).fallback);CHECK(SelectSpasBeltAmmoPalette(plan,nullptr,e.now,true).fallback);
    CHECK(SelectSpasBeltAmmoPalette(plan,&e.t,e.t.visual.input.deadlineNs,true).fallback);return 0;
}
}
int main(){if(IdleSecondPackDoesNotRetainPriorVisibleBuffer()||HeldCycleRequiresActualFreshMatchingNativeLease()||CycleControlAndOwnedItemWinOverBelt()||SecondPackFallbackPreservesCurrentSuppressionChoice()||AvailabilityUsesRealSourceWithoutMintingItems()||FailClosedSourceOwnerAndPriority()||OldPaletteCannotRenewOrBorrowAChangedPool()||BeltIsTheActualContactInBothAnchorFrames()||RecenteredBeltDoesNotOrbitWithHeadYaw()||PrivatePaletteMovesOnlyProvenIdleShell()||NativeVisibleMalformedAndScaleGuards()||EachPackRevalidatesAndFallsBackToOrdinary())return 1;std::cout<<"Bc2BeltAmmo: 12 groups passed (offline; no native/headset claim)\n";}
