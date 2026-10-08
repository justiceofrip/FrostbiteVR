"""Audit exact magazine descriptors against measured geometry and generate PRIVATE offline probes.

No registry review is minted. Disabled registry is emitted by the existing exporter;
the separate fixture registry models native grants solely in a standalone test EXE.
Never pass the fixture header to a game/native build.
"""
from __future__ import annotations
import argparse,hashlib,json,re,sys
from pathlib import Path

LIMIT=128*1024*1024
def load(path):
 data=path.read_bytes()
 if len(data)>LIMIT:raise ValueError('Input size bound')
 return json.loads(data),hashlib.sha256(data).hexdigest()

def audit(jobs,geometry_sets,registry):
 if jobs.get('schema')!='fvr.bc2.magazine_descriptor_jobs':raise ValueError('Expected exact descriptor jobs')
 profiles={}
 for data in geometry_sets:
  if data.get('schema')!='fvr.bc2.authored_magazine_geometry':raise ValueError('Expected measured geometry schema')
  for p in data['profiles']:
   profiles.setdefault(p['native_asset_name'],[]).append(p)
 rows=[]
 for d in jobs['descriptors']:
  if d['source_class'] not in ('wcAssault','wcSmg','wcLmg'):continue
  c=d['configuration'];g=profiles.get(c['assetName'],[]);compatible=registry.eligible(d)
  gaps=list(d['deferred_reasons'])
  if not g:gaps.append('missing_in_supplied_batch_geometry')
  if g:
   gaps+=['main_grip_and_support_native_proof','current_native_configuration_and_three_branch_cycle_receipts',
          'native_mesh_skin_and_rig_identity','native_magazine_render_and_empty_control_acceptance','headset_acceptance']
  rows.append({'key':d['key'],'descriptor_digest':d['descriptor_digest'],'asset':c['assetName'],'path':c['assetPath'],
               'class':d['source_class'],'dispatch_compatible':compatible,'measured_geometry_profiles':len(g),
               'native_bolt_values':{k:c['values'][k] for k in ('boltDelay','boltTime','holdBoltUntilFireRelease','holdBoltUntilZoomRelease')},
               'geometry_meshes':sorted({p['configured_mesh_path'] for p in g}),
               'fixture_candidate':compatible and bool(g) and not d['existing_baseline_reference'],
               'existing_baseline_reference':d['existing_baseline_reference'],
               'new_runtime_admission':False,'missing_admission':sorted(set(gaps))})
 return {'schema':'fvr.bc2.magazine_pipeline_coverage.v1','rows':rows,
         'summary':{'descriptors':len(rows),'measured_names':len(profiles),'fixture_rows':sum(r['fixture_candidate'] for r in rows),
                    'fixture_names':sorted({r['asset'] for r in rows if r['fixture_candidate']}),'new_runtime_admissions':0},
         'scope':{'supplied_measured_names':sorted(profiles),
                  'existing_baseline_exact_references':[{'asset':r['asset'],'path':r['path'],'key':r['key']} for r in rows if r['existing_baseline_reference']],
                  'missing_geometry_means':'Absent from these supplied AR/SMG and detachable-drum batches; not a project-wide evidence verdict.',
                  'outside_scope':'Accepted AEK/scoped-XM8 geometry and separate belt-fed authored mechanism evidence are not supplied to this batch.'},
         'limits':['Fixture native receipts are mocked; passing portable behavior does not prove native execution.',
                   'Authored bolt data is preserved and remains deferred; no automatic normalization.',
                   'Measured magazine contact is independent of ordinary weapon grip/support admission.']}

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

def prepare(jobs,geometry_sets,headers,registry,out):
 coverage=audit(jobs,geometry_sets,registry);out.mkdir(parents=True,exist_ok=False)
 names={r['asset'] for r in coverage['rows'] if r['fixture_candidate']}
 selected=dict(jobs);selected['descriptors']=[d for d in jobs['descriptors'] if d['configuration']['assetName'] in names and registry.eligible(d) and not d['existing_baseline_reference']]
 disabled,summary=registry.build(selected)
 (out/'DisabledRegistry.h').write_text(disabled)
 # PRIVATE standalone fixture only. This does not use the review interface and
 # is never emitted as a production candidate or native evidence document.
 fixture=disabled.replace('ReloadDescriptorAdmission::Candidate','ReloadDescriptorAdmission::ReviewedNative').replace('MagazineCycleAdmission::Candidate','MagazineCycleAdmission::ReviewedReload11Transfer12')
 fixture=re.sub(r'("[0-9a-f]{64}",)false(\},)',r'\1true\2',fixture)
 (out/'FixtureRegistry.h').write_text('// PRIVATE MOCK-NATIVE TEST FIXTURE. NEVER ENABLE IN GAME.\n'+fixture)
 merged=['// PRIVATE measured geometry, unchanged profile bodies; offline probe only.']
 arrays=[]
 for n,h in enumerate(headers):
  text=h.read_text();count=int(re.search(r'std::array<MagazineGeometryProfile,(\d+)> ExperimentalMagazineGeometry',text)[1])
  text=text.replace('ExperimentalMagazineGeometry',f'CoverageGeometry{n}');merged.append(text);arrays.append((n,count))
 total=sum(c for _,c in arrays)
 merged+=['namespace fvr::bc2::generated {',f'inline const std::array<MagazineGeometryProfile,{total}> ExperimentalMagazineGeometry=[] {{',f' std::array<MagazineGeometryProfile,{total}> result{{}};unsigned n=0;']
 merged += [f' for(const auto& p:CoverageGeometry{i})result[n++]=p;' for i,_ in arrays]
 merged+=[' return result; }();','}']
 (out/'MeasuredGeometry.h').write_text('\n'.join(merged)+'\n')
 coverage['fixture_generation']={'registry_rows':summary['rows'],'measured_geometry_rows':total,'production_enabled_rows':summary['enabled']}
 return coverage

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--jobs',type=Path,required=True)
 p.add_argument('--geometry',type=Path,action='append',required=True);p.add_argument('--header',type=Path,action='append',required=True)
 p.add_argument('--private-output',type=Path);p.add_argument('--probe-results',type=Path,action='append',default=[])
 p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 sys.path.insert(0,str(a.source/'tools'));import bc2_magazine_registry_header as registry
 jobs,jh=load(a.jobs);sets=[];inputs={'jobs':jh}
 for g in a.geometry:data,h=load(g);sets.append(data);inputs[str(g)]=h
 for h in a.header:inputs[str(h)]=hashlib.sha256(h.read_bytes()).hexdigest()
 result=prepare(jobs,sets,a.header,registry,a.private_output) if a.private_output else audit(jobs,sets,registry)
 if a.probe_results:
  runs={str(p):[json.loads(line) for line in p.read_text().splitlines() if line.strip()] for p in a.probe_results}
  result=reconcile(result,runs)
 result['inputs']=inputs
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result['summary']))
if __name__=='__main__':main()
