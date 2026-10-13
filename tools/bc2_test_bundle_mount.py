"""Reversible SP_02/SP_03 fixture for installed multiplayer common resources.

Not an installer or player feature. Prepare a private backup/receipt, apply only
against the recorded original, then restore against the recorded candidate.
No native calls, executable patching, asset extraction or signature bypass.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path

SOURCE = b'BundleSource game:levels/sp_02/level-00.fbrb'
TARGETS = (
    b'BundleTarget package:levels/mp2_common/level-00.fbrb',
    b'BundleTarget game:levels/mp_common/level-00.fbrb',
)
EXE_SHA = '3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def candidate(original, level='sp_02', include_package_common=True):
    if level not in ('sp_02', 'sp_03'):
        raise ValueError('Unreviewed fixture level')
    source = ('BundleSource game:levels/' + level + '/level-00.fbrb').encode()
    if not original or len(original) > 2 * 1024 * 1024 or b'\0' in original:
        raise ValueError('Manifest size or encoding')
    original.decode('ascii')
    lines = original.splitlines(keepends=True)
    hits = [i for i, line in enumerate(lines) if line.rstrip(b'\r\n') == source]
    if len(hits) != 1:
        raise ValueError('Expected exactly one selected level section')
    start = hits[0]
    end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith(b'BundleSource ')), len(lines))
    entries = [line.rstrip(b'\r\n') for line in lines[start + 1:end] if line.strip()]
    expected = [('BundleTarget package:levels/' + level + '/level-00.fbrb').encode(),
                ('BundleTarget game:levels/' + level + '/level-00.fbrb').encode(),
                b'BundleTarget game:levels/sp_common/level-00.fbrb']
    if entries != expected:
        raise ValueError('Level dependencies changed; preserve external modifications')
    newline = b'\r\n' if lines[start].endswith(b'\r\n') else b'\n'
    if not lines[start].endswith(newline):
        raise ValueError('Unterminated source line')
    insert = start + 1
    while insert < end and lines[insert].strip():
        insert += 1
    targets = TARGETS if include_package_common else TARGETS[1:]
    return b''.join(lines[:insert] + [t + newline for t in targets] + lines[insert:])


def prepare(game, output, level='sp_02', include_package_common=True):
    game = game.resolve(strict=True)
    if sha((game / 'BFBC2Game.exe').read_bytes()) != EXE_SHA:
        raise ValueError('Unreviewed executable; no manifest changes')
    for rel in ('Dist/win32/levels/mp_common/level-00.fbrb', 'Package/levels/mp2_common/level-00.fbrb'):
        if not (game / rel).is_file():
            raise ValueError('Missing installed common archive: ' + rel)
    manifest = (game / 'Dist/win32/package.mft').resolve(strict=True)
    if not manifest.is_relative_to(game):
        raise ValueError('Manifest outside installation')
    original = manifest.read_bytes()
    patched = candidate(original, level, include_package_common)
    output.mkdir(parents=True, exist_ok=False)
    (output / 'original.mft').write_bytes(original)
    (output / 'candidate.mft').write_bytes(patched)
    receipt = dict(schema='fvr.bc2.test_bundle_mount.v1', manifest=str(manifest),
                   executable_sha256=EXE_SHA, original_sha256=sha(original), candidate_sha256=sha(patched),
                   additions=[t.decode() for t in (TARGETS if include_package_common else TARGETS[1:])],
                   level=level, include_package_common=include_package_common, applied=False, player_feature=False)
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return receipt


def change(output, restore=False):
    receipt = json.loads((output / 'receipt.json').read_text())
    if receipt['schema'] != 'fvr.bc2.test_bundle_mount.v1':
        raise ValueError('Receipt schema')
    original = (output / 'original.mft').read_bytes()
    patched = (output / 'candidate.mft').read_bytes()
    if sha(original) != receipt['original_sha256'] or sha(patched) != receipt['candidate_sha256'] or candidate(original, receipt.get('level','sp_02'), receipt.get('include_package_common',True)) != patched:
        raise ValueError('Backup or candidate changed')
    manifest = Path(receipt['manifest'])
    expected, target = (patched, original) if restore else (original, patched)
    if manifest.read_bytes() != expected:
        raise ValueError('Installation changed since preparation; refusing overwrite')
    manifest.write_bytes(target)
    if manifest.read_bytes() != target:
        raise IOError('Manifest verification failed; retain private backup')
    receipt['applied'] = not restore
    receipt['restored_exactly'] = restore
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return receipt


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('operation', choices=['prepare', 'apply', 'restore'])
    ap.add_argument('--fixture', type=Path, required=True)
    ap.add_argument('--game', type=Path)
    ap.add_argument('--level', choices=['sp_02','sp_03'], default='sp_02')
    ap.add_argument('--base-only', action='store_true', help='Isolate the installed base mp_common archive')
    args = ap.parse_args()
    if args.operation == 'prepare':
        if args.game is None:
            ap.error('prepare requires --game')
        result = prepare(args.game, args.fixture, args.level, not args.base_only)
    else:
        result = change(args.fixture, args.operation == 'restore')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
