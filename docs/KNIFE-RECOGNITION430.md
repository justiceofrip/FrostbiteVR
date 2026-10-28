# Selected knife recognition and opening acceptance

The human 0.1.10 run prepared no selected-knife attacks. Its periodic native
snapshots survived the unexpected process exit; the final report did not.
Those snapshots establish the failing stage but do not identify the exact
recognizer predicate that rejected the user's movements.

A deterministic regression now reproduces a real bug in that stage. BC2 can
gather input repeatedly before the next OpenXR controller sample. The renderer
independently reconstructs the wrist calibration from tracked pose and current
body orientation. Floating-point cancellation produces slightly different blade
coordinates even for the same physical controller sample. SelectedMeleeAttack
treated those derived differences as a changed controller packet and erased
its settling/stroke history.

The correction samples derived geometry once per input generation. Repeated
evaluation cannot advance the stroke, replace the sampled geometry or emit an
attack. Immutable controller fields, source times, hand/actor/weapon identity and
input epoch remain exact. A current native-readiness loss still disarms the
recognizer and requires fresh neutral input. Missing geometry invalidates
physical motion while preserving the existing trigger fallback. Pending native
attacks retain their original identity and outcome rules.

The integration regression calls the actual TrackedRig, wrist calibration,
selected render receipt and MapGeometry code with three controller headings
and changing body orientation between gathers. The old header fails the repeated
packet check. The corrected header observes 47/48/49 geometry changes while
emitting exactly one physical stroke in each case. Additional checks cover
readiness loss, geometry loss, trigger fallback, duplicate intents, pending
transactions and the optional recognizer observation API.

The existing periodic telemetry now records bounded preparation-gate, policy
and stroke-decision counts. It also includes geometry failure categories and
separately timestamped hand/source/native-readiness and last motion observations.
It adds no native hook, write or extra native memory read. Invalid floating
values serialize as JSON null. This allows diagnosis after a crash without
depending exclusively on the final report.

Focused x86 regressions, the native x86 syntax check and ten telemetry JSON
checks passed. Full builds and actual-game encounter checks are tracked in
CURRENT.md. These results do not yet establish that this was the only cause of
the human failure, a successful guard kill, campaign progression or headset
acceptance. The installed public launcher still uses 0.1.10.
