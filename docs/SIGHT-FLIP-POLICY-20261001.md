# Portable physical sight-flip policy â€” October 1, 2026

`include/fvr/interaction/SightFlip.h` provides a pure, header-only interaction
policy. It does not enable the BC2 launcher, move a sight, select a slot, or write
native state. Verified native sight/handle geometry and mode dispatch are still
required before integration. Existing SPAS/XM8 behavior is unchanged.

The adapter supplies a sight pivot and unit hinge axis in weapon-local metres,
the tracked hand in that same frame, measured contact distance, configured grab
and hold radii, signed detent threshold/hysteresis, per-sample angular bound,
dwell, gesture/ack timeouts and maximum tracking age. Geometry and timing fields
default to invalid zero values: there is no guessed BC2 pivot or activation angle.
Test dimensions and times are synthetic fixtures, not a native profile.

A neutral squeeze arms the button. A fresh squeeze past the press threshold can
grab only inside the supplied contact radius and with a non-degenerate lever arm.
Holding has wider contact and squeeze release thresholds. Squeezing away from the
sight cannot grab later while still held; release is required to rearm.

Hand positions are projected onto the plane perpendicular to the hinge axis.
Signed angular steps use `atan2(axis dot (previous cross current), previous dot
current)` and accumulate up to a half-turn. Positive rotation opens primary to
secondary mode; negative rotation closes it. The adapter chooses axis direction
from the actual mechanism. Moving a weapon without changing weapon-local hand
position cannot activate the gesture. Implausible steps or degenerate radial
geometry cancel it.

Crossing the configured angle starts an intentional detent dwell. Movement within
the hysteresis band retains that dwell; falling below the lower boundary clears
it. After dwell, exactly one semantic request is emitted. No native success is
predicted and no automatic retry is performed. A `committedMode` event occurs only
when a fresh sample supplies both the matching request ID and the authoritative
target native mode. A mode match alone, old request ID, duplicate input or late
acknowledgement cannot commit a pending interaction. Release is required after
completion before another flip can begin.

Owner identity comprises actor, actor generation, **semantic physical item**, and
tracking space. BC2 may use separate internal slots for the rifle and launcher;
that expected mode transition must preserve the verified physical-item identity.
An unrelated item change cancels. The adapter must not use raw backend slot
identity as the semantic equipped key or issue acknowledgement merely because a
selection command was sent. It must correlate actual native completion with the
request's item/owner context.

One acknowledged-transition exception is explicit: switching internal mode can
invalidate old equipment-scoped contact for a frame before the new pose is
published. A matching authoritative acknowledgement may commit without that old
contact geometry, after fresh-sequence, same-identity, tracking/focus, release and
timeout checks. This does not fabricate contact. Missing contact without the
matching acknowledgement still cancels, and an unrelated physical item change
cancels before acknowledgement processing.

Tracking/focus/contact loss, release, unexpected identity or native-mode changes,
clock/sequence rollback, stale input, invalid data and bounded timeouts abort an
active gesture. Pending cancellation reports the exact request ID. The adapter
must cancel an undispatched request and reconcile native state if already
dispatched; this policy cannot undo a native transition. Native mode remains
authoritative. `nowNs` is monotonic caller time even for repeated tracking packets,
so stale packets cannot leave an operation pending forever. Duplicate packets do
not create button/gesture/ack edges.

The active-gesture config is immutable; validated pivot/axis can refresh while idle
without resetting arming or request IDs. Replacing the policy instance requires
cancelling any outstanding adapter request and clearing acknowledgement state.
Request IDs are monotonic per instance; adapters must not carry acknowledgements
between destroyed/recreated policy instances.

`tests/SightFlipTests.cpp` covers opening and closing, axis sign and arbitrary
pivot axes, contact and squeeze hysteresis, detent jitter/reset, release/rearm,
wrong-direction and implausible motion, malformed geometry, fresh versus duplicate
input, tracking/owner/equipment/space cancellation, unrelated native mode changes,
old/wrong/late acknowledgements, gesture timeout and acknowledgement timeout.
Standalone x86 and x64 builds, including the missing-old-contact acknowledgement
regression, pass with `/W4 /WX`; binaries/results live under
`reports/sight-flip-policy-20261001`. `SightFlipTests` is registered with CTest;
the root agent owns full coordinated project builds and native validation.

The original standalone result has since been followed by the native campaign pilot described in LAUNCHER-PILOT-20261001.md. Headset acceptance remains pending.

## BC2 contact observation added

`src/games/bc2/Bc2SightContact.h` contains a pure contact measurement helper.
`Bc2RigPublication` exposes `WeaponShotFrame::sight` through the existing shot
publication and
`ReadSightContact(soldier, weak, weapon, ownerGeneration, space)`.
The result contains validity, generation/deadline, hinge pivot/axis, raw hand in
weapon-local metres, and contact distance.

The binding accepts only exact `XM8_sp_s`/`40mmgl`, source rig fingerprint
`fnv1a64:a7f219a1426216ab`, weapon root `jntWpn_1`, and direct child
`jntWpn_9`. The fingerprint covers ordered names, parents and canonical inverse
binds. The root agent correlated `jntWpn_9` with the visible rear ladder through
native source captures and image projection in
`reports/native-trace-20261001-081204-659` and paired images in
`reports/native-ipc-20261001-081204` (pairs 45/90). The observed ladder extends
approximately 0.11 metres along joint-local negative Z; joint row zero supplies
the hinge axis. This length is a pilot contact parameter, not an asset rewrite.

The native sight pose is transformed relative to the native weapon root. The
independent `targets->left` is transformed relative to the *placed* weapon, before
any visible support-hand attachment. Distance is measured to the bounded sight
segment, not an infinite ray. Owner/item/space/generation/deadline guards match
existing immutable shot publication; raw left-tracking loss invalidates sight
reads immediately. Native sight matrices and animation remain untouched.

`tests/Bc2SightContactTests.cpp` covers translated/rotated weapon frames, the native
90-degree fold, world-unit conversion, both segment endpoints, configurable
segment length, independent free-hand distance, asset/rig/name/parent rejection,
hidden/invalid geometry, and fingerprint sensitivity. Both architectures pass
standalone with `/W4 /WX`. The root agent registers this test and runs full/native
validation. Contact integration alone does not enable the physical mode gesture
or establish headset acceptance.

Final integrated native evidence: reports/launcher-sight-20261001.json; two physical gestures and two commits, native support seating/release and exact source preservation verified. Headset feel and grenade firing origins remain separate.

## Headset timing repair

Idle duplicate calls preserve existing neutral arming, while current owner/focus/
tracking/time guards still run and active contact loss still cancels. The shared
SightFlipPackets helper matches published geometry to raw input from its exact
generation; current release/loss overrides history. See
LAUNCHER-INPUT-TIMING-20261001.md for regressions and the pending ammo-enabled
mode round-trip/headset confirmation.

## Headset acceptance and grasp preview

The geometry repair was headset-accepted in094621:8requests/acks/commits. The next
candidate adds authored palm contact and portable SightGrasp visual attachment,
without changing request/detent policy. See SIGHT-GRASP-20261001.md. Free-hand
open/curl/capacitive animation remains deferred per the user.
