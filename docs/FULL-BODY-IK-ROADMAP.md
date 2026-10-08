# Local full-body IK roadmap

Status: October 1, 2026. Design and source review only; no runtime changes,
new hooks, game launches or live probes were made for this document.

The practical first increment is a portable torso-target policy and an offline
skeleton preview, retaining native pelvis/leg animation and the current arm,
weapon and hand layers. A visible local body also requires proving which soldier
mesh sections BC2 supplies and renders. Named leg bones alone do not establish
visible legs. This plan does not depend on a server or remote-pose transport.

## Existing foundation and exact limits

| Evidence | What it establishes | What it does not establish |
| --- | --- | --- |
| [ArmIk](../include/fvr/interaction/ArmIk.h), [implementation](../src/interaction/ArmIk.cpp) | Two independent arm chains, native segment lengths, shoulder/pole targets, bounded reach, descendant propagation and native fallback for disabled arms | Spine, pelvis, knees, feet, ground contact or mesh collision |
| [TrackedRig](../src/interaction/TrackedRig.cpp) | Common eye/body reference, independently tracked wrists, anatomical shoulder anchors, dynamic authored weapon attachment and owner/space resets | A measured full-body pose or a complete torso solver |
| [Bc2Rig](../src/games/bc2/Bc2Rig.cpp) | Local first-person owner/rig relationships, named topology, inverse binds, evaluated native IK palette and bind/skin consistency | Third-person body ownership, vertex weights, body draw sections or their culling |
| [Bc2RigPublication](../src/games/bc2/Bc2RigPublication.cpp) | Private upper-torso, arm, hand and weapon layering at a verified synchronous palette-copy boundary | A universal actor-palette publisher or verified body visibility toggle |
| [Hand anatomy capture](../reports/hand-bind-20261001.json) | 147 named bones; fingerprint `a7f219a1426216ab`; rechecked owner/topology and selected hand bind evidence | A complete saved torso/leg reference pose, body mesh or material inventory |

The captured hierarchy is `grannyRootBone -> Hips -> Spine -> Spine1 ->
Spine2 -> Spine3 -> Spine4`. Legs branch from Hips through Left/RightUpLeg,
Left/RightLeg and Left/RightFoot. Neck/Neck1/Head and both shoulders descend
from Spine4. The current publisher's common arm ancestor is therefore Spine4
for this topology. The weapon hierarchy branches earlier through `jntWpn_0`
under Spine; moving Hips or Spine indiscriminately would also move weapon roots.

Current torso stabilization places the shared upper-body subtree from bind
anatomy when both hands are tracked. `PhysicalTorsoFrame` follows physical HMD
translation without rotating wrists from HMD orientation. `TrackedBodyFrame`
follows authoritative horizontal actor movement, compensates only roomscale
movement actually consumed by the collider, and retains native vertical stance.
These solve important alignment problems; they do not estimate pelvis position,
distribute spine bend or plant feet.

Preserve the user's accepted SPAS/XM8 grip handling, recenter and free-left-hand
animation. The newest right-index, capacitive-input and reload-grip candidate
has native/deterministic evidence but still needs its own headset acceptance;
see [current hand candidate](HAND-INPUT-RELOAD-20261001.md). Sight manipulation
works mechanically and still needs visual polish. Full-body work must not
silently change those acceptance scopes.

## First increment: body targets and diagnostic replay

Implement a small shared `BodyPoseTargets` policy, initially consumed only by
a developer skeleton preview/replay. Use synthetic anatomy and existing saved
hand/owner observations first; obtain a complete bounded native body-pose
capture later before claiming BC2 torso/leg calibration.

Suggested shared inputs and outputs are contracts, not an implemented ABI:

| Shared field | Meaning |
| --- | --- |
| Actor generation, rig-profile revision, reference-space generation, sample sequence/time and deadline | Prevent state reuse across respawn, rig replacement, recenter or stale input |
| Explicit role map and validated parent topology | Root, pelvis, ordered spine/neck chain, head, two arm chains, two leg chains and feet; optional roles stay absent |
| Reference/native matrices and anatomical lengths | Adapter-supplied canonical poses; no BC2 names, offsets, bone numbers or mesh objects in core math |
| Head/hand targets and per-target provenance | Tracked, NativeAnimation, Estimated or Unavailable, plus validity; inferred pelvis/feet are never labeled tracked |
| Native root/stance/locomotion and optional ground contacts | Authoritative native context separate from the cosmetic body pose |
| Body targets, bounded pose edits and reason/status | Inspectable result that existing arm/hand solvers and a monitor preview can consume |

