#include "Test.h"
#include "Bc2AuthoredAim.h"
#include "Bc2AuthoredSupport.h"
#include "../profiles/weapons56-aim/Aim.generated.h"
#include "../profiles/weapons43/CombinedSupport.h"
#include "fvr/interaction/AimFrame.h"
#include "fvr/interaction/SupportGrip.h"
#include <iostream>
#include <limits>
#include <set>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
math::Matrix4 At(float x=0,float y=0,float z=0){math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;m.values[3]={x,y,z,1};return m;}
template<std::size_t N> void Text(std::array<char,N>& out,std::string_view value){out={};std::copy(value.begin(),value.end(),out.begin());}
bool Close(const math::Matrix4& a,const math::Matrix4& b){for(unsigned i=0;i<16;++i)if(!Near(a.values[i/4][i%4],b.values[i/4][i%4],.0001f))return false;return true;}
struct Fixture {
    static constexpr std::int64_t now=1000000000;
    AuthoredAimProfile profile=generated::AuthoredAims[0];
    std::shared_ptr<SelectedMeshesSnapshot> snapshot=std::make_shared<SelectedMeshesSnapshot>();
    WeaponEquipmentIdentity equipment{};AuthoredAimOwner owner{10,11,12,13,14,20,now+90000000};
    Fixture(){Reset(profile);}
    void Reset(const AuthoredAimProfile& p){profile=p;equipment={};equipment.weapon=15;equipment.data=16;Text(equipment.asset,p.assetName);
        snapshot=std::make_shared<SelectedMeshesSnapshot>();auto& s=*snapshot;
        s.owner={9,10,11,15,12,18,14};s.sequence=19;s.observedNs=now-10000000;s.deadlineNs=now+200000000;
        s.weaponData=16;s.stateTypeInfo=1;s.meshTypeInfo=2;s.inventory=3;s.selectedSlot=4;Text(s.weaponName,p.assetName);
        s.stateCount=1;s.soleConfiguredArray=5;s.states[0].array=5;s.states[0].state=6;s.states[0].count=1;
        auto& m=s.states[0].meshes[0];m.address=7;m.typeInfo=2;m.namePointer=8;Text(m.assetPath,p.meshPath);
        Text(s.configurationPath,p.configurationPath);s.configurationPathVerified=true;s.configurationPathPointer=21;
    }
    AuthoredAimResult Bind(){return BindAuthoredAim({&profile,1},snapshot,equipment,owner,profile.rigFingerprint,now);}
    bool Current(const AuthoredAimBinding& b,std::int64_t time=now){return AuthoredAimCurrent(b,snapshot.get(),equipment,owner,profile.rigFingerprint,time);}
};
int ExactBatchAndConfiguration(){
    CHECK(generated::AuthoredAims.size()==193);std::set<std::string_view> names,meshes;bool qju=false;
    Fixture f;
    for(const auto& p:generated::AuthoredAims){f.Reset(p);names.insert(p.assetName);meshes.insert(p.meshPath);qju|=p.assetName=="QJU88_sp";
        const auto bound=BindAuthoredAim(generated::AuthoredAims,f.snapshot,f.equipment,f.owner,p.rigFingerprint,Fixture::now);
        CHECK(bound.status==AuthoredAimStatus::Bound&&bound.binding&&bound.binding->profile==&p);
        CHECK(f.Current(*bound.binding));
        Text(f.snapshot->configurationPath,"Objects/Weapons/UnreviewedConfiguration");CHECK(f.Bind().status==AuthoredAimStatus::NoProfile);
    }
    CHECK(qju&&names.size()==80&&meshes.size()==44);
    f.Reset(generated::AuthoredAims[0]);f.snapshot->configurationPathVerified=false;CHECK(!f.Bind().binding);
    f.snapshot->configurationPathVerified=true;f.snapshot->configurationPathPointer=0;CHECK(!f.Bind().binding);
    f.Reset(generated::AuthoredAims[0]);Text(f.snapshot->states[0].meshes[0].assetPath,"Objects/Weapons/OtherMesh");CHECK(!f.Bind().binding);
    f.Reset(generated::AuthoredAims[0]);f.snapshot->states[0].count=2;f.snapshot->states[0].meshes[1]=f.snapshot->states[0].meshes[0];CHECK(!f.Bind().binding);
    f.Reset(generated::AuthoredAims[0]);const std::array duplicates{f.profile,f.profile};
    CHECK(BindAuthoredAim(duplicates,f.snapshot,f.equipment,f.owner,f.profile.rigFingerprint,Fixture::now).status==AuthoredAimStatus::AmbiguousConfiguration);
    Text(f.equipment.asset,"UnreviewedGun");CHECK(f.Bind().status==AuthoredAimStatus::NoProfile);
    f.Reset(generated::AuthoredAims[0]);Text(f.snapshot->configurationPath,"Objects/Weapons/UnreviewedConfiguration");
    f.snapshot->deadlineNs=Fixture::now;CHECK(f.Bind().status==AuthoredAimStatus::Expired);
    f.snapshot->deadlineNs=Fixture::now+200000000;f.snapshot->owner.weapon++;CHECK(f.Bind().status==AuthoredAimStatus::IdentityMismatch);
    return 0;
}
int OriginalLeasesAndIdentity(){
    Fixture f;const auto b=*f.Bind().binding;CHECK(f.Current(b)); // stable null persistence accepted
    f.snapshot=std::make_shared<SelectedMeshesSnapshot>(*f.snapshot);++f.snapshot->sequence;
    f.snapshot->observedNs=Fixture::now;f.snapshot->deadlineNs=Fixture::now+250000000;CHECK(f.Current(b));
    f.owner.inputDeadlineNs=Fixture::now+300000000;CHECK(!f.Current(b,b.owner.inputDeadlineNs));
    auto longer=b;longer.owner.inputDeadlineNs=f.owner.inputDeadlineNs;
    CHECK(!f.Current(longer,b.selected->deadlineNs)); // refreshed mesh cannot extend source lease
    f.owner=b.owner;CHECK(f.Current(b));
    const auto s=*f.snapshot;
    ++f.snapshot->configurationPathPointer;CHECK(!f.Current(b));*f.snapshot=s;
    ++f.snapshot->owner.equipGeneration;CHECK(!f.Current(b));*f.snapshot=s;
    ++f.snapshot->states[0].meshes[0].address;CHECK(!f.Current(b));*f.snapshot=s;
    ++f.snapshot->selectedSlot;CHECK(!f.Current(b));*f.snapshot=s;
    ++f.equipment.persistence;CHECK(!f.Current(b));f.equipment=b.equipment;
    ++f.equipment.data;CHECK(!f.Current(b));f.equipment=b.equipment;
    ++f.owner.equipmentGeneration;CHECK(!f.Current(b));f.owner=b.owner;
    ++f.owner.space;CHECK(!f.Current(b));f.owner=b.owner;
    f.snapshot->sequence=f.owner.inputSequence+1;CHECK(f.Current(b));
    CHECK(f.Bind().status==AuthoredAimStatus::Expired); // confirmation is not new pose authority
    *f.snapshot=s;
    CHECK(!AuthoredAimCurrent(b,f.snapshot.get(),f.equipment,f.owner,f.profile.rigFingerprint+1,Fixture::now));
    f.snapshot->owner.weapon++;CHECK(f.Bind().status==AuthoredAimStatus::IdentityMismatch);
    return 0;
}
int ConfigurationRefreshBeforeGuardPublication(){
    constexpr std::int64_t ms=1000000;
    unsigned checked=0;bool qju88=false,t194=false;
    for(const auto& profile:generated::AuthoredAims){
        Fixture f;f.Reset(profile);f.owner.inputSequence=2549;
        f.snapshot->sequence=2540;
        const auto bound=f.Bind();CHECK(bound.binding);
        const auto& aim=*bound.binding;
        const auto supportBinding=BindAuthoredSupportReference(generated::AuthoredSupports,
            f.snapshot,f.equipment,f.owner,profile.rigFingerprint,Fixture::now);
        if(profile.assetName=="QJU88_sp"||profile.assetName=="T194_sp"){
            CHECK(supportBinding&&AuthoredSupportContact(supportBinding));
            qju88|=profile.assetName=="QJU88_sp";t194|=profile.assetName=="T194_sp";
        }
        InputFrame input{};input.generation=2549;input.spaceGeneration=f.owner.space;
        input.predictedNs=Fixture::now;input.focused=input.headValid=true;
        for(auto& hand:input.hands){hand.gripTracked=hand.aimTracked=true;hand.active=Components;}
        input.hands[0].grip.position.z=-.4f;
        const SupportGripOwner supportOwner{1,2,3};SupportGrip policy;
        const auto contact=[&](const SelectedMeshesSnapshot* current,std::int64_t now){
            // ReadWeaponFrame admits the shared contact only if every bound
            // authored source is current, including the independent aim axes.
            const bool valid=AuthoredAimCurrent(aim,current,f.equipment,f.owner,profile.rigFingerprint,now)&&
                (!supportBinding||AuthoredSupportCurrent(*supportBinding,current,f.equipment,f.owner,now));
            return SupportGripContact{valid,.135149f,{}};
        };
        CHECK(!policy.Update(supportOwner,input,contact(f.snapshot.get(),Fixture::now)).holding);
        ++input.generation;++f.owner.inputSequence;input.predictedNs+=ms;input.hands[0].squeeze=1;
        const auto grabbed=policy.Update(supportOwner,input,contact(f.snapshot.get(),Fixture::now+ms));
        CHECK(grabbed.engaged&&grabbed.holding);

        // Model the source-supported ordering for event232: configuration2551 is published at Gather's start,
        // while the live rendered pose2549 and controller guard2550 remain.
        auto refreshed=*f.snapshot;refreshed.sequence=2551;
        refreshed.observedNs=Fixture::now+10*ms;refreshed.deadlineNs=Fixture::now+210*ms;
        ++input.generation;input.predictedNs=refreshed.observedNs;
        CHECK(refreshed.sequence>f.owner.inputSequence);
        const auto held=policy.Update(supportOwner,input,contact(&refreshed,input.predictedNs));
        CHECK(held.holding&&!held.released&&!held.engaged&&held.token==grabbed.token);
        // Event233 already recovered source on the same controller packet.
        f.owner.inputSequence=input.generation;
        const auto repeated=policy.Update(supportOwner,input,contact(&refreshed,input.predictedNs+ms));
        CHECK(repeated.holding&&!repeated.released&&repeated.token==grabbed.token);
        CHECK(aim.selected==f.snapshot&&aim.owner.inputSequence==2549);
        CHECK(aim.owner.inputDeadlineNs==Fixture::now+90*ms);
        CHECK(!contact(nullptr,input.predictedNs).valid);
        auto replaced=refreshed;++replaced.owner.equipGeneration;CHECK(!contact(&replaced,input.predictedNs).valid);
        replaced=refreshed;++replaced.states[0].meshes[0].address;CHECK(!contact(&replaced,input.predictedNs).valid);
        replaced=refreshed;++replaced.configurationPathPointer;CHECK(!contact(&replaced,input.predictedNs).valid);
        replaced=refreshed;replaced.observedNs=input.predictedNs+ms;CHECK(!contact(&replaced,input.predictedNs).valid);
        replaced=refreshed;replaced.sequence=f.snapshot->sequence-1;CHECK(!contact(&replaced,input.predictedNs).valid);
        auto olderGuard=f.owner;olderGuard.inputSequence=aim.owner.inputSequence-1;
        CHECK(!AuthoredAimCurrent(aim,&refreshed,f.equipment,olderGuard,profile.rigFingerprint,input.predictedNs));
        CHECK(!contact(&refreshed,aim.owner.inputDeadlineNs).valid);
        auto longer=aim;longer.owner.inputDeadlineNs=f.owner.inputDeadlineNs=Fixture::now+300*ms;
        CHECK(!AuthoredAimCurrent(longer,&refreshed,f.equipment,f.owner,profile.rigFingerprint,aim.selected->deadlineNs));
        ++checked;
    }
    CHECK(checked==193&&qju88&&t194);
    return 0;
}
int InvalidEvidenceAndAxes(){
    Fixture f;f.profile.authoredAxisEvidence={};CHECK(f.Bind().status==AuthoredAimStatus::InvalidProfile);
    f.Reset(generated::AuthoredAims[0]);f.profile.meshSha256="invalid";CHECK(!f.Bind().binding);
    f.Reset(generated::AuthoredAims[0]);f.profile.modelForward={};CHECK(!f.Bind().binding);
    f.Reset(generated::AuthoredAims[0]);f.profile.modelUp=f.profile.modelForward;CHECK(!f.Bind().binding);
    f.Reset(generated::AuthoredAims[0]);f.profile.modelForward.x=std::numeric_limits<float>::quiet_NaN();CHECK(!f.Bind().binding);
    f.Reset(generated::AuthoredAims[0]);auto aim=At();aim.values[0][0]=-1;CHECK(!AuthoredWeaponAimFrame(*f.Bind().binding,aim));
    CHECK(!AuthoredAimBinding::nativeFireCapability&&!AuthoredAimBinding::muzzleCapability&&
          !AuthoredAimBinding::gripCapability&&!AuthoredAimBinding::supportCapability&&!AuthoredAimBinding::headsetAccepted);
    return 0;
}
int AbsoluteAlignmentKeepsAttachmentAndNativeChildren(){
    Fixture f;const auto binding=*f.Bind().binding;
    InputFrame input{};input.generation=input.spaceGeneration=1;input.predictedNs=Fixture::now;
    input.focused=input.headValid=true;
    for(auto& h:input.hands){h.gripTracked=h.aimTracked=true;h.active=Components;}
    input.hands[0].grip.position={-.2f,-.3f,-.4f};input.hands[1].grip.position={.2f,-.3f,-.4f};
    input.hands[1].aim.orientation={0,std::sin(.2f),0,std::cos(.2f)};
    const auto beforeInput=input;
    const auto body=At(3,1.5f,8),left=At(2.8f,1.2f,8.4f),right=At(3.2f,1.2f,8.4f);
    auto nativeWeapon=At(3.7f,1.4f,9.5f); // deliberately different initial stock orientation
    const std::array<ArmAnchor,2> arms{ArmAnchor{{2.8f,1.4f,8},{-.1f,-.3f,.1f}},ArmAnchor{{3.2f,1.4f,8},{.1f,-.3f,.1f}}};
    TrackedRig rig;TrackedRigOwner owner{1,2,3,4,5};
    const auto calibrated=rig.Update(owner,input,body,left,right,nativeWeapon,arms);CHECK(calibrated);
    const auto ray=TrackedAimFrame(input,body);CHECK(ray);
    const auto orientation=AuthoredWeaponAimFrame(binding,*ray);CHECK(orientation);
    const auto aligned=rig.Update(owner,input,body,left,right,nativeWeapon,arms,{},{},orientation);CHECK(aligned);
    const auto inverseBefore=InverseRigid(calibrated->weapon),inverseAfter=InverseRigid(aligned->weapon);CHECK(inverseBefore&&inverseAfter);
    CHECK(Close(Multiply(calibrated->right,*inverseBefore),Multiply(aligned->right,*inverseAfter)));
    for(unsigned c=0;c<3;++c)CHECK(Near(calibrated->right.values[3][c],aligned->right.values[3][c],.0001f));
    const auto forward=binding.profile->modelForward;
    for(unsigned c=0;c<3;++c){const float actual=forward.x*aligned->weapon.values[0][c]+forward.y*aligned->weapon.values[1][c]+forward.z*aligned->weapon.values[2][c];
        CHECK(Near(actual,ray->values[2][c],.0001f));}
    CHECK(input.generation==beforeInput.generation&&input.hands[1].aim.orientation.y==beforeInput.hands[1].aim.orientation.y);
    const std::array<std::int32_t,3> parents{-1,0,1};
    const auto nativeChild=Multiply(At(.03f,-.02f,.1f),nativeWeapon);
    const std::array native{body,nativeWeapon,nativeChild};
    const auto writes=RetargetRigSubtree(parents,native,1,aligned->weapon);CHECK(writes&&writes->size()==2);
    auto after=native;for(const auto& w:*writes)after[w.index]=w.transform;
    CHECK(Close(Multiply(after[2],*InverseAnimatedTransform(after[1])),Multiply(native[2],*InverseAnimatedTransform(native[1]))));
    // The same orientation composes with an existing authored right-hand socket.
    const AuthoredGripAttachment authored{At(.04f,-.08f,-.23f)};
    const auto withGrip=rig.Update(owner,input,body,left,right,nativeWeapon,arms,{},{},orientation,authored);
    CHECK(withGrip&&withGrip->weaponAttachmentAuthored&&Close(Multiply(authored.rightInWeapon,withGrip->weapon),withGrip->right));
    // Existing support policy corrects the XR input before either aim consumer.
    SupportGrip support;const SupportGripOwner supportOwner{1,2,3};const SupportGripContact contact{true,0,{}};
    input.hands[0].grip.position={.2f,-.3f,-.8f};support.Update(supportOwner,input,contact);
    ++input.generation;input.predictedNs+=10000000;input.hands[0].squeeze=.9f;
    CHECK(support.Update(supportOwner,input,contact).holding);
    ++input.generation;input.predictedNs+=10000000;input.hands[0].grip.position.y+=.08f;
    const auto corrected=support.Update(supportOwner,input,contact);CHECK(corrected.holding&&corrected.correctionRadians>.1f);
    const auto correctedRay=TrackedAimFrame(corrected.input,body);CHECK(correctedRay);
    const auto correctedOrientation=AuthoredWeaponAimFrame(binding,*correctedRay);CHECK(correctedOrientation);
    const auto supported=rig.Update(owner,corrected.input,body,left,right,nativeWeapon,arms,{},{},correctedOrientation,authored);CHECK(supported);
    CHECK(Close(Multiply(authored.rightInWeapon,supported->weapon),supported->right));
    for(unsigned c=0;c<3;++c){const float actual=forward.x*supported->weapon.values[0][c]+forward.y*supported->weapon.values[1][c]+forward.z*supported->weapon.values[2][c];
        CHECK(Near(actual,correctedRay->values[2][c],.0001f));}
    return 0;
}
}
int main(){
    if(ExactBatchAndConfiguration()||OriginalLeasesAndIdentity()||ConfigurationRefreshBeforeGuardPublication()||
       InvalidEvidenceAndAxes()||AbsoluteAlignmentKeepsAttachmentAndNativeChildren())return 1;
    std::cout<<"Authored aim: exact reviewed enrollment, lease/configuration refusal, independent orientation and preserved attachments passed\n";
    return 0;
}
