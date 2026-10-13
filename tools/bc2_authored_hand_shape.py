"""Derive a reusable mechanism hand shape from an explicit BC2 clip/skeleton.

Only the authored starting endpoint is sampled; this does not assume the native
animation interpolator or grant mechanism, inventory or firing authority. JSON
and optional C++ output contain transform metadata, never original game assets.
"""
import argparse
import json
import re
from pathlib import Path

from bc2_weapon_animation_pipeline import Skeleton, Clip, multiply, inverse_rigid
from inspect_bc2_mesh_asset import Archive


def derive_hand_shape(skeleton, clip, hand):
    if hand not in ('LeftHand', 'RightHand'):
        raise ValueError('Explicit left or right hand required')
    names = [hand + finger + str(joint + 1)
             for finger in ('Thumb', 'Index', 'Middle', 'Ring', 'Pinky') for joint in range(3)]
    indices = {name: n for n, name in enumerate(skeleton.names)}
    needed = {hand, 'jntWpn_1', *names}
    if not needed <= indices.keys():
        raise ValueError('Required authored hand joints missing')
    for name in list(needed):
        index = skeleton.parents[indices[name]]
        while index >= 0:
            needed.add(skeleton.names[index])
            index = skeleton.parents[index]
    if not needed <= clip.tracks.keys():
        raise ValueError('Required tracks absent or unsupported')
    if any(curve.knots[0] < 0 for name in needed for curve in clip.tracks[name]):
        raise ValueError('Starting endpoint would require interpolation')
    for name in names:
        expected = hand if name.endswith('1') else name[:-1] + str(int(name[-1]) - 1)
        parent = skeleton.parents[indices[name]]
        if parent < 0 or skeleton.names[parent] != expected:
            raise ValueError('Authored hand topology differs')
    pose = clip.evaluate(skeleton, 0., [hand, *names])
    inverse = inverse_rigid(pose['weapon_relative'][hand])
    matrices = [multiply(pose['weapon_relative'][name], inverse) for name in names]
    for matrix in matrices:
        inverse_rigid(matrix)  # Check that every local shape transform is rigid.
    return dict(schema='fvr.bc2.authored_mechanism_hand.v1',
                rig_fingerprint=skeleton.fingerprint, clip_sha256=clip.sha256,
                pose='authored_start_endpoint', time_seconds=0., hand=hand,
                source_curve_status=pose['bone_evaluation_status'], runtime_interpolation_used=False,
                wrist_in_weapon=pose['weapon_relative'][hand], joint_names=names,
                wrist_from_joint=matrices, native_ammunition_authority=False)


def shape_header(record, symbol):
    if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', symbol):
        raise ValueError('Invalid generated symbol')
    if record['schema'] != 'fvr.bc2.authored_mechanism_hand.v1' or len(record['wrist_from_joint']) != 15:
        raise ValueError('Authored shape schema')
    def number(value):
        text = format(value, '.9g')
        return text + ('f' if any(c in text for c in '.eE') else '.f')
    lines = ['#pragma once', '#include "fvr/interaction/AuthoredHandPose.h"', 'namespace fvr::bc2 {',
             '// Authored starting hand shape; presentation metadata only.',
             '// Clip SHA256: ' + record['clip_sha256'],
             'inline const interaction::AuthoredHandShape ' + symbol + '=[] {',
             '    interaction::AuthoredHandShape out;out.counts={3,3,3,3,3};']
    for n, matrix in enumerate(record['wrist_from_joint']):
        inverse_rigid(matrix)
        values = '{{{' + ','.join('{' + ','.join(number(x) for x in matrix[i*4:i*4+4]) + '}' for i in range(4)) + '}}}'
        lines.append(f'    out.wristFromJoint[{n//3}][{n%3}]={values};')
    return '\n'.join(lines + ['    return out;', '}();', '}']) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--clip', required=True)
    parser.add_argument('--skeleton-archive', type=Path, required=True)
    parser.add_argument('--skeleton-resource', required=True)
    parser.add_argument('--hand', choices=('LeftHand', 'RightHand'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--header', type=Path)
    parser.add_argument('--symbol')
    args = parser.parse_args()
    if bool(args.header) != bool(args.symbol):
        parser.error('--header and --symbol must be supplied together')
    archive = Archive(args.archive)
    skeleton_archive = Archive(args.skeleton_archive)
    clip = Clip(archive.read_selected([args.clip])[args.clip])
    skeleton = Skeleton(skeleton_archive.read_selected([args.skeleton_resource])[args.skeleton_resource])
    record = derive_hand_shape(skeleton, clip, args.hand)
    record.update(archive=str(args.archive), archive_index_sha256=archive.index_sha256, clip=args.clip,
                  skeleton_archive=str(args.skeleton_archive), skeleton_resource=args.skeleton_resource)
    rendered = shape_header(record, args.symbol) if args.header else None
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, indent=2) + '\n')
    if args.header:
        args.header.parent.mkdir(parents=True, exist_ok=True)
        args.header.write_text(rendered)
    print(json.dumps(dict(output=str(args.output), joints=15, hand=args.hand, native_authority=False)))


if __name__ == '__main__':
    main()
