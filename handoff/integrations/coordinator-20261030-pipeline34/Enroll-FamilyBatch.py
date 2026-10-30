"""Enroll the pinned family batch; no extraction, build, game, or installation."""
import copy
import hashlib
import json
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(root / 'tools'))
import bc2_mechanism_registry_header as mechanism

family = root / 'reports/coordinator-20261030-pipeline34/family-batch'
magazine = root / 'reports/coordinator-20261030-family-enrollment41'
out = root / 'profiles/mechanism-family41'
out.mkdir(parents=True, exist_ok=True)
evidence_root = Path(json.loads((root / 'handoff/tasks/CHAT14-16/20261030-familybatch36/input-plan.json').read_text())['evidence_root'])

def read(path):
    return json.loads(path.read_text(encoding='utf-8'))

def pin(path):
    return dict(path=path.relative_to(evidence_root).as_posix(), sha256=hashlib.sha256(path.read_bytes()).hexdigest())

def write(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n', encoding='utf-8')

jobs = read(family / 'descriptor-jobs.json')
native_reviews = read(family / 'exact-native-reviews.json')
annotations = copy.deepcopy(read(family / 'reviewed-annotations.json'))
candidates = read(family / 'candidate-receipt.json')
assert candidates['valid'] and candidates['summary']['emitted_rows'] == 29
by_key = {row['key']: row for row in jobs['descriptors']}
by_candidate = {row['descriptor_key']: row for row in candidates['rows']}
builtins = {('M60_sp', 'Objects/Weapons/Handheld/BU_lmg_M60/SP_lmg_M60'),
            ('MP443_sp', 'Objects/Weapons/Handheld/RU_hg_MP443Grach/SP_hg_MP443Grach')}
retained, preserved = [], []
for row in annotations['profiles']:
    identity = by_key[row['descriptor_key']]['identity']
    if (identity['assetName'], identity['assetPath']) in builtins:
        preserved.append(dict(asset=identity['assetName'], configuration=identity['assetPath']))
    else:
        row['enabled'] = True
        retained.append(row)
assert len(retained) == 27 and len(preserved) == 2
annotations['profiles'] = retained

review = dict(schema='fvr.bc2.family_source_enrollment_review.v1',
    reviewer='coordinator pipeline34 / enrollment41',
    evidence_level='offline exact authored role, physical policy, and existing native dispatch review',
    inputs={name: pin(family / name) for name in ('descriptor-jobs.json', 'exact-native-reviews.json',
        'native-family-review.json', 'reviewed-annotations.json', 'candidate-receipt.json')},
    preserved_builtins=preserved,
    limitations=['Controller calibration and gameplay remain for the user integration test.',
                 'M1911 slide contact is authored policy, not a verified controller/native endpoint.',
                 'Exact runtime identity, three-copy convergence, persistent custody and accounting gates remain mandatory.'],
    actual_game_tested=False, headset_accepted=False, installed=False,
    rows=[dict(descriptor_key=row['descriptor_key'], profile_digest=by_candidate[row['descriptor_key']]['profile_digest'],
        native_class=row['native_class'], physical_reviewed=True, native_reviewed=True,
        role_review=row['role_review'], geometry_evidence=row['geometry_evidence']) for row in retained])
review_path = out / 'source-review.json'
write(review_path, review)
annotations['sources']['family_source_review'] = pin(review_path)
admissions = dict(schema=mechanism.ADMISSIONS, reviews=[])
for index, row in enumerate(retained):
    candidate = by_candidate[row['descriptor_key']]
    admissions['reviews'].append(dict(profile_digest=candidate['profile_digest'],
        descriptor_key=row['descriptor_key'], descriptor_digest=row['descriptor_digest'],
        native_class=row['native_class'], physical_reviewed=True, native_reviewed=True,
        reviewer=review['reviewer'], evidence=dict(source='family_source_review', pointer=f'/rows/{index}')))
loaded = {}
for name, source in annotations['sources'].items():
    path = evidence_root / source['path']
    assert hashlib.sha256(path.read_bytes()).hexdigest() == source['sha256'], name
    loaded[name] = (read(path), source['sha256'])
for kind, name in (('feed', 'Feed.generated.h'), ('pistol_slide', 'Slide.generated.h')):
    header, receipt = mechanism.build(jobs, native_reviews, annotations, loaded, admissions, header_kind=kind)
    assert receipt['valid'] and receipt['summary']['enabled_rows'] == 27
    assert all(row['profile_digest'] == by_candidate[row['descriptor_key']]['profile_digest'] for row in receipt['rows'])
    (out / name).write_text(header, encoding='utf-8')
write(out / 'admissions.json', admissions)
write(out / 'annotations.json', annotations)
write(out / 'mechanism-receipt.json', receipt)

batch = read(magazine / 'batch-receipt.json')
assert batch['complete'] and not batch['residuals'] and batch['cache']['total_rows'] == 57
cache = magazine / 'body-ammo.fvrprop'
assert hashlib.sha256(cache.read_bytes()).hexdigest() == batch['cache']['sha256']
for source, target in (('Registry.additions.generated.h', 'PistolRegistry.generated.h'),
                       ('Geometry.additions.generated.h', 'PistolGeometry.generated.h')):
    (out / target).write_bytes((magazine / source).read_bytes())

# Extend the existing composition while preserving its component namespaces.
registry = (root / 'profiles/opening426/CombinedRegistry.h').read_text()
registry = registry.replace('#include "NambuRegistry.generated.h"', '#include "../opening426/NambuRegistry.generated.h"')
registry = registry.replace('namespace fvr::bc2::generated {', '#define generated pistol41\n#include "PistolRegistry.generated.h"\n#undef generated\nnamespace fvr::bc2::generated {')
registry = registry.replace('opening426::MagazineNativeRegistrations.size()>', 'opening426::MagazineNativeRegistrations.size()+pistol41::MagazineNativeRegistrations.size()>')
registry = registry.replace(' return out;', ' for(const auto& p:pistol41::MagazineNativeRegistrations)out[n++]=p;\n return out;')
(out / 'CombinedRegistry.h').write_text(registry, encoding='utf-8')
geometry = (root / 'profiles/opening426/CombinedGeometry.h').read_text()
geometry = geometry.replace('#include "NambuGeometry.generated.h"', '#include "../opening426/NambuGeometry.generated.h"')
geometry = geometry.replace('namespace fvr::bc2::generated {', '#define ExperimentalMagazineGeometry Pistol41Geometry\n#include "PistolGeometry.generated.h"\n#undef ExperimentalMagazineGeometry\nnamespace fvr::bc2::generated {')
geometry = geometry.replace('Opening426Geometry.size()>', 'Opening426Geometry.size()+Pistol41Geometry.size()>')
geometry = geometry.replace(' return out;', ' for(const auto& p:Pistol41Geometry)out[n++]=p;\n return out;')
(out / 'CombinedGeometry.h').write_text(geometry, encoding='utf-8')
catalog = root / 'config/body-ammo-assets.json'
original = catalog.read_bytes()
new = (magazine / 'body-ammo-assets.json').read_text(encoding='utf-8').replace('\r\n', '\n')
if original.count(b'\r\n') > original.count(b'\n') // 2:
    new = new.replace('\n', '\r\n')
catalog.write_bytes(new.encode('utf-8'))
write(out / 'enrollment-receipt.json', dict(schema='fvr.bc2.family_enrollment.v1',
    additional_feed=23, additional_slide=4, total_feed=24, total_slide=5,
    magazine_batch=pin(magazine / 'batch-receipt.json'), cache=batch['cache'],
    combined_normal_build=False, installed=False, actual_game_tested=False, headset_accepted=False))
print('Enrolled 23 feed + 4 slide additions; preserved 2 builtins; magazine/cache metadata 57. Source only.')
