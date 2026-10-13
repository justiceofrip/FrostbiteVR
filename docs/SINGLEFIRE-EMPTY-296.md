# Local296: pistol fires through empty and manually refills

The exact MP443_sp fired 17 distinct ordinary controller-trigger presses in
the actual campaign (17/85 -> 0/85), stayed empty for 5.071 seconds, then removed
its empty magazine and loaded a chest replacement (0/85 -> 17/68). Independent
raw original-Update records confirm every shot on all three engine branches,
the empty dwell and both magazine operations. All three branches applied and
restored empty-reload suppression. There was no automatic refill, presentation
fallback, conflicting hand claim, GPU failure or camera-restore failure. All
240 published stereo pairs were consumed, with 40 pixel checks. Hooks drained
and BC2 remained alive. No operating-system input or headset was used.

This is additional native acceptance for the SingleFire magazine family enabled
in295. It adds an input-only diagnostic mode, disabled in normal builds; normal
gameplay behavior is unchanged. The driver never fabricates shots or ammo. It
retains one bounded trigger pulse across temporary body publication suspension,
then waits for fresh coherent native ammo before its next press. Negative tests
reject missing shots, automatic refill, reserve changes and changed identities.
Both architectures pass all 243 suites and four focused suites.

The first audit assumed sequential server/client callbacks. The retained native
records show a valid client Update overlapping the server Update: client exit
is after the server exit, with exact matching counts on both reads. The corrected
independent audit requires that actual ordering and uses the genuine helper-owning
server Update as its server vote. It does not change runtime admission, deadlines
or engine state. Both the original rejected audit and corrected result are saved.

Prior295 verifies returning the full original pistol magazine and discard/chest
replacement. Prior294 verifies the VR pause pointer and checkpoint recovery with
a removed rifle magazine; prior290 verifies eight SPAS pumps through empty plus
shell loading and F2000 magazine return; prior293 verifies five M95 bolt cycles
through empty (native magazine-refill fallback remains). These are separate
native proofs, not fleet, headset or full-campaign acceptance.

Still open: a pistol shot after the newly inserted magazine, slide/chamber
interaction, hip-holster routing, belt-fed native loading, broader exact weapon
enrollment, sustained campaign/death recovery and headset placement/visual checks.
Eighteen compiled magazine prerequisites do not mean eighteen accepted weapons.
No headset session was launched and nothing was published.

Evidence: `pistol296-native01/singlefire-magazine-audit.json`.
Source: `68b333be94a91a2039371f48c50eb95fd579684c0dfc09bf3c1e518d76929fab`.

Trace: `80f1e42d13c265ff17157f64b170202d89b289892c76b8daeb347dd3fcf1608c`.
