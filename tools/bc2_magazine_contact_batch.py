"""Offline batch of exact authored magazine/contact candidates, never admission.

Configuration, grip and geometry readers are shared with existing runtime data
generation. Native class labels are inventory filters, not physical-mechanism
proof: the caller selects exact assets and can defer known exceptional actions.
Only JSON provenance/derived transforms are written; no installed bytes are saved.
"""
from __future__ import annotations
import argparse,contextlib,json,hashlib
from pathlib import Path
import inspect_bc2_mesh_asset as archive_reader
import bc2_authored_grip_bindings as grip
import bc2_authored_magazine_geometry as magazine
from bc2_weapon_mesh_bindings import join,reference_resource
from bc2_weapon_config_pipeline import parse

def digest_bytes(data):return hashlib.sha256(data).hexdigest()
def read(path):
    data=path.read_bytes()
    if len(data)>64*1024*1024:raise ValueError('Input JSON exceeds64MiB')
    return json.loads(data),digest_bytes(data)

class ArchivePool:
    """Bounded, ephemeral read-through cache; no extracted game bytes on disk."""
    def __init__(self,game,common,common_index,archive_type=archive_reader.Archive):
        self.game=game.resolve();self.common=grip.path_key(common);grip.checked_hash(common_index)
        self.common_index=common_index;self.archives={};self.bytes=0;self.archive_type=archive_type
    def __call__(self,path):
        path=Path(path).resolve()
        if not path.is_relative_to(self.game):raise ValueError('Archive leaves installation')
        key=path.relative_to(self.game).as_posix().casefold()
        if key in self.archives:return self.archives[key]
        if len(self.archives)>=128:raise ValueError('Archive cache count bound')
        previous=archive_reader.MAX_PAYLOAD
        try:
            if key==self.common and key=='dist/win32/levels/mp_common/level-00.fbrb':
                archive_reader.MAX_PAYLOAD=640*1024*1024
            source=self.archive_type(path)
        finally:archive_reader.MAX_PAYLOAD=previous
        if key==self.common and source.index_sha256!=self.common_index:raise ValueError('Common archive index differs')
        cache={};original=source.read_selected
        def selected(names):
            names=list(names)
            if len(names)!=len(set(names)):raise ValueError('Duplicate selected resources')
            wanted=set(names);entries=[e for e in source.entries if e.flags==65536 and e.name in wanted]
            if len(entries)!=len(wanted) or sum(e.size for e in entries)>archive_reader.MAX_SELECTED:raise ValueError('Selection bound or absent resource')
            missing=[n for n in names if n not in cache]
            if missing:
                blobs=original(missing);size=sum(map(len,blobs.values()))
                if self.bytes+size>256*1024*1024:raise ValueError('Batch resource cache exceeds256MiB')
                cache.update(blobs);self.bytes+=size
            return {n:cache[n] for n in names}
        source.read_selected=selected;self.archives[key]=source;return source

@contextlib.contextmanager
def readers(pool):
    old=(grip.Archive,magazine.Archive)
    try:
        grip.Archive=magazine.Archive=pool;yield
    finally:grip.Archive,magazine.Archive=old

def key(w):return grip.path_key(w['resource']),w['instance_guid'].upper()
def filter_inputs(configuration,meshes,assets):
    if configuration.get('schema')!='fvr.bc2.authored_weapon_configuration' or meshes.get('schema')!='fvr.bc2.authored_weapon_mesh_bindings':raise ValueError('Configuration/mesh schema')
    if not assets or len(assets)>128:raise ValueError('Exact asset selection bound')
    selected=[w for w in configuration['resolved_weapons'] if w['native_name'] in assets]
    if len(selected)>1024 or len({key(w) for w in selected})!=len(selected):raise ValueError('Duplicate/excessive configuration identity')
    identities={key(w) for w in selected}
    matched=[w for w in meshes['weapons'] if key(w) in identities]
    return {**configuration,'resolved_weapons':selected},{**meshes,'weapons':matched}

