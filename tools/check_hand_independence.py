"""Check recorded native hand-independence evidence; no process access or writes."""
import argparse
import json
import math
from pathlib import Path


def analyze(path):
    report = json.loads(Path(path).read_text())
    publication = report['gameplay']['rig_publication']
    samples = publication['hand_evidence']
    if len(samples) < 10:
        raise ValueError('Too few arm samples')
    arms = []
    for side in range(2):
        rows = [sample['arms'][side] for sample in samples]
        vectors = ('grip', 'shoulder', 'target', 'resolved')
        if any(len(row[key]) != 3 or not all(map(math.isfinite, row[key]))
               for row in rows for key in vectors):
            raise ValueError('Malformed/nonfinite arm evidence')
        arm = {key + '_max_displacement': max(math.dist(row[key], rows[0][key]) for row in rows)
               for key in vectors}
        arm['max_wrist_target_error'] = max(math.dist(row['resolved'], row['target']) for row in rows)
        arm['max_reach_error'] = max(row['error'] for row in rows)
        arms.append(arm)
    return {'report': str(Path(path).resolve()), 'samples': len(samples), 'arms': arms,
            'calibrations': publication['calibrations'],
            'native_animation_written': publication['native_animation_written'],
            'errors': {key: publication[key] for key in
                       ('tracking_rejected', 'tracking_unavailable', 'rejected', 'source_changes', 'packing_failures')}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report')
    parser.add_argument('--baseline')
    args = parser.parse_args()
    current = analyze(args.report)
    passed = (current['calibrations'] == 1 and not current['native_animation_written']
              and not any(current['errors'].values())
              and all(arm['grip_max_displacement'] < 1e-6
                      and arm['shoulder_max_displacement'] > .1
                      and arm['max_wrist_target_error'] < .001
                      and arm['max_reach_error'] < .001 for arm in current['arms']))
    result = {'passed': passed, 'current': current, 'headset_tested': False}
    if args.baseline:
        result['baseline'] = analyze(args.baseline)
    print(json.dumps(result, indent=2, allow_nan=False))
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
