"""Select cached exact pistol magazine inputs; no assets/native review generated.

Root runs this with an explicit output directory, then the existing geometry
generator. Existing MP443_sp contact review remains exact to that configuration.
"""
import argparse
import copy
import hashlib
import json
import sys
from pathlib import Path

WORKTREE = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(WORKTREE / 'tools'))
from bc2_singlefire_magazine_proof import apply

ASSETS = ('M9', 'M9-3', 'M1911', 'MP443', 'MP443_sp')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--jobs', type=Path, help='Optional root consolidated descriptor jobs')
    args = parser.parse_args()
    plan = json.loads(Path(__file__).with_name('input-plan.json').read_text(encoding='utf-8'))
    documents = {}
    for name, source in plan['sources'].items():
        raw = Path(source['path']).read_bytes()
        if hashlib.sha256(raw).hexdigest() != source['sha256']:
            raise ValueError('Pinned input changed: ' + name)
        documents[name] = json.loads(raw)
    bindings = copy.deepcopy(documents['pistol_bindings'])
    bindings['profiles'] = [p for p in bindings['profiles'] if p['native_asset_name'] in ASSETS]
    bindings['profiles'] += [p for p in documents['m1911_bindings']['profiles'] if p['native_asset_name'] == 'M1911']
    bindings['gaps'] = []
    bindings['summary'] = {'selected_exact_configurations': 5, 'native_admission': False}
    meshes = copy.deepcopy(documents['pistol_meshes'])
    meshes['weapons'] = [w for w in meshes['weapons'] if w['native_name'] in ASSETS and w['native_name'] != 'M1911']
    meshes['weapons'] += [w for w in documents['m1911_meshes']['weapons'] if w['native_name'] == 'M1911']
    meshes['summary'] = {'selected_exact_configurations': 5, 'native_admission': False}
    configured = copy.deepcopy(documents['metadata']['configuration'])
    configured['resolved_weapons'] = [w for w in configured['resolved_weapons'] if w['native_name'] in ASSETS]
    configured['unresolved_weapons'] = []
    configured['filter'] = {'exact_assets': list(ASSETS)}
    jobs = copy.deepcopy(json.loads(args.jobs.read_text(encoding='utf-8')) if args.jobs else documents['original_jobs'])
    jobs['descriptors'] = [d for d in jobs['descriptors'] if d['identity']['assetName'] in ASSETS]
    jobs = apply(jobs, documents['singlefire_proof'])
    expected = {row['asset']: row for row in plan['configurations']}
    for name in ASSETS:
        b = [p for p in bindings['profiles'] if p['native_asset_name'] == name]
        m = [w for w in meshes['weapons'] if w['native_name'] == name]
        c = [w for w in configured['resolved_weapons'] if w['native_name'] == name]
        d = [w for w in jobs['descriptors'] if w['identity']['assetName'] == name]
        if not all(len(rows) == 1 for rows in (b, m, c, d)):
            raise ValueError('Expected one exact configuration: ' + name)
        pin = expected[name]
        for resource, guid, sha in [(b[0]['weapon']['resource'], b[0]['weapon']['instance_guid'], b[0]['weapon']['sha256']),
                                   (m[0]['resource'], m[0]['instance_guid'], m[0]['resource_sha256']),
                                   (c[0]['resource'], c[0]['instance_guid'], c[0]['resource_sha256']),
                                   (d[0]['identity']['weapon']['resource'], d[0]['identity']['weapon']['guid'], d[0]['identity']['weapon']['sha256'])]:
            if (resource, guid.lower(), sha) != (pin['configuration_resource'], pin['configuration_guid'].lower(), pin['configuration_sha256']):
                raise ValueError('Exact configuration mismatch: ' + name)
    geometry = documents['existing_geometry']
    sp = next(p for p in geometry['profiles'] if p['native_asset_name'] == 'MP443_sp')
    review = sp['geometry']['design']['paired_reload_grasp']['semantic_contact_review']
    outputs = {'reference-bindings.json': bindings, 'mesh-bindings.json': meshes,
               'configured-catalog.json': configured, 'selected-jobs.json': jobs,
               'existing-contact-reviews.json': [review],
               'existing-native-reviews.json': documents['existing_native_reviews']}
    args.output.mkdir(parents=True, exist_ok=True)
    for name, document in outputs.items():
        (args.output / name).write_text(json.dumps(document, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    print(json.dumps({'exact_configurations': 5, 'existing_contact_reviews': 1,
                      'existing_native_reviews': 1, 'native_admission_generated': False,
                      'output': str(args.output.resolve())}))


if __name__ == '__main__':
    main()