def field_values(w):return {k:v['value'] for k,v in w['fields'].items()}
def coverage(configuration,bindings,geometry,assets,deferred):
    rows=[]
    for asset in sorted(assets):
        configs=[w for w in configuration['resolved_weapons'] if w['native_name']==asset]
        bound=[p for p in bindings['profiles'] if p['native_asset_name']==asset]
        poses=[p for p in geometry['profiles'] if p['native_asset_name']==asset]
        gaps=[g for g in geometry['gaps'] if g['native_asset_name']==asset]
        grip_gaps=[g for g in bindings['gaps'] if g.get('native_name')==asset]
        rows.append({'native_asset_name':asset,'configuration_definitions':len(configs),'grip_state_bindings':len(bound),
            'paired_geometry_candidates':len(poses),'candidate_digests':[p['profile_digest'] for p in poses],
            'status':'deferred' if asset in deferred else 'paired_candidate' if poses else 'unresolved',
            'deferred_reason':deferred.get(asset),'geometry_gaps':gaps,'grip_gaps':grip_gaps,
            'configuration_candidates':[{'resource':w['resource'],'sha256':w['resource_sha256'],'instance_guid':w['instance_guid'],
                'weapon_class':w['weapon_class'],'firing_resource':w['firing_resource'],'firing_sha256':w['firing_sha256'],
                'fields':field_values(w),'physical_ammo_family':'requires reviewed mesh/animation mechanism; ReloadType alone is insufficient'} for w in configs],
            'positions':{'hand_to_magazine':'same-frame authored reload wrist+15 fingers; experimental interpolation' if poses else 'unresolved',
                         'closed_magazine':'exact static authored frame' if poses else 'unresolved',
                         'entry_axis':'geometry-derived long-axis / body-facing end estimate' if poses else 'unresolved',
                         'insertion_travel':'shared VR assist design, not extracted native insertion path',
                         'body_supply':'shared VR body-slot policy, not an authored gun coordinate'},
            'runtime_admitted':False,'headset_tested':False})
    return {'schema':'fvr.bc2.magazine_contact_coverage.v1','rows':rows,'runtime_admission':False,
            'summary':{'assets_requested':len(assets),'assets_with_paired_candidates':sum(bool(r['paired_geometry_candidates']) for r in rows),
                'configuration_definitions':sum(r['configuration_definitions'] for r in rows),'paired_state_profiles':sum(r['paired_geometry_candidates'] for r in rows),
                'unresolved_assets':sum(r['status']=='unresolved' for r in rows),'deferred_assets':sum(r['status']=='deferred' for r in rows)},
            'limits':['Definitions and variants are not distinct guns. Authored class labels include some sidearms.',
                'No native descriptor/current-owner/mesh or runtime capability is established by this batch.',
                'Current accepted profiles are not modified; private candidates require explicit later review/admission.']}

def refresh_meshes(configuration,inventory,pool,game):
    # Earlier metadata exports omitted unlock/sight DBX. Resolve every exact
    # configured mesh reference from the current same common archive instead.
    archive=pool(game/configuration['archive']);entries={}
    for e in archive.entries:
        if e.flags!=65536:continue
        k=grip.path_key(e.name)
        if k in entries:raise ValueError('Ambiguous archive resource case')
        entries[k]=e
    names={grip.path_key(reference_resource(ref)) for w in configuration['resolved_weapons'] for state in w['weapon_states'] for ref in state['mesh_asset_references']}
    documents=[]
    for name,raw in archive.read_selected([entries[k].name for k in sorted(names) if k in entries]).items():documents.append(parse(name,raw))
    return join(configuration,documents,inventory)

def run(game,configuration,meshes,hand_poses,assets,deferred,output,inventory=None):
    if not set(deferred)<=assets:raise ValueError('Deferred assets must be selected')
    configuration,meshes=filter_inputs(configuration,meshes,assets)
    pool=ArchivePool(game,configuration['archive'],configuration['index_sha256'])
    with readers(pool):
        if inventory is not None:meshes=refresh_meshes(configuration,inventory,pool,game)
        # A reload contact is independent of whether a static rifle-support grip
        # exists. This distinct schema cannot be exported as a runtime grip.
        bindings=grip.load(game,Path(configuration['archive']),configuration,meshes,hand_poses,reload_references_only=True)
        output.mkdir(parents=True,exist_ok=True)
        (output/'grip-bindings.json').write_text(json.dumps(bindings,indent=2,allow_nan=False)+'\n')
        (output/'mesh-bindings.json').write_text(json.dumps(meshes,indent=2,allow_nan=False)+'\n')
        usable=assets-set(deferred)
        geometry=magazine.derive(game,bindings,meshes,usable,paired_grasp_assets=usable) if usable else {'profiles':[],'gaps':[]}
    result=coverage(configuration,bindings,geometry,assets,deferred)
    result['cache']={'archives':len(pool.archives),'bytes':pool.bytes,'memory_only':True,'limit_bytes':256*1024*1024}
    for name,value in (('paired-geometry.json',geometry),('coverage.json',result)):
        (output/name).write_text(json.dumps(value,indent=2,allow_nan=False)+'\n')
    return result

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--game',type=Path,required=True);p.add_argument('--common-metadata',type=Path,required=True)
    p.add_argument('--hand-poses',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--inventory',type=Path,help='Exact installed geometry inventory; refreshes all authored mesh references including scopes')
    p.add_argument('--asset',action='append',required=True);p.add_argument('--defer',action='append',default=[],metavar='ASSET=REASON')
    a=p.parse_args(argv);metadata,mh=read(a.common_metadata);poses,ph=read(a.hand_poses)
    deferred={}
    for value in a.defer:
        asset,sep,reason=value.partition('=')
        if not sep or not asset or not reason or asset in deferred:raise ValueError('Unique ASSET=REASON required')
        deferred[asset]=reason
    inventory,ih=read(a.inventory) if a.inventory else (None,None)
    result=run(a.game,metadata['configuration'],metadata['mesh_bindings'],poses,set(a.asset),deferred,a.output,inventory)
    receipt={'input_sha256':{'common_metadata':mh,'hand_poses':ph,'inventory':ih},'assets':sorted(set(a.asset)),'deferred':deferred,
             'tool_sha256':digest_bytes(Path(__file__).read_bytes()),'game_assets_exported':False}
    (a.output/'reproduction.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(result['summary']));return 0
if __name__=='__main__':raise SystemExit(main())
