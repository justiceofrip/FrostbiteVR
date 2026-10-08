#include "Bc2WeaponVisibility.h"
#include "Bc2WeaponVisibilityCoverage.h"
#include "Test.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
using namespace fvr;using namespace fvr::bc2;using namespace weapon_visibility_detail;
namespace {
std::vector<std::string> Names(){std::vector<std::string> names{"Root","LeftHand","RightHand","LeftHandIndex1","RightHandIndex1"};
    for(const auto name:Xm8Bones)names.emplace_back(name);return names;}
auto Palette(unsigned n){std::vector<std::array<std::byte,64>> out(n);
    for(unsigned i=0;i<n;++i)for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){
        float v=c==3?std::numeric_limits<float>::quiet_NaN():float(i*10+r*3+c+1);
        std::memcpy(out[i].data()+r*16+c*4,&v,4);}return out;}
int CompleteAuthoredAttachments(){const auto names=Names();auto spas=ResolveWeightedBones(names,SpasBones),xm8=ResolveWeightedBones(names,Xm8Bones);
    CHECK(spas&&spas->size()==9&&xm8&&xm8->size()==16);
    for(const auto name:{"jntWpnwpnJnt_14","jntWpnwpnJnt_16","jntWpn_10","jntWpn_17"}){
        const auto index=std::uint32_t(std::find(names.begin(),names.end(),name)-names.begin());CHECK(std::find(xm8->begin(),xm8->end(),index)!=xm8->end());}
    auto missing=names;std::erase(missing,std::string("jntWpn_10"));CHECK(!ResolveWeightedBones(missing,Xm8Bones));
    missing=names;missing.emplace_back("JNTWPN_1");CHECK(!ResolveWeightedBones(missing,Xm8Bones));return 0;}
int BothHandsAndEveryOtherBytePreserved(){const auto names=Names();const auto indices=*ResolveWeightedBones(names,Xm8Bones);
    const auto original=Palette(unsigned(names.size()));const auto after=CollapseWeightedPalette(original,indices);CHECK(after);
    for(unsigned n=0;n<original.size();++n){const bool weapon=std::find(indices.begin(),indices.end(),n)!=indices.end();
        if(!weapon)CHECK((*after)[n]==original[n]);
        else for(unsigned row=0;row<4;++row){CHECK(std::memcmp((*after)[n].data()+row*16+12,original[n].data()+row*16+12,4)==0);
            for(unsigned col=0;col<3;++col){float value=1;std::memcpy(&value,(*after)[n].data()+row*16+col*4,4);CHECK(value==0);}}}
    CHECK(original==Palette(unsigned(names.size()))); // no source mutation
    CHECK((*after)[1]==original[1]&&(*after)[2]==original[2]);return 0;}
int WeightedTrianglesCollapseDespiteDifferentBones(){const auto names=Names();const auto indices=*ResolveWeightedBones(names,Xm8Bones);
    const auto original=Palette(unsigned(names.size()));const auto hidden=*CollapseWeightedPalette(original,indices);
    // Independent linear skin evaluation: three vertices use different bones
    // and mixed normalized weights. A root-only collapse would leave a triangle.
    const float vertices[3][4]={{-.3f,.7f,1.f,1},{.8f,-.1f,-1.f,1},{.1f,.2f,.3f,1}};
    float output[3][3]{};
    for(unsigned v=0;v<3;++v)for(unsigned b=0;b<2;++b)for(unsigned c=0;c<3;++c)for(unsigned r=0;r<4;++r){
        float scalar=1;std::memcpy(&scalar,hidden[indices[v*2+b]].data()+r*16+c*4,4);
        output[v][c]+=vertices[v][r]*scalar*(b?.6f:.4f);}
    for(const auto& p:output)for(float value:p)CHECK(value==0);return 0;}
int HandOverlapMalformedAndNonfiniteReject(){const auto names=Names();const std::array<std::string_view,1> left{"LeftHand"};CHECK(!ResolveWeightedBones(names,left));
    auto palette=Palette(3);const std::array<unsigned,2> duplicate{1,1},outside{0,3};CHECK(!CollapseWeightedPalette(palette,duplicate));CHECK(!CollapseWeightedPalette(palette,outside));
    const std::array<unsigned,1> one{1};float nan=std::numeric_limits<float>::quiet_NaN();std::memcpy(palette[1].data(),&nan,4);CHECK(!CollapseWeightedPalette(palette,one));return 0;}