Use canonical row-vector LH transforms with explicit units. A portable diagnostic
snapshot should normalize translation to metres and state its origin/basis,
rather than exporting world-unit-dependent matrices without metadata. Keep raw
tracked targets, resolved wrists and native/estimated body poses distinguishable.

Start with native pelvis and legs unchanged. Estimate only a bounded upper-body
heading/lean target; distribute it through individually attached spine joints
when a validated reference exists. Use the native locomotion/body heading as the
baseline, with yaw dead zone, hysteresis and bounded angular speed. Fade head-yaw
influence when looking almost vertically; do not let a unilateral hand reach,
gun recoil or controller aiming spin the torso. Looking over a shoulder must
allow head/torso separation. Apply head orientation to a cosmetic neck/head chain
later without feeding it back into camera, aim or controller coordinates.

The first increment passes when replay preserves bone lengths/attachments and
the existing resolved wrist and weapon targets, leaves native legs unchanged,
produces bounded yaw/lean, and resets deterministically. It can proceed without
a headset or a new render hook. It is useful to the flatscreen diagnostic viewer,
but it is not yet a visible in-game full body.

## BC2 visibility work before an in-game body

Perform a bounded, read-only inventory in a later native session:

1. Follow the owned local soldier's render/mesh instances and distinguish its
   first-person and third-person representations. Record their actor/weak
   ownership, skeleton, palette, instance lifetime and generation relationships.
   The current local `ReadFirstPersonRig` path is not proof for another mesh.
2. Identify torso, pelvis, leg, neck/head and arm sections, their skin-bone
   mappings, active LOD, material/depth behavior and submitted draw membership.
   Prove that the first-person mesh contains useful torso/leg vertices or that
   an owned third-person body can be rendered with an independently validated
   palette binding. A complete skeleton can serve an arms-only mesh.
3. Establish local-player visibility, first/third-person flags, per-eye culling,
   animation LOD and shadow membership. Separate suppressed geometry from
   absent geometry. Verify callback/thread lifetime before any override.
4. Determine how to retain one set of arms, the accepted weapon and visible
   torso/legs without duplicate arms, seams or duplicated body shadows. If
   sections cannot be separated, report that limitation instead of guessing a
   bone scale or broad draw suppression.

The known first-person projection correction is scoped to BC2's authored
viewmodel branch; see [native projection evidence](CONTROLS-20260930.md).
A world-space torso/leg mesh must use its verified world projection and depth
path. Do not simply inherit the gun's authored FOV classification.

For near-eye geometry, prefer verified local-view section visibility or a
separately validated body representation. Hiding the head must preserve neck,
shoulder and attached geometry, with an explicit shadow policy. Zero-scaling
Head or moving it far away can corrupt shared skin weights/bounds and does not
prove a clean cut. Global near-plane changes are not a substitute for section
ownership. Check looking down, extreme head rotation, leaning into the chest,
both eyes, occlusion and mirrors/shadows where present.

**First visible-body milestone:** owned torso/legs rendered in local VR with
native lower-body animation and the existing hands/weapon, before custom foot
placement. This is the earliest useful visible result; its blocker is the mesh,
section and render ownership evidence above, not a missing general arm solver.

## Pelvis, feet and locomotion after visibility

Three tracked devices do not measure knees, feet, hip position or hip yaw.
Add an estimated pelvis only after explicit body calibration and validated
native stance/root inputs. Use skeleton proportions and user height/calibration;
a recentered eye origin alone is not floor height. Keep the gameplay collider
authoritative. Distribute physical lean and crouch within reachable anatomy,
and avoid applying consumed roomscale movement twice.

Leave native lower-body locomotion active initially. For later foot IK, require
adapter-supplied ground hits, normals, stance/velocity and moving-support identity
with timestamps. Build bounded foot planting, release/step transitions and knee
pole estimates on top of those inputs. Preserve segment lengths; release plants
on stairs, teleports, support changes or invalid contacts. A heuristic flat floor
is appropriate for a labeled preview, not a claim of native ground contact.
Future body trackers can replace estimated targets through the same provenance
contract without rewriting the BC2 publisher.

## Layering and lifecycle requirements

Always start from the current original native evaluated pose. A proposed order
is native lower body -> validated pelvis/spine layer -> existing arm solver ->
item attachment/support or mechanism wrist constraint -> explicit hand roles ->
right-index overlay -> exact private palette plan. Roles and branch conflicts
must be explicit; a parent edit propagates to all intended descendants before
child layers resolve their final targets.

