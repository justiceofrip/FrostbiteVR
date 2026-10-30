"""Embed public replay metadata without modifying any prepared cache bytes."""
import argparse
import copy
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'tools'))
import bc2_firearm_magazine_batch as batch
import bc2_authored_magazine_policy as policy
import bc2_body_ammo_assets as ammo
import bc2_magazine_registry_header as registry


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--enrollment',type=Path,required=True);p.add_argument('--prepared',type=Path,required=True)
    a=p.parse_args();out=a.prepared;receipt=json.loads((a.enrollment/'receipt.json').read_text())
    batch.require(receipt['complete'] and receipt['joined_configurations']==128 and not receipt['residuals'],'Require completed exact installed enrollment')
    plan=json.loads((a.enrollment/'batch.json').read_text());jobs=json.loads((out/'descriptor-jobs.json').read_text());measured=json.loads((out/'geometry.json').read_text())
    old={p['profile_digest']:p for p in measured['profiles']}
    converted={digest:policy.embed_profile(p) for digest,p in old.items()}
    measured['profiles']=[converted[p['profile_digest']] for p in measured['profiles']]
    catalog=json.loads((a.enrollment/'body-ammo-assets.json').read_text())
    baseline_pin=plan['sources']['baseline_catalog'];baseline=batch.read_pin(baseline_pin)
    batch.write(out/'baseline-body-ammo-assets57.json',baseline)
    plan['sources']['baseline_catalog']=batch.pin(out/'baseline-body-ammo-assets57.json')
    plan['baseline_catalog_original_pin']=baseline_pin
    for row in catalog['profiles']:
        if row.get('mod_authored_magazine'):
            profile=converted[row['geometry_profile_digest']]
            row['geometry_profile_digest']=profile['profile_digest'];row['mod_policy_review_digest']=profile['geometry']['design']['mod_authored_policy']['review_digest']
            row['mod_policy_source']['profile']=profile
        if row.get('mod_authored_display'):
            profile=converted[row['mod_policy_profile']['profile_digest']]
            row['mod_policy_profile']=profile;row['mod_policy_review_digest']=profile['geometry']['design']['mod_authored_policy']['review_digest']
    groups={}
    for row in plan['rows']:
        profile=converted[row['geometry_digest']];row['geometry_digest']=profile['profile_digest']
        row['source_complete']=True
        content=batch.descriptors.digest(batch.content_key(profile))
        row['geometry_content_group']=content
        groups.setdefault(content,dict(content_identity=list(batch.content_key(profile)),configurations=[]))['configurations'].append(row['key'])
    plan['content_groups']=groups
    plan['summary'].update(source_complete=128,initial_missing_reload_actions=plan['summary']['missing_reload_actions'],missing_reload_actions=0,independent_content_groups=len(groups))
    plan['source_jobs_remaining']=[]
    for job in plan['source_jobs']:job['status']='resolved_by_exact_mp_common_mesh_and_reference_closure'
    batch.emit(ROOT,out,plan,jobs,measured)
    evidence=dict(schema='fvr.bc2.magazines45.portable_source_enrollment.v1',installed_enrollment=batch.pin(a.enrollment/'receipt.json'),
                  portable_replay_tool=batch.pin(ROOT/'tools/bc2_authored_magazine_policy.py'),native_execution=False,headset_acceptance=False,
                  rows=[dict(key=r['key'],descriptor_digest=r['descriptor_digest'],geometry_digest=r['geometry_digest'],native_family=r['native_family'],instruction_proof_digest=r['instruction_proof_digest']) for r in plan['rows']])
    batch.write(out/'native-evidence.json',evidence);evidence_hash=batch.pin(out/'native-evidence.json')['sha256']
    additions=[r for r in plan['rows'] if not r['preserved_registration']];keys={r['key'] for r in additions};reviews=[]
    for row in additions:
        review=dict(key=row['key'],descriptor_digest=row['descriptor_digest'],family_proof=row['native_family'],evidence_sha256=evidence_hash)
        if row['instruction_proof_digest']:review['instruction_proof_digest']=row['instruction_proof_digest']
        reviews.append(review)
    header,summary=registry.build(dict(jobs,descriptors=[j for j in jobs['descriptors'] if j['key'] in keys]),reviews)
    batch.write(out/'Registry.additions.generated.h',header);batch.write(out/'exact-native-reviews.json',reviews);batch.write(out/'registry-receipt.json',summary)
    additional_geometry=[p for p in measured['profiles'] if (p['native_asset_name'],p['weapon']['resource'][:-4]) not in batch.preserved_identities(ROOT)]
    batch.write(out/'Geometry.additions.generated.h',policy.header(additional_geometry))
    batch.write(out/'body-ammo-assets.json',catalog);batch.write(out/'BodyAmmoAssetData.generated.h',ammo.catalog_header(catalog['profiles']))
    for name in ('visibility.json','Visibility.generated.h'):
        batch.write(out/name,(a.enrollment/name).read_text(encoding='utf-8'))
    enrolled=json.loads((a.enrollment/'enrollment.json').read_text())
    for row in enrolled['profiles']:
        if row.get('geometry_digest') in converted:row['geometry_digest']=converted[row['geometry_digest']]['profile_digest']
    batch.write(out/'enrollment.json',enrolled)
    final=dict(schema='fvr.bc2.magazines45.complete_portable_enrollment.v1',complete=True,configurations=128,
               strict_geometry=80,explicit_mod_authored_geometry=48,preserved_native_registrations=23,new_exact_native_registrations=105,
               catalog_rows=len(catalog['profiles']),cache_bytes=receipt['cache_bytes'],cache_pin=receipt['cache_pin'],
               cache_bytes_unchanged=True,numerical_geometry_unchanged=True,source_complete=True,
               portable_replay_uses_private_paths=False,compiled=False,installed=False,native_execution=False,headset_acceptance=False,
               inputs=dict(installed_enrollment=batch.pin(a.enrollment/'receipt.json')),
               outputs={n:batch.pin(out/n) for n in ('batch.json','geometry.json','body-ammo-assets.json','enrollment.json','CombinedRegistry.h','CombinedGeometry.h','Registry.additions.generated.h','Geometry.additions.generated.h','Visibility.generated.h','BodyAmmoAssetData.generated.h')})
    batch.write(out/'completion-receipt.json',final);print(json.dumps({k:v for k,v in final.items() if k not in ('inputs','outputs')}))


if __name__=='__main__':main()
