# Combined inventory and reload diagnostic — 204

This adds a bounded simulated-player sequence through the normal BC2 body
inventory, chest ammunition and magazine consumers. It keeps the same consumer
instances and shared hand arbiter alive throughout the run. It does not start
OpenXR or claim headset acceptance.

The explicit `--inventory-reload-probe` mode on both BC2NativeTrace and
BC2NativeIpcProbe requires a 60-second static async stream. The starting weapon
is the measured scoped XM8, with a partly used magazine and spare ammunition,
and a second assigned shoulder weapon. The current live checkpoint uses SPAS.
The driver reads the actual inventory assignments; shoulder slots are not
chosen from weapon names.

1. Wait for the ordinary automatic startup holster to settle; draw if necessary.
2. Holster the rifle and verify empty hands remain empty while clearing the shoulder.
3. Draw the other shoulder weapon, verify its new gun claim, then holster it.
4. Draw the original rifle and verify another current claim.
5. Return its original partly used magazine, remove it again, carry a replacement
   off the rail with wrist rotation, and insert it from the normal chest supply.

The driver changes only its private controller input. Native requests, ammunition
receipts, hand claims and renderer copy acknowledgements come from the ordinary
consumers. Completed stages do not reconstruct those consumers or manufacture
observations. Actor/space loss, stale inputs, changed assignments and missing
acknowledgements stop progress under finite deadlines.

## Diagnostic recording

The native callback journal retains its existing 20-second window and fixed
capacity. This combined mode opens that recording window once, when its reload
portion begins. Native invocation authority and the runtime start clock are
separate and unchanged. No journal is cleared, and repeated opens cannot renew
the recording deadline. Ordinary modes still record from runtime startup.

The source-bound operation receipt is in `profiles/checkpoint204`; calibration
headers remain in `profiles/checkpoint202`. Protected changes consist of the
explicit driver/wiring, chest-aware diagnostic motion, and recording-window
selection. Native hook signatures, admission capabilities, hide/show bindings,
ammo authority and restoration rules are unchanged.

## Validation and limits

All 186 C++ suites pass on x86 and x64. Added checks cover normal versus deferred
recording, immutable recording deadlines, startup settling, lost empty-hand
continuity, missing real-policy acknowledgements, changed inventory, expired
claims, duplicate packets, and actor/focus loss. The existing actual-consumer
reload loop also passes using chest supply for both magazine interactions.

The first actual-game attempt completed the switch and both magazine interactions
without consumer cancellations or magazine visual fallbacks. Its combined audit
remains **inconclusive**: the initial gesture overlapped automatic startup stow,
and the old callback recording window ended before the second native transfer.
Both diagnostic defects are now corrected and covered.

The corrected actual-game run passed the strict combined audit with all 17
stages observed in order, distinct current gun claims (2, 3, 4), and sustained
empty hands between both holsters. It completed one original-magazine return
and one replacement, with zero consumer cancellations and zero magazine palette
fallbacks. Client/server ammo changed from 20/191 to 30/181, conserving all 211
rounds. The existing strict native reload and startup-pulse audits also passed.

The receiver obtained 240 stereo pairs. The mapped executable sections matched
the frozen DLL, native hooks and helpers drained, native views were restored,
and BC2 remained responsive and was minimized afterward. The late server reader
retained 30 accepted samples and one rejected ownership-changing read; that
rejection remains in the raw evidence. No headset session was started.

Exact tested source closure:
`42b08f8976489a8d56fb2e4303e2e28915fd63b9af978c642231453d6f0b1d3e`.
Tested x86 probe SHA-256:
`e45f123a636f9bbdb9e862035af0c65763b855923fcb4c381b82c8c799e2306f`.
Raw machine-local evidence is retained under the `inventory-reload204-05`
monitor run and is not distributed as game content. Local evidence index:
`bc2vr-recovery/inventory204-evidence.json` outside this source repository.

This sequence does not establish active-reload interruption recovery in the live
game, pickup/vehicle/death transitions, every weapon family, or headset comfort.
Checkpoint 203's active-reload cross-profile recovery remains CPU-tested until
its own live sequence is completed. Repository changes remain local; the public
fork snapshot is not automatically updated.