Keep Free, WeaponSupport and MechanismGrip distinct. Authored gun support remains
fixed per grasp token through reload; mechanism grasp keeps its fixed contact
anchor; free hands use their independent reference poses. Contact/gesture
decisions still read coherent raw tracking, never the already attached visible
hand. A body solver cannot reattach the off-hand implicitly, reset support on
every spine update, or accumulate the right-index overlay. Right empty hands
remain disabled until the separate item/hide/fire-suppression contract is met.

Extend the existing publication guards rather than replacing them: actor/weak,
owner generation, equipped item, skeleton/profile, source pointer/count, exact
native source bytes, input space and original deadline. Stage privately and
publish at a verified copy boundary; never use last frame's solved palette as
native anatomy. Preserve recognized native hidden leaves and unchanged branches.
Both eyes consume the same solved native frame; no second animation/simulation
advance. The current first-person packer validation cannot be assumed to cover
a newly discovered third-person body path.

| Transition | Required behavior |
| --- | --- |
| HMD removal, stale tracking or Steam Link reconnect | Cancel stale body estimates/interaction previews; retain only explicitly valid native fallback; recover through existing reference/neutral rules |
| Manual recenter | Reset body-estimation history by space generation; preserve settled native weapon attachment semantics |
| One hand untracked | Preserve or fall back that branch; the current both-hand torso stabilization gate must not be silently widened |
| Crouch, sprint, jump or animation LOD change | Preserve native stance initially; validate inferred constraints independently and reject collapsed anatomy as a length source |
| Vehicle/seated/turret | Separate seat-relative profile and render ownership, or native fallback; current on-foot rig does not establish seated-body support |
| Death, ragdoll, respawn or actor replacement | Invalidate by owner/lifecycle generation before publication; do not solve old standing anatomy into a replacement/ragdoll palette |
| Equip/weapon mode transition | Keep body estimator separate from settled item attachment and native acknowledgement; preserve support/mechanism cancellation rules |

The earlier death crash and LOD artifacts remain deferred issues. This roadmap
does not claim they are fixed or that those transitions have passed validation.

## Validation and parallel work boundaries

1. **Portable policy/replay:** synthetic unequal limbs, nonzero bind rotations,
   yaw wrap/vertical look, identical HMD/controller translation, head-only turns,
   crouch height, missing roles, stale/duplicate frames and owner/space changes.
   Assert native lower-body preservation and unchanged accepted wrist targets.
2. **Read-only native evidence:** coherent body-role/reference capture plus owned
   mesh/section/LOD inventory; preserve rejected racing samples. No capability
   is granted merely from a familiar name or executable hash.
3. **Opt-in local publication after bindings:** source byte preservation,
   unrelated actors/weapon branches unchanged, correct palette packing and
   restoration/fallback, no drift over repeated/partially animated frames,
   identical body pose for both eyes. Keep diagnostics bounded and opt-in.
4. **Visual acceptance:** first stationary look-down and arm reach; then walking,
   physical lean/crouch and turning. Test stairs, headset/recenter, seated/vehicle
   and death/respawn separately as their bindings mature. Confirm near-camera
   surfaces, arm seams, feet, culling and shadows in headset and monitor views.

Work can proceed in parallel on portable estimation/replay, BC2 mesh/visibility
research, and diagnostic visualization. The existing native hand candidate stays
frozen until each new integration has its own gate. More anatomy polish should
not block the first native-legged body milestone.

## Relationship to 2142 and remote avatars

Read-only reference:
`<local-bf2142-workspace>\public-source\BF2142VR\docs\ik\LESSONS.md` and
`src\bf2142\multiplayer\RemotePresentation.cpp`.
The useful demonstrated approach is to preserve native hips/legs while
retargeting connected upper-body subtrees, account for authored bind rotations,
and keep native LOD output separate from previous IK. Its torso policy already
reduces unreliable near-vertical head-yaw influence. Its bone layout, bind pitch
constants, draw hooks and network ownership are Refractor-specific and cannot
be transplanted.

Local body visibility, a monitor skeleton/debug preview, and other players seeing
a VR avatar are separate deliverables. Share canonical target/provenance,
anatomical role and pose-edit contracts. A remote adapter still must resolve its
own actor/session identity, native root/stance, rig/LOD, held-item authority and
publication lifetime. The current first-person local owner check supplies none
of that remote binding. Server/transport work belongs to the separate networking
track; it does not make a missing local body mesh appear.
