"""Audit exact magazine descriptors against measured geometry and generate PRIVATE offline probes.

No registry review is minted. Disabled registry is emitted by the existing exporter;
the separate fixture registry models native grants solely in a standalone test EXE.
Never pass the fixture header to a game/native build.
"""
from __future__ import annotations
import argparse,copy,hashlib,json,re,sys
from pathlib import Path
import bc2_authored_magazine_geometry as geometry
from bc2_weapon_component_closure import canonical,checked_profile,config,rig,sha

LIMIT=128*1024*1024
def load(path):
 data=path.read_bytes()
 if len(data)>LIMIT:raise ValueError('Input size bound')
 return json.loads(data),hashlib.sha256(data).hexdigest()

def _metadata_rows(documents,schemas):
 total=0
 for data in documents:
  if data.get('schema') not in schemas or data.get('schema_version')!=1 or data.get('runtime_admission') is not False:
   raise ValueError('Expected non-admitted authored metadata schema1')
  rows=data.get('profiles')
  if not isinstance(rows,list):raise ValueError('Expected metadata profiles')
  total+=len(rows)
  if total>4096:raise ValueError('Metadata profile count bound')
  yield from rows

def _not_admitted(row):
 if any(row.get(k) is True for k in ('native_admission','native_admitted','runtime_admitted','runtime_accepted','active_native_mesh_binding')):
  raise ValueError('Metadata native admission forbidden')

def _exact_geometry(geometry_sets,binding_sets):
 """Resolve original digest-checked configuration backlinks; never clone by name."""
 bindings={};joined={};excluded=[];names=set()
 for source in _metadata_rows(binding_sets,{'fvr.bc2.authored_grip_bindings','fvr.bc2.authored_reload_reference_bindings'}):
  try:
   h=checked_profile(source,'binding_digest');_not_admitted(source)
   config(source['weapon'])
   bindings[h]=source
  except (ValueError,KeyError,TypeError,AttributeError) as e:
   excluded.append({'kind':'binding','source_sha256':source.get('binding_digest'),'reason':str(e)})
 for row in _metadata_rows(geometry_sets,{'fvr.bc2.authored_magazine_geometry'}):
  try:
   names.add(row['native_asset_name'])
   h=checked_profile(row,'profile_digest');_not_admitted(row)
   source=bindings.get(sha(row['grip_binding_digest']))
   if source is None:raise ValueError('Missing exact grip digest backlink')
   identity=config(source['weapon'])
   if 'weapon' in row and config(row['weapon'])!=identity:raise ValueError('Magazine weapon contradicts exact backlink')
   if row['native_asset_name']!=source['native_asset_name'] or rig(row['rig_fingerprint'])!=rig(source['rig_fingerprint']):
    raise ValueError('Asset or rig backlink mismatch')
   if sha(row['skeleton_sha256'])!=sha(source['skeleton_sha256']) or sha(row['static_clip_sha256'])!=sha(source['animation_sha256']):
    raise ValueError('Skeleton/static-pose digest backlink mismatch')
   path=canonical(row['configured_mesh_path'])
   if path!=canonical(source['configured_mesh_path']):raise ValueError('Selected mesh backlink mismatch')
   meshes=[m for m in source['meshes'] if canonical(m['configured_mesh_path'])==path]
   if len(meshes)!=1 or not meshes[0].get('geometry'):raise ValueError('Missing unique selected mesh geometry backlink')
   mesh=meshes[0]['geometry']
   if sha(row['mesh_sha256'])!=sha(mesh['mesh_sha256']) or sha(row['lod_sha256']) not in {sha(l['data_sha256']) for l in mesh['lods']}:
    raise ValueError('Mesh/LOD digest backlink mismatch')
   derived=copy.deepcopy(row)
   derived['weapon']=copy.deepcopy(source['weapon'])
   derived['source_profile_digest']=h
   derived.pop('profile_digest')
   derived['profile_digest']=geometry.digest(derived)
   # The production exporter validates the complete authored role, matrices,
   # fingers and hashes, even when this is only an audit with no header output.
   geometry.cpp_header({'schema':'fvr.bc2.authored_magazine_geometry','schema_version':1,
                        'runtime_admission':False,'profiles':[derived]},[row['native_asset_name']])
   joined.setdefault((row['native_asset_name'],identity),{})[h]=derived
  except (ValueError,KeyError,TypeError,AttributeError) as e:
   excluded.append({'kind':'geometry','source_sha256':row.get('profile_digest'),'asset':row.get('native_asset_name'),'reason':str(e)})
 return joined,excluded,names