int ExplicitDefaultOffAndNoNativeAuthority(){RigSnapshot rig;WeaponVisibilityRequest r;
    CHECK(BuildWeaponVisibilityPalette(rig,{},r,100).reason==WeaponVisibilityReason::Disabled);
    r.enabled=true;CHECK(BuildWeaponVisibilityPalette(rig,{},r,100).reason==WeaponVisibilityReason::StaleInput);
    CHECK(!WeaponVisibilityPlan::nativeAnimationWritten&&!WeaponVisibilityPlan::submittedVisibilityVerified);return 0;}
int OriginalGuardCannotBeRenewedOrReassigned(){
    WeaponVisibilityPlan p;p.reason=WeaponVisibilityReason::None;p.nativeOwner={0x10000,0x20000,0x30000,0x40000,5,3,7};
    p.request=11;p.inputSequence=20;p.meshSequence=100;p.physicalEquipGeneration=17;p.observedNs=1000000000;
    p.deadlineNs=p.inputDeadlineNs=1100000000;p.hidden=true;
    auto selected=std::make_shared<SelectedMeshesSnapshot>();selected->owner=p.nativeOwner;selected->sequence=p.meshSequence;
    selected->observedNs=p.observedNs;selected->deadlineNs=1200000000;selected->stateCount=1;selected->weaponData=0x50000;
    selected->soleConfiguredArray=selected->states[0].array=0x60000;selected->states[0].count=1;
    std::strcpy(selected->weaponName.data(),"SPAS12_sp");auto& mesh=selected->states[0].meshes[0];mesh.kind=SelectedMeshKind::Spas12;
    mesh.address=0x70000;mesh.typeInfo=0x80000;mesh.namePointer=0x90000;
    std::strcpy(mesh.assetPath.data(),"Objects/Weapons/Handheld/UL_shg_SPAS12/UL_shg_SPAS-12_Mesh");p.selected=selected;
    WeaponVisibilityRequest r;r.enabled=true;r.hide=true;r.request=11;r.nativeOwner=p.nativeOwner;r.selected=selected;
    r.input.owner={(std::uint64_t(p.nativeOwner.weak)<<32)|p.nativeOwner.soldier,5,17,7};r.input.sequence=20;
    r.input.observedNs=p.observedNs;r.input.deadlineNs=p.inputDeadlineNs;r.input.focused=true;r.input.tracked={true,true};
    CHECK(WeaponVisibilityCurrent(p,r,1010000000));
    r.authorizationDeadlineNs=1050000000;CHECK(!WeaponVisibilityCurrent(p,r,1010000000)); // unclamped old plan cannot inherit tighter trial
    const auto originalDeadline=p.inputDeadlineNs;p.deadlineNs=1050000000;
    CHECK(WeaponVisibilityCurrent(p,r,1010000000));CHECK(p.inputDeadlineNs==originalDeadline&&r.input.deadlineNs==originalDeadline);
    CHECK(!WeaponVisibilityCurrent(p,r,1050000000));
    r.authorizationDeadlineNs=1200000000;CHECK(!WeaponVisibilityCurrent(p,r,1050000000)); // no renewal
    r.authorizationDeadlineNs=0;p.deadlineNs=originalDeadline;
    for(unsigned bad=0;bad<8;++bad){auto changed=r;
        if(bad==0)changed.hide=false;if(bad==1)++changed.request;if(bad==2)++changed.nativeOwner.equipGeneration;
        if(bad==3)++changed.input.owner.equipGeneration;if(bad==4)++changed.input.observedNs;
        if(bad==5)++changed.input.deadlineNs;if(bad==6)changed.input.tracked[1]=false;
        if(bad==7){auto unknown=std::make_shared<SelectedMeshesSnapshot>(*selected);unknown->states[0].count=2;changed.selected=unknown;}
        CHECK(!WeaponVisibilityCurrent(p,changed,1010000000));}
    r.input.sequence=21;r.input.observedNs=1090000000;r.input.deadlineNs=1190000000;
    CHECK(!WeaponVisibilityCurrent(p,r,1100000000)); // new input cannot renew the old palette
    return 0;
}
}
int main(){if(CompleteAuthoredAttachments()||BothHandsAndEveryOtherBytePreserved()||WeightedTrianglesCollapseDespiteDifferentBones()||
    HandOverlapMalformedAndNonfiniteReject()||ExplicitDefaultOffAndNoNativeAuthority()||OriginalGuardCannotBeRenewedOrReassigned())return 1;
    std::cout<<"WeaponVisibility: 6 private-palette cases passed; no native visibility claim\n";}
