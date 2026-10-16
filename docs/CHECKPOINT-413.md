# Checkpoint 413 — pump/bolt interruption and mechanism audio

Automated validation and the final actual-game audio check passed. The desktop
launcher now selects normal413, with the previous412 profile preserved for
rollback. The latest player result is the [412 lockout](HEADSET-FAILURE-412.md);
neither this checkpoint nor the campaign has headset acceptance.

## Shared mechanical changes

The runtime now distinguishes a busy gate, a request that never reached the
native service, a dispatched rejection, and a release overtaken by native
recovery. Pump and bolt consumers use the same verdict contract. A busy gate
retries the exact still-current packet; a proven unsent terminal request retires
only that physical attempt and requires neutrality and a fresh full stroke.
It does not erase the shot's owed mechanical step or grant ammunition.

Pure observation and pre-dispatch contention no longer cancel the native cycle.
Missed native update/finish/commit/restore callbacks still preserve conservative
handling because they can conceal a real engine transition. The first contention
site and first rejected native input are journaled independently so a later
failure cannot overwrite the original evidence.

An established held cycle can enter guarded recovery before the same native
update applies an incompatible input. Exact firing-copy, owner, configuration,
source-time and lease checks remain required. A release raced by that transition
is explicitly redirected into recovery rather than left waiting indefinitely.
Bolt recovery also retains the left-hand weapon while waiting for the right
hand to return to neutral.

These are shared pump/bolt transaction and hand-custody changes. They do not
register every weapon model or prove every gun's geometry. Existing weapon
adapters retain their measured configuration and contact requirements.

## Audio

Accepted physical rearward movement and closure emit separate, deduplicated
presentation events. Audio cannot release a cycle, change ammunition or own a
hand. BC2's installed SPAS sound is split into two locally generated cues and
sent to the endpoint that already hosts BC2's audio session. Proprietary sound
data is kept outside the source repository.

Selective stock-cue suppression defaults to observation-only. The personal
launcher enables verified SPAS replacement on a fresh game process. NPC,
third-person and unrelated sounds pass through.
The M95 adapter emits the same physical stages, but its stock sound also carries
casing impacts; replacement for that asset remains disabled. See
[audio evidence and validation limits](MECHANISM-AUDIO-413.md).

## Validation

- The first combined x86 compile succeeded; 283 of 284 suites passed. The bolt
  recovery failure exposed an expired gun-custody lease while awaiting neutral.
- That retention correction and terminal non-dispatch paths subsequently passed
  all six focused pump/bolt/custody suites, including an 800 ms neutral wait.
- The first complete x86 and x64 builds each passed all 284 suites against source
  `137749ee42125e16380654e3db175e42255b5fa7ffecf95461a3bea955de5d52`.
  Normal binaries are frozen separately from the finite input diagnostics.
- Actual BC2 interrupted-pump input passed: a partial stroke, about 510 ms of
  tracking loss, a fresh full recovery stroke, manual shell insertion, shoulder
  holsters, and subsequent rifle reload/firing. All 777 guarded native callbacks
  retained and restored the three firing copies. No critical contention,
  control rejection or unresolved firing block remained. This sequence verifies
  subsequent rifle firing, not SPAS refiring after that interrupted stroke.
- A separate actual-game sequence fired and manually pumped all eight SPAS
  rounds, conserving the reserve, then manually inserted one shell from empty,
  used the shoulder holsters and returned to rifle reload/firing. The strict
  eight-cycle and empty-tube checks passed. Receiver image output was disabled;
  the native diagnostic still saved eight bounded RGBA snapshots (66,355,200 bytes).
- The single-cycle audio capture detected exactly one rear and one closure cue,
  with physical-stage to endpoint-output latency of about 38 and 49 ms,
  respectively (roughly 1 ms clock uncertainty). Both cues completed and the
  early stock cue was suppressed. This is PC endpoint evidence, not a headset
  or Steam Link delivery measurement.
- Review then found audio-output changes, driver-buffer ownership and a stock
  sound passing through before a replacement receipt as separate risks. The
  final adapter checks its exact output route periodically, stops replacement
  for the session when that route or stock-suppression ownership is lost, and
  retains rather than reuses any buffer with uncertain driver ownership.
  A late stock callback can briefly overlap an already-playing cue before the
  worker stops it. No audio result grants gameplay completion.
- Final review also found that a busy bolt recovery admission before its grant
  must keep renewing the original supporting hand. The added regression holds
  admission busy for 600 ms and then requires a fresh full bolt stroke.
- Final composed source is
  `e0b5940640f31346f9ebb26b62ef0a197691210e16d94c0b70843a5e4a65364d`.
  Its full x86 and x64 builds each pass all 284 suites (112.68 and 35.02 seconds
  of test time). Its final actual-game check passed one pump cycle, a conserved
  shell insertion, holster/rifle return, full-magazine return and two rifle shots.
  Both physical audio cues completed; all 512 output-route checks passed, with
  zero playback/cleanup failures or retained driver buffers. This was a stable
  endpoint check, not an actual hardware-disconnect test. Earlier interruption,
  eight-cycle and waveform evidence retain their own source/binary pins. Nine
  audio-tool Python regressions and 35 source-package tests also pass.

The finite input drivers exercise the real game and shared interaction path;
they are not a human campaign playthrough. No 413 headset acceptance is claimed.

Private evidence is under `../bc2vr-recovery/overnight413`. Generated data stays
on G:, existing build trees are reused, and audio captures are bounded to
15 seconds / 3 MiB. `--no-image-files` suppresses receiver output only; native
diagnostic snapshots are accounted separately. Image retirement releases their
memory, not their disk files. The session's 256 MiB total admission covered the
snapshots, but its raw-capture sub-budget was incorrectly recorded as zero.
The future private runner now explicitly admits the existing 512 MiB maximum
for those eight images, plus 256 MiB for other session output, without increasing
the global storage or per-run capture limits.

Source is preserved on `checkpoint/413-recovery-audio`, based on `6faca19`.
Remote main advanced to `c345db7` with build/documentation work during validation;
that work is retained separately, not overwritten or claimed as tested here.
See [the compact validation receipt](../handoff/checkpoint413/validation.json).
