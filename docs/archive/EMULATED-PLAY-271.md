# Checkpoint271: continuous play with simulated controllers

On October9, actual BC2 ran the same frozen271 diagnostic binaries with
simulated controller poses and buttons. Native game callbacks supplied the
ammunition, ownership and pump results. No desktop input or headset was used.
The normal player build remains unchanged; its existing full builds passed
240 suites on each architecture. The diagnostic build enables only the
ordinary controller-input and interruption scenario flags, with scene restart
disabled. This test does not run the OpenXR compositor or Steam Link.

| Run | Result | Verified coverage |
| --- | --- | --- |
| resource-scar271-01 | Pass | SCAR two-hand support, original magazine return, chest replacement, shoulder swaps, SPAS manual pump with tracking interruption/recovery, shell insertion/support return, rifle return and firing |
| resource-f2000271-01 | Fail, preserved | F2000 two-hand support, original magazine return and chest replacement passed; after SPAS draw, support was lost before firing and the scripted player timed out |
| resource-f2000271-02 | Pass on unchanged code | Same complete sequence as SCAR, using F2000 |

Each completed sequence contains three magazine cycles and six distinct native
command receipts. Original magazine returns conserve ammunition; replacing the
discarded full magazine changes30/210 to30/180. The final rifle shot leaves29/180.
The SPAS fires once, retains its owed pump through roughly half a second of
deliberate left-controller tracking loss, completes a fresh physical stroke,
then accepts one shell:7/24 to8/23. Support returns129.751ms after shell
completion for SCAR and133.2015ms for F2000. Independent recorded native idle
observations verify all three firing branches before recovery acknowledgement.

All three sessions consumed240 stereo pairs each and detached with BC2 alive
and responding. Forty pairs per run were pixel-compared and distinct; the other
200 were not CPU pixel-compared. There were no recorded GPU or camera-restore
failures. This runner has no exception watcher, so it does not establish zero
exceptions. The successful runs dropped17 and6 diagnostic record-lock samples
respectively; full callback recording is not claimed.

## Preserved failure and remaining work

F2000 run01 acquired SPAS left support claim8 and right gun claim7. At
105024224178700ns, about312ms after support acquisition, Gameplay recorded
`input_unavailable` and cleared both hand claims. Fresh input reacquired the
right gun35.805ms later, but the scripted player kept the left grip squeezed.
The pump driver requires350ms of held support before firing this scenario;
it never released/regripped and hit its five-second phase timeout.

No SPAS shot or pump started. The native cycle service remained Watching with
cycle0/shot0, failure0 and firing unblocked. This is distinct from the fixed270
post-pump callback cancellation. The two F2000 magazine cycles had four verified
native receipts before this failure. The full run remains failed even though
the one bounded repeat passed on identical source and binaries.

At the initial audit, the exact reason for `input_unavailable` was not established: that shared path
covers freshness, native-playing state, input validity, focus and head validity.
Code inspection explains why sustained squeeze cannot rearm support after
cancellation, and why this diagnostic driver remains waiting. It does not prove
which input condition initiated the loss. Next investigation should capture
those specific conditions and reproduce pre-shot loss in the controller fixture.
Any diagnostic neutral/regrip retry must be explicit, bounded and reported;
never disguise a retry as uninterrupted grip or clear native action debt.

## Evidence and test boundary

Private evidence lives in `bc2vr-recovery/pipeline-runtime-20261005/root-monitor`
under the three run names above. Passing runs have `receipt-review.json` and
`expanded-review.json`; the failed run has `partial-emulation-audit.json`.
`checkpoint271-local/emulated-play-audit.json` checks identical binaries/source,
native receipts, ammunition accounting, failure events and frame accounting.

No production source, weapon-specific calibration or native thresholds changed
between these runs. Source digest:
`4a76d481b5ee23907c37572611a26cb79b3b7bf70f9ab70f77314396c4b830ae`.

This is representative shared-pipeline evidence, not all-weapon acceptance.
Headset comfort, hand alignment, haptics, visual polish and real tracking/network
behavior still need headset testing. Logic failures reproducible through these
automated paths should be investigated here before requesting that testing.
The unexpected pre-shot loss was left open; no headset reproduction was requested.
Follow-up272 inspection found `stale_input` in the retained support-event log
and a125ms receiver gap immediately after pixel-checked pair120. See
[272 follow-up](EMULATED-PLAY-272.md) for the diagnostic fixes and acceptance status.
