#include "Bc2ReticleDrawObservation.h"
#include "Bc2ReloadProducerBinding.h"
#include "Test.h"
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>
using namespace fvr;using namespace fvr::bc2;
namespace {
constexpr std::int64_t Now=1000000000;
struct Fixture {
    Bc2ReloadProducerBinding binding;ReloadProducerView key{1,2,3,4,0};ReloadPackedSource source{};
    std::shared_ptr<SelectedMeshesSnapshot> selected=std::make_shared<SelectedMeshesSnapshot>();
    ReloadDrawFrameEvidence frame{};ReloadReticleDrawCurrent draw{};
    std::vector<std::byte> raw=std::vector<std::byte>(6*64),packed=std::vector<std::byte>(6*48);
    Fixture(){
        selected->owner={101,102,103,104,105,106,107};selected->sequence=10;selected->observedNs=Now;selected->deadlineNs=Now+100000000;
        selected->weaponData=108;selected->inventory=109;selected->stateTypeInfo=110;selected->meshTypeInfo=111;selected->stateCount=1;
        selected->soleConfiguredArray=112;selected->states[0].array=112;selected->states[0].state=113;selected->states[0].count=1;
        auto& mesh=selected->states[0].meshes[0];mesh.address=114;mesh.typeInfo=115;mesh.namePointer=116;mesh.kind=SelectedMeshKind::Acog4x;
        std::memcpy(mesh.assetPath.data(),AcogReticleApertureProfile().mesh.data(),AcogReticleApertureProfile().mesh.size());
        source.owner={102,103,104,105,107};source.rigPose=117;source.rigFingerprint=0xa7f219a1426216abull;source.inputGeneration=11;
        source.observedNs=Now;source.deadlineNs=Now+100000000;source.boneCount=6;source.opticIndex=4;source.opticNamed=true;
        source.opticSelected=selected;source.physicalEquipmentGeneration=20;
        for(unsigned b=0;b<6;++b)for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){const float value=r==c?1.f:0.f;
            std::memcpy(raw.data()+b*64+r*16+c*4,&value,4);if(c<3)std::memcpy(packed.data()+b*48+c*16+r*4,&value,4);}
        binding.Enable(true);{Bc2ReloadProducerBinding::Scope scope(binding,key,true,Now);
            binding.ObservePacked(1,true,source,1000,Now+1,raw,packed);binding.ObservePacked(1,false,source,2000,Now+2,raw,packed);scope.Complete(true,Now+3);}
        const auto result=binding.Read(key,source.owner,Now+4);if(!result)throw 1;
        frame.world=1;frame.request=2;frame.view=3;frame.nativeFrame=4;frame.eye=0;frame.nowNs=Now+4;frame.producer=result->evidence;
        draw.world=1;draw.request=2;draw.view=3;draw.nativeFrame=4;draw.eye=0;draw.nowNs=Now+5;draw.physicalEquipmentGeneration=20;
        draw.actor=102;draw.weak=103;draw.weapon=104;draw.ownerGeneration=105;draw.space=107;
        draw.eyeWorldValid=true;draw.eyeCanonicalLh={0,1.1439183f,.25f};draw.selected=selected;
    }
    ReticleDrawObservation Run(bool geometry=true,bool complete=true,unsigned matches=1){return ObserveReticleAperture(frame,draw,geometry,complete,matches);}
};
int ActualProducerAndPerEye(){Fixture f;auto r=f.Run();CHECK(r.evaluated&&r.visibility==math::ApertureVisibility::Visible&&!r.hiddenCandidate);
    CHECK(r.missing==ReticleGpuInstanceAssociation);CHECK(!f.frame.producer.selectedMeshIdentityVerified);
    f.key.eye=f.frame.eye=f.draw.eye=1;f.draw.eyeCanonicalLh.x=-.064f;r=f.Run();CHECK(r.evaluated&&r.hiddenCandidate);
    f.draw.eyeCanonicalLh.x=0;CHECK(f.Run().visibility==math::ApertureVisibility::Visible);return 0;}
int OriginalLeaseCannotRenew(){for(unsigned variant=0;variant<2;++variant){Fixture f;auto newer=std::make_shared<SelectedMeshesSnapshot>(*f.selected);
    newer->observedNs=Now+100000000;newer->deadlineNs=Now+200000000;++newer->sequence;f.draw.selected=newer;f.draw.nowNs=Now+100000001;
    if(variant)f.frame.producer.deadlineNs=Now+200000000;auto r=f.Run();CHECK(!r.evaluated&&(r.missing&ReticleSelected));
    if(!variant)CHECK(r.missing&ReticleSourceLease);}return 0;}
