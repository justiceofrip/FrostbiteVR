# Checkpoint 436 R56 — shared equipment and destruction rendering

Prepared October 28, 2026. This source checkpoint preserves the latest integrated
normal build and the evidence needed to continue development. It does not update
the public 0.1.15 installer or install a new runtime on this workstation.

## What changed

- Magazine transfer admission and the final native entry check are serialized.
  A transfer that actually entered retains its completion evidence; a request
  rejected before entry cannot silently grant ammunition.
- Verified actor replacement can retire old local reload debt after the old
  callbacks drain. This does not award ammo or reset an unfinished interaction
  merely because another frame arrived.
- Continuous gun ownership survives processing latency using fresh controller
  evidence. The bolt adapter can derive contact from an already-owned attachment
  during a missing rendered-pose publication, without treating an expired pose
  as fresh or acquiring a new grip from it.
- The native destruction-edge projection caller is included in the existing
  stereo projection correction. Depth and exact temporary-state restoration are
  preserved; there is no second simulation step or blanket culling override.
- The snapshot also carries the accumulated launcher, menu, knife, rendering,
  scheduling and transition changes since the previous source upload. See
  [architecture](../../docs/ARCHITECTURE.md) and the dated history for details.

## Validation and its limits

Normal native source digest:
`af13daab2acb73ac73273f1a7719515b7befdbb89d3537db2b25eeba2c6f59e8`.
Both full normal builds passed **305/305 suites**. The custody regressions and
five read-only diagnostic regressions also passed. Normal gameplay feature
flags were preserved; finite diagnostic drivers are not enabled in this build.

| Actual-game episode | Observed result | Important limit |
| --- | --- | --- |
| F2000 + SPAS | Magazine removal/return/replacement, shoulder round trip, shotgun shot/pump and one manual shell completed; final rifle fire succeeded. | Representative combined sequence on the preceding shared integration, not every shotgun. |
| SCAR | Partial magazine return, replacement, support grip recovery and subsequent firing passed. | No full SMG-family or headset acceptance. |
| MP443 | 17/85 → preparation shot → partial-mag return → replacement → postfire 16/68; all three native firing copies agree. | Hip holster, slide/chamber and other pistols are not covered. |
| AEK | Thirty shots to empty, more than five seconds without automatic refill, manual replacement and postfire passed. Two-handed evidence retained the same support/root through empty. | The two-hand run's final external read was refused; its mechanical audit passed, not the entire runner. |
| M95, R56 | Five manual bolt cycles completed on all three native paths, then stock magazine fallback ended at 5/40. | This is manual bolt validation, not manual sniper magazine loading. No accepted missing-render-pose sample occurred in this run. |
| Destruction | Matched damaged-house captures show the formerly detached brick ring attached in both delivered eyes at two separated times. | One reproduced scene; broad scene/headset validation remains open. |
| Nambu100, R56 | Diagnostic build and exact normal restoration passed. Startup stopped before any gameplay input or mod attachment because the game did not retain focus; the temporary profile was restored. | Native reload test remains pending. This was a setup refusal, not a reload failure or pass. |

The test diagnostics use ordinary emulated controller trajectories plus bounded
native observations. Ammo checks join server, predicted client and replicated
client state. They do not substitute for physical-headset visual/comfort checks.
Recorded drops and receiver timeouts remain disclosed in the evidence summaries.

## Build and resume

Use [BUILDING.md](../../BUILDING.md) and the exact
`profiles/checkpoint436-r56` operation profile. Its source-bound header and
receipt are included; no local recovery directory is required to verify the
native dependency closure. The `opening426` calibration filenames are retained
because they are shared input records, not the checkpoint's version number.

The normal runtime remains locally frozen as
`unreleased-checkpoint436-custody-frame01`. The included evidence summary records
its binary digests for identification, not a promise of byte-identical binaries
from another compiler. Game assets, saves, raw traces, recordings, caches and
compiler outputs are excluded from this GitHub snapshot.

Continue in this order:

1. Exercise magazine handling with the normal knife, grenade and contextual-tool
   consumers enabled together. The finite reload fixtures disable those three;
   their isolated passes do not establish the combined behavior. A private
   combined-fixture proposal exists locally, but is not integrated here.
2. Classify the first cancelled stereo request after checkpoint replacement.
   Current recovery can deliver later paired images, but that is not a complete
   death/restart fix. A minimal observer is prepared locally, not enabled here.
3. Expand representative shared-mechanic coverage, using exact mounted campaign
   assets. Do not count catalog rows as working weapons. Belt-fed LMG cover/feed
   handling and broader pistol/sniper chambering still need implementation.
4. Verify the jeep and other campaign vehicle roles, then polish HUD flicker,
   knife audio/response, cutscene orientation and remaining visual issues.

No new headset acceptance, complete campaign, all-vehicle coverage or guaranteed
90 Hz follows from this checkpoint. Preserve earlier failed traces when comparing
results; do not replace them with a later passing summary.
