# Launcher sight input timing repair — October 1, 2026

The first headset pilot accepted manual recenter and launcher handle grip after
normal weapon cycling. Physical sight flipping failed: no apparent attachment
or mode change. The final native report recorded zero mode requests and four
gesture cancellations. Its first-192 sight samples covered only the beginning
of the session, so they cannot identify every later failed attempt.

The completed headset evidence is in
reports/headset-launcher-feedback-20261001-084351/acceptance.json.
The original implementation and binaries remain preserved in
reports/launcher-sight-20261001/checkpoint.json.

## Reproduced defects

BC2 can gather input multiple times per XR packet. SightFlip previously cleared
fresh-neutral arming on an idle duplicate when contact was unavailable or not
preferred. A subsequent intentional squeeze could consequently do nothing.
The focused regression fails before the fix and passes on x86/x64 with /W4 /WX.
Idle duplicate calls now preserve existing arming; they still cannot create a
grab, rearm, advance geometry, or commit an acknowledgement. Tracking, identity,
focus and time guards remain authoritative, and active contact loss still cancels.

Separately, the adapter used current XR squeeze with geometry from a prior
published rig generation. A reach and squeeze could be evaluated against the
previous distant hand. The shared SightFlipPackets helper pairs raw intent and
published geometry by generation and complete ownership, retaining original
deadlines. Current release or tracking loss takes priority over buffered input.
A matching native acknowledgement retains the existing expected-mode handoff;
the policy still validates its request token and actual target mode.

Neither correction changes controller calibration, recentering, accepted weapon
grip math, contact radius, sight geometry or native animation. Native geometry
evidence supports the existing hinge axis and positive opening direction.

## Verification

Policy cadence evidence: reports/sight-flip-cadence-20261001.
The native fixture now reaches and squeezes in the same XR packet, without the
old neutral dwell at the sight. It still exercises opening, launcher support
attachment/release and closing. Both builds pass: 40 x86 and 39 x64 suites.

Native capture090241-507 successfully grabs and issues one mode request using
matching input/contact generations; 240 stereo pairs, zero timeouts, no source,
packing or fallback errors. The game stays responsive and all hooks disable.
However, no native mode acknowledgement occurs, so the strict round-trip audit
fails and neither reverse switching nor launcher support is revalidated by this
run. The read-only ammunition snapshot taken after that run was empty, with the reflected native
AllowSwitchingToWeaponOutOfAmmo flag false. The eligibility callback rejects it.
That snapshot is after the request, not an exact request-time ammo observation.
No ammunition or eligibility state was changed.

Summary: reports/launcher-input-timing-20261001.json.
Strict failed audit: reports/launcher-input-timing-roundtrip-20261001.json.
Native ammo evidence: reports/launcher-empty-ammo-20261001.json.
Correction after session092339: the launcher is selectable by cycling and a
fresh read-only selected-mode snapshot has1/5 ammunition. Latest gestures produced
zero native requests, so empty ammo is not their explanation. The old0/0 reading
above is historical, not a current prerequisite or exact request-time diagnosis.
See reports/launcher-selection-audit-20261001.json and LAUNCHER-GEOMETRY-20261001.md.
Candidate checkpoint: reports/launcher-input-timing-20261001/checkpoint.json.

Headset feel remains unverified until a new test. The reproduced defects are not
claimed to be the sole explanation for the first headset failure. Grenade muzzle
translation remains disabled and projectile origin/impact verification is separate.
