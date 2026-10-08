#include "Bc2M95ShotPartCapture.h"
namespace fvr::bc2 {
bool M95ShotPartCapture::Observe(const reloadFlowRuntime::PumpPartNativeSample& before,
    const reloadFlowRuntime::PumpPartNativeSample& after,const RigSnapshot& rig,const SelectedMeshesSnapshot& selected,
    std::uint64_t sequence,std::int64_t observed,std::int64_t deadline,float units,std::int64_t now,unsigned fixturePhase){
    const auto fail=[&]{++rejected_;return false;};
    if(fixturePhase<1||fixturePhase>3||!sequence||observed<0||deadline<=now||(observed&&!interaction::weapon_cycle_detail::Window(observed,deadline,now))||
       before.identity!=after.identity||before.config!=after.config||!M95StockShotConfig(before.config)||
       before.observedNs>after.observedNs||before.completedNs>after.observedNs||
       before.completedNs<before.observedNs||after.completedNs<after.observedNs||after.completedNs>now||
       before.observedNs<observed||!interaction::weapon_cycle_detail::Window(before.observedNs,before.deadlineNs,now)||
       !interaction::weapon_cycle_detail::Window(after.observedNs,after.deadlineNs,now)||
       selected.owner!=before.identity.owner||selected.weaponData!=before.config.weaponData||
       selected.observedNs>before.observedNs||selected.observedNs<=0||selected.deadlineNs<=now||
       selected.deadlineNs-selected.observedNs>250000000||rig.identity.soldier!=selected.owner.soldier||
       rig.identity.weak!=selected.owner.weak||!std::isfinite(units)||units<=0||units>1000)return fail();
    if(!selected.configurationPathVerified||!ReloadDescriptorText(selected.configurationPath,"Objects/Weapons/Handheld/BU_sni_M95/SP_sni_M95")||
       selected.stateCount!=1||selected.states[0].count!=1||!selected.soleConfiguredArray||
       !ReloadDescriptorText(selected.states[0].meshes[0].assetPath,"Objects/Weapons/Handheld/BU_sni_M95/BU_sni_M95_Mesh"))return fail();
    // Capture the actual rig fingerprint even if it differs from the accepted
    // rendering rig. This is observational topology, never part admission.
    const auto named=[&](std::string_view name)->std::optional<unsigned>{
        const auto at=std::find(rig.names.begin(),rig.names.end(),name);
        if(at==rig.names.end()||std::find(at+1,rig.names.end(),name)!=rig.names.end())return {};
        return unsigned(at-rig.names.begin());};
    const auto part=named("jntWpn_3"),weapon=named("jntWpn_1"),wrist=named("RightHand");
    if(rig.names.empty()||rig.names.size()>1024||rig.parents.size()!=rig.names.size()||rig.inverseBind.size()!=rig.names.size()||
       rig.evaluatedWorld.size()!=rig.names.size()||!part||!weapon||!wrist||rig.weaponBone!=*weapon||rig.right.wrist!=*wrist||
       rig.parents[*part]!=int(*weapon)||std::find(rig.parents.begin(),rig.parents.end(),int(*part))!=rig.parents.end())return fail();
    for(unsigned n=0;n<rig.names.size();++n){int at=int(n);unsigned hops=0;while(at!=-1){if(at<0||unsigned(at)>=rig.names.size()||++hops>rig.names.size())return fail();at=rig.parents[at];}}
    const auto inverse=interaction::InverseRigid(rig.evaluatedWorld[*weapon]);
    const auto inverseWrist=interaction::InverseRigid(rig.evaluatedWorld[*wrist]);if(!inverse||!inverseWrist)return fail();
    const auto fingerprint=SightRigFingerprint(rig.names,rig.parents,rig.inverseBind);if(!fingerprint)return fail();
    M95ShotPartCaptureRow out;out.before=before;out.after=after;out.rig=rig.identity;out.rigFingerprint=fingerprint;out.fixturePhase=fixturePhase;
    out.inputSequence=sequence;out.inputObservedNs=observed;out.inputDeadlineNs=deadline;
    out.selectedObservedNs=selected.observedNs;out.selectedDeadlineNs=selected.deadlineNs;
    out.partFromWeapon=interaction::Multiply(rig.evaluatedWorld[*part],*inverse);
    out.wristFromWeapon=interaction::Multiply(rig.evaluatedWorld[*wrist],*inverse);
    for(unsigned n=0;n<3;++n){out.partFromWeapon.values[3][n]/=units;out.wristFromWeapon.values[3][n]/=units;}
    if(!interaction::reload_insertion_detail::Rigid(out.partFromWeapon)||!interaction::reload_insertion_detail::Rigid(out.wristFromWeapon))return fail();
    unsigned finger=0;
    for(auto digit:{"Thumb","Index","Middle","Ring","Pinky"})for(unsigned joint=1;joint<=3;++joint){
        const auto name=std::string("RightHand")+digit+std::to_string(joint);const auto bone=named(name);if(!bone)return fail();
        const auto parentName=joint==1?std::string("RightHand"):std::string("RightHand")+digit+std::to_string(joint-1);
        const auto parent=named(parentName);if(!parent||rig.parents[*bone]!=int(*parent))return fail();
        auto pose=interaction::Multiply(rig.evaluatedWorld[*bone],*inverseWrist);for(unsigned k=0;k<3;++k)pose.values[3][k]/=units;
        if(!interaction::reload_insertion_detail::Rigid(pose))return fail();out.fingersFromWrist[finger++]=pose;
    }
    out.stableStateBracket=before.callbackRevision==after.callbackRevision;
    for(unsigned n=0;n<3;++n){const auto& a=before.branches[n];const auto& b=after.branches[n];
        if(a.owner!=before.identity.owner||b.owner!=after.identity.owner||a.firing!=before.identity.firing[n]||b.firing!=after.identity.firing[n]||a.branch!=n||b.branch!=n)return fail();
        if(a.current!=b.current||a.previous!=b.previous||a.next!=b.next||a.loaded!=b.loaded||a.reserve!=b.reserve)out.stableStateBracket=false;
    }
    // Preserve the last32 prefire rows and the first320 shot/tail rows.
    // Later neutral streaming cannot overwrite the fired interval.
    const auto latest=count_?&rows_[fired_?count_-1:(prefireNext_+PrefireCapacity-1)%PrefireCapacity]:nullptr;
    if(latest&&before.observedNs-latest->before.observedNs<20000000)return false;
    if(fixturePhase==1){if(fired_)return fail();rows_[prefireNext_]=out;prefireNext_=(prefireNext_+1)%PrefireCapacity;count_=std::min(count_+1,PrefireCapacity);return true;}
    if(!fired_){
        if(count_==PrefireCapacity)std::rotate(rows_.begin(),rows_.begin()+prefireNext_,rows_.begin()+PrefireCapacity);
        fired_=true;
    }
    if(postfireCount_==AfterFireCapacity||count_==Capacity){++dropped_;return false;}rows_[count_++]=out;++postfireCount_;return true;
}
void M95ShotPartCapture::Report(std::ostream& out)const {
    const auto matrix=[&](const math::Matrix4& m){out<<'[';for(unsigned r=0;r<4;++r){if(r)out<<',';out<<'[';for(unsigned k=0;k<4;++k){if(k)out<<',';out<<m.values[r][k];}out<<']';}out<<']';};
    const auto native=[&](const reloadFlowRuntime::PumpPartNativeSample& s){const auto& o=s.identity.owner;
        out<<"{\"observed_ns\":"<<s.observedNs<<",\"completed_ns\":"<<s.completedNs<<",\"deadline_ns\":"<<s.deadlineNs
           <<",\"callback_revision\":"<<s.callbackRevision<<",\"hold_phase\":"<<s.holdPhase
           <<",\"player\":"<<o.player<<",\"soldier\":"<<o.soldier<<",\"weak\":"<<o.weak<<",\"weapon\":"<<o.weapon
           <<",\"actor_generation\":"<<o.actorGeneration<<",\"equip_generation\":"<<o.equipGeneration<<",\"space\":"<<o.space
           <<",\"weapon_data\":"<<s.config.weaponData<<",\"firing_data\":"<<s.config.firingData
           <<",\"server_player\":"<<s.identity.serverPlayer<<",\"server_soldier\":"<<s.identity.serverSoldier<<",\"server_item\":"<<s.identity.serverItem<<",\"branches\":[";
        for(unsigned n=0;n<3;++n){if(n)out<<',';const auto& b=s.branches[n];out<<"{\"firing\":"<<b.firing<<",\"current\":"<<b.current
            <<",\"previous\":"<<b.previous<<",\"next\":"<<b.next<<",\"timer\":"<<b.timer<<",\"loaded\":"<<b.loaded<<",\"reserve\":"<<b.reserve<<'}';}out<<"]}";};
    out<<"\"m95_shot_part_capture\":{\"schema_version\":1,\"runtime_authority\":false,\"submitted_mesh_proven\":false,\"rows\":"<<count_
       <<",\"prefire_capacity\":"<<PrefireCapacity<<",\"postfire_capacity\":"<<AfterFireCapacity<<",\"fired_window\":"<<(fired_?"true":"false")<<",\"rejected\":"<<rejected_<<",\"dropped\":"<<dropped_<<",\"samples\":[";
    for(unsigned n=0;n<count_;++n){if(n)out<<',';const auto& r=rows_[n];out<<"{\"before\":";native(r.before);out<<",\"after\":";native(r.after);
        out<<",\"fixture_phase\":"<<r.fixturePhase<<",\"stable_state_bracket\":"<<(r.stableStateBracket?"true":"false")<<",\"input_sequence\":"<<r.inputSequence
           <<",\"input_observed_ns\":"<<r.inputObservedNs<<",\"input_deadline_ns\":"<<r.inputDeadlineNs
           <<",\"selected_observed_ns\":"<<r.selectedObservedNs<<",\"selected_deadline_ns\":"<<r.selectedDeadlineNs
           <<",\"rig_fingerprint\":"<<r.rigFingerprint<<",\"rig_animation\":"<<r.rig.animation<<",\"rig_skeleton\":"<<r.rig.skeleton
           <<",\"rig_pose\":"<<r.rig.pose<<",\"part_from_weapon_m\":";matrix(r.partFromWeapon);out<<",\"wrist_from_weapon_m\":";matrix(r.wristFromWeapon);out<<",\"right_fingers_from_wrist_m\":[";
        for(unsigned k=0;k<r.fingersFromWrist.size();++k){if(k)out<<',';matrix(r.fingersFromWrist[k]);}out<<"]}";}
    out<<"]}";
}
}
