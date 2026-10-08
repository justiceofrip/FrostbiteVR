"""Exact authored optic/zoom/HUD reference catalog, not a runtime classifier.

Preserves zoom-mesh and lens-filter paths separately. Neither weapon class nor
a shared native asset name enables ADS, magnification or draw suppression.
"""
from __future__ import annotations
import argparse,collections,json
from pathlib import Path
from inspect_bc2_mesh_asset import Archive,sha
from bc2_weapon_config_pipeline import parse,named,scalar,Resolver,guid
import bc2_authored_grip_bindings as g
from bc2_weapon_mesh_bindings import reference_resource

ZOOM_FIELDS={name:'float32' for name in ('FieldOfView','TransitionTime','FovDelayTime','FovTransitionTime',
    'ForegroundBlurFilterDeviation','ForegroundBlurViewDistance','DispersionMultiplier','RecoilMultiplier','MoveSpeedMultiplier')}
ZOOM_FIELDS.update(ForegroundBlurFilter='string',FadeToBlackInZoomTransition='bool',UseFovSpecialisation='bool')
MESH_TYPES={'Render.MeshAsset','Render.SkinnedMeshAsset'}
FILTER_TYPES={'Render.ScopeFilterData','Render.SniperLensScopeFilterData','Render.ColorTintScopeFilterData'}

def values(node,schema):
    result={};missing=[]
    for name,kind in schema.items():
        matches=[c for c in node.children if c.attributes.get('name')==name]
        if not matches:missing.append(name);continue
        # Duplicated fields or malformed known scalars are invalid evidence,
        # never silently replaced with a native default or marked absent.
        field=named(node,name);result[name]={'value':scalar(field,kind),'encoding':kind,'offset':field.offset}
    return {'fields':result,'missing':missing}

def bound_node(resolver,origin,field,expected):
    if field.tag!='field' or field.children or field.value.kind!=2 or field.value.data!='' or 'ref' not in field.attributes:
        raise ValueError('Malformed reference field')
    ref=field.attributes['ref']
    if ref=='null':return {'status':'explicit_null','reference':ref},None,None
    g.ref_key(ref);doc,instance=g.referenced_instance(resolver,origin,ref)
    actual=instance.attributes['type']
    if actual not in expected:raise ValueError('Unexpected reference type: '+actual)
    return {'status':'resolved','reference':ref,**g.receipt(doc,instance)},doc,instance

def describe(resolver,doc,weapon):
    if weapon.attributes['type']!='GameSharedResources.SoldierWeaponData':raise ValueError('Not SoldierWeaponData')
    row={'configuration':g.receipt(doc,weapon),'asset':scalar(named(weapon,'Name'),'string'),
         'weapon_class':scalar(named(weapon,'WeaponClass'),'string'),
         'weapon_render_fov':values(weapon,{'RenderFov':'float32','ZoomRenderFov':'float32'}),
         'hud':values(named(weapon,'Hud'),{'CrosshairTypeId':'string','WeaponClass':'string'}),
         'states':[],'runtime_admitted':False,'runtime_path':'unknown'}
    aim,aim_doc,aim_node=bound_node(resolver,doc,named(weapon,'AimingController'),{'GameSharedResources.SoldierAimingSimulationData'})
    row['aiming']=aim;row['zoom_levels']=[]
    if aim_node is not None:
        row['zoom_type']=scalar(named(aim_node,'ZoomType'),'string')
        refs=g.fields(aim_node,'ZoomLevels')
        if len(refs)>16:raise ValueError('Zoom level count bound')
        for index,ref in enumerate(refs):
            zoom_doc,zoom=resolver.resolve(aim_doc,ref,'GameSharedResources.ZoomLevelData')
            row['zoom_levels'].append({'index':index,'reference':ref,**g.receipt(zoom_doc,zoom),**values(zoom,ZOOM_FIELDS)})
    states=named(weapon,'WeaponStates')
    if states.tag!='array' or not 0<len(states.children)<=32:raise ValueError('Weapon state count/schema')
    for index,state in enumerate(states.children):
        if state.tag!='complex':raise ValueError('Invalid weapon state item')
        record={'index':index,'meshes':[],'zoom_transition':values(state,{'ZoomMeshTransitionFactor':'float32'})}
        for ref in g.fields(state,'Meshes1p'):
            mesh_doc,mesh=g.referenced_instance(resolver,doc,ref)
            if mesh.attributes['type'] not in MESH_TYPES:raise ValueError('Invalid first-person mesh reference type')
            name=scalar(named(mesh,'Name'),'string');g.path_key(name)
            if g.path_key(name+'.dbx')!=g.path_key(mesh_doc.resource):raise ValueError('Configured mesh Name/path mismatch')
            record['meshes'].append({'reference':ref,'name':name,**g.receipt(mesh_doc,mesh)})
        for field,types in (('MeshZoom1p',MESH_TYPES),('ZoomedScopeFilter',FILTER_TYPES),('NonZoomedScopeFilter',FILTER_TYPES)):
            evidence,source,target=bound_node(resolver,doc,named(state,field),types)
            if target is not None:
                if field=='MeshZoom1p':
                    evidence['name']=scalar(named(target,'Name'),'string');g.path_key(evidence['name'])
                    if g.path_key(evidence['name']+'.dbx')!=g.path_key(source.resource):raise ValueError('Zoom mesh Name/path mismatch')
                else:evidence.update(values(target,{'BlurScale':'float32'}))
            record[field]=evidence
        has_mesh=record['MeshZoom1p']['status']=='resolved'
        has_filter=any(record[k]['status']=='resolved' for k in ('ZoomedScopeFilter','NonZoomedScopeFilter'))
        record['authored_route']=('zoom_mesh_and_filter' if has_filter else 'zoom_mesh_only') if has_mesh else ('filter_without_zoom_mesh' if has_filter else 'no_zoom_mesh_or_filter')
        # These names describe serialized relationships only. A normal optic
        # can have no filter; a zoom mesh can contain a reticle or mask. Neither
        # establishes a live render target, magnification, or active ADS state.
        row['states'].append(record)
    row['digest']=g.digest(row);return row

