#include "Bc2ShotgunEquipment.h"
#include "Bc2AmmoSupply.h"
#include "Bc2ReloadPresentation.h"
#include "Bc2BeltAmmo.h"
#include "Bc2PhysicalPump.h"
#include "Bc2ShellReloadAudioProfile.h"
#include "fvr/interaction/MechanismAudio.h"
#include "Test.h"
#include <cstring>
#include <set>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
auto Pose(){return reload_insertion_detail::Identity();}
template<std::size_t N> void Text(std::array<char,N>& out,std::string_view text){out={};std::memcpy(out.data(),text.data(),text.size());}
bool Same(const math::Matrix4& a,const math::Matrix4& b){for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)if(!Near(a.values[r][c],b.values[r][c]))return false;return true;}
struct Evidence {
    static constexpr std::int64_t now=1000000000;
    Bc2ReloadInteractionSample sample;ReloadRoundLease native;SelectedMeshesSnapshot meshes;
    explicit Evidence(const ShotgunReloadEquipmentProfile& e){
        native.identity.owner={0x10000,0x20000,0x30000,0x40000,5,3,7};native.identity.firing={0x50000,0x60000,0x70000};
        native.identity.serverPlayer=0x80000;native.identity.serverSoldier=0x90000;native.identity.serverItem=0xa0000;
        native.cycle=12;native.sequence=20;native.observedNs=now;native.deadlineNs=now+100000000;
        native.loaded=2;native.reserve=8;native.capacity=8;native.nativeBindingVerified=native.allThreeHeld=true;
        sample.equipment=&e;sample.configurationPath=e.native->config->assetPath;sample.assetName=e.native->config->assetName;
        sample.meshPath=e.meshPath;sample.rigFingerprint=e.rigFingerprint;sample.selectedMeshIdentityVerified=true;
        auto& s=sample.insertion;s.identity={{(std::uint64_t(0x30000)<<32)|0x20000,5,17,7},{0x40000,17},{0xb0000,1},7};
        s.profile={e.shellInsertion->id,e.shellInsertion->revision};s.sequence=s.geometrySequence=50;s.nowNs=s.observedNs=now;s.deadlineNs=now+100000000;
        s.itemClaim={{100,s.identity.owner,InteractionHand::Left,HandClaimKind::AmmoObject,s.identity.item,{201,1},0},s.deadlineNs,s.sequence};
        s.weaponClaim={{101,s.identity.owner,InteractionHand::Right,HandClaimKind::GunHold,s.identity.weapon,{202,1},0},s.deadlineNs,s.sequence};
        s.focused=s.itemTracked=s.weaponTracked=s.held=s.eligible=true;
        sample.weaponWorldMeters=Pose();sample.rawLeftWristWorldMeters=reload_insertion_detail::InsertionHandWorld(*e.shellInsertion,0,Pose());
        sample.native=*ToBc2ReloadInteractionLease(*BindBc2ReloadOwners(s.identity.owner,s.identity.weapon,native,now),native,now);
        meshes.owner=native.identity.owner;meshes.sequence=20;meshes.observedNs=now;meshes.deadlineNs=now+200000000;
        meshes.stateCount=1;meshes.soleConfiguredArray=0xc0000;meshes.states[0].count=1;meshes.configurationPathVerified=true;
        Text(meshes.configurationPath,e.native->config->assetPath);Text(meshes.weaponName,e.native->config->assetName);
        Text(meshes.states[0].meshes[0].assetPath,e.meshPath);
    }
};
int ExactRowsAndNativeSeparation(){
    const auto rows=EnrolledShotgunEquipment();CHECK(rows.size()==25);std::set<std::uint64_t> ids;unsigned candidates=0;
    for(const auto& e:rows){
        CHECK(e.native&&e.shellInsertion&&e.pump&&e.physicalReviewed&&ValidateReloadInsertionProfile(*e.shellInsertion));
        CHECK(ids.insert(e.shellInsertion->id).second);
        CHECK(FindShotgunEquipment(e.native->config->assetPath,e.native->config->assetName,e.meshPath,e.rigFingerprint)==&e);
        CHECK(!FindShotgunEquipment({},e.native->config->assetName,e.meshPath,e.rigFingerprint));
        CHECK(!FindShotgunEquipment(e.native->config->assetPath,e.native->config->assetName,e.meshPath,e.rigFingerprint+1));
        auto copied=e;CHECK(!KnownShotgunEquipment(&copied));
        if(e.native->config->admission==ReloadDescriptorAdmission::Candidate)++candidates;
        Evidence f(e);CHECK(ShotgunEquipmentSelected(f.meshes,f.meshes.owner,&e,f.now));
        f.meshes.configurationPathVerified=false;CHECK(!ShotgunEquipmentSelected(f.meshes,f.meshes.owner,&e,f.now));
        f.meshes.configurationPathVerified=true;f.meshes.configurationPath[0]='?';CHECK(!ShotgunEquipmentSelected(f.meshes,f.meshes.owner,&e,f.now));
        Text(f.meshes.configurationPath,e.native->config->assetPath);f.meshes.states[0].count=2;f.meshes.states[0].meshes[1]=f.meshes.states[0].meshes[0];
        CHECK(!ShotgunEquipmentSelected(f.meshes,f.meshes.owner,&e,f.now));
    }
    CHECK(candidates==24);return 0;
}
int SupplyAndInteractionUseExactProfile(){
    const auto rows=EnrolledShotgunEquipment();
    for(const auto& e:rows){
        Evidence f(e);const auto source=ShotgunAmmoSupplySource(f.sample,f.native);CHECK(source);
        CHECK((source->identity.profile==HandInteractionKey{e.shellInsertion->id,e.shellInsertion->revision}));
        CHECK(!SpasAmmoSupplySource(f.sample,f.native));
        auto bad=f.sample;bad.configurationPath={};CHECK(!ShotgunAmmoSupplySource(bad,f.native));
        auto foreign=e;bad=f.sample;bad.equipment=&foreign;CHECK(!ShotgunAmmoSupplySource(bad,f.native));
        auto expired=f.native;expired.deadlineNs=f.now;CHECK(!ShotgunAmmoSupplySource(f.sample,expired));
        Bc2ReloadInteraction policy{true};const auto result=policy.Update(f.sample);CHECK(result.targets&&result.insertion.rawItem);
        CHECK(result.targets->equipment==&e);
        CHECK(Same(result.targets->weaponFromLeftWristMeters,Multiply(e.shellInsertion->itemFromHand,result.targets->weaponFromShellCenterMeters)));
        const auto& other=rows[(&e-rows.data()+1)%rows.size()];Evidence changed(other);
        CHECK(policy.Update(changed.sample).reason==Bc2ReloadInteractionReason::NativeCycleChanged);
        Bc2ReloadRequestBridge bridge{true};const auto& s=f.sample.insertion;
        const ReloadInsertionSeat seat{1,s.sequence,s.identity,s.profile,s.itemClaim.token,s.weaponClaim.token,ReloadOperation::InsertRound};
        const ManualReloadRequest request{1,{s.identity.owner.actor,s.identity.owner.actorGeneration,s.identity.weapon.id,s.identity.owner.equipGeneration,s.identity.owner.space},ReloadOperation::InsertRound,0,0};
        auto wrong=*result.targets;wrong.equipment=&other;
        CHECK(!bridge.Begin(f.sample,f.native,seat,request,wrong).submit);
        CHECK(bridge.Begin(f.sample,f.native,seat,request,*result.targets).submit);
        auto swapped=f.sample;swapped.equipment=&other;swapped.configurationPath=other.native->config->assetPath;
        swapped.assetName=other.native->config->assetName;swapped.meshPath=other.meshPath;swapped.rigFingerprint=other.rigFingerprint;
        CHECK(bridge.ObservePendingInput(swapped).phase==Bc2ReloadBridgePhase::Cancelled);
    }
    return 0;
}
int SupplyVisualRetainsEquipment(){
    const auto rows=EnrolledShotgunEquipment();
    for(const auto& e:rows){
        Evidence f(e);BeltAmmoTracking t;t.equipment=&e;t.owner=f.native.identity.owner;t.meshes=std::make_shared<const SelectedMeshesSnapshot>(f.meshes);
        t.reserve={f.native.identity,f.native.sequence,f.native.observedNs,f.native.deadlineNs,f.native.loaded,f.native.reserve,f.native.capacity,true,false,true};
        auto& v=t.visual;v.enabled=true;v.source=*ShotgunAmmoSupplySource(f.sample,t.reserve);
        const auto& s=f.sample.insertion;v.input={s.identity.owner,s.sequence,s.observedNs,s.deadlineNs,s.nowNs,true,{true,true},{true,false}};
        v.gun=s.weaponClaim;v.contact={{0,0,0},.15f};
        CHECK(ShotgunBeltAmmoFresh(t,f.now)&&ShotgunBeltAmmoRetained(t,t,f.now));CHECK(!SpasBeltAmmoFresh(t,f.now));
        auto changed=t;changed.equipment=&rows[(&e-rows.data()+1)%rows.size()];
        CHECK(!ShotgunBeltAmmoFresh(changed,f.now)&&!ShotgunBeltAmmoRetained(t,changed,f.now));
        auto old=t;old.reserve.deadlineNs=f.now;CHECK(!ShotgunBeltAmmoRetained(old,t,f.now));
    }
    return 0;
}
RigSnapshot SyntheticRig(const Bc2ShotgunShellGeometry& g){
    RigSnapshot rig;rig.names={"root","jntWpn_1","LeftHand"};rig.parents={-1,0,0};rig.weaponBone=1;
    std::uint32_t parent=1;if(g.parent!="jntWpn_1"){parent=std::uint32_t(rig.names.size());rig.names.emplace_back(g.parent);rig.parents.push_back(1);}
    const auto shell=std::uint32_t(rig.names.size());rig.names.emplace_back(g.bone);rig.parents.push_back(parent);
    for(const auto name:g.hiddenDescendants){rig.names.emplace_back(name);rig.parents.push_back(shell);}
    for(const char* name:{"Thumb","Index","Middle","Ring","Pinky"})for(unsigned j=1;j<=3;++j){
        rig.parents.push_back(j==1?2:std::int32_t(rig.names.size()-1));rig.names.push_back(std::string("LeftHand")+name+std::to_string(j));}
    rig.names.push_back("unrelated");rig.parents.push_back(0);
    const auto size=std::uint32_t(rig.names.size());rig.identity.count=size;rig.identity.evaluatedMatrices=0x10000;
    rig.inverseBind.assign(size,Pose());rig.evaluatedWorld.assign(size,Pose());rig.nativeEvaluated.resize(size);rig.nativeWorld.resize(size);
    for(auto& bytes:rig.nativeEvaluated)bytes.fill(std::byte{0x91});
    return rig;
}
int OwnCentersHandsAndDeclaredDescendants(){
    std::set<std::string_view> shellBones;
    for(const auto& e:EnrolledShotgunEquipment()){
        const auto g=ShotgunShellGeometry(&e);CHECK(g);shellBones.insert(g->bone);
        const auto bone=ShotgunShellBoneAtCenter(&e,Pose(),1);CHECK(bone&&Same(Multiply(g->centerInBone,*bone),Pose()));
        auto rig=SyntheticRig(*g);const auto binding=bc2_reload_detail::DerivePresentationBinding(rig,&e);CHECK(binding);
        CHECK(binding->hiddenDescendants.size()==g->hiddenDescendants.size());
        Evidence f(e);Bc2ReloadInteraction policy{true};const auto target=policy.Update(f.sample).targets;CHECK(target);
        Bc2ReloadPresentationObservation s;s.enabled=s.selectedMeshIdentityVerified=s.sectionOwnershipVerified=s.shellSectionVisible=true;
        s.equipment=&e;s.configurationPath=e.native->config->assetPath;s.assetName=e.native->config->assetName;s.meshPath=e.meshPath;
        s.identity=target->identity;s.inputSequence=target->inputSequence;s.nativeCycle=target->nativeCycle;s.nowNs=f.now;s.placedWeaponWorld=Pose();
        const auto before=rig.nativeEvaluated;const auto plan=BuildBc2ReloadPresentation(rig,*binding,s,*target);CHECK(plan.palette);
        CHECK(plan.palette->edits.size()==17+g->hiddenDescendants.size()&&rig.nativeEvaluated==before);
        CHECK(Same(plan.writes[2].transform,Multiply(g->handShape->wristFromJoint[0][0],plan.writes[1].transform)));
        for(const auto index:binding->hiddenDescendants){
            const auto it=std::find_if(plan.palette->edits.begin(),plan.palette->edits.end(),[&](const auto& edit){return edit.index==index;});CHECK(it!=plan.palette->edits.end());
            for(unsigned row=0;row<4;++row){for(unsigned col=0;col<12;++col)CHECK(it->after[row*16+col]==std::byte{0});
                for(unsigned col=12;col<16;++col)CHECK(it->after[row*16+col]==std::byte{0x91});}
        }
        rig.names.push_back("undeclared_shell_child");rig.parents.push_back(binding->shell);rig.inverseBind.push_back(Pose());rig.evaluatedWorld.push_back(Pose());
        CHECK(!bc2_reload_detail::DerivePresentationBinding(rig,&e));
    }
    CHECK(shellBones.size()==4);return 0;
}
int PumpConsumerAndAudioUseTheSelectedEquipment(){
    for(const auto& equipment:EnrolledShotgunEquipment()){
        const auto* audio=ShotgunEquipmentAudio(&equipment);CHECK(audio);
        auto foreign=equipment;CHECK(!ShotgunEquipmentAudio(&foreign));
        CHECK(ShotgunAudioRoleFor(audio,audio->pump)==ShotgunAudioRole::Pump);
        CHECK(ShellReloadSoundFor(audio,audio->insertion)==ShellReloadSound::InsertLoop);
        CHECK(ShellReloadSoundFor(audio,audio->finish)==ShellReloadSound::Finish);
        CHECK(ShellReloadSoundFor(audio,audio->pump)==ShellReloadSound::Unrelated);
        if(audio->pump!=LegacySpasAudio.pump)CHECK(ShotgunAudioRoleFor(audio,LegacySpasAudio.pump)==ShotgunAudioRole::Unrelated);
        struct Native {Bc2NativeCycleView view{};unsigned releases=0,cancels=0;} native;
        Bc2PhysicalPumpApi api{&native,
            [](void* p,const Bc2NativeCycleControl& control)noexcept{auto& n=*static_cast<Native*>(p);if(control.release)++n.releases;return true;},
            [](void* p,std::int64_t)noexcept->std::optional<Bc2NativeCycleView>{return static_cast<Native*>(p)->view;},
            [](void*,const WeaponCycleReady&)noexcept{return true;},
            [](void* p)noexcept{++static_cast<Native*>(p)->cancels;}};
        auto calibration=std::make_shared<const Bc2PumpCalibration>(*equipment.pump);Bc2PhysicalPump pump{calibration,api};
        Bc2PhysicalPumpSample sample;sample.nativeOwner={0x10000,0x20000,0x30000,0x40000,5,3,7};sample.item={0x40000,17};
        sample.asset=equipment.native->config->assetName;
        sample.input.owner={(std::uint64_t(0x30000)<<32)|0x20000,5,17,7};sample.input.focused=true;sample.input.tracked={true,true};
        sample.input.nowNs=1000000000;HandInteraction hands;std::uint64_t intent=0;
        native.view.native.owner=sample.nativeOwner;native.view.phase=Bc2NativeCyclePhase::Held;
        native.view.cycle=native.view.shot=1;native.view.blocksFire=true;
        const MechanismAudioIdentity identity{sample.input.owner.actor,5,sample.item.id,17,1,1};
        MechanismAudioSuppressionLedger<> suppression;MechanismAudioLedger<> ledger;std::vector<MechanismAudioEvent> cues;
        // The exact selected native callback establishes ownership before a
        // real physical consumer can emit rear/closure cues for the same debt.
        CHECK(ShotgunAudioRoleFor(audio,audio->pump)==ShotgunAudioRole::Pump&&suppression.Record(identity));
        const auto tick=[&](float travel,bool grip){
            auto& input=sample.input;++input.sequence;input.nowNs+=10000000;input.observedNs=input.nowNs;input.deadlineNs=input.nowNs+100000000;
            input.released[0]=!grip;sample.grip=grip;hands.Update(input);
            const HandContactProof proof{{1,17},input.sequence,input.deadlineNs,true};const auto gun=hands.Current(InteractionHand::Right);
            if(gun)hands.Renew(input,gun->token,proof);
            else hands.Acquire(input,{input.owner,InteractionHand::Right,HandClaimKind::GunHold,sample.item,proof,++intent,0});
            native.view.held=WeaponCycleLease{input.owner,sample.item,{calibration->profileId,17},1,1,input.sequence,input.observedNs,input.deadlineNs,true};
            auto contact=calibration->closedWrist;
            for(unsigned n=0;n<3;++n)contact.values[3][n]+=calibration->geometry.axis[n]*calibration->rearDirection*travel;
            sample.raw={sample.nativeOwner,{},calibration->rigFingerprint,input,contact,{},true};
            const auto result=pump.Tick(sample,hands,intent);
            if(result.audio&&suppression.Contains(result.audio->identity)&&ledger.Accept(*result.audio,input.nowNs)==MechanismAudioAdmission::Accepted)cues.push_back(*result.audio);
            return result;
        };
        tick(0,false);tick(0,false);tick(0,true);CHECK(tick(0,true).ownsHand);
        for(unsigned n=1;n<=10;++n)tick(calibration->geometry.stroke*n/10.f,true);
        for(unsigned n=0;n<5;++n)tick(calibration->geometry.stroke,true);
        for(int n=9;n>=0;--n)tick(calibration->geometry.stroke*n/10.f,true);
        for(unsigned n=0;n<5&&!native.releases;++n)tick(0,true);
        CHECK(native.releases==1&&native.cancels==0&&cues.size()==2);
        CHECK(cues[0].stage==MechanismAudioStage::RearTravel&&cues[1].stage==MechanismAudioStage::Closure);
        CHECK(cues[0].identity==identity&&cues[1].identity==identity);
        ShellReloadAudioSuppression<> shells;const ShellReloadAudioIdentity shell{identity.actor,5,identity.item,17,2};
        CHECK(!ShellReloadSuppressStart(shells,ShellReloadSoundFor(audio,audio->finish),shell,true));
        CHECK(ShellReloadSuppressStart(shells,ShellReloadSoundFor(audio,audio->insertion),shell,true));
        CHECK(ShellReloadSuppressStart(shells,ShellReloadSoundFor(audio,audio->finish),shell,true));
        CHECK(!ShellReloadSuppressStart(shells,ShellReloadSoundFor(audio,audio->pump),shell,true));
    }
    return 0;
}
}
int main(){if(ExactRowsAndNativeSeparation()||SupplyAndInteractionUseExactProfile()||SupplyVisualRetainsEquipment()||OwnCentersHandsAndDeclaredDescendants()||PumpConsumerAndAudioUseTheSelectedEquipment())return 1;
    std::puts("Shotgun equipment:25 exact rows, candidate separation, supply/bridge identity and own shell presentation passed.");return 0;}
