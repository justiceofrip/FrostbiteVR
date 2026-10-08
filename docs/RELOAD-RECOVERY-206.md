# Delayed hand geometry and native reload recovery — 206

A grip can arrive before the renderer publishes the matching hand pose. The
shared hand arbiter previously advanced its release barrier on every open-hand
packet. Fresh renderer N-2/N-3 geometry from the same continuous open-hand period
then looked older than the latest release. Magazine removal consumed the grip,
failed to acquire the hand, and stayed at `NeedNeutral` until the user released.

The release barrier now records the **start of the current released period**.
This accepts still-fresh geometry from that same period without retrying a
failed held grip. Geometry preceding the latest release, tracking/focus/owner
loss, explicit release or dependency invalidation remains rejected. Exact input
history, original deadlines, intent IDs and occupied-hand checks are unchanged.
This is shared hand policy, not weapon-specific positioning or ammunition logic.

## Reproduction and coverage

The new two-frame-delay test failed before the fix with `HandUnavailable`, then
`NeedNeutral`, before any native start. It uses the real hand arbiter, tracked
rig, magazine consumer and chest-supply policy with mock native/renderer edges.
The same loop now passes with both two- and three-frame pose delays, including
returning the original magazine followed by a chest replacement. Focused hand
tests separately cover release intervals, safety loss, occupied support/sight
hands and consumed intents. The existing 30 interruption/recovery cases remain.

The explicit `--inventory-recovery-probe` diagnostic adds a native check:

1. Drive ordinary XM8/SPAS shoulder changes using the actual slot assignments.
2. Pull the original magazine; require a verified rendered pair before removing
   left-controller tracking for at least 350 ms through the ordinary input path.
3. Observe actual cancellation, native retirement, restored tracked input and
   unchanged ammunition. No consumer, ammo count or ownership state is reset.
4. Repeat normal shoulder changes, return the original magazine, then load a
   chest replacement through those same persistent consumers.

The script is bounded to 60 seconds and remains separate from headset mode.
Reports retain lifetime counters, both inventory laps and the interrupted
episode. First rejected grab reasons are recorded before held-input neutral
rejections hide them. The recovery diagnostic records at most 40 seconds and
40,960 callback rows; ordinary recording remains 20 seconds and 20,480 rows.
These bounds never grant native hold, transfer or interaction authority.

The initial larger aggregate record array made compiler memory usage excessive.
Recording storage now allocates its fixed bound once during setup; callbacks
never allocate or grow it. Ordinary runs allocate only the ordinary capacity.
Allocation failure cannot enable the larger recorder or manufacture evidence.
The user's PC crashed during full rebuilds; those interrupted builds are not
validation. Continue with one compiler job and one architecture at a time.

## Native evidence and limits

The first run recovered from tracking loss and completed both shoulder laps,
then stalled on the later magazine grab. It also exhausted the old record
capacity; that run is not accepted. The next instrumented run completed the
whole interaction: one cancellation, one original return and one replacement,
with ammo 22/191 to 30/183. Its interaction audit passed, but two display-transfer
timeouts kept the combined audit inconclusive. This timing-sensitive success
does not invalidate the deterministic two-frame-delay reproduction.

The final 206d rebuild passes all **186 C++ suites on both x86 and x64**, using
one compiler job and one architecture at a time. Its protected source digest is
`b3706c124ea2286eb0460425bf6f2d29611a4d1ae1016f2136216eb1072cacd1`.
The corrected binary's native check remains pending. The user supplied the
fork's native UI-thread startup method, and its bindings were verified locally.
A separate auto-Continue helper now builds and passes offline lifecycle tests;
review and actual-game startup verification remain before the recovery run.

Three private audit tests retain the earlier transport timeouts as inconclusive
and reject shortened tracking loss, changed ammo, missing cancellation, extra
operations, incomplete shoulder laps, changed native counts, failed shutdown
and unverified mapped code. These mutate copies of saved evidence, not a game
session or its original logs. They validate the auditor, not current native play.

Build, binary and saved-run receipts are retained in the local checkpoint evidence.
No new human headset acceptance or all-weapon coverage is implied. Pickups,
vehicle transitions, empty-ammo behavior and broader weapon-family cases still
need composed coverage. The shared hand fix does not enable unfinished pump,
bolt, chambering or underbarrel-reload features.

`Build-Checkpoint.ps1` selects the reviewed source receipt in
`profiles/checkpoint206`; calibration headers remain from 202. Local x86 output
is `build/x86-checkpoint206`. This checkpoint is local; no GitHub push is made.
