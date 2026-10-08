"""Validate recorded hand recovery, equip/reload and roomscale evidence offline."""
import argparse
import json
import math
from pathlib import Path


def read(folder):
    root = Path(folder)
    d = json.loads((root / 'native-trace.json').read_text())
    completion = json.loads((root / 'completion.json').read_text())
    g = d['gameplay']
    p = g['rig_publication']
    assert d['hooks_disabled'] and not completion['game_exited'] and not completion['new_crash_report']
    assert d['native_stream']['consumed'] == 240
    assert not any(d['native_stream'][k] for k in ('discarded', 'camera_restore_failures', 'gpu_failure_stage'))
    assert not any(p[k] for k in ('tracking_rejected', 'rejected', 'source_changes', 'packing_failures', 'fallback_failures'))
    assert p['hand_calibrations'] == [1, 1] and not p['native_animation_written']
    assert not g['body_read_failures'] and not g['mismatches']
    rows = p['hand_evidence']
    assert len(rows) >= 20
    residual = max(math.dist(a['target'], a['resolved']) for r in rows for a in r['arms'] if a['tracked'])
    reach = max(a['error'] for r in rows for a in r['arms'] if a['tracked'])
    assert all(abs(math.dist(a['target'], a['resolved']) - a['error']) < .001 for r in rows for a in r['arms'] if a['tracked'])
    return g, p, rows, {'report': str(root.resolve()), 'pairs': 240,
                       'tracked_poses': p['tracked_poses'], 'partial_poses': p['partial_poses'],
                       'hand_calibrations': p['hand_calibrations'], 'max_wrist_residual_m': residual, 'max_reach_clamp_m': reach}


def horizontal_delta(row, first, key):
    return math.hypot(row[key][0] - first[key][0], row[key][2] - first[key][2])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--recovery', required=True)
    ap.add_argument('--switch', required=True)
    ap.add_argument('--roomscale', required=True)
    args = ap.parse_args()
    g, p, rows, recovery = read(args.recovery)
    for key in ('left_only_action_samples', 'right_only_action_samples',
                'fire_with_left_tracking_missing', 'recovery_fire_suppressed'):
        assert g[key] > 0
        recovery[key] = g[key]
    assert recovery['max_reach_clamp_m'] < .001
    assert g['untracked_fire_samples'] == 0 and p['partial_poses'] > 0
    assert any(r['arms'][0]['tracked'] and not r['arms'][1]['tracked'] for r in rows)
    assert any(r['arms'][1]['tracked'] and not r['arms'][0]['tracked'] for r in rows)
    g, p, rows, switches = read(args.switch)
    assert switches['max_reach_clamp_m'] < .001
    weapons = [r['weapon'] for i, r in enumerate(rows) if i == 0 or r['weapon'] != rows[i-1]['weapon']]
    assert len(weapons) == 3 and weapons[0] == weapons[2] and weapons[0] != weapons[1]
    assert g['next_weapon_commands'] == g['previous_weapon_commands'] == 1
    assert len({r['owner_generation'] for r in rows}) == 1
    first = rows[0]
    native_sway = max(horizontal_delta(r, first, 'native_root') for r in rows)
    assert native_sway > .05
    drifts = []
    for side in range(2):
        assert all(r['arms'][side]['tracked'] for r in rows)
        assert max(math.dist(r['arms'][side]['grip'], first['arms'][side]['grip']) for r in rows) < 1e-6
        drift = max(math.hypot(*[(r['arms'][side]['target'][axis] - first['arms'][side]['target'][axis]) -
                               (r['actor_position'][axis] - first['actor_position'][axis]) for axis in (0, 2)]) for r in rows)
        assert drift < .001
        drifts.append(drift)
    switches.update(weapon_sequence=weapons, native_sway_m=native_sway,
                    hand_horizontal_drift_relative_to_actor_m=drifts)
    g, p, rows, roomscale = read(args.roomscale)
    assert g['body_follow_samples'] > 0 and p['partial_poses'] > 0
    consumed = max(abs(r['consumed'][0]) for r in g['body_follow_evidence'])
    assert .2 < consumed < .45
    # Shoulder anchors follow the physical torso even when native collider
    # movement is delayed or stopped by the lean radius.
    first = rows[0]
    shoulder_drifts = [max(math.hypot(*[(r['arms'][side]['anchor'][a] - first['arms'][side]['anchor'][a]) -
                                      (r['anatomy_root'][a] - first['anatomy_root'][a]) for a in (0, 2)]) for r in rows)
                       for side in range(2)]
    assert max(shoulder_drifts) < .001
    roomscale['shoulder_drift_relative_to_physical_torso_m'] = shoulder_drifts
    roomscale['maximum_consumed_x_m'] = consumed
    print(json.dumps({'passed': True, 'headset_tested': False, 'recovery': recovery,
                      'switch_and_reload': switches, 'roomscale': roomscale}, indent=2, allow_nan=False))


if __name__ == '__main__':
    main()
