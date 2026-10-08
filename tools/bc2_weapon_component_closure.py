"""Join private authored metadata to immutable packages; no native admission."""
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
from bc2_weapon_package_index import COMPONENTS, canonical, digest, sha, identity

SCHEMAS={'fvr.bc2.authored_grip_bindings','fvr.bc2.authored_reload_reference_bindings',
         'fvr.bc2.authored_support_bindings','fvr.bc2.authored_magazine_geometry'}
def guid(v):
    v=v.replace('-','').upper()
    if len(v)!=32 or any(c not in '0123456789ABCDEF' for c in v):raise ValueError('invalid GUID')
    return v
def rig(v):
    v=v.removeprefix('fnv1a64:')
    if len(v)!=16 or any(c not in '0123456789abcdef' for c in v):raise ValueError('invalid rig')
    return v
def matrix(v):
    return isinstance(v,list) and len(v)==16 and all(type(x) in (int,float) and math.isfinite(x) for x in v)
def checked_profile(r,key):
    expected=sha(r[key])
    if digest({k:v for k,v in r.items() if k!=key})!=expected:raise ValueError('profile digest mismatch')
    return expected
def config(w):return canonical(w['resource']),sha(w['sha256']),guid(w['instance_guid'])
def package_config(p):
    i=p['identity'];return canonical(i['configuration_resource']),sha(i['configuration_sha256']),guid(i['instance_guid'])
def mesh_edges(r,p,resources):
    expected={(canonical(x[0]),sha(x[1])) for x in p['identity']['meshes']}
    actual={(canonical(m['reference']),sha(m['mesh_document_sha256'])) for m in r['meshes']}
    if expected!=actual or len(actual)!=len(r['meshes']):return False
    for m in r['meshes']:
        matches=[x for x in resources if canonical(x['mesh'])==canonical(m['configured_mesh_path'])]
        if len(matches)!=1:return False
        g=m.get('geometry');res=matches[0]
        # A grip/magazine role consumes its selected base mesh. Ancillary
        # configured meshes still have complete DBX/resource evidence in the
        # immutable package receipt; an omitted accessory geometry extraction
        # does not masquerade as missing base-role geometry. If supplied, its
        # geometry must agree too.
        if not g:
            if canonical(m['configured_mesh_path'])==canonical(r['configured_mesh_path']):return False
            continue
        if g['mesh_sha256']!=res['metadata_sha256']:return False
        measured={(l['lod'],canonical(l['data_resource']),l['data_sha256']) for l in g['lods']}
        full={(l['lod'],canonical(l['resource']),l['sha256']) for l in res['lods']}
        if not measured or not measured.issubset(full) or len(measured)!=len(g['lods']):return False
    return True
def lookup(document,package_id,role):
    """Exact immutable-package lookup; returns metadata only, never a runtime claim."""
    sha(package_id)
    rows=[r for r in document['rows'] if r['package_id']==package_id]
    if len(rows)!=1:raise ValueError('unknown or ambiguous exact package')
    if digest(rows[0]['identity'])!=package_id or rows[0].get('runtime_enabled') is not False:raise ValueError('changed package identity or admission')
    evidence=[r for r in rows[0]['component_evidence'] if r['role']==role]
    if len(evidence)>1:raise ValueError('ambiguous role')
    return copy.deepcopy(evidence[0]) if evidence else None

