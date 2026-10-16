# Checkpoint 409 work in progress

This checkpoint addresses two measured failures from the normal campaign tests:
partial knife return when the emulated host closes, and different damage-effect
values in the native stereo views. It is not yet a headset-accepted build.

## Evidence and changes

The normal 408 held-knife test drew and held the knife successfully, but only one
of the two native return consumers completed before the unchanged two-second
deadline. The engine legitimately puts the already-deselected knife into state 1.
The pending consumer must check its own knife state, rather than requiring the
already-returned copy to remain idle. Initial admission still requires all three
copies to agree; final completion still requires both native consumers, all
three original selections, unchanged ammunition and drained hooks. A bounded
journal records the actual close predicates without allocating in the hook.

The actual mission 06 run reached damage from 100 to 7.5 health. The legacy scalar
observer recorded 83 complete paired frames: the extra view's four effect values
at +1650 were zero throughout, while the main view differed in 82 frames. The
values were stable across each frame's draw and capture observations. The
proposed correction transfers only these four verified inline scalars for the
extra eye's native draw, then restores the exact previous bytes. It shares the
existing scoped RGB restoration logic and requires both scopes to finish before
publishing the stereo pair. No opaque render/resource block is copied.

Mission 06 did not pass its complete run criterion: it delivered 235 of 240 required
pairs before the producer ended. Its newer opaque-block observer also used the
wrong request phase and refused every sample. Those unread values are not
evidence about the fields. The valid legacy scalar observations are retained
separately. The next diagnostic corrects the phase and stops sending input at
the original 18-second bound, even if its frame target is incomplete.

## Storage and validation

Builds reuse the existing x86/x64 trees. Release-style test executables omit
duplicate linker symbols; runtime/tool symbols are retained. Temporary compiler
output stays under the build directory, after checking the original temporary
volumes' free space. Seventeen focused storage-policy checks passed.

The next held-knife check keeps its real GPU readback comparisons but saves no
receiver images. The planned damage check saves 24 native stereo pairs in its
six-second event window, plus at most 8 startup snapshots, with a diagnostic-only
1920x1080 capture bound: 442.97 MiB maximum raw output. Both runs remain subject to
the 512 MiB raw/run and 40 GiB generated-storage limits.

Both full builds pass 283 suites against source
`e909ac55fdc4297cb581d702040084341a931ecf424ed2995edfd0b87749406e`.
The native dependency receipt contains 509 files; config ABI 1220 and input/frame
protocols 3/7 remain unchanged. Prior 408 pump mechanical and stock-ring visual
acceptance is retained without repeating that unchanged native check.

## Actual-game results

Game40's normal held-knife close completes in 737.7 ms of its unchanged two-second
deadline, with both native chooser offers, all three original idle selections,
unchanged SCAR ammunition (30/210), 17 coherent post-return reads and drained
hooks. It delivered 240 stereo pairs and nine real pixel comparisons, saving
only eight native snapshots (63.3 MiB). Its original checker incorrectly compared
numeric transport success `1` with Python Boolean identity. An additive corrected
assessment passes all 22 checks; the original failure is preserved. The previous
state1 scheduling interleaving did not occur in this run, so that exact live
interleaving is still covered only by native code evidence and regression tests.

Mission07 delivers 240 pairs. The effect scope performs 118 writes and 118 exact
restorations with no rejection or restoration failure. All 252 coherent extra-eye
observations match the main view's four effect values. Raw output is exactly
442.97 MiB, within the admitted limit. All 30 delivery/render collection checks
pass; strict input delivery remains qualified by two Busy nondeliveries (not
failed or delivered actions). The original exit2 result is retained.

Root reviewed all 24 captured stereo pairs. The blood border now appears in both
eyes, but eye0 still has stronger red tint/blur and late darkening than eye1.
**Visual acceptance fails:** the four-scalar fix is a measured partial correction.
Twenty-three images have unique exact ledger joins; the first pair155 does not.
Health reached 9.0035, with 56 missing reads and four client/server disagreements
(340 coherent samples), and a replacement actor later;
zero health was not sampled. This is not continuous death/restart acceptance.

Both owned games closed and their original settings/profile were restored. Next
work uses the retained measured fields and executable to locate the remaining
postprocess difference. No unchanged capture rerun is needed. Full campaign and
headset acceptance remain outstanding.
