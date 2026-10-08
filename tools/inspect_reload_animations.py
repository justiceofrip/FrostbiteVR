"""Batch the shared animation decoder; emit metadata, never curves or game assets."""
import argparse
from collections import Counter
import hashlib
import importlib
import json
from pathlib import Path
import sys

from build_manual_reload_catalog import animation_hints, animation_evidence, document, resource, sha


def run(game, inventory, inspect_archive):
    document(inventory, 'fvr.bc2.installed_weapon_inventory', 'asset_profiles')
    root = game.resolve()
    clips, gaps, seen = [], [], set()
    for archive in inventory['archives']:
        relative = resource(archive['archive'])
        if relative.casefold() in seen:
            raise ValueError('Duplicate inventory archive')
        seen.add(relative.casefold())
        if not any(animation_hints(a['name']) for a in archive.get('animations', [])):
            continue
        path = (root / relative).resolve()
        if not path.is_relative_to(root):
            raise ValueError('Archive leaves game root')
        try:
            batch = inspect_archive(path, game_root=root)
            wanted = {resource(a['name']).casefold() for a in archive.get('animations', []) if animation_hints(a['name'])}
            selected = []
            for clip in batch['clips']:
                if resource(clip['resource']).casefold() not in wanted:
                    continue
                if (resource(clip['archive_relative']).casefold() != relative.casefold() or
                        sha(clip['archive_index_sha256']) != sha(archive['index_sha256'])):
                    raise ValueError('Decoded archive differs from inventory snapshot')
                # Use catalog whitelist: controls, vertices and evaluations never
                # leave the decoder through this derived metadata exporter.
                evidence = animation_evidence([dict(schema='fvr.bc2.authored_animation_batch', schema_version=1, clips=[clip])])
                entry = next(iter(evidence.values()))
                selected.append(dict(archive=relative, archive_relative=relative,
                    archive_index_sha256=archive['index_sha256'], resource=clip['resource'],
                    resource_kind=clip['resource_kind'], **entry))
            present = {c['resource'].casefold() for c in selected}
            if present != wanted:
                raise ValueError('Inventory animation occurrence missing from decoded archive')
            clips.extend(selected)
        except (ValueError, KeyError, OSError) as exc:
            gaps.append(dict(archive=relative, reason=str(exc)))
    return dict(schema='fvr.bc2.authored_animation_batch', schema_version=1, clips=clips,
        archive_gaps=gaps, inventory_archives=len(seen), runtime_admission=False, exported_assets=False,
        summary=dict(clip_occurrences=len(clips), unique_resource_versions=len({(c['resource'].casefold(), c['resource_sha256']) for c in clips}),
                     statuses=dict(Counter(c['status'] for c in clips)), archive_gaps=len(gaps)),
        limits=['Archive cooccurrence is not an authored animation-tree or current native mesh binding.',
                'Varying authored controls do not identify handle/bolt/pump semantic roles or chamber state.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('game', 'inventory', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--decoder-tools', type=Path, help='Shared animation tools directory; defaults beside this script.')
    parser.add_argument('--asset-tools', type=Path, help='Shared bounded Archive reader directory if separate.')
    args = parser.parse_args()
    for p in (args.asset_tools, args.decoder_tools):
        if p is not None:
            sys.path.insert(0, str(p.resolve()))
    module = importlib.import_module('bc2_weapon_animation_pipeline')
    dependency_paths = [Path(module.__file__)] + [Path(importlib.import_module(n).__file__) for n in ('bc2_granny_resource', 'bc2_granny_curves', 'inspect_bc2_mesh_asset')]
    before = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in dependency_paths}
    data = args.inventory.read_bytes()
    if len(data) > 64 * 1024 * 1024:
        raise ValueError('Inventory exceeds 64 MiB bound')
    result = run(args.game, json.loads(data), module.inspect_archive)
    if before != {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in dependency_paths}:
        raise ValueError('Decoder changed during extraction')
    result.update(inventory_sha256=hashlib.sha256(data).hexdigest(), decoder_sources=before)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    print(json.dumps(result['summary'], sort_keys=True))


if __name__ == '__main__':
    main()