class Store:
    def __init__(self,archive):
        self.archive=archive;self.documents={};self.entries={}
        for e in archive.entries:
            if e.flags!=65536 or not e.name.lower().endswith('.dbx'):continue
            key=g.path_key(e.name)
            if key in self.entries:raise ValueError('Ambiguous archive DBX path')
            self.entries[key]=e
    def load(self,paths):
        pending=sorted(set(map(g.path_key,paths))-self.documents.keys())
        if len(self.documents)+len(pending)>4096:raise ValueError('Document count bound')
        if any(p not in self.entries or self.entries[p].kind!='<non-resource>' for p in pending):raise ValueError('Exact referenced DBX absent/type mismatch')
        # Existing Archive enforces32MiB selected read. Explicit chunks keep a
        # full configured catalog bounded without broad raw asset extraction.
        while pending:
            batch=[];size=0
            while pending and size+self.entries[pending[0]].size<=16*1024*1024:
                p=pending.pop(0);batch.append(p);size+=self.entries[p].size
            if not batch:raise ValueError('Single DBX exceeds catalog selection bound')
            blobs=self.archive.read_selected([self.entries[p].name for p in batch])
            for p in batch:self.documents[p]=parse(self.entries[p].name,blobs[self.entries[p].name])
    def resolver(self):return Resolver(self.documents.values())

def required_references(doc,weapon,resolver):
    # Only follow the typed optical fields, never every reference in a weapon.
    refs=[];aim_ref=named(weapon,'AimingController').attributes.get('ref')
    if aim_ref and aim_ref!='null':refs.append(aim_ref)
    try:
        aim_doc,aim=resolver.resolve(doc,aim_ref,'GameSharedResources.SoldierAimingSimulationData')
        refs+=g.fields(aim,'ZoomLevels')
    except ValueError:pass # Exact error survives in describe(), not a default.
    for state in named(weapon,'WeaponStates').children:
        refs+=g.fields(state,'Meshes1p')
        for field in ('MeshZoom1p','ZoomedScopeFilter','NonZoomedScopeFilter'):
            ref=named(state,field).attributes.get('ref')
            if ref and ref!='null':refs.append(ref)
    return [reference_resource(r) for r in refs if '/' in r]

def run(game,relative,expected_index=None):
    g.path_key(relative.as_posix());path=(game/relative).resolve()
    if not path.is_relative_to(game.resolve()):raise ValueError('Archive escapes game root')
    if expected_index is not None:
        # Reuse the existing exact-index common-archive adapter (including its
        # explicit640MiB mp_common bound and256MiB ephemeral selected cache).
        from bc2_magazine_contact_batch import ArchivePool
        archive=ArchivePool(game,relative.as_posix(),expected_index)(path)
    else:archive=Archive(path)
    store=Store(archive)
    initial=[key for key in store.entries if key.startswith('objects/weapons/handheld/') and not any(s in key for s in ('animtree','mesh','aiclones/','shaders/'))]
    store.load(initial);targets=[(doc,node) for doc in store.documents.values() for node in doc.instances.values() if node.attributes['type']=='GameSharedResources.SoldierWeaponData']
    if len(targets)>2048:raise ValueError('Weapon count bound')
    for _ in range(3):
        needed=[];resolver=store.resolver()
        for doc,node in targets:
            try:needed+=required_references(doc,node,resolver)
            except ValueError:pass
        # Missing external data remains a per-configuration gap, not a reason
        # to pick a similarly named resource from another bundle.
        pending=set(needed)-store.documents.keys();present=pending&store.entries.keys()
        if not present:break
        store.load(present)
    rows=[];gaps=[];resolver=store.resolver()
    for doc,node in targets:
        try:rows.append(describe(resolver,doc,node))
        except (ValueError,KeyError,TypeError) as error:gaps.append({'configuration':g.receipt(doc,node),'reason':str(error)})
    routes=collections.Counter(s['authored_route'] for row in rows for s in row['states'])
    return {'schema':'fvr.bc2.authored_optic_catalog.v1','archive':relative.as_posix(),'index_sha256':archive.index_sha256,
        'documents':len(store.documents),'configurations':rows,'gaps':gaps,'summary':{'configurations':len(rows),'states':sum(len(r['states']) for r in rows),'gaps':len(gaps),'routes':dict(routes)},
        'runtime_admitted':False,'limits':['Serialized FOV fields are retained independently; no runtime projection/magnification ratio is inferred.',
        'MeshZoom1p and lens-filter routes are separate. Null filter does not prove no fullscreen optic.',
        'Exact class/config/mesh data is not live ADS state, selected ownership, optical geometry, GPU draw identity or render-target evidence.',
        'Existing XM8/ACOG rendering and removed controller ADS binding are unchanged.']}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--game',required=True,type=Path);p.add_argument('--archive',required=True,type=Path);p.add_argument('--output',required=True,type=Path);p.add_argument('--index-sha256')
    a=p.parse_args();result=run(a.game,a.archive,a.index_sha256);a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf-8');print(json.dumps(result['summary']));return 0
if __name__=='__main__':raise SystemExit(main())
