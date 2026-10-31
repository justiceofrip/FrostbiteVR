"""Verify the shipped feed header against pinned source inputs, without rewriting assets."""
from pathlib import Path
import json,sys
ROOT=Path(__file__).resolve().parents[3]
sys.dont_write_bytecode=True
sys.path.insert(0,str(ROOT/'tools'))
import bc2_magazine_descriptor_manifest as descriptor
import bc2_mechanism_registry_header as exporter
jobs_root=Path('G:/Unity/VRMMO/bc2vr-task-worktrees/campaign-coordinator-20261028-r1/reports/coordinator-20261030-pipeline34/family-batch')
profile=ROOT/'profiles/mechanism-family41'
jobs,_=descriptor.load(jobs_root/'descriptor-jobs.json')
reviewed,_=descriptor.load(jobs_root/'exact-native-reviews.json')
annotations,_=descriptor.load(profile/'annotations.json')
admissions,_=descriptor.load(profile/'admissions.json')
evidence=Path('G:/Unity/VRMMO').resolve()
loaded={}
for name,pin in annotations['sources'].items():
    path=(evidence/pin['path']).resolve()
    if not path.is_relative_to(evidence):raise ValueError('Evidence source escaped root')
    loaded[name]=descriptor.load(path)
header,receipt=exporter.build(jobs,reviewed,annotations,loaded,admissions,header_kind='feed')
if not receipt['valid']:raise ValueError(receipt)
if header!=(profile/'Feed.generated.h').read_text():raise ValueError('Production feed header differs from pinned generator output')
print(json.dumps(dict(passed=True,generated_feed_header_matches=True,summary=receipt['summary'],native_admission_changed=False)))
