"""Consolidate existing exact family jobs without rescanning archives or changing native data."""
import copy
import hashlib
import json
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(root / 'tools'))
import bc2_mechanism_registry_header as mechanism
import bc2_magazine_registry_header as native
import bc2_singlefire_magazine_proof as singlefire

plan_path = root / 'handoff/tasks/CHAT14-16/20261030-familybatch36/input-plan.json'
plan = json.loads(plan_path.read_text(encoding='utf-8'))
evidence_root = Path(plan['evidence_root'])
out = root / 'reports/coordinator-20261030-pipeline34/family-batch'
out.mkdir(parents=True, exist_ok=True)
cache = {}
consumed = {}

def read_pin(name):
    pin = plan['sources'][name]
    if name not in cache:
        raw = (evidence_root / pin['path']).read_bytes()
        assert hashlib.sha256(raw).hexdigest() == pin['sha256'], name
        cache[name] = json.loads(raw)
        consumed[name] = pin
    return cache[name]

rows = []
for config in plan['configurations']:
    ref = config['descriptor_job']
    row = mechanism.pointer(read_pin(ref['source']), ref['pointer'])
    assert row['identity']['weapon']['resource'] == config['configuration_resource']
    assert row['identity']['weapon']['guid'] == config['configuration_guid']
    assert row['identity']['weapon']['sha256'] == config['configuration_sha256']
    rows.append(copy.deepcopy(row))
jobs = dict(schema='fvr.bc2.magazine_descriptor_jobs', schema_version=1,
            runtime_enabled=False, registry_change=False, descriptors=rows)
jobs = singlefire.apply(jobs, singlefire.EXPECTED_PROOF)
assert len(jobs['descriptors']) == 29
assert all(native.eligible(row) for row in jobs['descriptors'])

def write(name, value):
    path = out / name
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    return hashlib.sha256(path.read_bytes()).hexdigest()

jobs_sha = write('descriptor-jobs.json', jobs)
# This is an offline code/data compatibility review of the existing dispatch
# classes. It does not turn authored configuration into a native runtime test.
review = dict(schema='fvr.bc2.mechanism_native_family_review.v1',
    reviewer='coordinator pipeline34', evidence_level='offline_source_and_exact_configuration_review',
    descriptor_jobs_sha256=jobs_sha, source_inputs=consumed,
    reviewed_classes=['AutomaticMagazineZeroBoltTransfer', 'SingleFireMagazineZeroBolt'],
    singlefire_instruction_proof_digest=singlefire.PROOF_DIGEST,
    invariants=['Exact immutable configuration and six timing words required.',
                'Existing server callback and own three-copy convergence remain mandatory.',
                'Finite capacity and ammunition accounting remain per configuration.',
                'No native chamber, latch, or extra-round state is inferred.'],
    native_runtime_tested=False, headset_verified=False, installation_changed=False)
review_sha = write('native-family-review.json', review)
reviews = []
for row in jobs['descriptors']:
    is_slide = row['configuration']['values']['fireLogicType'] == 0
    entry = dict(key=row['key'], descriptor_digest=row['descriptor_digest'],
                 evidence_sha256=review_sha,
                 family_proof=native.SINGLE_FIRE_FAMILY if is_slide else native.FAMILY)
    if is_slide: entry['instruction_proof_digest'] = singlefire.PROOF_DIGEST
    reviews.append(entry)
native.build(jobs, reviews)
write('exact-native-reviews.json', reviews)

annotations = dict(schema=mechanism.SCHEMA, sources={}, profiles=[])
for relative in ('handoff/tasks/CHAT16/20261030-batch37/reviewed-annotations.json',
                 'handoff/tasks/CHAT14/20261030-batch37/reviewed-annotations.json',
                 'handoff/tasks/CHAT14/20261030-batch37/companion-contract-annotations.json'):
    path = root / relative
    if not path.exists(): continue
    part = json.loads(path.read_text(encoding='utf-8'))
    for name, pin in part['sources'].items():
        if name in annotations['sources']: assert annotations['sources'][name] == pin, name
        annotations['sources'][name] = pin
    annotations['profiles'].extend(part['profiles'])
by_key = {row['key']: row for row in jobs['descriptors']}
for row in annotations['profiles']:
    # The shared exact SingleFire proof adds its numeric word/timing record;
    # geometry identity and native identity key are unaffected.
    row['descriptor_digest'] = by_key[row['descriptor_key']]['descriptor_digest']
    assert row['enabled'] is False
write('reviewed-annotations.json', annotations)
loaded = {}
for name, pin in annotations['sources'].items():
    raw = (evidence_root / pin['path']).read_bytes()
    assert hashlib.sha256(raw).hexdigest() == pin['sha256'], name
    loaded[name] = (json.loads(raw), pin['sha256'])
for kind, filename in [('feed', 'Feed.candidates.h'), ('pistol_slide', 'Slide.candidates.h')]:
    header, receipt = mechanism.build(jobs, reviews, annotations, loaded, header_kind=kind)
    write('candidate-receipt.json', receipt)
    if not receipt['valid']:
        print(json.dumps([row for row in receipt['rows'] if not row['emitted']], indent=2)); raise SystemExit(2)
    (out / filename).write_text(header, encoding='utf-8')
(out / 'consumer.cpp').write_text(r'''
#include "Feed.candidates.h"
#include "Slide.candidates.h"
#include "Test.h"
#include <cstdio>
int main(){
    using namespace fvr::bc2;
    CHECK(generated::GeneratedFeedProfiles.size()==24);
    CHECK(generated::GeneratedPistolSlideProfiles.size()==5);
    unsigned companions=0;
    for(auto row:generated::GeneratedFeedProfiles){
        CHECK(!row.enabled&&!row.reviewed);
        // Pure offline validation of the exact candidate, not registration.
        row.enabled=row.reviewed=true;row.config.admission=ReloadDescriptorAdmission::ReviewedNative;
        if(!ValidateBc2FeedRegistration(row)){
            std::printf("invalid feed candidate: %.*s %.*s\n",int(row.config.assetName.size()),row.config.assetName.data(),
                int(row.config.assetPath.size()),row.config.assetPath.data());return 1;}
        companions+=row.geometry.coverCompanionCount;
    }
    CHECK(companions==3);
    for(auto row:generated::GeneratedPistolSlideProfiles){
        CHECK(!row.enabled&&!row.reviewed);
        row.enabled=row.reviewed=true;row.configuration.admission=ReloadDescriptorAdmission::ReviewedNative;
        if(!Bc2PistolSlideRegistrationValid(row)){
            std::printf("invalid slide candidate: %.*s\n",int(row.configuration.assetName.size()),row.configuration.assetName.data());return 1;}
    }
    std::puts("29 real configuration profiles pass shared C++ candidate contracts; no runtime admission granted.");
}
''', encoding='utf-8')
print(json.dumps(dict(exact_jobs=len(rows), reviewed_annotation_rows=len(annotations['profiles']),
                     enabled_rows=0, output=str(out))))
