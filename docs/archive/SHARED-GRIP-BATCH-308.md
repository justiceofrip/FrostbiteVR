# Shared grip batch308

The grip pipeline compiles existing exact authored reference data for whole
weapon families. It produces58 contacts across32 distinct meshes:13 assault,
17 wcSmg (including five explicitly classified handgun variants),9 LMG,
12 sniper and7 shotgun variants. The existing SPAS12_sp contact is preserved
separately, unchanged. This is contact coverage, not58 completed manual reloads.

`tools/bc2_support_batch.py` joins existing extraction outputs by exact asset,
configured mesh and rig fingerprint. It validates source digests, rejects
conflicting configurations, and lists nonconstant/missing references rather than
borrowing another weapon's pose. The existing seven contact profiles retain
identical transforms and aiming modes. No right-hand transform is replaced.

Primary-hand support applies to the measured M9, M9-3, MP412, MP443 and MP443_sp
variants. Close support hands retain the right controller's aim. Rifles and
other long guns use two-point support. The same five handgun identities use
pistol-only hip carry metadata; native reload classification remains independent.
M1911 lacks a reference in this extraction. AN94, M416, M249, MG3, Saiga and
T194 variants with varying references remain listed as gaps. No slide/chamber,
belt feed, pump, bolt or native reload capability is granted by grip data.

The emulated magazine approach now permits a bounded ordinary idle rearm:
no grip, claim or native operation may have begun, and the exact resource,
request, owner and ammunition must remain unchanged. Only an explicit idle
cancellation can wait, for at most300ms; the diagnostic never resets the consumer
or changes ammunition. Regression checks reject changed counts, request, owner,
active custody, motion faults, missing evidence and an interruption during pull.
The combined script uses its existing one-second preparation interval after
inventory readiness instead of repeating six-second startup waits twice.

Nine focused C++ suites and13 Python checks pass. Full normal x86 and x64 builds
pass243 suites each (133.36s and41.14s). Frozen normal binaries have all synthetic
input drivers disabled. Two separate input-only native diagnostics use the same
source and persistent production consumers, with no desktop input:

- `support308-scar-native01`: rifle support, three magazine cycles across shoulder
  exchanges, one SPAS shot/pump, one shell insertion, support return104.53ms after
  insertion completion, and firing the rifle afterward. Six magazine commands
  have independently matched server/client Update receipts. Final rifle counts
  are29/180; SPAS finishes8/23. No presentation fallbacks or ownership failures.
- `support308-pistol-native01`: support grab/release preserves primary-hand aim,
  discard and chest replacement17/85 ->0/85 ->17/68, then a shot16/68. Native
  authority and both client copies match. Only two attached support samples are
  retained by the counters; no detailed held pose row survives the bounded ring.
  This proves the interaction route, not prolonged hold or headset comfort.

Each consumes240 stereo pairs,40 pixel-compared, with clean hook disable/drain
and BC2 still alive. Raw receipt audits pass. Neither diagnostic needed the idle
rearm allowance (CPU regressions cover it). The combined trace lost11 ordinary
record-lock rows; critical native command votes are present. No full callback
coverage or headset acceptance is claimed. Six combined-run image samples were
visually reviewed; they do not directly show the brief rifle support phase.

The previous307 partial failure and the initial308 auditor schema mismatch are
preserved. The latter used a recovery-only schema for an ordinary pump; the new
audit checks the actual own-Update completion evidence rather than accepting a
driver success flag alone.

Source:137ae3ca64687c942bc3671e1d5f8d557388a2a58c48ced7635c1b732f1f2709.
Normal308 is staged locally with a fresh-game launcher and source archive;
normal305 remains intact. No GitHub publication or player release was made.
Physical grenades remain deferred. Knife draw/hold and motion attacks, third
native weapon capacity, full belt-fed reloading, pistol chambering, broader
reload enrollment, sustained campaign/death recovery and headset comfort remain
open. See KNIFE-BINDING-INVESTIGATION.md for the separate read-only knife evidence.
