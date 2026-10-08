"""Controlled static-head, equal-width asymmetric-FOV diagnostic.

Requires numpy and Pillow. This checks far-background projection alignment;
it does not establish physical IPD, close-object stereo, or headset comfort.
Exit 0: controlled projection check passes, 2: fails, 3: inconclusive.
"""
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image


def correlate(left, right, dx):
    lo, hi = max(20, 20-dx), min(460, 460-dx)
    a = left[30:170, lo:hi].ravel()
    b = right[30:170, lo+dx:hi+dx].ravel()
    a, b = a-a.mean(), b-b.mean()
    denominator = np.sqrt((a@a)*(b@b))
    return float(a@b/denominator) if denominator > 1e-9 else -1.0



def decode_geometry_camera(buffer, cameras):
    """Recognize sampled VP and either WVP/affine-world ordering by matrix agreement.

    Unknown layouts remain inconclusive. Never assume every shader buffer is VP.
    """
    raw = np.asarray(buffer, dtype=np.float64)
    if raw.size < 16 or not np.isfinite(raw[:16]).all():
        return {'matches_eye_camera': None, 'matrix_layout': 'unrecognized'}
    clip = raw[:16].reshape(4, 4)
    candidates = [('view_projection', clip)]
    if raw.size >= 32:
        world = raw[16:32].reshape(4, 4)
        if (np.isfinite(world).all() and np.allclose(world[3], [0, 0, 0, 1], rtol=0, atol=1e-6)
                and abs(np.linalg.det(world[:3, :3])) > 1e-6):
            candidates.append(('world_view_projection', clip @ np.linalg.inv(world)))
        if (np.allclose(clip[3], [0, 0, 0, 1], rtol=0, atol=1e-6)
                and abs(np.linalg.det(clip[:3, :3])) > 1e-6 and np.isfinite(world).all()):
            candidates.append(('world_then_world_view_projection', world @ np.linalg.inv(clip)))
    choices = []
    for layout, matrix in candidates:
        errors = [float(np.max(np.abs(matrix[[0, 1, 3]]-camera[[0, 1, 3]]))) for camera in cameras]
        choices.append((min(errors), layout, errors))
    error, layout, errors = min(choices)
    owner = int(np.argmin(errors)) if error <= .001 else None
    if sum(value <= .001 for value in errors) > 1:
        owner = None
    return {'matches_eye_camera': owner, 'matrix_layout': layout if owner is not None else 'unrecognized',
            'max_xyw_errors': errors}


def analyze(folder, pair=0, trace=None):
    rows = [json.loads(line) for line in (folder/'pairs.jsonl').read_text().splitlines()]
    record = next(row for row in rows if row.get('pair') == pair)
    sample = next(row for row in rows if row.get('tracking_sample') == record['tracking'])
    lf, rf = sample['fov']
    result = {'pair': pair, 'native_frame': record['native_frame'], 'status': 'inconclusive',
              'headset_verified': False, 'scope': 'static-head distant-background projection'}
    widths = [lf[1]-lf[0], rf[1]-rf[0]]
    if not sample['static_pose'] or abs(widths[0]-widths[1]) > 1e-6 or lf[2:] != rf[2:]:
        result['reason'] = 'Requires static pose, equal horizontal spans and identical vertical FOV'
        return result
    expected = 480*(lf[0]/widths[0]-rf[0]/widths[1])
    if abs(expected) < 10 or abs(expected) > 115:
        result['reason'] = 'Need a distinguishing FOV offset within the diagnostic search range'
        return result
    images = [np.asarray(Image.frombytes('RGBA', (1920, 1080),
              (folder/f'pair-{pair}-eye-{eye}.rgba').read_bytes()).convert('L').resize((480, 270)), dtype=np.float64)
              for eye in range(2)]
    scores = [(correlate(*images, dx), dx) for dx in range(-120, 121)]
    confidence, measured = max(scores)
    result.update(expected_dx_at_width_480=expected, measured_dx_at_width_480=measured,
                  best_correlation=confidence, zero_shift_correlation=correlate(*images, 0),
                  expected_shift_correlation=correlate(*images, round(expected)))
    result['status'] = ('pass' if abs(measured-expected) <= 3 else 'fail') if confidence >= .75 else 'inconclusive'
    if trace:
        manifest = json.loads((trace/'tracked-camera-evidence.json').read_text())
        result['gpu_sample_native_frame'] = manifest['native_frame']
        result['gpu_sample_matches_pixels'] = manifest['native_frame'] == record['native_frame']
        owners = []
        # The first full-size geometry constant buffer includes transposed VP at
        # byte zero. Native depth slicing changes Z; compare only X/Y/W rows.
        for eye in range(2):
            path = trace/f'eye-{eye}-vs-cb-0.bin'
            if not path.exists():
                owners.append({'eye': eye, 'status': 'unavailable'})
                continue
            cameras = []
            for camera in range(2):
                data = np.fromfile(trace/f'eye-{camera}-step-0-camera-0.bin', dtype='<f4')
                cameras.append(data[0x3a0//4:0x3a0//4+16].reshape(4, 4).T)
            owners.append({'eye': eye, **decode_geometry_camera(np.fromfile(path, dtype='<f4'), cameras)})
        result['first_geometry_draw'] = owners
        if result['gpu_sample_matches_pixels']:
            verified = all(owner.get('matches_eye_camera') == owner['eye'] for owner in owners)
            result['gpu_eye_ownership_verified'] = verified
            if not verified:
                result['status'] = 'fail' if any(o.get('matches_eye_camera') is not None and o['matches_eye_camera'] != o['eye'] for o in owners) else 'inconclusive'

    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', type=Path)
    parser.add_argument('--pair', type=int, default=0)
    parser.add_argument('--trace', type=Path)
    args = parser.parse_args()
    result = analyze(args.folder, args.pair, args.trace)
    destination = args.folder/f'projection-check-{args.pair}.json'
    destination.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))
    raise SystemExit({'pass': 0, 'fail': 2, 'inconclusive': 3}[result['status']])
