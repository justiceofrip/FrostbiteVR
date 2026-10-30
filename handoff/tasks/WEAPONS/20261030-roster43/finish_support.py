"""Complete the two missing roster support keys from the existing exact source packet."""
import argparse
import importlib.util
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'tools'))
import bc2_authored_grip_bindings as grip
from bc2_authored_support_bindings import derive_explicit_contact_policy, header

spec = importlib.util.spec_from_file_location('support43_sources', ROOT / 'handoff/tasks/WEAPONS/20261030-support43/generate.py')
sources = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sources)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--game', type=Path, required=True)
    p.add_argument('--bindings', type=Path, required=True)
    args = p.parse_args()
    document = sources.load(args.bindings)
    assets = {'G3', 'M1A1 Thompson'}
    selected = [r for r in document['profiles'] if r['native_asset_name'] in assets]
    if len(selected) != 8:
        raise ValueError('Expected all eight exact configurations')
    exact = sources.ExactSources(args.game)
    resources = exact.validate(document, assets)
    manifest = sources.load(ROOT / 'handoff/tasks/WEAPONS/20261030-support43/authoring-policy.json')
    policy = {**manifest['policy'], 'input_binding_document_digest': grip.digest(document),
              'source_binding_digests': sorted(r['binding_digest'] for r in selected)}
    result = derive_explicit_contact_policy(document, assets, resources, policy, enable_contact=True)
    if len(result['profiles']) != 2:
        raise ValueError('Expected two exact support selectors')
    sources.write(Path(__file__).with_name('support-completion.json'), result)
    (ROOT / 'profiles/weapons43/Support.completion.generated.h').write_text(header(result), encoding='utf8')
    print(json.dumps({'support_keys': 2, 'exact_configurations': 8, 'native_claim': False}))


if __name__ == '__main__':
    main()
