"""Preserve a rejected health baseline without admitting it or native actions.

The caller supplies the existing verified, repeated health snapshot. This helper
performs only report and ready-file publication; it makes no health/death claim.
"""
from copy import deepcopy
import json
from pathlib import Path


BASELINE_CHECKS = (
    'both_positive',
    'health_copies_equal',
    'static_config_bound_pass',
)


def publish_baseline(baseline, report, ready_path, *, pid,
                     process_start_filetime, start_tick_ms):
    """Keep the R2 admission gate and ready protocol, retaining failed evidence."""
    report['baseline_candidate'] = deepcopy(baseline)
    report['failed_checks'] = []
    for check in BASELINE_CHECKS:
        if not baseline[check]:
            report['failed_checks'].append(check)
    if report['failed_checks']:
        raise ValueError('Positive coherent pre-action health required')

    # This remains after the same three-condition gate, before ready publication.
    report['baseline'] = baseline
    ready_path = Path(ready_path)
    ready_temp = ready_path.with_suffix('.tmp')
    if ready_temp.exists():
        raise ValueError('Ready temporary path already exists')
    with ready_temp.open('x', encoding='utf-8') as ready:
        json.dump(dict(schema='fvr.private.health_series395.ready.v1', pid=pid,
                       process_start_filetime=process_start_filetime,
                       start_tick_ms=start_tick_ms, positive_baseline=baseline,
                       source_sha256=report['source_sha256'], read_only=True),
                  ready, allow_nan=False)
    ready_temp.replace(ready_path)
