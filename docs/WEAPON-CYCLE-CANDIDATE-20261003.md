# SPAS pump, shared bolt cycle, and LMG boundary

This candidate adds reusable code and a runnable, isolated native diagnostic. It
does **not** enable manual pumping in ordinary play. No live process, game input,
XR session, or GPU test was run by this task. Canonical files were not edited.

## Implemented components

* `WeaponCycle.h`: raw weapon-local contact, neutral acquisition or an exact
  already-held Mechanism/GunHold pair, rear/forward pump sequencing, and
  unlock/rear/forward/lock bolt sequencing. An ordered gesture emits a bounded
  release request; only a matching fresh native-ready receipt completes it.
  There is no chamber count, ammunition arithmetic, timer write, or automatic
  fire permission. A cancelled cycle remains blocked for its adapter to retire.
* `Bc2PumpPart`: one private palette edit for the SPAS fore-end leaf. It keeps
  closed-part orientation/lateral placement and applies the raw recognizer's
  axial travel once, independently of the native animation's current offset.
  The exact rig, current input/claims/native cycle and source deadlines must
  remain valid. Native source bytes and SIMD padding remain unchanged.
  Rear direction defaults unbound; a state-labelled closed/rear capture must
  establish its sign. The extrema replay tests motion reconstruction only.
  Production binding uses the existing captured SPAS mesh/rig gate. No ordinary
  renderer call or activation was added.
* `PumpHoldDiagnostic::SpasOneShot`: an explicit schema word, default Disabled.
  Config size changes from1180 to1184 in sender, reader and the actual layout
  test. It accepts only15-second tracked hands/fire/support diagnostic flags.
  Normal sessions reject that nonzero word. It cannot combine with physical
  reload, holsters, magazines, sight, menu or other synthetic diagnostics.
* Diagnostic receiver: stable tracked input, two ordinary80ms right-trigger
  pulses at3s and6s, no reload/equip/use commands. A read-only preflight requires
  exact SPAS and at least3 loaded rounds, excluding last-round auto-reload.

## Measured part and native code evidence

The supported executable SHA256 is
`3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258`.
Saved full function evidence is `reports/reload-native-code-20261001.json`.
The native Update at preferredVA0x6e9000 is397 bytes with SHA256
`0babdb25cdcc8f494a544d27cdf628c67f495215872b1ff5fa90f62fd5ca426e`.
It loads caller-context delta at+0x18 and skips the state-dispatch loop when the
remaining delta is zero (0x6e9135–0x6e913e). The existing verified prelude calls
and original-once/SEH exact four-byte restoration are reused unchanged.

The candidate holds only a coherent three-copy **current7, previous6, next8**
SPAS cohort, positive remaining loaded ammunition, unchanged equal native counts
and effective capacities, safe neutral context, and a100–250ms remaining native
timer. All copies' observed safe contexts must be fresh. The one-shot bound is
350ms; cancellation, expiry or any proof failure releases it. No firing object
count, state, authored setting, or reload multiplier is written. Contemporaneous
cohort callback revisions are checked before the diagnostic requests the patch.

Authored SPAS capacity4 is not used as effective capacity: the existing native
capacity multiplier/override reader must agree across all three copies (previous
campaign captures show8). State8 starts the native pump notification and the
native state transition owns readiness. Zero-loaded/last-round behavior is not
admitted. This state hold still needs an actual fired native trial.

Trace041114-514 has96 late SPAS bone samples,10.39s total. `jntWpn_4` moves
94.95844mm along weapon-local Z, less than0.128mm laterally, with maximum rotation
element drift5.4e-6. Its1,041 rigid-weight vertices form the ribbed fore-end in the
derived per-part view. Installed mesh metadata/data hashes and motion details
are saved in `spas-skin-parts-derived.json` and
`spas-current-motion-derived.json`; original archive bytes were not copied.
The attempted assembled view is **not** validated; do not treat its transformed
bind positions as a grip socket. The native flow record window ended before the
96 animation samples. Bone motion is therefore measured, but its correlation to
all-three native7→8 states and exact closed stop remains unproved.

## First native diagnostic, parent-operated only

After integration and both full builds, use a fresh BC2 process, on foot with
SPAS selected, at least3 loaded rounds, aimed at a harmless campaign surface.
No competing VR session. The wrapper preflights exact native identity/counts.

```powershell
& <local-workspace>\Test-NativeStream.ps1 -PumpHoldProbe -Seconds 15 -Pairs 240 -StaticPose -Async
```

Expected bounded evidence, not a predeclared pass:

1. Actual ordinary first shot consumes exactly one round on all three copies;
   exact firing identity is retained into state7/previous6/next8.
2. `diagnostic_hold.target == 1`, all three applied/restored counts positive and
   equal,350ms common deadline, no patch/restore error; held calls retain timer,
   states and ammunition. Only the invocation's original delta bytes restore.
3. After release, each same firing copy reaches native8→1→2 with no extra ammo
   transfer. The second ordinary shot follows the native unheld path. Final
   read-only counts decrease by two with reserve unchanged. If a trigger pulse
   did not reach native fire, classify preparation failure, not cycle success.
4. Correlate captured fore-end poses to these intervals; inspect both images.
   Any observer gap or missing branch makes that part of the verdict incomplete.
5. Clean hook drain and stable process after detach. Do not infer headset or
   continuous manual-pump acceptance from this finite diagnostic.

The next production seam is an exact native held-cycle capability renewed by
current observations, current shared-arbiter transfer from WeaponSupport to
Mechanism, ordinary Fire suppression while cycling, paired private fore-end/hand
presentation, gesture release, and native ready/tail receipt. The same closed
baseline must be retained under the exact cycle, never rederived from animated
or guided hand output. Native animation tail behavior remains a separate needed
observation; the private planner is not wired to hide that unknown.

## Actual family coverage

| Family | Existing usable component | Native/mechanism proof still needed |
|---|---|---|
| SPAS tube reload | Existing accepted shell supply/rail/native receipts; this candidate adds ordered pump and measured private part planner | Fired three-copy hold/release; closed-part contact; ordinary Fire suppression; animation tail; last-round/death/weapon switch |
| Bolt action | Same ordered policy with explicit unlock/lock angles and identical native receipt contract, deterministic tests | Per-weapon authored firing config and actual bolt/handle geometry; native cycle boundary and ready/tail proof; no sniper enabled |
| Belt-fed LMG | Existing `FeedMechanism` contact/order policy, shared supply/hand/insertion/receipt contracts | Actual cover/latch/container/belt/charge channel bindings and native takeover/transfer/closure receipts; no LMG playable manual reload |
| Magazine-fed LMG | Shared magazine infrastructure may fit after exact config/profile proof | A magazine-shaped model or native `rtMagazine` does not prove the physical mechanism; no automatic family admission |

The asset agent has separately extracted323 archives/55 meshes and is hardening
authored DBX links. Those are useful metadata, not proof of live authority. No
new LMG, sniper, or unsupported weapon feature is marked completed here.

## Offline verification

Both x86 and amd64 focused runs pass13 shared-cycle groups,10 native hold-policy
groups (including original-once exact delta restoration during SEH), and4 private
part groups including the two actual captured extrema. The latter confirms the
translation reconstruction, not which extreme is the closed native stop.
The x86 runtime, bootstrap and NativeProbe translation units compile; the amd64
receiver compiles. Existing NativeProbe-only alignment/conversion/shadow warnings
are suppressed in the isolated compile script; no new native warnings are hidden.
Both PowerShell launchers parse. Full normal builds and native execution are
parent-owned and remain pending at this candidate freeze.