def build(index,documents,visibility=None):
    if index.get('schema')!='fvr.bc2.weapon-package-index.prototype.v1' or index.get('native_admission_granted') is not False:
        raise ValueError('non-admitted immutable package index required')
    if index.get('component_closure_schema'):raise ValueError('enriched index must be rebuilt from immutable visibility index')
    out=copy.deepcopy(index);packages={};keys={};profiles={};pending=[];excluded=[];full={}
    if visibility is not None:
        if visibility.get('schema')!='fvr.bc2.visibility-descriptors.v1':raise ValueError('visibility evidence schema')
        for row in visibility['rows']:
            try:key=digest(identity(row))
            except (ValueError,KeyError,TypeError,AttributeError):continue
            if key in full:raise ValueError('ambiguous full package resource evidence')
            full[key]=row['resources']
    for p in out['rows']:
        sha(p['package_id'])
        if p['package_id']!=digest(p['identity']) or p['package_id'] in packages or p.get('runtime_enabled') is not False:raise ValueError('package identity or admission')
        packages[p['package_id']]=p;keys.setdefault(package_config(p),[]).append(p)
        p['component_evidence']=[]
    for doc in documents:
        if doc.get('schema') not in SCHEMAS or doc.get('schema_version')!=1 or doc.get('runtime_admission') is not False:raise ValueError('unsupported or admitted authored metadata')
        if len(doc['profiles'])>4096:raise ValueError('profile count bound')
        for r in doc['profiles']:
            key='profile_digest' if doc['schema'].endswith('magazine_geometry') else 'binding_digest'
            h=checked_profile(r,key)
            if key=='binding_digest' and doc['schema']!='fvr.bc2.authored_support_bindings':
                if h in profiles and profiles[h]!=r:raise ValueError('ambiguous binding digest')
                profiles[h]=r
            pending.append((doc['schema'],r,h))
    seen={};ambiguous=set()
    def add(p,role,r,h,ready,component=None):
        k=(p['package_id'],role)
        if k in ambiguous:raise ValueError('ambiguous exact-package role')
        if k in seen:
            if seen[k]!=h:
                ambiguous.add(k)
                p['component_evidence']=[e for e in p['component_evidence'] if e['role']!=role]
                if component:p['components'].pop(component,None)
                raise ValueError('ambiguous exact-package role')
            return
        seen[k]=h
        p['component_evidence'].append({'role':role,'source_sha256':h,'data_ready':ready,'native_admitted':False,
          'configured_mesh_path':r['configured_mesh_path'],'configuration_identity_verified':True,
          'geometry_dependency_scope':'support_anchor_only;no_mesh_geometry_claim' if role=='support_anchor' else 'selected_configured_mesh;all_supplied_ancillary_geometry_verified',
          'complete_package_resource_receipt_verified':True})
        if component and ready:
            p['components'][component]={'source_sha256':h,'data_ready':True,'native_admitted':False}
    for schema,r,h in pending:
        try:
            if any(r.get(k) is True for k in ('native_admission','native_admitted','runtime_admitted','runtime_accepted','active_native_mesh_binding')):raise ValueError('metadata native admission forbidden')
            source=profiles.get(r.get('grip_binding_digest')) if schema.endswith('magazine_geometry') else r
            if schema.endswith('authored_support_bindings'):
                weapons=r['source_weapons']
                if not weapons or len({config(w) for w in weapons})!=1:raise ValueError('support source configuration ambiguous')
                w=weapons[0]
            else:
                if not source:raise ValueError('missing exact grip digest backlink')
                w=source['weapon']
            matches=keys.get(config(w),[])
            if len(matches)!=1:raise ValueError('configuration absent or ambiguous in supplied package set')
            p=matches[0]
            if p['package_id'] not in full:raise ValueError('complete package mesh/LOD resource evidence missing')
            if r['native_asset_name']!=p['asset'] or rig(r['rig_fingerprint'])!=p['identity']['rig']:raise ValueError('asset or rig mismatch')
            path=canonical(r['configured_mesh_path'])
            if path not in {canonical(x[0]).rsplit('/',1)[0] for x in p['identity']['meshes']}:raise ValueError('configured mesh outside exact package')
            if schema.endswith('authored_support_bindings'):
                # Support-only receipts bind exact source weapon, but do not claim
                # right-hand or complete configured mesh closure.
                ready=r.get('left_hand_status')=='static_authored_pose' and matrix(r.get('left_hand_in_weapon'))
                add(p,'support_anchor',r,h,ready)
            else:
                if not mesh_edges(source,p,full[p['package_id']]):raise ValueError('complete mesh/LOD backlink mismatch')
                if schema.endswith('magazine_geometry'):
                    if 'weapon' in r and config(r['weapon'])!=config(source['weapon']):raise ValueError('magazine weapon contradicts exact backlink')
                    if rig(source['rig_fingerprint'])!=p['identity']['rig']:raise ValueError('backlinked grip rig mismatch')
                    m=[m for m in source['meshes'] if canonical(m['configured_mesh_path'])==path]
                    if len(m)!=1 or not m[0].get('geometry'):raise ValueError('mesh geometry backlink missing')
                    g=m[0]['geometry']
                    if sha(r['mesh_sha256'])!=g['mesh_sha256'] or r['lod_sha256'] not in {l['data_sha256'] for l in g['lods']}:raise ValueError('mesh/LOD digest mismatch')
                    geometry=r['geometry']
                    ready=all(matrix(geometry.get(k)) for k in ('attached_item','item_from_insertion','weapon_from_entry','item_from_hand'))
                    add(p,'detachable_magazine_contacts',r,h,ready,'feed_geometry')
                    # Contacts are not the visible hand-ammo cache payload.
                else:
                    ready=schema=='fvr.bc2.authored_grip_bindings' and r.get('right_hand_status')==r.get('left_hand_status')=='static_authored_pose' and all(matrix(r.get(k)) for k in ('left_hand_in_weapon','right_hand_in_weapon'))
                    add(p,'two_hand_grip' if schema=='fvr.bc2.authored_grip_bindings' else 'reload_reference_frame',r,h,ready,'grip' if schema=='fvr.bc2.authored_grip_bindings' else None)
        except (KeyError,ValueError,TypeError,AttributeError) as e:
            excluded.append({'source_sha256':h,'asset':r.get('native_asset_name'),'reason':str(e)})
    for p in out['rows']:
        p['missing_components']=[k for k in COMPONENTS if p['components'].get(k,{}).get('data_ready') is not True]
    out['component_closure_schema']='fvr.bc2.weapon-component-closure.v1'
    out['component_excluded']=excluded
    out['limits']=['Metadata prerequisites only; no runtime enrollment or native adapter proof.',
      'Magazine contacts do not imply visible ammo cache, feed commit, action cycle, chamber, or holster support.',
      'Support-only data does not imply complete two-hand grip or hand poses.']
    return out

def main():
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('--index',type=Path,required=True)
    a.add_argument('--metadata',type=Path,action='append',required=True);a.add_argument('--visibility',type=Path,required=True);a.add_argument('--output',type=Path,required=True);args=a.parse_args()
    inputs=[args.index,args.visibility,*args.metadata]
    if args.output.resolve() in {p.resolve() for p in inputs}:a.error('output must not overwrite evidence')
    try:
        raw=[p.read_bytes() for p in inputs];docs=[json.loads(b.decode('utf-8-sig')) for b in raw]
        result=build(docs[0],docs[2:],docs[1]);result['input_sources']=[{'path':str(p.resolve()),'sha256':hashlib.sha256(b).hexdigest()} for p,b in zip(inputs,raw)]
        args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    except (OSError,ValueError,KeyError,TypeError,UnicodeError) as e:a.error(str(e))
if __name__=='__main__':main()
