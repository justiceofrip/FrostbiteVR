# Shared ammunition follow-up — October 3

The 04:28 Eastern headset test accepted the AEK grip/underbarrel support but
rejected replacement magazine placement and body ammunition. The user clarified
that 30–37 seconds shows a replacement magazine spawning at the barrel, not merely
a seated magazine moving in native animation. The recorded run lacks complete
operation/count logs, so the exact path taken in that video remains unproven.

## Implemented shared carry repair

An actual-consumer regression reproduces an independent temporal-space bug:
the replacement target was relative to the sampled weapon, but presentation
multiplied it by a newer weapon pose. Moving the weapon displaced the loading
hand by 0.681559 world units in the failing baseline. Stationary tests missed it.

Free replacement presentation now retains its original sampled weapon frame,
owner, item, claims and deadline. It cannot renew that evidence or transplant
the target into a newer gun pose. Guided insertion, attached magazines and
same-original return retain their existing gun-relative behavior. Both accepted
XM8 and the generated AEK pass movement, body transform, world scale, recenter,
expiry and shared-consumer checks. Native calls are mocked in these tests;
this is not a claim that the recorded barrel-spawn failure is headset-fixed.

The magazine consumer also publishes a render-only supply source using its
actual native reserve, right GunHold and configured belt contact. It allocates
no ammunition. Availability currently covers the existing partial-positive
replacement route; full-magazine original return remains separate, and the
empty-magazine automatic native reload route is still unfinished.

## SPAS cancellation and availability

A pending old shell reservation no longer blocks different, freshly proved
equipment after an exact old-cycle callback-drain receipt. The pending outcome
stays quarantined; neither counts nor deadlines are fabricated. The original
equipment stays blocked until reconciliation. Recenter alone does not qualify
as replacement. Scoped XM8 uses a persistent physical item key distinct from its
native rifle pointer; a fresh exact magazine-family mapping admits that replacement
only for retiring the old block. SPAS acquisition retains its direct identity check.
Mesh observation and input sequence counters are independent.

Bounded transition diagnostics now explain supply availability before any shell
is acquired, including current source/mesh/raw evidence, counts, hand ownership,
body contacts and cancellation flags. Gameplay labels magazine-busy cancellation
explicitly. These changes do not establish why the previous headset SPAS supply
attempt failed: its final detailed trace was empty.

## Independent body-ammo rendering remains incomplete

Gameplay and rig publication now carry the same source/contact into a common
body-ammo pose and per-eye renderer adapter. The independent prop uses named
part-local geometry and the authored grasp, without relocating the magazine
still seated in the gun. Exact asset, mesh, part and rig identities accompany
cache hashes. Fresh input/native equipment and original source expiry are checked
again at the render boundary.

The adapter is wired to the post-native-eye boundary and reports actual target
descriptors and specific missing prerequisites. Drawing remains unadmitted:
native query completion and the matching scene depth/projection are not proved,
and a production geometry upload route is not yet connected. CPU geometry and
state/owner checks do not substitute for that evidence. Belt magazines are NOT
claimed visible or fixed by this checkpoint; existing SPAS palette rendering
remains separate. No new GPU, game, input or headset actions were performed.

## Validation and next check

Full builds/CPU suites: x86 141/141; x64 141/141.
The shared carry test explicitly requires both XM8 and generated AEK when the
private geometry header is configured. The body-source test likewise checks both
profiles and source/space/claim expiry. FVR_TEST_GPU remains OFF.

Next native work must collect the new availability/render-boundary records and
verify moving-hand magazine presentation before another broad acceptance claim.
Empty-magazine native cancellation, independent belt draws, wider weapon-family
admission and the reported SPAS no-shell case remain open. Accepted AEK grip is
preserved; chambering stays deferred. No BF2142 changes or release publication.
