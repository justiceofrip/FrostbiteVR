# Headset375 failure and candidate381 pump handoff

The October 12 headset test **failed acceptance**. The user reported unreliable
holsters, partial SPAS animation distortion and losing the support hand when
returning the pump forward. The MP4 supplied by the user is 56.3 seconds long.
The overview and 6–14 second pump samples are saved privately alongside the review;
they are sampled visual evidence, not an inspection of every frame.

## Captured game result

The normal375 run recorded two native pump releases and two acknowledgements.
On cycle 3 it entered a persistent failure: the native service changed from
Converging to Cancelled with an Owner failure. Two immediately preceding control
rejections report a server PublishedLinks mismatch. The exact differing link was
not retained, so this is not yet a proven pointer, phase-flag or lifetime cause.
The normal native event recording window ended before this failure.

The final report contains 5,951 rejected pump controls and 7,483 blocked inventory
samples. The inventory path explicitly blocks holster gestures while pump debt
is outstanding. That explains how an unresolved pump can also prevent holstering;
it does not explain every earlier shoulder gesture miss. Do not clear the debt
or relax ownership validation merely to hide the failure.

Physical-pump counters in the final trace describe the last selected consumer
instance. They must not be mistaken for session-wide totals: its zero releases
does not contradict the native service's two earlier releases/acknowledgements.

The mod was stopped with permission. Native hooks disabled successfully,
bootstrap returned 0, no new crash report appeared, and BC2 remained alive and
responsive. SteamVR was left running. The module remains resident but disabled
until game exit.

## Changed behavior

Previously support continuation required native Ready. Completing the full
physical stroke dropped the mechanism claim, and the squeezed hand waited for
native completion before it could acquire support. This coupled hand attachment
to asynchronous engine work.

Candidate 381 permits the submitted physical stroke to return that hand to the
closed fore-end using a subsequent fresh matching contact. The 200 ms continuation
window begins at physical submission. Native completion still independently
blocks firing and equipment transitions; hand attachment does not acknowledge a
cycle, alter ammunition, clear debt or submit a second operation. Both ordinary
and interrupted-cycle recovery use this handoff. Release, tracking loss, expired
input, changed gun ownership and occupied hands still invalidate continuation.

The regression holding native completion for 400 ms fails on 375 and passes with
this change. It checks continuous support, unchanged transaction, one native
submission and firing blocked until the real acknowledgement. Existing checks
retain original contact provenance, token-bound wrist placement and rejection
of interrupted or unrelated claims.

Native control-rejection reporting now retains the already-read server flag
diagnostics. This is observation only; it does not waive a failed native check.
The persistent cycle 3 failure, animation distortion and holster acceptance are
still unresolved. No additional weapon is admitted and no new headset test is
requested by this checkpoint.

## Shoulder contact evidence

The recorded right-controller presses near the left shoulder also explain some
misses before the pump lockup. The production shoulder center is
(-0.20, -0.12, -0.18) metres in the recentered body frame, with a 0.17 m radius.
Fourteen saved presses near that area have neither reload-busy nor cancellation
flags. Five fall inside the sphere and report slot 1; nine fall outside and
report no contact. Six misses are just 0.7–14.0 mm beyond the boundary. Two are
21.1–32.4 mm outside; one is 102.2 mm outside.

This agrees with geometric detection for all fourteen recorded points. It does
not prove intent for every reach or explain every holster problem: the gesture
ring dropped seven rows, and native inventory admission is a separate step.
The reproducible measurements are in `shoulder-contact-review.json` alongside
the review. No anchor or grab-volume change is included in 381. A subsequent
comfort change should separate acquisition volume from the rendered anchor and
retain rejection of normal forward-hand positions and unrelated body slots.

## Evidence and validation scope

Private review: `bc2vr-recovery/headset381-review/review.json`.
Original native trace: `headset375-20261011/reports/native-trace-20261012-121610-740`.
Original XR session: `headset375-20261011/reports/session-20261012-121608-711`.

The focused physical-pump, pump-recovery, native-cycle and holster executables
pass. Their native responses are mocked. An initial CTest name filter matched
no tests; the executables were then explicitly run and their actual outputs
saved. Both full player builds now pass all 246 CTest checks. The first parallel
x64 build exhausted the compiler heap (C1060); its single-job retry completed
successfully. The focused pump suite contains 29 groups, pump recovery 4,
native-cycle service 52 and holsters 53. These are regression checks, not proof
that the recorded native lockup or visual problems are fixed.

Native source digest:
`fde746d0a4641451eec113c934411aaacb09e6b43ada2e7fcb765e148ce04370`.
The full source and validation receipts are saved privately in
`bc2vr-recovery/checkpoint381-local`. No new player stage is offered as ready.

Next actual-game checks must exercise repeated pumps while continuously holding
support, return before native Ready, holster changes and the third-cycle failure.
Successful compilation or the previous 375 emulation is not acceptance of 381.
