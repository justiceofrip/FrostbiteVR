# Reload presentation and live ammo counter — 207

Update: this checkpoint subsequently ran in the headset and was not accepted.
Its counter produced zero valid samples; chest flicker, full-magazine discard
recovery and reload/grip failures were reported. See [208 investigation](RELOAD-FEEDBACK-208.md)
for the final run evidence and current fixes. The preparation record below
describes validation available before that run.

## Actual headset evidence

The October 8 test ran frozen **206d**, not the new fixes below. The user
reported substantially greater stability, magazine ghosting while held, chest
ammo flickering, delayed fore-end grip after insertion, and a possible late
XM8 reload failure that was difficult to distinguish without visible counts.
The mod ended with permission; BC2 remained responsive and hooks disabled.
No new crash report or native restoration failure was recorded.

Private evidence: `headset206d-20261008/reports/session-20261008-062823-863`
and `native-trace-20261008-062825-912` under `bc2vr-recovery`.
Two replacement-magazine transactions completed (14/191 to 30/175 and
23/175 to 30/168), plus one original-magazine return and eight acknowledged
shell insertions. Rifle transfers took about 2.72 seconds each. These receipts
do not establish that every subsequent interaction worked.

The host submitted 8,023 pairs, with two async timeouts, 540 invalid body-prop
instances and zero body-prop renderer failures. Invalid instances are not a
count of failed frames. The native eye ring includes the same input/owner with
ammo available in eye zero but absent after eye one. Other pairs sampled
different input generations. The native renderer's zero magazine-fallback
count does not cover a missing target before palette admission.

## Shared fixes

- Support contact has its own narrow publication getter. A pending native
  reload can suppress Fire while a free hand still grabs the fore-end. Actor,
  weapon, equipment, space, left-hand tracking, authored mesh and original
  publication deadlines remain checked. Sight/fire consumers keep their gates.
- A submitted shell releases only its original ammo hand claim. Its native
  reservation stays pending until the exact acknowledgement; a replacement
  support claim cannot be removed by that old receipt.
- Body ammo reads one immutable, validated tracking/source snapshot. Stereo
  composition latches original geometry and joins surviving props by identity
  across both eyes. Missing ammo cannot shift the dense slots of carried guns;
  expired or revoked sources disappear from both eyes.
- The VR host has a small **loaded / reserve** counter. Counts come from the
  existing verified native ammo lease, paired with current physical GunHold
  ownership. Invalid/stale/mismatched samples, menus, tracking loss, vehicles
  and empty hands hide it. A separate head-relative quad expires independently
  of a retained world image. No predicted reload success or invented count.
- Pending magazine presentation survives a short typed observation deferral
  only under its original target, reserve, configured-mesh and claim leases.
  This addresses a reproduced absent-target path, not a proven sole cause of
  every visible ghost in the headset recording.

Frame-channel version is now 5; body-prop payload version is 2. The probe and
host must be deployed together. A fresh game process is needed before loading
the new DLL; the stopped 206d DLL remains mapped in the current game process.

## Validation

Full checkpoint builds pass **189 C++ suites on each architecture**, x86 then
x64 with one compiler job. The HUD also passes actual software-D3D texture
readback with mocked XR acquisition/wait/release, expiry, menu hiding, zero
counts and mismatched-owner cases. Original control/claim/target deadlines and
non-replayed native submissions are covered by the consumer regressions.

No actual-game or headset acceptance of 207 yet. The WARP check verifies pixels
and lifecycle, not headset legibility or Steam Link stability. Native ammo
transfer timing has not been accelerated: support interaction is separated from
that wait, and the counter shows the actual native result.

Frozen receipt: private `pipeline-runtime-20261005/normal-recovery207-build-receipt.json`.
Matching host/probe/cache/loader/scripts: private `headset207-20261008`.
Source digest: `88797a2b8728055ad0baadfc7c4d413fde983a318fb67f58be634f6841eab7bc`.
`Build-Checkpoint.ps1` selects `profiles/checkpoint207`; local native output is
`build/x86-checkpoint207`, and host output is `build/x64`.

## Next focused headset check

1. With XM8 held, verify the counter shows loaded / reserve and changes on shots.
2. Remove and hold its magazine: inspect ghosting; insert a replacement, then
   release and squeeze the fore-end immediately. Watch the actual count change.
3. Insert one SPAS shell and immediately regrip the fore-end. Its native ammo
   transfer remains separate from the physical release of the shell.
4. Look down at chest ammo, then holster/draw once; confirm props and counter
   agree with the held weapon. No broad weapon-by-weapon acceptance requested.

Weapon coverage has not expanded: two enabled exact magazine configurations
(scoped XM8 and AEK), plus the SPAS shell consumer. The 21 additional prepared
configurations remain candidates. All fixes here target shared presentation,
ownership and resource mechanisms instead of new per-weapon state machines.
