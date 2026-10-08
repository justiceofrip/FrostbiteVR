# Support-event retention and headset feedback — October 1, 2026

Headset session120128-090 received generally positive user feedback. The user
reported two early shotgun support-grip drops, then no recurrence, and could not
reload. On their explicit instruction only BC2XrHost138248 was stopped; native
trace120130-486 flushed and disabled its hooks, BC2 remained responsive and no new
crash report appeared. SteamVR was left open.

The original feedback, payload hashes, source-log hashes and limits are saved in
reports/headset-hand-input-feedback-20261001-120128/acceptance.json.
This is positive feedback with an unresolved intermittent support issue, not a
complete grip-fix or per-feature certification.

## What the logs establish

- 5,661 pairs published, 5,639 consumed and22 discarded; zero GPU-stage or camera
  restoration failures. These counters are not a claim of uninterrupted display.
- 15 support grabs and15 releases,10,839 held samples and1,079 supported-fire
  samples. Native source changes, packing/fallback and input-preservation errors
  were zero.
- Right-index posing produced2,825 publications with zero reported pose failures.
  Actual headset input contains both thumb and index active bits, with all four
  touched masks observed. General user feedback was positive; detailed sensor/
  anatomical calibration was not individually measured.
- One physical launcher mode request committed and received its native ack.
- Four rig rejections remain recorded:3 eye-base and1 output-deadline rejection.
  They cannot be matched to the reported two drops from the retained data.

The192-record rolling support buffer contains only the last23seconds, with no
release edge in that retained tail. Earlier release causes were overwritten.
Do not blame reload, controller tracking, distance thresholds or the user from
this evidence, and do not call the issue resolved because it stopped recurring.

A bounded read-only post-test capture found both SPAS and XM8 at0loaded/0reserve
in both inspected firing branches; launcher was0/1. This is consistent with
ammunition exhaustion preventing a reload. It is not an attempt-time snapshot
and does not establish that every reload-button press was delivered.
Source: reports/headset-hand-input-post-state-20261001-120128.json.

## Diagnostic change

Bc2Gameplay now retains the earliest512 grab/release events separately from the
rolling100ms support samples. Once full, it preserves those early events and
increments events_dropped; events_total and per-reason counts remain available.
An indefinitely long session is not promised unlimited detailed retention.

Each event records timestamp, previous grab token, actor/weak/item identity,
rig/reference-space generation, contact distance, squeeze, gameplay actions,
tracking flags and raw left/right grip positions. Policy release reasons retain
their existing enum values. Cancel flags identify sight ownership, invalid aim,
inactive action policy, Use, NextWeapon or PreviousWeapon.

Explicit support resets outside SupportGrip.Update are also retained while held:
not_on_foot, stale_input, native_inactive, invalid_input, focus_loss or
head_untracked. For these records reset_reason is non-null, event_ms is reset
time, and all sample/context fields describe the last held sample at sample.ms.
They must not be interpreted as current tracking flags or a new policy release.
forced_resets is separate from the existing policy release counter.

No grip thresholds, state transitions, native animation, ammo, input mapping or
gameplay decisions changed. There are no extra native memory reads for these
events; the existing resolved profile supplies the weapon category. The output
is appended after native callbacks drain; it is not per-frame file logging.

## Verification and next check

Both full builds pass48x86/47x64. Bounded native fixture121153-717 /
receiver121153 observes exactly3 grabs and3 releases. All six events survive
with token pairs1/2/3; two deliberate squeeze releases are reasonButton, and
the deliberate off-hand tracking loss while squeezed is reasonTracking.
Counters match, with no dropped events or source/packing/fallback errors.
The explicit forced-reset and512-event overflow runtime paths were not exercised.

All240 pairs arrived with0 timeouts. Two completions exceeded50ms, with maximum
124.219ms; one output-deadline rig rejection is retained. This diagnostic change
does not claim a rendering/performance improvement. Hooks disabled cleanly;
BC2 remained responsive with no new crash report.

Candidate: reports/support-event-retention-20261001.json.
The user's accepted hand/input baseline remains in
reports/hand-input-reload-20261001/checkpoint.json plus the script-only readiness
fix at reports/xr-launch-ready-20261001.
The new DLL changes diagnostics only and has its own native payload manifest.

At the next appropriate headset test, obtain legitimate ammo/checkpoint availability
before evaluating reload. Use normal SPAS firing/reload/support handling, retain
this event trace, and compare actual drop timestamps/actions/ownership. Physical
pump/manual reload is still disabled. Do not loosen release guards to conceal
an unclassified event.
