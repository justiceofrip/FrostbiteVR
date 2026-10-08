# Guided magazine and shell insertion

`ReloadInsertion.h` is a portable geometry and presentation policy. It places an
owned item using its authored hand attachment, catches its loading landmark near
a compatible socket, blends into a rail, and follows the user's actual insertion
stroke. `ReloadInsertionTests.cpp` exercises this with synthetic geometry. There
is no BC2 runtime integration, native ammunition write, granted magazine/shell,
reload acknowledgement or headset acceptance in this increment.

## Adapter contract

The immutable profile supplies an ID/revision, Magazine or SingleShell family,
and proper rigid canonical row-vector transforms in metres:

| Transform | Meaning |
| --- | --- |
| `itemFromHand` | Hand-local into item-local at the authored grasp |
| `itemFromInsertion` | The item's loading landmark into item-local |
| `weaponFromEntry` | Matching socket-entry landmark into weapon-local; local +Z is the inward rail |

Thus the freely held item is `inverse(itemFromHand) * weaponFromHand`. The policy
measures `itemFromInsertion * rawItem * inverse(weaponFromEntry)`, so a wrist near
the socket cannot substitute for the actual magazine/shell tip. Guided hand pose
is always `itemFromHand * guidedItem`; the item does not slide through its hand.
The adapter must derive raw `weaponFromHand` from same-packet hand and weapon
poses, before IK, grip snapping or previous guided output.

Profiles also supply travel, capture/hold distances and orientation cones,
seating tolerance/dwell, alignment duration, maximum observed pose step,
maximum packet age/gap and total guided duration. All test dimensions/timings are
synthetic. No measured BC2 shell or magazine profile is provided. Native generic
bone motion, including a bone correlated with a reloading wrist, is insufficient
to identify a prop, socket, loading landmark or insertion axis.

Samples identify actor, actor generation, equip generation, reference space,
weapon and resource IDs/generations, tracking epoch, profile revision, original
input/geometry sequence and observation/deadline times. They carry current exact
`HandInteraction` AmmoObject and other-hand GunHold claims. Both claims must match
the identities, have valid contact keys and remain unexpired. The adapter first
updates the arbiter and supplies its current claims; this policy cannot mint,
steal or renew them. Focus, both tracking states, held state and exact resource /
socket eligibility remain mandatory. Eligibility includes mechanism access and
compatibility, not just controller proximity.

## Interaction behavior

1. A fresh raw sample must first show the held item's loading landmark outside
   the entry sphere on its front side. Starting inside or behind a socket does
   not seat it. A discontinuous position/orientation step cancels intent.
2. Capture requires the nearby entry sphere, front half-space and orientation
   cone. Its first guided pose equals the freely held pose. Smoothstep alignment
   then removes radial/orientation error over the configured duration. Raw axial
   displacement drives continuous progress; no distant item is snapped to the
   socket and no timer inserts an item by itself.
3. A larger hold volume/cone prevents capture chatter. The user can reverse the
   stroke. Withdrawing beyond the front hold bound, leaving the hold volume,
   releasing, losing eligibility/tracking/ownership or exceeding the time bound
   cancels guidance. Recovery requires a new observed front-side approach.
4. At full alignment, reaching the final seating tolerance starts an observed
   dwell. Twice that tolerance is the hysteresis exit. A completed dwell yields
   exactly one seat candidate and one `Seated` haptic intent, then latches until
   release/cancellation. Final visual correction is bounded by seat tolerance.
   Pulling back after that event cancels local guidance; it sends no unseat or
   native rollback operation.

Magazine orientation is keyed. Shell orientation is also keyed by default.
Only an explicitly authored `AxialSymmetry` shell profile ignores roll about its
loading axis: it checks the loading-axis cone and applies the shortest swing to
the rail while retaining roll. This option is rejected for Magazine profiles.
It is not a blanket assertion that every game's shell model/socket is symmetric.
Pose-jump limits still cover actual full rotation, including rapid axial roll.

Duplicate input sequences can trigger current safety cancellation but cannot
move guidance, advance alignment/dwell, rearm or repeat a seat/haptic event.
Deadlines expire at `now >= deadline`, matching `HandInteraction`. Even a fresh
packet after a tracking gap cannot continue an old stroke. A hand-claim token
replacement, including after lease loss, is a new ownership boundary.

## Reload and resource authority

The seat result contains exact identity, profile, hand claims, source sequence
and a monotonic instance-local event ID. Magazine emits `SeatMagazine`; shell
emits `InsertRound`. The adapter bridges this to `ManualReload` only while the
operation, resource and native binding remain eligible. Multiple policy instances
need an adapter-wide monotonic gesture ID; do not forward colliding local IDs.
A physical seat candidate is not native success. `ManualReload` still waits for
its exact native acknowledgement, and the resource owner reconciles an applied
native effect if tracking or presentation is interrupted. Seating does not
subtract reserves, populate inventory or create a second copy of an item.

The [feed mechanism policy](FEED-MECHANISM-20261001.md) remains separate. A future
LMG feed/box insertion can reuse authored grasp/rail math only after its cover,
latch, feed and container prerequisites and physical geometry are verified.
This policy deliberately accepts no Belt family; it does not skip those steps or
turn an LMG reload into a magazine operation.

## Validation and remaining work

Deterministic tests cover authored attachment placement, no initial capture pop,
mid-blend rotation/position, exact distance/step boundaries, front/back approach,
pose jumps, rail reversal/hysteresis, keyed versus explicit round-shell roll,
seating dwell/hysteresis, single haptic/seat events, duplicate packets, matching
ManualReload acknowledgement separation, claim and identity changes, current
safety loss, exact deadline expiration, gap/clock rollback, timeout, invalid rigid
geometry, persistent event IDs across reset and a moving hand/weapon pair.

Capture currently needs an observed fresh pose in the entry volume; it does not
reconstruct a fast stroke that crosses the entire volume between packets. A
future swept-contact extension needs its own geometry and reversal regressions.

BC2 still needs positively identified item render geometry, hand attachment,
socket/rail measurements and resource ownership/native transfer bindings.
Accepted support/weapon/sight mechanics are unchanged. Per-item geometry profiles
can share this recognizer across Frostbite adapters; weapon names and guessed
native offsets do not belong in it. See the [porting roadmap](FROSTBITE-PORTING-ROADMAP.md).