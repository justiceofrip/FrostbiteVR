#include "Test.h"
#include "Bc2VisibilityDescriptorData.h"
#include "Bc2WeaponVisibility.h"
#include "Bc2BodyHolster.h"
#include "Bc2WeaponVisibilityProbe.h"
#include <cstring>
using namespace fvr::bc2;
namespace {
constexpr std::int64_t now=1000000000;
ReloadStateOwner owner{0x10000,0x20000,0x30000,0x40000,1,2,3};
SelectedMeshesSnapshot Snapshot(const VisibilityDescriptor& d){
    SelectedMeshesSnapshot s;s.owner=owner;s.sequence=1;s.observedNs=now-1000000;s.deadlineNs=now+100000000;
    s.weaponData=0x50000;s.meshTypeInfo=0x60000;s.stateTypeInfo=0x70000;s.stateCount=1;
    s.soleConfiguredArray=s.states[0].array=0x80000;s.states[0].count=std::uint8_t(d.meshes.size());
    std::memcpy(s.weaponName.data(),d.asset.data(),d.asset.size());
    for(unsigned n=0;n<s.states[0].count;++n){auto& m=s.states[0].meshes[n];m.address=0x90000+n*0x100;
        m.typeInfo=s.meshTypeInfo;m.namePointer=0xa0000+n*0x100;
        std::memcpy(m.assetPath.data(),d.meshes[n].data(),d.meshes[n].size());}
    return s;
}
int EveryMeasuredRowUsesRealMatcher(){
    CHECK(VisibilityDescriptors.size()>2);
    for(const auto& d:VisibilityDescriptors){const auto s=Snapshot(d);
        CHECK(!d.nativeAdmitted);CHECK(VisibilityConfigurationMatches(s,owner,d,now));
        CHECK(!ResolveVisibilityDescriptor(s,owner,VisibilityDescriptors,now));
        CHECK(ResolveVisibilityDescriptor(s,owner,VisibilityDescriptors,now,true)==&d);
        CHECK(ResolveWeaponVisibilityNames(s,owner,now,true).data()==d.weightedNames.data());
        std::vector<std::string> names{"LeftHand","RightHand"};for(auto n:d.weightedNames)names.emplace_back(n);
        const auto indices=weapon_visibility_detail::ResolveWeightedBones(names,d.weightedNames);CHECK(indices);
        std::vector<std::array<std::byte,64>> palette(names.size());
        for(auto& p:palette){float v=2;for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)std::memcpy(p.data()+r*16+c*4,&v,4);}
        const auto hidden=weapon_visibility_detail::CollapseWeightedPalette(palette,*indices);CHECK(hidden);
        CHECK((*hidden)[0]==palette[0]&&(*hidden)[1]==palette[1]);
    }return 0;
}
int RejectIncompleteOrStaleConfiguration(){
    const auto& d=VisibilityDescriptors[0];const auto original=Snapshot(d);
    for(unsigned n=0;n<11;++n){auto s=original;auto o=owner;
        if(n==0)++o.equipGeneration;if(n==1)s.deadlineNs=now;if(n==2)s.observedNs=now+1;
        if(n==3)s.stateCount=2;if(n==4)++s.states[0].array;if(n==5)++s.states[0].count;
        if(n==6)--s.states[0].count;if(n==7)s.states[0].meshes[0].typeInfo++;
        if(n==8)s.states[0].meshes[0].address=0;if(n==9)s.weaponName.fill('x');
        if(n==10)s.states[0].meshes[0].assetPath.fill('x');
        CHECK(!VisibilityConfigurationMatches(s,o,d,now));
    }
    std::array<std::string_view,2> meshes{d.meshes[0],d.meshes[0]};auto duplicate=d;duplicate.meshes=meshes;
    auto s=original;s.states[0].count=2;s.states[0].meshes[1]=s.states[0].meshes[0];
    CHECK(!VisibilityConfigurationMatches(s,owner,duplicate,now));
    const std::array rows{d,d};CHECK(!ResolveVisibilityDescriptor(original,owner,rows,now,true));return 0;
}
int DiagnosticNeverGrantsProductionAdmission(){
    const auto d=std::find_if(VisibilityDescriptors.begin(),VisibilityDescriptors.end(),[](const auto& x){return x.asset=="AEK971_sp";});
    CHECK(d!=VisibilityDescriptors.end());auto s=std::make_shared<SelectedMeshesSnapshot>(Snapshot(*d));
    Bc2BodyHolster production{{true,true,3}};
    CHECK(!production.AcceptsProfile(owner,s,now));
    Bc2BodyHolster trial;
    CHECK(trial.AdmitDiagnostic(now,now+15000000000ll,BodyHolsterDiagnosticProfile::ExactConfiguredTable));
    CHECK(trial.AcceptsProfile(owner,s,now));CHECK(!trial.AcceptsProfile(owner,s,now+15000000000ll));
    CHECK(!trial.AdmitDiagnostic(now+1,now+15000000001ll,BodyHolsterDiagnosticProfile::ExactConfiguredTable));
    ++s->states[0].count;CHECK(!trial.AcceptsProfile(owner,s,now));
    CHECK(!ValidBodyHolsterDiagnosticProfile(static_cast<BodyHolsterDiagnosticProfile>(6)));return 0;
}
int BuiltinDiagnosticUsesGeneratedCoverage(){
    const auto at=std::find_if(VisibilityDescriptors.begin(),VisibilityDescriptors.end(),[](const auto& d){return d.asset=="SPAS12_sp";});
    CHECK(at!=VisibilityDescriptors.end());auto s=Snapshot(*at);s.states[0].meshes[0].kind=SelectedMeshKind::Spas12;
    const auto ordinary=ResolveWeaponVisibilityNames(s,owner,now,false);
    CHECK(!ordinary.empty()&&ordinary.data()!=at->weightedNames.data());
    const auto diagnostic=ResolveWeaponVisibilityNames(s,owner,now,true);
    CHECK(diagnostic.data()==at->weightedNames.data()&&diagnostic.size()==at->weightedNames.size());return 0;
}
int ActualProbeUsesBoundedExactConfiguration(){
    const auto at=std::find_if(VisibilityDescriptors.begin(),VisibilityDescriptors.end(),[](const auto& x){return x.asset=="AEK971_sp";});CHECK(at!=VisibilityDescriptors.end());const auto& d=*at;auto selected=std::make_shared<SelectedMeshesSnapshot>(Snapshot(d));
    fvr::interaction::HandInteractionSample input;
    input.owner={(std::uint64_t(owner.weak)<<32)|owner.soldier,owner.actorGeneration,4,owner.space};
    input.sequence=1;input.observedNs=now-1000000;input.deadlineNs=now+100000000;input.nowNs=now;
    input.focused=true;input.tracked={true,true};
    Bc2WeaponVisibilityProbe off;CHECK(!off.Tick(owner,input,selected,d.asset,{}).intent.enabled);
    Bc2WeaponVisibilityProbe probe(true);auto result=probe.Tick(owner,input,selected,d.asset,{});
    CHECK(result.phase==WeaponVisibilityProbePhase::Baseline&&result.intent.enabled);
    CHECK(result.intent.authorizationDeadlineNs==now+11000000000ll);
    // A fresh snapshot of another configured set cannot borrow this baseline.
    ++input.sequence;++input.nowNs;selected=std::make_shared<SelectedMeshesSnapshot>(*selected);
    ++selected->states[0].array;selected->soleConfiguredArray=selected->states[0].array;
    result=probe.Tick(owner,input,selected,d.asset,{});
    CHECK(result.phase==WeaponVisibilityProbePhase::Failed&&!result.intent.enabled);return 0;
}
}
int main(){if(EveryMeasuredRowUsesRealMatcher()||RejectIncompleteOrStaleConfiguration()||DiagnosticNeverGrantsProductionAdmission()||BuiltinDiagnosticUsesGeneratedCoverage()||ActualProbeUsesBoundedExactConfiguration())return 1;
    std::printf("%zu exact descriptor consumers; rejection and diagnostic isolation checks passed\n",VisibilityDescriptors.size());}


