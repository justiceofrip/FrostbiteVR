# September 28 image-delivery repair

The user reported an image appearing and then going black, unlike the previous
regular stereo flicker. This occurred during native-xr-20260928-125347-254:
108 of 447 published pairs consumed, 339 discarded, at about60 native frames/s.
It prevented visual acceptance of the captured decal/terrain correction.

## Cause and change

The async host incorrectly shared a 50 ms request lifetime with the synchronous
CPU wait budget. BC2 captures both eyes and restores its native cameras, queues
legacy shared-texture copies, polls the GPU fence on a later native draw, publishes
through IPC, and waits for a host poll. Several native frames can span this
handoff. Expired requests discarded finished images; prolonged runs of misses
left the host without a pair inside its 250 ms presentation-age limit.

RemoteFrameProvider now has an independent 150 ms async lifetime, bounded at 200 ms.
FrameChannel keeps the 50 ms synchronous blocking cap. Neither native nor host async
calls acquire a blocking wait. Ready pairs are delivered as soon as available;
150 ms is an expiry limit, not a target latency. Original render pose/FOV, deadline
rejection, recenter/focus cancellation, GPU ownership and retained age are intact.
No native engine offsets, game settings, job order or artifact scopes changed.
This repair is in the shared IPC framework for other Frostbite adapters too.

Host and native receiver report completed requests, timeouts, mean/max request
latency and completions after50 ms. --request-lifetime and the scripts'
-RequestLifetimeMs select 1..200 ms; default 150. --frame-budget is synchronous only.

## Verification

- 23 x86 + 22 x64 suites pass. New 80 ms delayed-delivery regression keeps the
  request's original pose, despite newer tracking and a 1 ms blocking budget.
  True expiry, bounds, delivered GPU ownership and recenter remain covered.
- GPU legacy x86 producer/x64 consumer: four exact-pixel pairs, dropped-pair
  recovery, peer exit and abandoned mutex pass.
- Fake-runtime OpenXR presentation fixture:100 frames,13 fresh,53 reused,
  expected 34 blank and 2 rejected cases, zero API errors.
- Old50 ms live control: native-ipc-20260928-130338 / trace130339-491;
  64 consumed,25 discarded,26 receiver timeouts. First 256 frame intervals median
  16.612ms; game ran faster after the active capture interval.
- Corrected150 ms slow check: native-ipc-20260928-130832 / trace130832-858;
  world719/12 s (~60 FPS),96 consumed,0 discarded,0 timeouts/GPU/restore failures.
  Mean 52.802ms, max 76.335ms,40 of 96 received past the old cutoff. Native restoration
  and15 s crash check pass. Two faster runs also passed but were not the60 FPS proof.

Reports, prior/verified sources, binaries and hashes are preserved under
reports/delivery-repair-20260928. The earlier artifact checkpoint and Sept27
headset success are untouched. Actual headset image continuity and black-dot
acceptance still need the corrected build tested in SteamVR.

## Corrected headset retest, 13:12 UTC

User reconnected SteamVR. Native-xr-20260928-131155-006 and
native-trace-20260928-131155-999 recorded the corrected 30-second test. BC2 held
60 FPS: 444 published,443 consumed, 1 discarded. The earlier failed test had 447
published, 108 consumed and 339 discarded at the same native frame rate.
The corrected host presented 2665 frames:443 fresh and 2222 reused,0 rejected.
Mean request latency 50.239ms, max 78.145ms; 233 completed past the old 50 ms deadline.
There was 1 request timeout. The 1340 blanks include the additional 15 seconds the
host runs after the producer; aggregate counters cannot rule out shorter gaps.

All camera/context/native state restored; no GPU or derived-projection failure,
no crash in the 15-second post-check. BC2 minimized at completion. Actual user
feedback on continuity and dots is pending. This is a completed headset run,
not yet a visual acceptance. Raw logs and an acceptance-pending.json are saved.
