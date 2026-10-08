#include "Bc2PumpCapture.h"
namespace fvr::bc2 {
bool PumpPartCapture::Observe(const reloadFlowRuntime::PumpPartNativeSample& before,
    const reloadFlowRuntime::PumpPartNativeSample& after,const RigSnapshot& rig,const SelectedMeshesSnapshot& selected,
    std::uint64_t sequence,std::int64_t observed,std::int64_t deadline,float units,std::int64_t now){
    const auto fail=[&]{++rejected_;return false;};
    if(!sequence||observed<0||deadline<=now||(observed&&!interaction::weapon_cycle_detail::Window(observed,deadline,now))||
       before.identity!=after.identity||before.config!=after.config||!IsDiagnosticSpasConfig(before.config)||
       before.observedNs>after.observedNs||before.completedNs>after.observedNs||
       before.completedNs<before.observedNs||after.completedNs<after.observedNs||after.completedNs>now||
       before.observedNs<observed||!interaction::weapon_cycle_detail::Window(before.observedNs,before.deadlineNs,now)||
       !interaction::weapon_cycle_detail::Window(after.observedNs,after.deadlineNs,now)||
       selected.owner!=before.identity.owner||selected.weaponData!=before.config.weaponData||
       selected.observedNs>before.observedNs||selected.observedNs<=0||selected.deadlineNs<=now||
       selected.deadlineNs-selected.observedNs>250000000||rig.identity.soldier!=selected.owner.soldier||
       rig.identity.weak!=selected.owner.weak||!std::isfinite(units)||units<=0||units>1000)return fail();
    const auto mesh=FindSelectedMesh(selected,selected.owner,SelectedMeshKind::Spas12,now);
    if(!mesh)return fail();
    // Capture the actual rig fingerprint even if it differs from the accepted
    // rendering rig. This is observational topology, never part admission.
    const auto binding=bc2_pump_detail::DerivePart(rig);
    if(!binding||rig.evaluatedWorld.size()<=std::max(binding->part,rig.left.wrist))return fail();
    const auto inverse=interaction::InverseRigid(rig.evaluatedWorld[binding->weapon]);if(!inverse)return fail();
    PumpPartCaptureRow out;out.before=before;out.after=after;out.rig=rig.identity;out.rigFingerprint=binding->fingerprint;
    out.inputSequence=sequence;out.inputObservedNs=observed;out.inputDeadlineNs=deadline;
    out.selectedObservedNs=selected.observedNs;out.selectedDeadlineNs=selected.deadlineNs;
    out.partFromWeapon=interaction::Multiply(rig.evaluatedWorld[binding->part],*inverse);
    out.wristFromWeapon=interaction::Multiply(rig.evaluatedWorld[rig.left.wrist],*inverse);
    for(unsigned n=0;n<3;++n){out.partFromWeapon.values[3][n]/=units;out.wristFromWeapon.values[3][n]/=units;}
    if(!interaction::reload_insertion_detail::Rigid(out.partFromWeapon)||!interaction::reload_insertion_detail::Rigid(out.wristFromWeapon))return fail();
    out.stableStateBracket=true;
    for(unsigned n=0;n<3;++n){const auto& a=before.branches[n];const auto& b=after.branches[n];
        if(a.owner!=before.identity.owner||b.owner!=after.identity.owner||a.firing!=before.identity.firing[n]||b.firing!=after.identity.firing[n]||a.branch!=n||b.branch!=n)return fail();
        if(a.current!=b.current||a.previous!=b.previous||a.next!=b.next||a.loaded!=b.loaded||a.reserve!=b.reserve)out.stableStateBracket=false;
    }
    // Sampling cadence is diagnostic, not a replacement for original leases.
    if(count_&&before.observedNs-rows_[count_-1].before.observedNs<15000000)return false;
    if(count_==Capacity){++dropped_;return false;}rows_[count_++]=out;return true;
}
void PumpPartCapture::Report(std::ostream& out)const {
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
    out<<"\"pump_part_capture\":{\"schema_version\":1,\"runtime_authority\":false,\"submitted_mesh_proven\":false,\"rows\":"<<count_
       <<",\"rejected\":"<<rejected_<<",\"dropped\":"<<dropped_<<",\"samples\":[";
    for(unsigned n=0;n<count_;++n){if(n)out<<',';const auto& r=rows_[n];out<<"{\"before\":";native(r.before);out<<",\"after\":";native(r.after);
        out<<",\"stable_state_bracket\":"<<(r.stableStateBracket?"true":"false")<<",\"input_sequence\":"<<r.inputSequence
           <<",\"input_observed_ns\":"<<r.inputObservedNs<<",\"input_deadline_ns\":"<<r.inputDeadlineNs
           <<",\"selected_observed_ns\":"<<r.selectedObservedNs<<",\"selected_deadline_ns\":"<<r.selectedDeadlineNs
           <<",\"rig_fingerprint\":"<<r.rigFingerprint<<",\"rig_animation\":"<<r.rig.animation<<",\"rig_skeleton\":"<<r.rig.skeleton
           <<",\"rig_pose\":"<<r.rig.pose<<",\"part_from_weapon_m\":";matrix(r.partFromWeapon);out<<",\"wrist_from_weapon_m\":";matrix(r.wristFromWeapon);out<<'}';}
    out<<"]}";
}
}
