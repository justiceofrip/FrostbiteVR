# Checkpoint 413 fork handoff

This follows the checkpoint412 headset failure: the SPAS could lose firing,
pumping and holstering while grip still worked. It is a developer checkpoint,
not a public player release or campaign acceptance.

## Shared fix

Native-cycle dispatch now distinguishes temporary gate contention, proven
non-dispatch, an ambiguous rejection, and a release overtaken by recovery.
Observation contention cannot cancel an owed cycle. A proven unsent request
can retire its physical attempt and accept a fresh full stroke; it cannot grant
ammunition or forgive the owed mechanical step. Critical native callback
failures still receive conservative handling.

Both pump and bolt adapters use that contract. Bolt recovery retains the
supporting gun hand while waiting for neutral input, including a busy admission
gate. These are shared mechanism changes, not new registrations of every gun.

## Evidence

- Both final architectures passed all 284 C++ suites, including audio-route
  hardening and the 600 ms bolt-custody regression. The final source closure is
  `e0b5940640f31346f9ebb26b62ef0a197691210e16d94c0b70843a5e4a65364d`.
  Nine audio-tool Python regressions and 35 source-package tests also pass.
- Real BC2 with synthetic controllers passed a partially pulled pump followed
  by tracking loss, fresh full recovery stroke, shell insertion, shoulder
  holstering and rifle reload/firing. It did not test SPAS refiring after that
  interrupted stroke.
- A separate real-game run passed all eight SPAS shots and full manual pump
  cycles, one conserved manual shell insertion from empty, then shoulder
  holstering and rifle reload/firing. All eight support returns completed;
  there were no native cycle failures or unresolved firing blocks.
- The eight-cycle audio journal recorded eight suppressed stock starts and
  sixteen played movement/closure cues, without playback failures. A separate
  one-cycle PC endpoint recording found both exact cues once, approximately
  38/49 ms after their physical stages. This does not measure Steam Link delivery.
- The final hardened source passed a separate actual-game cycle, conserved shell
  insertion, holster/rifle return and two rifle shots. Both audio cues completed;
  all512 stable-route checks passed, with no playback/cleanup failures or retained
  driver buffers. Hardware disconnects were not injected. The earlier interrupted,
  eight-cycle and waveform runs used source `137749ee42125e16380654e3db175e42255b5fa7ffecf95461a3bea955de5d52`.

The personal desktop launcher now selects final normal413 and enables the local
SPAS sound cache for fresh BC2 processes. No headset session was started by this
promotion. [Compact validation](validation.json) records the exact build hashes.
Receiver image output was disabled, but each inspected native diagnostic kept
eight bounded snapshots (66,355,200 bytes); these are private evidence, not source.

See [checkpoint details](../../docs/CHECKPOINT-413.md),
[audio evidence](../../docs/MECHANISM-AUDIO-413.md), and
[the preceding failure](../../docs/HEADSET-FAILURE-412.md).

## Build and inspect

Use [the checkpoint composition](../../BUILDING.md#checkpoint413-player-composition).
`profiles/checkpoint413` binds the reviewed source dependency closure; inherited
aim/support/magazine data remain in their older numbered profile directories.
Native config is 1220 bytes; input3/frame7 are unchanged.

Key entry points are `Bc2NativeCycleDispatch`, `Bc2NativeCycleService`,
`Bc2NativeCycleUpdateRoute`, `Bc2PhysicalCycleDispatch`, `Bc2PhysicalPump`,
`Bc2PhysicalBolt`, `PhysicalWeaponCycle`, and `BoltWeaponCustody`.
`MechanismAudio` defines shared accepted-stage events; `Bc2MechanismAudio`
contains BC2 sound ownership and output routing. Audio does not own ammunition,
hand claims or native cycle release.

Audio replacement requires private cues prepared from the user's installation.
No WAV, game executable, mesh, save, raw trace or recording is included here.
Default audio is observation-only; M95 replacement remains disabled because
its stock sample also contains casing impacts. No generic default-output fallback
or Windows audio settings change is performed.

## Remaining limits

No checkpoint413 headset acceptance or complete campaign playthrough is claimed.
The eight-shot native test still measures roughly 0.50–0.70 seconds from pump
closure to native readiness. Immediate post-shot timing, spent-shell ejection,
broader gun geometry, belt-fed reloads, zoom scopes, full-body IK and multiplayer
remain separate work. Shared test coverage does not mean every weapon is tested
in the game. Prior failures and their evidence remain preserved.

`checkpoint-412` preserves the preceding source. The older
`checkpoint/410-headset-rollback` branch remains available. Local launcher-profile
rollback is separate from the launcher's app-version Rollback button.

This tested source is on `checkpoint/413-recovery-audio`, based on `6faca19`.
Remote main advanced to `c345db7` during validation with separate build and
documentation changes. It was not overwritten or merged into these tested pins.
