# Automated campaign loading and presentation evidence — 209

The private automatic Continue helper now reaches the saved campaign without
menu navigation. Initial launches used a focus click; the later
`monitor210-20261008-01` launch removed it and independently verified gameplay
with 11 accepted native samples, no desktop input and no activation request.
The helper waits for 60
stable menu observations, calls the normal Continue operation on the UI update
thread, verifies its type-4 request, and restores its exact vtable entry and page
protection. Gameplay is verified separately using read-only client/server state.

The initial helper rejected the main UI object because it used the vtable for a
secondary interface. The installed executable's constructor and live object
agree on the primary vtable and its update slot. The corrected helper retains
the exact executable/signature checks; it does not accept arbitrary vtables or
alter campaign settings. Private evidence is under `monitor209-20261008-03`
and `faststart209-continue-candidate` outside this source tree.

The user also confirmed removal of the loud startup logos. Only the EA and
Dolby movie files were renamed with `.fvr-disabled` backups in the local game
installation. Campaign movies and gameplay audio were not changed. The private
`startup-audio209/disabled-manifest.json` retains their original hashes and paths.
No game assets are distributed here.

## Native checkpoint 208 result

`magazine-player208-01` exercised the ordinary scoped XM8 consumer with synthetic
controller poses: return the original partly loaded magazine, remove it again,
take a replacement and insert it. The strict sequence audit passed. All 240
stereo pairs arrived with no transport timeout. Loaded/reserve counts changed
from 22/191 to 30/183 across the independent native copies, conserving 213 rounds.
The consumer recorded one original return and one completed replacement, with
zero cancellations. Both start attempts passed their recorded admission gates.
Mapped executable bytes matched the tested DLL; cleanup and late native readers
passed. BC2 was closed normally after these records were saved.

This establishes one bounded native sequence, not headset acceptance, AEK
acceptance, full-magazine discard, or all-weapon coverage. The earlier generic
single-cycle audit was inconclusive because this fixture has two cycles. The
existing return-then-replace auditor validates the first return before auditing
the second transfer; its checks were not relaxed.

## Receiver diagnostics

The emulated player now records `presentation.jsonl` for every successfully
copied stereo pair. It uses the same delivered body-prop frame and ammo-counter
freshness policy as the XR host. Records contain original source times, owner
generations, native counts, both eyes' prop identities, and presentation time.
They cannot renew observations, claim missing geometry, or enter native reload
coordination. This provides evidence for invisible counters and chest-prop gaps
without asking the user to put on the headset.

The native operation bindings, geometry and enabled weapon configurations are
unchanged. The protected native source digest remains the checkpoint 208 digest;
the complete build receipt also includes the updated diagnostic receiver.
Both architectures pass all 190 suites, and the rebuilt software-D3D HUD test
passes. Matching private artifacts are frozen in `headset209-20261008`.

## Background presentation result

The combined rifle run was cancelled by the cursor/focus guard before input or
injection. A separate SPAS configured-holster run then used only simulated
controllers, with no foreground activation or desktop input. It received all
240 stereo pairs and all 240 matching body-prop frames. The HUD policy accepted
175 paired samples, all reporting the real 8/24 count, and hid during the
holstered interval. This is live data-path evidence; headset legibility remains
unverified. The private model cache loaded successfully.

Eight of those 175 valid-counter frames lacked the chest-ammo prop. Several
native draw records show the source absent before drawing but present afterward.
Carried weapon props also have intermittent gaps. These are now reproducible
publication gaps, not evidence of a missing cache or a Steam Link reconnect.
The remaining cause is not yet established for every gap.

The initial postflight read rejected a changing client/server owner. Its failure
is retained; a later independent settled read passed, as did 31 client and 31
server drain samples. Mapped code and native view restoration were verified.
This run does not establish the cancelled combined rifle sequence. Evidence:
`background-presentation209-01/presentation-summary.json` in the private runtime.

Candidate 210 tests up to three fresh reads at each body-source boundary and
samples the validation clock after collecting sources. It records first-read,
retry and unavailable counts separately for ammo, selected holsters and carried
weapons. All existing typed ownership/expiry checks remain; there is no cached
fallback, native ammo mutation or renewed source deadline. The first native 210
combined run recovered two ammo-source reads and twelve carried-weapon reads on
the second attempt. Its workload differs from the SPAS baseline, so these counts
do not establish a before/after flicker percentage or headset acceptance.

All 240 stereo pairs and body-prop frames arrived, and 196 paired HUD samples
passed the production freshness policy. The shoulder stages and original-magazine
return completed. The replacement was acquired but never seated: an actual
59.6221 ms native frame exceeded the adapter's 50 ms delta limit and cancelled
the established hold with a Timing failure. The subsequent 30/183 count came
from native fallback, not a manual insertion receipt. The strict combined audit
correctly rejected this run; its raw evidence is retained. See
[the shared timing correction](RELOAD-FRAME-HITCH-211.md).
