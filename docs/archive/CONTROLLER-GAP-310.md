# Controller publication gap310

309's combined native run failed during an owed SPAS stroke after a123ms
callback gap. Invocation9514 had native owner snapshot1764, but the native cycle
service still held controller1756, whose deadline expired41ms earlier. Exact
actor/weapon and7/24 ammunition were unchanged. The first-failure journal,
not the later cleanup, records Held ->Cancelled/Owner. This was a failed run;
309 is not an accepted combined checkpoint.

310 separates a short controller publication gap from actual native ownership
loss. With recovery enabled, an already-established all-three-branch hold can
use fresh verified native cohort/configuration, unchanged counts and a safe
current callback context to enter the existing native-only suspension. Its
maximum deadline is anchored to the original controller expiry. No controller
sample is restamped; old hand leases are revoked. Fresh same-owner neutral
input is required before a new physical stroke can finish the existing debt.
Missing/changed native evidence still cancels. No ammo edits or synthetic
completion are added. New shots, partial arming and completed cycles cannot
use this path.

The deterministic regression reproduced the original failure before the fix.
Six focused suites pass afterward, including SPAS and M95 gap/resume/completion
and twelve rejection cases for owner/config/count/state/context/deadline changes.
Both full normal builds pass243 suites: x86144.77s and x6462.84s.
The normal player binaries are frozen with diagnostic input disabled. The
combined native run FAILED before pump arming: first failure8426, ShotObserved
->Cancelled/Owner, controller1555 expired30ms before native entry. Native actor,
weapon and7/24 ammunition remained unchanged. Rifle support and initial
magazine operations passed. Hooks drained and BC2 remained alive.310 was not
staged;311 extends the gap handling through pending own-shot observations.
Evidence: support310-scar-native01/native-cycle-first-failure.json and
runtime-status.json. No311 acceptance is implied.

309's sustained pistol grip/reload/fire result remains explicitly historical:
46 unique held inputs over751.6ms, primary aim preserved,17/85 ->0/85 ->17/68
->16/68. Its production grip/reload components are unchanged by310. Do not label
that a new310 pistol native run.308 remains preserved. No new headset acceptance.

Evidence is under bc2vr-recovery/support309-scar-native01 and
support309-pistol-native01. Exact source and build receipts are required before
staging a310 player package. Diagnostics must be off in the player DLL.

## Next headset check, once native audit passes

Use a fresh BC2 process because diagnostic modules remain resident after their
hooks drain. The normal player package does not synthesize input.

1. Draw a long gun from each shoulder, use the left support grip, then stow it.
   Empty-air right squeeze must not summon a weapon.
2. Fire the SPAS, keep the right hand steady and pump backward/forward with the
   left. Fire again; holster and redraw. The pump must not disable inventory.
3. Insert one chest shell and immediately regrip the fore-end. Swap to the
   rifle, remove/return its magazine and use one chest replacement. Fire again.
4. Open the VR menu and check visible pointer/select/back. Checkpoint restart
   and vehicle interactions have earlier evidence but are not revalidated by310.

Pistol support has native309 and hip carry305 evidence, not a new310 native run.
Neither first-person comfort nor the full campaign is established by automated
input. Chest knife, third native slot, pistol chambering, belt-fed reloads and
broader manual-action enrollment remain unfinished; physical grenades are deferred.
