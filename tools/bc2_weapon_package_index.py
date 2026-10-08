"""Offline exact-config package index. Metadata only; never grants native admission."""
import hashlib,json,re
from pathlib import Path
COMPONENTS=('grip','ammo_geometry','feed_geometry','action_geometry','attachment_geometry','visibility','body_geometry','hand_poses')
def sha(value):
    if not isinstance(value,str) or not re.fullmatch('[0-9a-f]{64}',value):raise ValueError('invalid digest')
    return value
def canonical(value):
    if (not isinstance(value,str) or not value or '\\' in value or ':' in value or
        value.startswith('/') or any(ord(c)<32 or ord(c)==127 for c in value) or
        any(p in ('','.', '..') for p in value.split('/'))):
        raise ValueError('invalid relative resource reference')
    return value.lower()
def digest(value):
    return hashlib.sha256(json.dumps(value,sort_keys=True,separators=(',',':')).encode()).hexdigest()
def identity(row):
    c=row.get('authored_configuration')
    if not isinstance(c,dict): raise ValueError('missing exact authored configuration')
    configuration_resource=canonical(c['resource'])
    if not configuration_resource.endswith('.dbx'):raise ValueError('invalid configuration resource extension')
    guid=c['instance_guid'].replace('-','').upper()
    if not re.fullmatch('[0-9A-F]{32}',guid) or not re.fullmatch('[0-9a-f]{64}',c['sha256']): raise ValueError('invalid configuration identity')
    refs=c['mesh_references']; resources=row['resources']; paths=row['meshes']
    if not re.fullmatch('[0-9a-f]{16}',row['rig_fingerprint']):raise ValueError('invalid rig fingerprint')
    if not 1<=len(refs)<=8 or len(refs)!=len(paths) or len(resources)!=len(paths) or len({canonical(p) for p in paths})!=len(paths):raise ValueError('incomplete mesh evidence')
    mesh=[]
    for ref,res,path in zip(refs,resources,paths):
        if res['mesh']!=path:raise ValueError('mesh ordering mismatch')
        if not ref['reference'] or not re.fullmatch('[0-9a-f]{64}',ref['sha256']):raise ValueError('invalid mesh identity')
        ref_guid=ref['instance_guid'].replace('-','').lower()
        if not re.fullmatch('[0-9a-f]{32}',ref_guid):raise ValueError('invalid mesh GUID')
        reference=canonical(ref['reference']);resource=canonical(ref['resource'])
        if not resource.endswith('.dbx') or reference.rsplit('/',1)[-1].replace('-','')!=ref_guid or reference.rsplit('/',1)[0]!=resource[:-4] or canonical(path)!=resource[:-4]:
            raise ValueError('mesh reference backlink mismatch')
        canonical(res['archive']);sha(res['index_sha256']);sha(res['metadata_sha256'])
        if not res.get('lods'):raise ValueError('missing LOD evidence')
        lods=set()
        for lod in res['lods']:
            if type(lod['lod']) is not int or lod['lod']<0 or lod['lod'] in lods:raise ValueError('duplicate or invalid LOD')
            lods.add(lod['lod']);canonical(lod['resource']);sha(lod['sha256'])
            if not lod.get('sections') or any(s.get('normalized') is not True for s in lod['sections']):raise ValueError('missing normalized section evidence')
        # Include complete geometry metadata/LOD evidence, not solely mesh DBX.
        mesh.append((reference,ref['sha256'],digest(res)))
    if len({m[0] for m in mesh})!=len(mesh):raise ValueError('duplicate mesh reference')
    return {'configuration_resource':configuration_resource,'configuration_sha256':c['sha256'],'instance_guid':guid,'meshes':sorted(mesh),'rig':row['rig_fingerprint']}
def build(document,components=()):
    if document.get('schema')!='fvr.bc2.visibility-descriptors.v1':raise ValueError('schema')
    rows=[];excluded=[];seen=set(); bindings={}
    for component in components:
        key=(component['package_id'],component['component'])
        sha(key[0]);sha(component['source_sha256'])
        if key in bindings or key[1] not in COMPONENTS:raise ValueError('ambiguous or invalid component')
        bindings[key]=component
    for row in document['rows']:
        try:i=identity(row)
        except (ValueError,KeyError,TypeError,AttributeError) as e:excluded.append({'asset':row.get('asset',''),'reason':str(e)});continue
        key=digest(i)
        if key in seen:raise ValueError('duplicate exact configuration')
        seen.add(key)
        have={'visibility':{'source_sha256':digest(row),'data_ready':True,'native_admitted':False}}
        for name in COMPONENTS:
            b=bindings.get((key,name))
            if b:
                if name=='visibility':raise ValueError('visibility already bound')
                have[name]={'source_sha256':b['source_sha256'],'data_ready':b.get('data_ready') is True,'native_admitted':False}
        rows.append({'package_id':key,'asset':row['asset'],'identity':i,'components':have,'missing_components':[n for n in COMPONENTS if n not in have or not have[n]['data_ready']],'runtime_enabled':False})
    unknown={key[0] for key in bindings}-seen
    if unknown:raise ValueError('component package does not match exact indexed configuration')
    return {'schema':'fvr.bc2.weapon-package-index.prototype.v1','rows':rows,'excluded':excluded,'native_admission_granted':False,'exported_geometry':False}
if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser();p.add_argument('--visibility',required=True);p.add_argument('--output',required=True);a=p.parse_args()
    if Path(a.output).resolve()==Path(a.visibility).resolve():p.error('output must not overwrite visibility input')
    try:result=build(json.loads(Path(a.visibility).read_text(encoding='utf-8-sig')))
    except (OSError,ValueError,KeyError,TypeError,AttributeError) as e:p.error(str(e))
    Path(a.output).write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')