def _audit(jobs,geometry_sets,registry,binding_sets):
 # Validate the exact descriptor identities/digests using the disabled exporter.
 registry.build(jobs)
 profiles,excluded,names=_exact_geometry(geometry_sets,binding_sets)
 selected={}
 rows=[]
 for d in jobs['descriptors']:
  if d['source_class'] not in ('wcAssault','wcSmg','wcLmg'):continue
  c=d['configuration'];w=d['identity']['weapon']
  identity=config({'resource':w['resource'],'sha256':w['sha256'],'instance_guid':w['guid']})
  if canonical(c['assetPath']+'.dbx')!=identity[0]:raise ValueError('Descriptor path contradicts exact weapon identity')
  g=list(profiles.get((c['assetName'],identity),{}).values());compatible=registry.eligible(d)
  gaps=list(d['deferred_reasons'])
  if not g:gaps.append('missing_exact_configuration_geometry')
  if len(g)>1:gaps.append('ambiguous_exact_configuration_geometry')
  if g:
   gaps+=['main_grip_and_support_native_proof','current_native_configuration_and_three_branch_cycle_receipts',
          'native_mesh_skin_and_rig_identity','native_magazine_render_and_empty_control_acceptance','headset_acceptance']
  candidate=compatible and len(g)==1 and not d['existing_baseline_reference']
  if candidate:
   derived=copy.deepcopy(g[0])
   # Preserve the descriptor's consumed spelling after exact canonical identity
   # equality; this changes no variant/configuration and keeps C++ strcmp exact.
   derived['weapon']['resource']=c['assetPath']+'.dbx'
   derived.pop('profile_digest');derived['profile_digest']=geometry.digest(derived)
   selected[d['key']]=derived
  rows.append({'key':d['key'],'descriptor_digest':d['descriptor_digest'],'asset':c['assetName'],'path':c['assetPath'],
               'class':d['source_class'],'dispatch_compatible':compatible,'measured_geometry_profiles':len(g),
               'native_bolt_values':{k:c['values'][k] for k in ('boltDelay','boltTime','holdBoltUntilFireRelease','holdBoltUntilZoomRelease')},
               'geometry_meshes':sorted({p['configured_mesh_path'] for p in g}),
               'geometry_source_profiles':sorted(p['source_profile_digest'] for p in g),
               'fixture_candidate':candidate,
               'existing_baseline_reference':d['existing_baseline_reference'],
               'new_runtime_admission':False,'missing_admission':sorted(set(gaps))})
 return {'schema':'fvr.bc2.magazine_pipeline_coverage.v1','rows':rows,'geometry_excluded':excluded,
         'summary':{'descriptors':len(rows),'measured_names':len(names),'dispatch_compatible_paths':sum(r['dispatch_compatible'] for r in rows),
                    'exact_geometry_paths':sum(r['measured_geometry_profiles']==1 for r in rows),'fixture_rows':len(selected),
                    'fixture_names':sorted({r['asset'] for r in rows if r['fixture_candidate']}),'new_runtime_admissions':0},
         'scope':{'supplied_measured_names':sorted(names),'geometry_join':'exact configuration resource/hash/GUID and original binding digest; selected mesh/LOD/rig/skeleton/static-pose verified',
                  'selected_build_enrollment':'not_observed','native_acceptance':'not_observed',
                  'existing_baseline_exact_references':[{'asset':r['asset'],'path':r['path'],'key':r['key']} for r in rows if r['existing_baseline_reference']],
                  'missing_geometry_means':'Exact geometry/backlink unavailable in supplied metadata; not a project-wide evidence verdict.',
                  'outside_scope':'Accepted AEK/scoped-XM8 geometry and separate belt-fed authored mechanism evidence are not supplied to this batch.'},
         'limits':['Fixture native receipts are mocked; passing portable behavior does not prove native execution.',
                   'Authored bolt data is preserved and remains deferred; no automatic normalization.',
                   'Measured magazine contact is independent of ordinary weapon grip/support admission.',
                   'Content-hashed metadata backlinks do not establish complete immutable-package resource closure or native acceptance.']},selected

def audit(jobs,geometry_sets,registry,binding_sets=()):
 return _audit(jobs,geometry_sets,registry,binding_sets)[0]

