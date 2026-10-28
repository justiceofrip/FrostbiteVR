# Pump corrections after headset274

The human274 run failed. The stock pump animation/hand kick reported by the
player and the inability to complete a manual stroke supersede earlier narrow
emulated acceptance. No275 headset acceptance is claimed.

## Retained evidence

Private evidence is in `bc2vr-recovery/headset274-20261009/reports/`:
`session-20261009-115337-325/session.json` confirms the mod ended cleanly;
`native-trace-20261009-115339-850/headset-failure-review.json` pins the trace.
BC2 remained alive after the mod ended. SteamVR was not terminated by us.

The physical pump began cycle1/shot1, produced638 targets, and never submitted a
release or acknowledgement. Input2083 cancelled with Geometry after roughly64mm
of lateral movement (limit60mm). Repeated later attempts reached the rear stop
then cancelled: some exceeded that lateral bound; several exceeded the9.5cm
stroke plus10cm overtravel allowance. Tracking and grip remained valid.

At input4051 the tracking-space generation advanced3->4. The same actor, native
equipment generation and SPAS remained selected. The native service cancelled
Owner from Converging at110696640874000ns, without a native invocation, retaining
cycle1/shot1, loaded7/reserve24 and blocked firing. This happened well after the
first physical failures. The log establishes a tracking-origin transition; it
does not distinguish the user's recenter gesture from a runtime origin change.

## Candidate changes

The shared sliding-rail policy accepts an optional held-grasp envelope around
the rail segment. SPAS uses28cm, matching the existing held support grip.
Acquisition stays10cm. Displayed wrist/part travel remains clamped to the measured
9.5cm stroke; real rear then forward movement is still required. Grip loss,
tracking discontinuity, ownership loss and distant reaches remain invalid.
Rotating bolt profiles and callers without this option keep their old policy.

Tracking-origin changes have an explicit drained native handoff. Only the same
actor, weapon, configuration, physical item and mechanism may rebase to a newer
space. Native cycle/shot/counts persist; old hand poses, recovery grants and idle
observations are invalidated. Fresh native observations, neutral input, a new
grip and a full stroke are required. Pending releases/outcomes and uncertain
native guards cannot rebase. Pump integration consumes a specific rebase receipt;
this does not enroll bolt-hand transfers or other weapon families.

The natural-stroke input driver now commands22cm rearward travel plus an8cm
sideways arc, instead of an almost ideal11cm straight stroke. This is diagnostic
controller motion, not a production weapon offset. Reports identify schedule
revision2 and the commanded distances.

## Validation and remaining work

Six focused x64 suites passed, including physical/native-service rebase and
new-grip recovery, stale authority rejection, unchanged ammo and native callback
drain checks. With the original rail limits temporarily restored, both the
measured-motion regression and the expanded synthetic-controller test failed;
both passed again with the correction. Original failure logs are preserved.

The measured test interpolates sampled outbound headset positions and supplies
a synthetic return. It is not an exact full-controller replay. Full x86/x64
builds pass all243 suites each. Normal275 player binaries and matching symbols
are frozen separately from the diagnostic DLL.

Actual-game F2000/SPAS emulation completed the expanded controller sequence in
`pipeline-runtime-20261005/root-monitor/resource-f2000275-02`: three magazine
cycles with six verified native commands, shoulder swaps, held support at the
shot, a partial pump interrupted by 500ms of tracking loss, neutral/regrip,
the 22cm/8cm recovery stroke, one shell, support return in 129.5404ms, and final
rifle firing. There was one pump acknowledgement and no physical control/raw
rejection. Recorded native receipts conserve ammunition. All 267 reported guard
updates have records, but nine recording-lock drops elsewhere mean this is not
complete callback coverage. The receiver consumed 240 stereo pairs and saved
40 image pairs without write failure; camera/list/callback restoration and
detachment checks passed. BC2 remained alive and responsive.

Preflight refusals are retained: the saved XM8 was not full, then an initial
F2000 ownership read was incoherent. Neither injected the candidate. After the
existing setup helper prepared F2000 and four paired reads were stable, the
unchanged candidate completed the sequence. The transient ownership read is not
declared fixed. Source and trace hashes are in `pump275-review.json` alongside
the independent `receipt-review.json` and `expanded-review.json`.

The native run exercised tracking loss, not a change of tracking-space origin.
Rebase coverage is currently deterministic service/physical integration only.
Runtime rebasing is admitted against the current ObservationWatch after callback
drain; a nonsuspended Held phase can still require the existing recovery path
before that watch is available. This timing path needs actual-game evidence.
Visual stock animation suppression, human hand placement, actual-runtime
recenter and final headset acceptance remain separate. Earlier273/274 magazine
and input-epoch failures are not declared fixed by a single successful run.
