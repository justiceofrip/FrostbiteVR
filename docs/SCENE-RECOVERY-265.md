# Checkpoint transition investigation265

## Interrupted interaction results

The qualified comparison `scene265-pump-06-confirmed/pump-recovery-audit.json`
passed with the menu helper kept alive across scene replacement: one SPAS shot,
unfinished native cycle, three pointer clicks, verified new scene,92 post-rebuild
stereo pairs and zero captured exceptions. Ten kinds of first new-scene callback
completed. Hooks and temporary views retired cleanly. The old264 cycle still
reported blocked after returning to the boat; on-foot reacquisition and the266
retirement fix were not exercised. This single pass does not establish that
helper shutdown caused the earlier graphics crash or that headset recovery is
fixed.

Two intervening comparisons are explicitly incomplete. `scene265-pump-04-retained`
reached the pump but its confirmation click left the dialog visible; native
pointer invocation is not click completion. `scene265-pump-05-confirmed` lost its
menu helper to Windows denying an atomic status-file replacement while a reader
held the destination. That run nevertheless completed the ordinary XM8/SPAS
sequence; independent receipt review verifies three magazine cycles, conserved
ammunition, interrupted physical pumping, one shell and support return133.8ms
after native completion. Neither run tests scene recovery.

The private helper now permits at most one extra confirm click, only after fresh
image classification of the same dialog. It handles transient Windows sharing/
access-denied errors with25 attempts separated by2ms and still fails persistent
errors. Real file-lock regressions pass. The runner requires an observed scene
rebuild; no input-driver or shipping264 code changed for this comparison.

`scene265-magazine-02/magazine-recovery-audit.json` independently verifies a
second restart, this time after native magazine removal completed. Three native
menu clicks restarted the checkpoint;150 stereo pairs followed the new-scene
rebuild, with zero captured exceptions. This returned to the boat. It does not
prove post-restart on-foot reload availability or retirement of an uncertain
ammunition write.

`scene265-pump-02` reached a SPAS shot with7 rounds remaining, then crashed after
the checkpoint confirmation. The exception watcher saved both exceptions and
dumps. The first exception executes address0 from an AMD graphics worker; its
command/function pointer is zero. `crash-analysis.json` records the matching
local driver image and exact evidence. This identifies the faulting operation,
not the producer of the invalid pointer. No old-scene cleanup or new rebuild was
recorded before this crash, unlike the older245 headset failure. Normal264 PDBs
are retained, but a matching diagnostic264 PDB was not retained; do not symbolize
that diagnostic DLL with the normal player's PDB.

The first controlled comparison, `scene265-pump-03-retained`, kept the menu
graphics consumer alive. It never reached the pump or clicked the menu: the
first magazine consumer cancelled after a native-view expiry. All children
exited and BC2 survived. This is an inconclusive shutdown-timing comparison,
not a crash fix or successful pump-restart test.

Source266 separately addresses proven persistent cycle lockout after real
client/server actor replacement. It preserves ordinary same-actor debt and
retains the old terminal outcome, requiring fresh replacement ownership and
callback exclusion/drain before retiring it. This does not address the captured
graphics fault. Both full240-suite builds pass; actual-game acceptance is pending.

## Verified neutral restart, October9

`bc2vr-recovery/scene265-native-04/neutral-recovery-audit.json` verifies one
actual native-menu Restart Checkpoint with normal264 player binaries. Three
image-verified pointer clicks produced three down/up pairs, with no rejected
click, stuck button, menu graphics failure or captured exception. Old world and
request owners retired, their replacements rebuilt, and71 stereo pairs arrived
after that rebuild. Inspected post-rebuild eye images show the campaign boat,
not a black screen. First new-scene gameplay, visibility and draw callbacks
completed; actor generation advanced1→2 and input epoch2→3. All hooks detached
normally and BC2 remained alive. Desktop mouse/keyboard input was not used.

This is an on-foot idle→checkpoint boat transition. It does not verify active
reload cancellation, owed pump debt, death, on-foot hand reacquisition or headset
behavior, and it does not establish that the earlier headset crash is fixed.
Frozen264 remains unchanged. The subsequent interrupted cases are recorded
above; do not repeat this neutral loop without new evidence.

The private automation required two fixes: a stale unsigned clock sample could
underflow into an immediate command timeout; Windows could briefly deny a read
while a command receipt was being published. The clock boundary has a passing
deterministic regression. Receipt polling now tolerates transient sharing errors
within its original timeout. The completed native run resumed after such a read
error without reinjection: `resume-commands.json` and verified menu images retain
the subsequent actions. Original runner error/result files remain unchanged;
the independent audit, rather than their initial failure status, records the
native result. Earlier265 attempts never clicked Restart Checkpoint.

Checkpoint264 player binaries and source archive are frozen and remain available
for headset acceptance. Its SCAR/F2000/SPAS sequences passed; do not rerun those
unchanged loops as a substitute for addressing the checkpoint restart failure.

The245 headset crash had no retained exception address. Durable evidence showed
old-eye/cache/request retirement and new-scene rebuild completed; it did not prove
the first subsequent gameplay/draw callback completed. A prior review identified
scene-lifetime and input-thread migration coverage gaps, but neither was proved
to be the cause of that crash. Current264 includes durable scene milestones.

This investigation first reuses normal264 in a fresh campaign with stationary,
neutral simulated hands, then performs one native Restart Checkpoint through
the existing menu IPC path. A private external menu helper captures native UI
pixels; the existing screenshot matcher verifies each menu/dialog before a
dependent pointer click. No desktop mouse/keyboard input or new native spawn/
checkpoint function is used. A bounded exception watcher retains exceptions,
while scene milestones and both-eye images identify
the transition boundary and recovery.

Private tools/evidence live under `bc2vr-recovery/scene265-*` and
`bc2vr-recovery/RunScene265.py`. The helper does not change shipping264. A neutral
restart pass cannot establish recovery with an active reload or owed pump, nor
headset/death recovery. Those are separate scenarios if evidence warrants them.
