"""Join reviewed offline metadata into exact LMG capture jobs; no admission."""
import argparse,hashlib,json,math
from pathlib import Path

def digest(data):return hashlib.sha256(data).hexdigest()
def identity(w):return (w['resource'].lower(),w['instance_guid'].upper())
def unique(rows,key):
    out={}
    for row in rows:
        k=key(row)
        if k in out:raise ValueError('Ambiguous duplicate metadata identity: '+str(k))
        out[k]=row
    return out
def build(inventory,blueprints,config,bindings):
    if inventory.get('schema')!='fvr.bc2.installed_weapon_inventory' or blueprints.get('schema')!='fvr.weapon_interaction_blueprints' or config.get('schema')!='fvr.bc2.authored_weapon_configuration' or bindings.get('schema')!='fvr.bc2.authored_weapon_mesh_bindings':raise ValueError('Unsupported metadata schema')
    assets=unique(inventory['asset_profiles'],lambda p:p['resource'].lower());joins=unique(bindings['weapons'],identity)
    archives=unique(inventory['archives'],lambda p:p['archive'])
    plans=[p for p in blueprints['profiles'] if p['family_proposal']=='belt_feed' or p['display_intent'] in ('MG36','XM8 LMG')]
    models={}
    for p in plans:
        resource=p['resource'];key=resource.lower();asset=assets.get(key)
        if asset is None:raise ValueError('Missing exact geometry resource: '+resource)
        if key in models:raise ValueError('Duplicate exact model plan')
        variants={v['sha256'] for v in asset['geometry_variants']}
        parts=[part for part in p['candidate_parts'] if part['lod']==0]
        if any(part['mesh_sha256'] not in variants for part in parts):raise ValueError('Part metadata belongs to a different mesh variant')
        animation=[]
        for source in asset['sources']:
            a=archives[source['archive']]
            for clip in a['animations']:
                if any(word in clip['name'].lower() for word in ('reload','deploy','fire')):
                    animation.append(dict(archive=source['archive'],archive_index_sha256=a['index_sha256'],**clip))
        belt=p['family_proposal']=='belt_feed'
        models[key]=dict(label=p['display_intent'],mesh_resource=resource,family_proposal=p['family_proposal'],
            family_basis='Explicit existing work plan; not inferred from rtMagazine or generic bone names',runtime_enabled=False,
            geometry_variants=asset['geometry_variants'],candidate_parts=parts,animation_resources=animation,definitions=[],
            physical_parts_to_identify=['cover','latch','container','feed','charge'] if belt else ['magazine','magazine_well','release','charge'],
            native_gate='unverified',contacts_verified=False,authored_mesh_matching=True)
    errors=[]
    for w in config['resolved_weapons']:
        if w['weapon_class']!='wcLmg':continue
        matched=joins.get(identity(w))
        if matched is None or matched['resource_sha256']!=w['resource_sha256'] or matched['native_name']!=w['native_name']:
            raise ValueError('Configuration and exact authored mesh source disagree: '+w['resource'])
        seen=set();unresolved=[]
        for state in matched['states']:
            for mesh in state['meshes']:
                k=mesh.get('mesh_resource','').lower()
                if mesh.get('status')!='authored_mesh_resolved':unresolved.append(mesh);continue
                if k not in models:continue
                expected={v['sha256'] for v in models[k]['geometry_variants']}
                found={v['mesh_sha256'] for v in mesh.get('geometry_variants',[])}
                if not found or not found<=expected:raise ValueError('Authored body mesh digest differs from installed geometry')
                seen.add(k)
        if len(seen)!=1:
            errors.append(dict(resource=w['resource'],native_name=w['native_name'],reason='No unique planned body mesh',body_matches=sorted(seen)));continue
        fields=w['fields'];time=fields.get('FireLogic.ReloadTime',{}).get('value');threshold=fields.get('FireLogic.ReloadThreshold',{}).get('value')
        timing_ok=type(time) in (int,float) and math.isfinite(time) and time>0 and type(threshold) in (int,float) and math.isfinite(threshold) and 0<threshold<=1
        definition={k:w[k] for k in ('native_name','resource','resource_sha256','instance_guid','firing_resource','firing_sha256','firing_guid','function_resource','function_sha256','function_guid','fields','weapon_states','abort_reload_on_sprint','missing_fields')}
        definition.update(source_archive=config['archive'],source_index_sha256=config['index_sha256'],
            selector=dict(asset_name=w['native_name'],asset_path=w['resource'][:-4]),runtime_enabled=False,
            observation_schedule_hint=dict(authored_reload_seconds=time,authored_threshold=threshold,
                nominal_transfer_seconds=time*threshold if timing_ok else None,
                native_multiplier_required=True,nominal_timing_is_not_acknowledgement=True),unresolved_optional_meshes=unresolved)
        models[next(iter(seen))]['definitions'].append(definition)
    for model in models.values():
        model['capture_jobs']=[dict(id=label,execute_automatically=False,native_writes=False,observations=observations) for label,observations in (
            ('native_configuration',['Exact selected asset path/name/data; client and server owner/generation; all three distinct firing objects and effective capacity modifiers','Reflected numeric fire/reload enum and inputs; reload threshold/time/post/bolt fields; compare authored values without overwriting native configuration']),
            ('partial_native_reload',['Prepare a positive partial clip with ordinary gameplay before attaching; record ordinary reload once, no native hold yet','All three before/after counts and native transfer path; same actor/equipment throughout','Raw complete rig topology/inverse-bind and leaf visibility around cover/feed/box/charge phases; canonical vertices reflect asset Z before transform']),
            ('state_contact_label',['Correlate each proposed cover/latch/container/feed/charge role to moving native bone, closed/open extrema and observed native phase','Require anatomical hand contact and actual per-part motion; bounds and animation names alone are insufficient','Identify partial versus empty order and whether a separate charge is needed; do not infer it from a generic belt family']),
            ('native_gate_cancel',['After ordinary observations establish exact family: finite hold before transfer, original delta restored for every native invocation','Explicit cancellation without transfer; one authorized refill receipt; exact loaded/reserve accounting and subsequent native ready','Death/equip/space/owner/timeout/abort and paired visual restoration before release of fire suppression']))]
        model['coverage']=dict(authored_definitions=len(model['definitions']),weighted_lod0_parts=len(model['candidate_parts']),native_trials=0,admitted_contacts=0)
    result=dict(schema='fvr.bc2.lmg_binding_jobs.v1',runtime_admission='none',exported_assets=False,models=list(models.values()),unresolved=errors,
        limitations=['rtMagazine describes native bulk transfer; it does not mean a detachable box magazine or prove cover/belt order.',
            'Authored capacities are not effective live capacities. MP definitions are installed data, not campaign pickup availability.',
            'The existing FeedMechanism is contact/order selection only; no LMG native hold/receipt/part/hand consumer is currently enabled.',
            'Generic jntWpn bone names and asset-bind bounds remain unassigned; each moving part requires native rig/state correlation.'])
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('inventory','blueprints','metadata','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();paths=[a.inventory,a.blueprints,a.metadata];raw=[path.read_bytes() for path in paths]
    if any(len(x)>64*1024*1024 for x in raw):raise ValueError('Input metadata bound')
    i,b,m=map(json.loads,raw);result=build(i,b,m['configuration'],m['mesh_bindings']);result['sources']=[dict(path=str(path),sha256=digest(data)) for path,data in zip(paths,raw)]
    a.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    print(json.dumps(dict(models=len(result['models']),definitions=sum(len(x['definitions']) for x in result['models']),unresolved=len(result['unresolved']))))
if __name__=='__main__':main()
