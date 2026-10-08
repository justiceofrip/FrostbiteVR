#include "Bc2WeaponVisibilityProbe.h"
#include "Test.h"
#include <cstring>
#include <iostream>
#include <sstream>
using namespace fvr;using namespace fvr::bc2;
namespace {
struct Fixture {
    ReloadStateOwner owner{0x10000,0x20000,0x30000,0x40000,5,6,7};
    interaction::HandInteractionSample input{};
    std::shared_ptr<SelectedMeshesSnapshot> meshes;
    Fixture(){input.owner={(std::uint64_t(owner.weak)<<32)|owner.soldier,5,17,7};input.focused=true;input.tracked={true,true};Advance(1000000000);}
    void Advance(std::int64_t now){++input.sequence;input.nowNs=input.observedNs=now;input.deadlineNs=now+100000000;
        meshes=std::make_shared<SelectedMeshesSnapshot>();meshes->owner=owner;meshes->sequence=input.sequence;meshes->observedNs=now;meshes->deadlineNs=now+200000000;
        meshes->stateCount=1;meshes->weaponData=0x50000;meshes->soleConfiguredArray=meshes->states[0].array=0x60000;meshes->states[0].count=1;
        std::strcpy(meshes->weaponName.data(),"SPAS12_sp");auto& mesh=meshes->states[0].meshes[0];mesh.kind=SelectedMeshKind::Spas12;
        mesh.address=0x70000;mesh.typeInfo=0x80000;mesh.namePointer=0x90000;
        std::strcpy(mesh.assetPath.data(),"Objects/Weapons/Handheld/UL_shg_SPAS12/UL_shg_SPAS-12_Mesh");}
    WeaponVisibilityProbeSample Tick(Bc2WeaponVisibilityProbe& p,std::optional<WeaponVisibilityReceipt> r={}){return p.Tick(owner,input,meshes,"SPAS12_sp",r);}
    // Synthetic native callback result for policy tests only; never executable
    // probe evidence and never claims authored rig or native Pack acceptance.
    WeaponVisibilityReceipt SyntheticReceipt(const WeaponVisibilityProbeSample& s,std::uint64_t draw=1){
        auto p=std::make_shared<WeaponVisibilityPlan>();p->reason=WeaponVisibilityReason::None;p->nativeOwner=owner;p->request=s.intent.request;
        p->inputSequence=input.sequence;p->physicalEquipGeneration=input.owner.equipGeneration;p->observedNs=input.observedNs;p->deadlineNs=p->inputDeadlineNs=input.deadlineNs;
        p->meshSequence=meshes->sequence;p->selected=meshes;p->hidden=s.intent.hide;p->rig.soldier=owner.soldier;p->rig.weak=owner.weak;
        p->ordinary.resize(3);for(auto& bone:p->ordinary)bone.fill(std::byte{1});p->originalNative=p->ordinary;p->privatePalette=p->ordinary;p->weightedBones={2};
        if(p->hidden)for(unsigned row=0;row<4;++row)std::memset(p->privatePalette[2].data()+row*16,0,12);
        return {owner,p->rig,p->request,input.sequence,input.owner.equipGeneration,draw,input.nowNs,input.deadlineNs,3,p->hidden,p};
    }
};
int ExplicitFlagsAndDefaultOff(){constexpr auto flags=WeaponVisibilityProbeFlag|0x197800u|9u;
    CHECK(ValidWeaponVisibilityProbeConfig(0,0));CHECK(ValidWeaponVisibilityProbeConfig(flags,15000));
    CHECK(!ValidWeaponVisibilityProbeConfig(flags,14000));CHECK(!ValidWeaponVisibilityProbeConfig(flags&~0x10000u,15000));
    for(auto bit:{0x400u,0x8000u,0x20000u,0x40000u,0x200000u,0x400000u,0x800000u,0x1000000u,0x2000000u,0x4000000u,0x8000000u,0x10000000u,0x20000000u})CHECK(!ValidWeaponVisibilityProbeConfig(flags|bit,15000));
    Fixture f;Bc2WeaponVisibilityProbe off;CHECK(!f.Tick(off).intent.enabled);return 0;}
int NoReceiptNoHiddenPhase(){Fixture f;Bc2WeaponVisibilityProbe p(true);CHECK(!f.Tick(p).intent.hide);
    for(int step=1;step<=10;++step){f.Advance(1000000000LL+step*1000000000LL);auto s=f.Tick(p);CHECK(s.phase==WeaponVisibilityProbePhase::Baseline);CHECK(s.intent.enabled&&!s.intent.hide);}
    f.Advance(13000000000LL);CHECK(!f.Tick(p).intent.enabled);CHECK(p.Phase()==WeaponVisibilityProbePhase::Failed);return 0;}
int ActualReceiptSequenceAndPreservedArms(){Fixture f;Bc2WeaponVisibilityProbe p(true);auto s=f.Tick(p);std::uint64_t draw=0;
    for(unsigned stage=0;stage<3;++stage){
        const auto start=f.input.nowNs;
        for(unsigned step=0;step<3;++step){f.Advance(start+std::int64_t(step)*1000000000);s=f.Tick(p);auto r=f.SyntheticReceipt(s,++draw);s=f.Tick(p,r);}
        CHECK(unsigned(p.Phase())==stage+unsigned(WeaponVisibilityProbePhase::Hidden));
    }
    CHECK(p.Phase()==WeaponVisibilityProbePhase::Done&&!s.intent.enabled);
    std::ostringstream report;p.Report(report);CHECK(report.str().find("\"paired_private_palette_sequence_verified\":true")!=std::string::npos);
    CHECK(report.str().find("\"empty_hands_acknowledged\":false")!=std::string::npos);return 0;}
int ReplacedOwnerExpiredAndRestampedSource(){for(unsigned bad=0;bad<4;++bad){Fixture f;Bc2WeaponVisibilityProbe p(true);auto s=f.Tick(p);
        if(bad==0){++f.owner.equipGeneration;f.meshes->owner=f.owner;}
        if(bad==1)f.input.nowNs=f.input.deadlineNs;
        if(bad==2)++f.input.deadlineNs;
        if(bad==3)f.input.tracked[0]=false;
        CHECK(!f.Tick(p).intent.enabled&&p.Phase()==WeaponVisibilityProbePhase::Failed);}
    return 0;}
int FalseReceiptAndArmMutationRejected(){for(unsigned bad=0;bad<5;++bad){Fixture f;Bc2WeaponVisibilityProbe p(true);auto s=f.Tick(p);auto r=f.SyntheticReceipt(s);
        if(bad==0)r.verifiedCopyMask=1;if(bad==1)++r.request;if(bad==2)r.deadlineNs=f.input.nowNs;
        if(bad==3){auto copy=std::make_shared<WeaponVisibilityPlan>(*r.evidence);copy->privatePalette[0][0]=std::byte{2};r.evidence=copy;}
        if(bad==4)++r.physicalEquipGeneration;
        CHECK(!f.Tick(p,r).receipt);}
    Fixture f;Bc2WeaponVisibilityProbe p(true);f.Tick(p);p.Cancel(8,f.input.nowNs);CHECK(!f.Tick(p).intent.enabled);return 0;}
int BoundedUnavailableStartupDoesNotAuthorizeUnsupportedWeapons(){
    Fixture f;Bc2WeaponVisibilityProbe p(true);auto unavailable=f.input;unavailable.sequence=0;unavailable.observedNs=unavailable.deadlineNs=0;
    auto s=p.Tick({},unavailable,{},"",{});CHECK(!s.intent.enabled&&p.Phase()==WeaponVisibilityProbePhase::Warmup);
    f.Advance(1100000000);s=p.Tick(f.owner,f.input,{},"",{});CHECK(!s.intent.enabled&&p.Phase()==WeaponVisibilityProbePhase::Warmup);
    f.Advance(1200000000);s=f.Tick(p);CHECK(s.intent.enabled&&p.Phase()==WeaponVisibilityProbePhase::Baseline);
    f.input.tracked[1]=false;CHECK(!f.Tick(p).intent.enabled&&p.Phase()==WeaponVisibilityProbePhase::Failed);
    Bc2WeaponVisibilityProbe timeout(true);unavailable.nowNs=1000000000;timeout.Tick({},unavailable,{},"",{});
    unavailable.nowNs=3100000000;s=timeout.Tick({},unavailable,{},"",{});CHECK(!s.intent.enabled&&timeout.Phase()==WeaponVisibilityProbePhase::Failed);
    Fixture selected;Bc2WeaponVisibilityProbe wrong(true);s=wrong.Tick(selected.owner,selected.input,selected.meshes,"40mmgl",{});
    CHECK(!s.intent.enabled&&wrong.Phase()==WeaponVisibilityProbePhase::Failed);return 0;
}
}
int main(){if(ExplicitFlagsAndDefaultOff()||NoReceiptNoHiddenPhase()||ActualReceiptSequenceAndPreservedArms()||ReplacedOwnerExpiredAndRestampedSource()||FalseReceiptAndArmMutationRejected()||BoundedUnavailableStartupDoesNotAuthorizeUnsupportedWeapons())return 1;
    std::cout<<"WeaponVisibilityProbe: 6 policy cases passed; no native visibility claim\n";}
