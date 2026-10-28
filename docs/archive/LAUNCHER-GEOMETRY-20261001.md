# Launcher hinge lifetime repair — October 1, 2026

The latest headset run092339-724 detected six sight grabs. Every attempt ended
on release, without a native switch request. The user could select the launcher
by thumbstick cycling. Read-only native audit confirms selected40mmgl with1/5
ammunition and common eligibility code for ordinary cycling and action33.
Earlier ammo snapshots do not explain these failed gestures.

## Concrete defect and repair

During equipment settling, authored jntWpn_9 can briefly collapse onto jntWpn_1.
Seven such collapsed observations were pending attachment. Across737 settled
scoped-XM8/launcher samples, the hinge instead remained near
(0.032,0.054,-0.585)m in weapon coordinates. The old policy cached the first
accepted hinge for the full session, even after correct contacts were published.

Sight contact now requires explicit settled attachment. Immediately before an
idle grab, the adapter refreshes hinge and axis from the exact publication paired
with its input packet. Policy geometry remains immutable while manipulating,
awaiting acknowledgment or latched. Refresh preserves neutral arming and unique
request IDs. No arbitrary contact-radius or angle-threshold change is included.

A bounded64-gesture ring retains configured/native hinge, start/end hand position,
minimum/maximum signed angle, peak progress, longest pre-dispatch detent dwell,
request/commit and ending reason. This evidence survives periodic sample rollover.
The old attempts lack configured hinge and intermediate motion; this repair is
not claimed to be their proven sole cause. Wrist orientation alone is still
ignored by the current positional gesture. A future6DoF grasp should transform
an actual contact anchor through the paired controller pose.

## Validation

Both builds pass:40x86 and39x64 regression suites, including pending attachment
rejection, idle hinge refresh, preserved arming/request IDs and detent diagnostics.
Seven offline checker regressions also pass.

Native094105-804 / receiver094105 passed strict launcher-first roundtrip:
GL→XM8→GL, actions36→33,2requests/2acknowledgements/2commits,0cancellations.
240stereo pairs completed with0timeouts. Both configured pivots match their paired
native publication. Peak gesture angles0.511/0.492rad, pre-dispatch dwell78ms each.
Maximum authored right-grip error0.348mm; attached support error0.162mm, free-hand
error0.178mm. The support test visibly seats a10.4mm scripted displacement.
All hooks disabled; game responsive with no new crash report after the bounded
watch. These are native scripted results; headset acceptance is still required.

The optional -SightStartSecondary fixture preserves the original rifle-first path
and allows repeatable testing from an already selected launcher. The wrapper
enables support for this fixture without launching a conflicting second input
script. Strict audit: reports/launcher-geometry-roundtrip-20261001.json.
Candidate hashes: reports/launcher-geometry-20261001.json.
Incremental checkpoint: reports/launcher-geometry-20261001/checkpoint.json.
Accepted grip, controller calibration, recenter and native animation are preserved.
No ammunition, eligibility flags, native equipped pointers or desktop inputs are
modified. Grenade translated muzzle remains disabled.

Evidence:
- reports/headset-launcher-feedback-20261001-092339/acceptance.json
- reports/launcher-selection-audit-20261001.json
- Earlier implementation: reports/launcher-input-timing-20261001/checkpoint.json

Gesture summaries may end with ContactLost after a successful commit when BC2
begins the mode equip animation. This is an end-of-latched-contact reason, not a
cancelled request; committed=true and aggregate cancellations=0 distinguish it.