int ScopeAndOwnerTransitions(){for(unsigned n=0;n<20;++n){Fixture f;auto current=std::make_shared<SelectedMeshesSnapshot>(*f.selected);f.draw.selected=current;
    switch(n){case 0:++f.draw.world;break;case 1:++f.draw.request;break;case 2:++f.draw.view;break;case 3:++f.draw.nativeFrame;break;
    case 4:++f.draw.eye;break;case 5:++current->owner.soldier;break;case 6:++current->owner.weak;break;case 7:++current->owner.weapon;break;
    case 8:++current->owner.actorGeneration;break;case 9:++current->owner.equipGeneration;break;case 10:++current->owner.space;break;
    case 11:++f.draw.physicalEquipmentGeneration;break;case 12:++current->states[0].meshes[0].address;break;
    case 13:++current->weaponData;break;case 14:++current->selectedSlot;break;
    case 15:++f.draw.actor;break;case 16:++f.draw.weak;break;case 17:++f.draw.weapon;break;
    case 18:++f.draw.ownerGeneration;break;case 19:++f.draw.space;break;}
    CHECK(!f.Run().evaluated);}return 0;}
int GeometryAndConstantEvidence(){Fixture f;CHECK(!f.Run(false).evaluated);CHECK(f.Run(false).missing&ReticleGeometry);
    CHECK(!f.Run(true,false).evaluated);CHECK(f.Run(true,false).missing&ReticleConstantsIncomplete);
    CHECK(!f.Run(true,true,0).evaluated);CHECK(!f.Run(true,true,2).evaluated);CHECK(f.Run(true,true,2).missing&ReticleConstantMatch);return 0;}
int MissingAndMalformed(){for(unsigned n=0;n<9;++n){Fixture f;
    switch(n){case 0:f.frame.producer.exactRequestAssociation=false;break;case 1:f.frame.producer.packedOpticValid=false;break;
    case 2:f.frame.producer.opticSelected.reset();break;case 3:f.draw.selected.reset();break;case 4:f.draw.eyeWorldValid=false;break;
    case 5:f.draw.eyeCanonicalLh.x=std::numeric_limits<float>::quiet_NaN();break;case 6:f.frame.producer.packedOptic={};break;
    case 7:f.frame.producer.rigFingerprint=0;break;case 8:f.draw.nowNs=Now-1;break;}
    CHECK(!f.Run().evaluated);}return 0;}
int MetadataNegatives(){for(unsigned n=0;n<7;++n){Fixture f;
    switch(n){case 0:f.selected->stateCount=2;break;case 1:f.selected->states[0].count=9;break;
    case 2:f.selected->states[0].meshes[1]=f.selected->states[0].meshes[0];f.selected->states[0].count=2;break;
    case 3:f.selected->states[0].meshes[0].assetPath[0]='x';break;case 4:f.selected->states[0].array=0;break;
    case 5:f.selected->deadlineNs=Now+250000001;break;case 6:f.selected->sequence=12;break;}
    CHECK(!f.Run().evaluated);}return 0;}
int PartialPreservesNative(){Fixture f;bool partial=false;for(unsigned n=0;n<2000;++n){f.draw.eyeCanonicalLh.x=n*.00002f;const auto r=f.Run();
    CHECK(r.evaluated);if(r.visibility==math::ApertureVisibility::Partial){partial=true;CHECK(!r.hiddenCandidate);CHECK(r.missing==ReticleGpuInstanceAssociation);}}
    CHECK(partial);return 0;}
int ExpiryAndIsolation(){Fixture f;auto current=std::make_shared<SelectedMeshesSnapshot>(*f.selected);++current->sequence;current->observedNs+=1;
    f.draw.selected=current;CHECK(f.Run().evaluated);f.draw.nowNs=f.frame.producer.deadlineNs;CHECK(!f.Run().evaluated);
    Fixture other;CHECK(other.Run().evaluated);f.frame.producer.packedOptic[0]=std::byte{0};CHECK(other.Run().evaluated);return 0;}
}
int main(){if(ActualProducerAndPerEye()||OriginalLeaseCannotRenew()||ScopeAndOwnerTransitions()||GeometryAndConstantEvidence()||
    MissingAndMalformed()||MetadataNegatives()||PartialPreservesNative()||ExpiryAndIsolation())return 1;
    std::cout<<"reticle draw observation: 8 groups passed (diagnostic only)\n";}