def reconcile(coverage,results):
 expected={(r['asset'],r['path']) for r in coverage['rows'] if r['fixture_candidate']}
 accepted={}
 for label,rows in results.items():
  observed=set();mode=None
  for row in rows:
   key=(row['asset'],row['path'])
   if key in observed or key not in expected or row.get('pass') is not True:raise ValueError('Probe row mismatch or failure')
   observed.add(key)
   if mode is None:mode=row['mode']
   if row['mode']!=mode:raise ValueError('Mixed probe mode')
   if mode=='mock_native_measured_geometry' and not all(row.get(k) is True for k in ('partial_reload','empty_reload','exact_identity_denials')):
    raise ValueError('Missing actual consumer stages')
   if mode not in ('production_disabled','mock_native_measured_geometry'):raise ValueError('Unknown probe mode')
  if observed!=expected:raise ValueError('Incomplete probe coverage')
  accepted[label]={'mode':mode,'passed_rows':len(observed),'native_execution_proven':False}
 coverage['probe_runs']=accepted
 return coverage

def prepare(jobs,geometry_sets,headers,registry,out,binding_sets=()):
 coverage,profiles=_audit(jobs,geometry_sets,registry,binding_sets)
 if not profiles:raise ValueError('No exact-configuration geometry available for private fixtures')
 selected=dict(jobs);selected['descriptors']=[d for d in jobs['descriptors'] if d['key'] in profiles]
 disabled,summary=registry.build(selected)
 measured=geometry.cpp_header({'schema':'fvr.bc2.authored_magazine_geometry','schema_version':1,'runtime_admission':False,
                               'profiles':list(profiles.values())},sorted({p['native_asset_name'] for p in profiles.values()}))
 out.mkdir(parents=True,exist_ok=False)
 (out/'DisabledRegistry.h').write_text(disabled)
 # PRIVATE standalone fixture only. This does not use the review interface and
 # is never emitted as a production candidate or native evidence document.
 fixture=disabled.replace('ReloadDescriptorAdmission::Candidate','ReloadDescriptorAdmission::ReviewedNative').replace('MagazineCycleAdmission::Candidate','MagazineCycleAdmission::ReviewedReload11Transfer12')
 fixture=re.sub(r'("[0-9a-f]{64}",)false(\},)',r'\1true\2',fixture)
 (out/'FixtureRegistry.h').write_text('// PRIVATE MOCK-NATIVE TEST FIXTURE. NEVER ENABLE IN GAME.\n'+fixture)
 (out/'MeasuredGeometry.h').write_text('// PRIVATE regenerated exact-configuration geometry; offline probe only.\n'+measured)
 coverage['fixture_generation']={'registry_rows':summary['rows'],'measured_geometry_rows':len(profiles),'production_enabled_rows':summary['enabled'],
                                 'legacy_headers_ignored':len(headers),'emitter':'bc2_authored_magazine_geometry.cpp_header',
                                 'geometry_provenance':[{'key':k,'source_profile_digest':p['source_profile_digest'],
                                  'grip_binding_digest':p['grip_binding_digest'],'derived_profile_digest':p['profile_digest']} for k,p in profiles.items()]}
 return coverage

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--jobs',type=Path,required=True)
 p.add_argument('--geometry',type=Path,action='append',required=True)
 p.add_argument('--bindings',type=Path,action='append',default=[],help='Original content-hashed grip/reload binding metadata for exact configuration joins')
 p.add_argument('--header',type=Path,action='append',default=[],help='Legacy provenance only; headers are never copied into generated geometry')
 p.add_argument('--private-output',type=Path);p.add_argument('--probe-results',type=Path,action='append',default=[])
 p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.resolve() in {path.resolve() for path in [a.jobs,*a.geometry,*a.bindings,*a.header,*a.probe_results]}:
  p.error('Report must not overwrite input evidence')
 sys.path.insert(0,str(a.source/'tools'));import bc2_magazine_registry_header as registry
 jobs,jh=load(a.jobs);sets=[];bindings=[];inputs={'jobs':jh}
 for g in a.geometry:data,h=load(g);sets.append(data);inputs[str(g)]=h
 for b in a.bindings:data,h=load(b);bindings.append(data);inputs[str(b)]=h
 for h in a.header:inputs[str(h)]=hashlib.sha256(h.read_bytes()).hexdigest()
 result=prepare(jobs,sets,a.header,registry,a.private_output,bindings) if a.private_output else audit(jobs,sets,registry,bindings)
 if a.probe_results:
  runs={str(p):[json.loads(line) for line in p.read_text().splitlines() if line.strip()] for p in a.probe_results}
  result=reconcile(result,runs)
 result['inputs']=inputs
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result['summary']))
if __name__=='__main__':main()
