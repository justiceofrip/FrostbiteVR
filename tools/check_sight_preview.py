"""Independent read-only audit of fixed-palm sight preview in a native capture."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import numpy as np

def supported_handover(doc,group):
    """Exact capture-local scoped-XM8 pair; no pointer/name-only inheritance."""
    first=group[0];identity=(first["physical_item"],first["owner"],first["space"])
    if not identity[0] or any((x["physical_item"],x["owner"],x["space"])!=identity for x in group):return False
    gameplay=doc["gameplay"];rig=gameplay["rig_publication"]
    if gameplay.get("weapon_mode_binding_verified") is not True:return False
    gestures=[x for x in gameplay["sight_flip"].get("gestures",[]) if x.get("start_generation")==first["grab_generation"]]
    if len(gestures)!=1:return False
    gesture=gestures[0]
    if gesture.get("id")!=first["token"] or gesture.get("weapon")!=first["weapon"] or not gesture.get("committed") or not gesture.get("request"):return False
    weapons=[]
    for row in group:
        if not weapons or row["weapon"]!=weapons[-1]:weapons.append(row["weapon"])
    if len(weapons)!=2 or weapons[0]==weapons[1]:return False
    raw=rig.get("weapon_profile_samples",[])
    supported={"XM8_sp_s":("Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM8_Scoped",1),
               "40mmgl":("Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM320_Scoped",3)}
    observations=[]
    for weapon in weapons:
        samples=[x for x in raw if (x.get("weapon"),x.get("owner_generation"),x.get("space"))==(weapon,first["owner"],first["space"])]
        if not samples:return False
        preview_generations=[x["generation"] for x in group if x["weapon"]==weapon]
        if not any(min(preview_generations)<=x.get("generation",0)<=max(preview_generations) for x in samples):return False
        values=set()
        for sample in samples:
            provenance=sample.get("capture_provenance",{});native_rig=provenance.get("rig",{})
            value=(sample.get("asset_name"),provenance.get("persistence"),sample.get("actor"),native_rig.get("weak"),provenance.get("weapon_data"))
            if sample.get("skeleton")!="fnv1a64:a7f219a1426216ab" or sample.get("asset_label_source")!="selected_soldier_weapon_data" or sample.get("native_pose_source")!="actor_first_person_rig_pre_vr":return False
            if not sample.get("weapon_bones_complete") or sample.get("weapon_bones_dropped") or not sample.get("native_weapon_bones"):return False
            if sample.get("bone_roles",{}).get("weapon_root")!="jntWpn_1":return False
            bones=sample["native_weapon_bones"]
            for name in ("jntWpn_1","jntWpn_10","jntWpn_11"):
                named=[x for x in bones if x.get("name")==name]
                if len(named)!=1:return False
                bone=named[0]
                if bone.get("hidden") or (name!="jntWpn_1" and bone.get("parent_name")!="jntWpn_1"):return False
                for field in ("native","inverse_bind"):
                    transform=np.asarray(bone.get(field,[]),dtype=float)
                    if transform.shape!=(16,) or not np.isfinite(transform).all():return False
            if value[1]!=identity[0] or not value[2] or native_rig.get("soldier")!=value[2] or not value[3] or not value[4]:return False
            values.add(value)
        if len(values)!=1:return False
        value=values.pop()
        if value[0] not in supported:return False
        configs=[x for x in gameplay.get("selected_meshes_observer",{}).get("configuration_identity_samples",[])
                 if (x.get("owner",{}).get("weapon"),x.get("owner",{}).get("actor_generation"),x.get("owner",{}).get("space"))==(weapon,first["owner"],first["space"])]
        if not configs:return False
        config_values=set()
        for config in configs:
            owner=config.get("owner",{});expected_path,slot=supported[value[0]]
            if not config.get("configuration_path_verified") or config.get("configuration_path")!=expected_path or config.get("weapon_name")!=value[0] or config.get("selected_slot")!=slot or config.get("weapon_data")!=value[4] or owner.get("soldier")!=value[2] or owner.get("weak")!=value[3] or not config.get("inventory"):return False
            config_values.add(config["inventory"])
        if len(config_values)!=1:return False
        observations.append((value,config_values.pop()))
    old,new=observations
    if {old[0][0],new[0][0]}!={"XM8_sp_s","40mmgl"} or old[1]!=new[1] or old[0][1:4]!=new[0][1:4]:return False
    expected_action=33 if old[0][0]=="XM8_sp_s" else 36
    expected_start=0 if expected_action==33 else 1
    if gesture.get("start_mode")!=expected_start:return False
    records=[x for x in gameplay.get("weapon_mode_records",[]) if x.get("gesture")==gesture["request"]]
    if len(records)!=1:return False
    record=records[0]
    if (record.get("from"),record.get("target"),record.get("action"),record.get("actor"),record.get("weak"),record.get("owner"),record.get("space"))!=(weapons[0],weapons[1],expected_action,old[0][2],old[0][3],first["owner"],first["space"]):return False
    return record.get("cancelled") is False and record.get("ack_ms",0)>=record.get("ms",0)>0


def check(folder):
    folder=Path(folder)
    path=folder/"native-trace.json"
    doc=json.loads(path.read_text(encoding="utf-8-sig"))
    done=json.loads((folder/"completion.json").read_text(encoding="utf-8-sig"))
    rig=doc["gameplay"]["rig_publication"]; preview=rig.get("sight_preview",{})
    failures=[]
    def require(ok,reason):
        if not ok and reason not in failures: failures.append(reason)
    def matrix(v):
        m=np.asarray(v,dtype=float).reshape(4,4)
        if not np.isfinite(m).all() or not np.allclose(m[:,3],[0,0,0,1],atol=1e-5):
            raise ValueError("Invalid affine matrix")
        q=m[:3,:3]
        if np.max(np.abs(q@q.T-np.eye(3)))>.02 or np.linalg.det(q)<.98:
            raise ValueError("Invalid proper rigid matrix")
        return m
    def point(p,m):
        return (np.r_[np.asarray(p,dtype=float),1.]@m)[:3]
    def hinge_angle(native,posed):
        axis=native[0,:3]/np.linalg.norm(native[0,:3])
        a=native[2,:3]/np.linalg.norm(native[2,:3])
        b=posed[2,:3]/np.linalg.norm(posed[2,:3])
        return math.atan2(float(axis@np.cross(a,b)),float(a@b))
    require(doc.get("hooks_disabled") is True and doc.get("state")=="observed","Hooks did not retire")
    require(done.get("game_responding") is True and not done.get("game_exited") and not done.get("new_crash_report"),"Native completion failed")
    require(rig.get("native_animation_written") is False,"Native animation was changed")
    for key in ("source_changes","packing_failures","fallback_failures"):
        require(rig.get(key)==0,key+" must be zero")
    for key in ("camera_restore_failures","gpu_failure_stage","gpu_failure_code"):
        require(doc["native_stream"].get(key)==0,key+" must be zero")
    require(preview.get("poses",0)>0 and preview.get("rejected")==0,"Preview was absent or rejected")
    rows=preview.get("records",[]);require(len(rows)>=8,"Insufficient preview coverage")
    groups={};baselines={};pivot_errors=[];palm_errors=[];resolved_errors=[];seating=[];angle_errors=[]
    for row in rows:
        token=row["token"];groups.setdefault(token,[]).append(row)
        require(token>0 and row["weapon"]>0 and row["owner"]>0 and row["space"]>0,"Missing preview identity")
        require(0<row["generation"]<=row["input_generation"],"Preview generation is ahead of input")
        rear,front,nrear,nfront,wrist,resolved,raw=(matrix(row[k]) for k in ("rear","front","native_rear","native_front","wrist","resolved_wrist","raw_wrist"))
        grasp=np.asarray(row["grasp"],dtype=float);palm=row["palm_wrist"];angle=float(row["angle"])
        require(np.isfinite(grasp).all() and math.isfinite(angle),"Nonfinite grasp or angle")
        for a,b in ((rear,nrear),(front,nfront)):
            pivot_errors.append(float(np.linalg.norm(a[3,:3]-b[3,:3])))
        # Preview transforms are absolute rotations from the frozen grab frames.
        # Current native animation may already have completed the requested mode.
        baselines.setdefault(token,(nrear,nfront))
        brear,bfront=baselines[token]
        angle_errors += [abs(hinge_angle(brear,rear)-angle),abs(hinge_angle(bfront,front)+angle)]
        palm_errors.append(float(np.linalg.norm(point(palm,wrist)-grasp)))
        resolved_errors.append(float(np.linalg.norm(point(palm,resolved)-grasp)))
        seating.append(float(np.linalg.norm(point(palm,raw)-grasp)))
        require(not row["reach_clamped"],"Fixture sight hand was reach-clamped")
    summaries=[]
    for token,group in groups.items():
        require(len(group)>=4,"Gesture has fewer than four preview samples")
        first=group[0];identity=(first["weapon"],first["owner"],first["space"])
        require(all((x["owner"],x["space"])==identity[1:] for x in group),"Owner or space changed inside a fixed grasp")
        same_weapon=all(x["weapon"]==identity[0] for x in group)
        require(all(x.get("physical_item")==first.get("physical_item") and x.get("physical_item",0)>0 for x in group),"Physical gun changed inside a fixed grasp")
        require(same_weapon or supported_handover(doc,group),"Unproven native weapon handover inside fixed grasp")
        starts=[x for x in doc["gameplay"]["sight_flip"].get("gestures",[]) if x.get("start_generation")==first.get("grab_generation")]
        initial=(len(starts)==1 and starts[0].get("id")==token and starts[0].get("weapon")==first["weapon"] and
                 starts[0]["start_generation"]<=first["generation"]<=starts[0].get("end_generation",0) and
                 first["generation"]<=first["input_generation"] and
                 first["phase"]==1 and abs(first["angle"])<1e-6 and abs(first["gesture_angle"])<1e-6 and
                 not first["native_observed"] and not first["raw_observed"])
        require(initial,"Missing initial zero-angle captured grasp basis")
        require(all(a["generation"]<=b["generation"] for a,b in zip(group,group[1:])),"Grasp generation went backward")
        anchors=[point(x["grasp"],np.linalg.inv(matrix(x["rear"]))) for x in group]
        anchor_error=max(float(np.linalg.norm(x-anchors[0])) for x in anchors)
        require(anchor_error<.0001,"Grab point slid along sight frame")
        require(-.111<=anchors[0][2]<=-.039,"Grab point is outside upper sight-frame region")
        angles=[x["angle"] for x in group]
        require(max(angles)-min(angles)>.15,"No continuous intermediate sight motion")
        summaries.append({"token":token,"samples":len(group),"min_angle":min(angles),"max_angle":max(angles),"max_anchor_drift_m":anchor_error})
    require(any(x["max_angle"]>.2 for x in summaries) and any(x["min_angle"]<-.2 for x in summaries),"Opening and closing preview coverage required")
    require(bool(pivot_errors) and max(pivot_errors)<.001,"Sight hinge drift exceeds1mm")
    require(bool(angle_errors) and max(angle_errors)<.005,"Rear/front preview rotation does not match opposite hinge angles")
    require(bool(palm_errors) and max(palm_errors)<.00002,"Desired palm is not locked to frame")
    require(bool(resolved_errors) and max(resolved_errors)<.001,"Rendered palm misses frame by over1mm")
    require(bool(seating) and max(seating)>.005,"No independent raw-to-seated hand displacement")
    return {"passed":not failures,"failures":failures,"headset_tested":False,"visual_skinning_inspected":False,
        "source":{"path":str(path.resolve()),"sha256":hashlib.sha256(path.read_bytes()).hexdigest()},
        "poses":preview.get("poses"),"base_fallbacks":preview.get("base_fallbacks"),"gestures":summaries,
        "max_pivot_error_m":max(pivot_errors,default=None),"max_angle_error_rad":max(angle_errors,default=None),
        "max_desired_palm_error_m":max(palm_errors,default=None),"max_resolved_palm_error_m":max(resolved_errors,default=None),
        "max_active_seating_m":max(seating,default=None)}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native",required=True,type=Path);parser.add_argument("--output",required=True,type=Path)
    args=parser.parse_args()
    try: result=check(args.native)
    except (KeyError,ValueError,TypeError,OSError,np.linalg.LinAlgError) as exc:
        result={"passed":False,"failures":[str(exc)],"headset_tested":False}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+"\n",encoding="utf-8")
    print(json.dumps(result,indent=2,allow_nan=False))
    return 0 if result["passed"] else 1
if __name__=="__main__":raise SystemExit(main())
