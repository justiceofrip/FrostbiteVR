"""Freeze only the eleven missing exact authored-axis reviews; no runtime writes."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
ROOT=Path(__file__).resolve().parents[2]
BASE=Path(__file__).resolve().parent
OUT=ROOT/'reports/coordinator-20261030-feedback56/axis-completion'
RECOVERY=Path('G:/Unity/VRMMO/bc2vr-recovery')
CONVENTION='canonical reflected-Z row-vector metres; named bone relative to jntWpn_1'


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pin(path):
    return dict(path=str(path.resolve()),sha256=sha(path))


def digest(value):
    return hashlib.sha256(json.dumps(value,sort_keys=True,separators=(',',':'),ensure_ascii=True,allow_nan=False).encode()).hexdigest()


def require(test, message):
    if not test:
        raise ValueError(message)


def main():
    paths={
        'new_bindings':ROOT/'reports/coordinator-20261030-magazines45-policy04/new-bindings.json',
        'm1_bindings':ROOT/'handoff/tasks/WEAPONS/20261030-support43/m1-reference-bindings.json',
        'opening_bindings':RECOVERY/'overnight426/smg/opening-level-reference-bindings.json',
        'preview_receipt':OUT/'preview-receipt.json',
        'muzzle_metadata':ROOT/'profiles/weapons54-muzzle/enrollment.json',
        'frozen_enrollment':ROOT/'profiles/weapons56-aim/enrollment.json',
        'fleet_audit':RECOVERY/'authored-axes371-fleet/audit.json',
        'fleet_typed_firing':RECOVERY/'authored-axes371-fleet/firing.json',
        'roster':ROOT/'handoff/tasks/WEAPONS/20261030-roster43/roster.json',
        'type5_mesh_proof':RECOVERY/'overnight426/smg/opening-level-mesh-bindings.json',
        'm1_source_derivation':ROOT/'handoff/tasks/WEAPONS/20261030-support43/derivation.json',
        'g3_source_packet':ROOT/'reports/coordinator-20261030-magazines45-policy04/ccc088ce53d98699/source.json',
        'thompson_source_packet':ROOT/'reports/coordinator-20261030-magazines45-policy04/77c108fdfdaac1ba/source.json',
        'storage_admission':OUT/'storage.json',
        'failed_incomplete_storage_receipt':ROOT/'reports/coordinator-20261030-weapons56-axis-completion/storage.json',
    }
    sources={name:pin(path) for name,path in paths.items()}
    data={name:read(path) for name,path in paths.items()}
    frozen_paths=[ROOT/'profiles/weapons56-aim'/name for name in ['enrollment.json','reviewed-meshes.json','README.md','Aim.generated.h']]
    frozen_pins={str(p):sha(p) for p in frozen_paths}
    preview=data['preview_receipt']
    for item in [preview['renderer'],*preview['tools'].values(),preview['type5_source_mesh_proof']]:
        require(sha(Path(item['path']))==item['sha256'],'Preview tool or mesh proof changed')
    require(data['storage_admission']['generated'] is not None,'Complete storage receipt required')
    observations={
        'G3':'In the upper Z/Y view the barrel/muzzle and front sight are left of the handguard and receiver; the buttstock extends right. The front sight rises above the barrel and the pistol grip/magazine descend below. The lower Z/X view confirms the long barrel follows Z with no transverse bend. Thus this exact mesh has -Z forward and +Y up.',
        'Thompson':'In the upper Z/Y view the straight barrel and front sight extend left of the box receiver, while the buttstock extends right; the rear sight projects above the receiver and the pistol grip and magazine descend below. The lower Z/X view corroborates the barrel centreline along Z. Thus this exact mesh has -Z forward and +Y up.',
        'M1Garand':'In the upper Z/Y view the muzzle and raised front sight sit at the left end of the long barrel/fore-end; the receiver and stock continue right. The rear sight rises above the receiver while the trigger guard and stock wrist are below. The lower Z/X view confirms the long bore/fore-end along Z. Thus this exact mesh has -Z forward and +Y up; displaced small auxiliary points are not treated as a muzzle origin.',
        'Type5':'In the upper Z/Y view the muzzle and front sight are left of the long fore-end and receiver, with the stock extending right. Front/rear sights are above the barrel/receiver and the trigger guard and stock wrist are below. The lower Z/X view confirms the barrel/fore-end along Z. Thus this exact mesh has -Z forward and +Y up; displaced auxiliary points are not direction or origin evidence.',
    }
    meshes=[];poses=[];mesh_by_path={}
    for index,p in enumerate(preview['poses']):
        require(not p['gaps'] and all(r['mixed_triangles']==0 for r in p['parts']),'Incomplete geometry requires separate review')
        require(sha(Path(p['geometry_image']['path']))==p['geometry_image']['sha256'],'Reviewed image changed')
        require(sha(Path(p['source_binding']['path']))==p['source_binding']['sha256'],'Preview binding source changed')
        b=read(Path(p['source_binding']['path']))['profiles'][p['profiles_index']]
        require(b['binding_digest']==p['binding_digest'] and b['weapon']==p['weapon'],'Preview exact configuration mismatch')
        require(b['weapon_bone']=='jntWpn_1' and b['coordinate_convention']==CONVENTION,'Preview root convention mismatch')
        m={k:p[k] for k in ['mesh_sha256','lod_sha256','skeleton_sha256','rig_fingerprint','weapon_bone','coordinate_convention']}
        m.update(mesh_path=p['configured_mesh_path'],family=p['family'],model_forward=[0,0,-1],model_up=[0,1,0],
                 pose_gallery_indices=[index+1],observation=observations[p['family']],review_level='authored_geometry_reviewed',
                 preview_receipt_source='preview_receipt',preview_receipt_poses_index=index,
                 rendered_rigid_triangles=sum(v['triangles'] for v in p['parts']),omitted_triangles=0,
                 scope='Exact mesh visual orientation only; no origin, grip, feed, firing or native acceptance.')
        meshes.append(m);mesh_by_path[m['mesh_path']]=m
        poses.append(dict(gallery_index=index+1,family=p['family'],geometry_image=p['geometry_image'],
                          preview_receipt_source='preview_receipt',preview_receipt_poses_index=index,
                          source_binding_digest=p['binding_digest'],observation=m['observation'],
                          model_forward=m['model_forward'],model_up=m['model_up'],projection=preview['projection']))
    rows=[]
    for source_id,indices in [('new_bindings',range(8)),('m1_bindings',range(2)),('opening_bindings',[6])]:
        for index in indices:
            b=data[source_id]['profiles'][index]
            require(digest({k:v for k,v in b.items() if k!='binding_digest'})==b['binding_digest'],'Source binding digest mismatch')
            m=mesh_by_path[b['configured_mesh_path']]
            geometry=[x['geometry'] for x in b['meshes'] if x['configured_mesh_path']==m['mesh_path']]
            require(len(geometry)==1 and geometry[0]['mesh_sha256']==m['mesh_sha256'] and
                    any(v['lod']==0 and v['data_sha256']==m['lod_sha256'] for v in geometry[0]['lods']),'Exact configuration mesh/LOD mismatch')
            require(b['skeleton_sha256']==m['skeleton_sha256'] and b['rig_fingerprint']==m['rig_fingerprint'] and
                    b['weapon_bone']=='jntWpn_1' and b['state_count']==1 and b['state_index']==0 and
                    b['coordinate_convention']==CONVENTION,'Exact configuration rig/root/state mismatch')
            key=(b['native_asset_name'],b['weapon']['resource'].removesuffix('.dbx'),b['configured_mesh_path'])
            muzzle=[v for v in data['muzzle_metadata']['profiles'] if (v['asset'],v['configuration_path'],v['mesh_path'])==key]
            require(len(muzzle)==1,'Exact typed firing join missing')
            muzzle=muzzle[0]
            require(muzzle['configuration_sha256']==b['weapon']['sha256'] and muzzle['configuration_guid']==b['weapon']['instance_guid'] and
                    b['binding_digest'] in muzzle['equivalent_source_binding_digests'] and muzzle['origin']['direction_claim']=='none' and
                    muzzle['firing']['fields']['InitialDirection']==[0,0,1],'Typed metadata corroboration mismatch')
            ref=dict(source_id=source_id,profiles_index=index,binding_digest=b['binding_digest'],clip_sha256=b['animation_sha256'],
                     configuration_sha256=b['weapon']['sha256'],configuration_guid=b['weapon']['instance_guid'],
                     weapon_bone=b['weapon_bone'],coordinate_convention=CONVENTION,state_count=1,state_index=0)
            row={k:m[k] for k in ['mesh_sha256','lod_sha256','skeleton_sha256','rig_fingerprint','model_forward','model_up','pose_gallery_indices']}
            row.update(native_asset_name=key[0],configuration_path=key[1],configured_mesh_path=key[2],binding_digest=b['binding_digest'],
                       source_binding_refs=[ref],muzzle_metadata_join=dict(profile_digest=muzzle['profile_digest'],direction_source=False,
                       status='exact_origin_only_typed_metadata_join'),axis_evidence_scope='exact_mesh_lod_skeleton_rig_root_visual_axis',
                       native_verified=False,headset_accepted=False,runtime_enabled_by_this_review=False)
            rows.append(row)
    require(len(rows)==11 and len(meshes)==4,'Bounded completion cardinality changed')
    prior_keys={(p['asset'],p['configuration_path'],p['mesh_path']) for p in data['frozen_enrollment']['profiles']}
    require(not any((p['native_asset_name'],p['configuration_path'],p['configured_mesh_path']) in prior_keys for p in rows),'Overlaps frozen enrollment')
    nambu=[p for p in data['frozen_enrollment']['unenrolled_origin_configurations'] if p['asset']=='Nambu100']
    require(len(nambu)==1,'Nambu preservation target changed')
    packet=dict(schema='fvr.bc2.authored_axis_batch_review.v1',review_date='2026-10-30',reviewer='feedback56_aim_gaps; direct view_image inspection of all four generated side/top previews',
        review_level='authored_geometry_reviewed',evidence_level='source_only_asset_review',coordinate_convention=CONVENTION,
        scope='Only the eleven missing G3/Garand/Type5/Thompson configurations. Existing 182 configurations and 40 reviewed meshes are untouched.',
        native_verified=False,headset_accepted=False,sources=sources,meshes=meshes,poses=poses,profiles=rows,additional_configuration_bindings=[],
        summary=dict(reviewed_geometry_images=4,reviewed_mesh_contents=4,exact_configurations=11,g3=6,m1_garand=2,type5=1,thompson=2,
                     geometry_pose_gaps=0,omitted_triangles=0,native_admissions=0,frozen_configurations_preserved=182,
                     potential_matching_origin_configurations_after_merge=180,total_origin_configurations=181),
        method=['Exact LOD0 mesh, skeleton and authored clip bytes were read from pinned local archives; no raw game bytes were written.',
                'All four actual CPU point-cloud PNGs were directly inspected with signed Z/Y and Z/X orthographic projections.',
                'Barrel versus stock and sights versus lower grip/trigger features establish axes separately for each mesh; no class or universal-axis inference.',
                'Every configuration independently matches mesh/LOD, skeleton, rig, jntWpn_1 root convention, exact config hash/GUID and typed firing metadata.',
                'Typed firing raw +Z only corroborates the reflected coordinate convention; origin-only metadata remains direction_claim none.',
                'The existing append_reviewed_batch function is executed in memory to validate this packet without changing frozen generator outputs.'],
        preparation_failures_retained=[
            dict(stage='storage scan in sandbox',result='refused_incomplete_scan',reason='Access denied to reports/coordinator-20261030-weapons50-python/tmphgzm1zkl',
                 receipt_source='failed_incomplete_storage_receipt',generated_receipt_field=None,preview_generated=False,
                 resolution='Elevated scan passed with actual recovery tree added; original incomplete receipt is not admission.'),
            dict(stage='optional Pillow imports',result='ModuleNotFoundError in available Python runtimes',preview_generated=False,resolution='Standalone standard-library PNG renderer; no install/download.'),
            dict(stage='first exact-source attempt',result='FileNotFoundError for G:/bc2/Dist/win32/levels/mp_common/level-00.fbrb',preview_generated=False,
                 resolution='Existing Type5 source inspection identified G:/New folder (2)/Battlefield Bad Company 2; fresh StorageBudget passed before rerun.')],
        preserved_existing_alignment=[dict(**nambu[0],action='leave_existing_authored_alignment_unchanged',axis_review_added=False)],
        unresolved_rows=[],limitations=['This review grants visual orientation only; native firing direction/accuracy, submitted-mesh alignment and headset acceptance remain unqualified.',
                                       'Four authored t=0 point-cloud views do not qualify native animation, grip/support, mechanisms, muzzle origins or every runtime pose.'])
    destination=BASE/'review.json'
    destination.write_text(json.dumps(packet,indent=2)+'\n',encoding='utf-8')
    spec=importlib.util.spec_from_file_location('axis_generator',ROOT/'tools/Build-Bc2AuthoredAimProfiles.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    appended=[];provenance={};source_result={}
    omitted=module.append_reviewed_batch(destination,source_result,provenance,appended,data['muzzle_metadata']['profiles'])
    require(len(appended)==11 and len(provenance)==4 and not omitted,'Existing generator rejected completion')
    require(all(sha(Path(p))==expected for p,expected in frozen_pins.items()),'Frozen aim files changed')
    status=[
        'FROZEN: 11 exact configurations / 4 meshes reviewed; source-only visual axes.',
        'G3 x6; M1 Garand x2; Type5 x1; Thompson x2. Nambu100 existing authored alignment untouched.',
        'Each mesh independently reviewed in actual CPU side/top PNGs: canonical forward (0,0,-1), up (0,1,0).',
        'Exact mesh/LOD/skeleton/rig/root/config hashes and typed firing joins are in review.json.',
        'Existing generator append_reviewed_batch accepted all 11 in memory; 0 omitted, 4 new mesh proofs.',
        'Potential origin coverage after coordinator merge: 180/181; combined aim rows 193, mesh families 44.',
        'Frozen 182-row aim files/helper/runtime unchanged; no build, native process, installation or headset check.',
        'StorageBudget elevated scan passed with recovery tree included; initial ACL/import/path failures preserved.',
        'Previews/receipt: reports/coordinator-20261030-feedback56/axis-completion (2,019,894 source bytes, 0 geometry gaps).',
        'Generator integration: append this packet after the existing independent packet; no need to re-review old meshes.',
        'Review SHA256: '+sha(destination),
        'Retired after this bounded completion packet; native accuracy and headset acceptance remain open.',
    ]
    (BASE/'STATUS.txt').write_text('\n'.join(status)+'\n',encoding='utf-8')
    print(json.dumps(dict(configurations=len(appended),meshes=len(provenance),omitted=len(omitted),review_sha256=sha(destination))))


if __name__=='__main__':
    main()
