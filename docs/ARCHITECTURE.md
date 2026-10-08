# Engine boundaries

## Combined player diagnostic — 204

One explicit bounded driver composes ordinary shoulder inventory and chest
reload input. Consumer/native/hand ownership instances persist across every
stage. Diagnostic observations never create acknowledgements. A separate
one-shot callback recording clock starts at the reload portion; native runtime,
invocation and lease clocks retain their original authority and lifetimes.
The ordinary recording mode and capacity are unchanged. See
[the sequence and limits](COMBINED-INVENTORY-RELOAD-204.md).

## Cross-profile reload retirement — 203

Retirement receipts describe the old native owner and cycle. The current attached
magazine describes the independently validated current family/profile. Keeping
these checks separate permits a drained old reload to rebaseline onto a different
rig without acknowledging an old ammo transfer. Persistent CPU transition tests
cover this separation; native/headset acceptance is tracked independently.

## Diagnostic completion retention — October 8, 202

BC2's independent native invocation result remains the gameplay authority. When
the diagnostic record gate is busy at End, a bounded 64-slot journal preserves
only the exact already captured completion and record id. Publication is bounded
and nonblocking; one consumer drains under the existing record gate. Drained
reports keep that gate through row reads. No native memory is reread during drain.
Original id/thread/time/duplicate validation remains active. Pending, overflow,
rejection and separate Begin/End contention counters expose incomplete evidence.

## Original start pulse and native context age — October 8, 201

BC2's physical magazine consumer passes a typed, immutable startup-pulse proof
with the exact original control. The output pulse uses that same end. The native
cycle can establish Holding only after the pulse ends and all three existing
safe contexts were genuinely observed afterward. Context timestamps precede their
memory reads; policy processing cannot make an old context fresh. KeepAlive never
renews the proof. Established holds reject missing or stale source evidence.
No native safety check, weapon geometry or capability admission is broadened.
The pulse proof is an internal adapter contract, not portable ammunition authority.

## Render geometry lifetime — October 7,193

MagazinePropTarget is presentation evidence. Its expiry is the minimum of the
original geometry/controller input and exact gun/hand claim deadlines. Native
ammo-observation freshness is independently required for native operations and
interaction progress; it is not the lifetime of an already valid visual pose.
The renderer still checks original and current input/owner/cycle/role/claim and
selected-mesh evidence. Cancellation or a changed target invalidates the palette.
This also keeps seated-magazine support-grip display stable without granting ammo.

## Native observation availability — October 7

BC2's magazine adapter exposes Ready, Deferred and Rejected observations. Native
owner/configuration/cancellation-epoch loss rejects; lock contention or a current
cycle with unavailable publication defers. Accepted control renewal alone cannot
prove all three native firing states are held. After an exact unseat acknowledgement, a continuous publication gap has a
fixed deadline capped by the prior accepted control and 50 ms; repeated packets
cannot extend that deadline. Startup has no prior positive hold to expire. Explicit Deferred waits poll the
existing portable transaction without gestures or acknowledgements, preserving
its original 4-second unseat and 30-second transaction bounds. Duplicate startup
packets also check those bounds. Native operation deadlines remain independent.

During this bounded wait, portable policies validate input, ownership, focus,
tracking, release and exact existing hand tokens. AmmoSupply can renew an existing
held resource without acquiring another or recording geometry history. Magazine
interaction can maintain a removal claim without advancing insertion or a native
transaction. Old presentation evidence expires at its original deadline. After
submission, the original native completion path remains active. Each future engine
adapter must define its own observation availability and native rejection evidence.
The October 7 staged CPU checks pass; full native/headset acceptance is pending.

## Physical ownership and native reload evidence — October 5

A tracked grip and a native ammo observation have different lifetimes. An already
held magazine mechanism, magazine prop or shell retains its physical hand claim
using the current controller packet's original deadline. A short native/source
observation must not prematurely expire that physical claim before the following
controller update can observe fresh native state. This rule is shared in
DetachableMagazine and AmmoSupply; it does not change the arbiter or revive an
expired token. Duplicate packets cannot extend their own lifetime.

Native/source ownership, freshness and budget remain required for interaction
progress. Renderer targets, historical geometry, seats, reservations and ammo
acknowledgements keep their own original deadlines. In particular, a current hand
claim does not make an expired source valid for ReserveFrom.

BC2's magazine completion adapter separately observes native client prediction
restoration. Only an exact immutable native Restore of the active operation's
predicted transfer can retract that client receipt before any server transfer.
Authoritative server accounting and the original completion deadline do not change.
Future adapters need their own native prediction evidence; these BC2 semantics
must not be inferred solely from sharing the Frostbite engine family.

## Menu contention and XR texture ownership — October1

XR swapchain format is typed even when the runtime returns typeless D3D11 texture
storage. OpenXrMenu constructs an explicit RTV in the negotiated format. Pointer
allocation is independent of menu pixels; failed initialization retains the chain
and retries at500ms intervals without submitting an uninitialized pointer.

A zero-wait IPC Busy result is neither native menu exit nor controller focus loss.
The presenter retains the last valid menu state only to its original QPC deadline;
no Busy read extends freshness. BC2's command sequencer preserves the edge baseline
on Busy while dispatching no new input. Actual timeout/closed/invalid/focus/space
changes retain the existing rearm behavior. Tests cover pointer pixels, real mutex
contention, original deadline expiry, and fresh world resume.

## Native menus and reload observation — October 1

MenuPointer owns ray/panel geometry, neutral rearming and tracking/focus loss. A
separate MenuChannel carries native menu state, controller intent, GPU raster
and exact consumption feedback; the stereo frame protocol is unchanged. The
D3D11 producer copies the final native menu buffer, and OpenXrMenu presents it
as a quad with separate cursor/laser layers. These images are never passed off
as stereo world views. Menu entry cancels pending world delivery and clears
retained world images; exit waits for fresh world rendering. Current pointing
requires the verified1280x720 logical canvas and16:9 raster.

BC2 alone discovers native menu ownership, APT cursor/buttons and distinct
Menu/Back actions. Its exact callback caller/controller/input-node proof permits
worker migration with nonblocking serialization. Every held pointer/Cancel press
retains an exact native cleanup capability across focus/menu epoch changes. No
OS input injection is used. Native open/click/close and GPU fixtures pass; headset
acceptance and persistent desktop-window hiding remain separate open items.

Bc2ReloadState verifies bounded native weapon/config snapshots; Bc2ReloadFlow
verifies exact update/commit/transfer code; Bc2ReloadFlowRuntime only observes
those original calls and their nesting. Twenty-second/12288-record caps and fresh
owner leases bound work. This layer supplies evidence, not ammunition authority,
manual reload gating or successful operation acknowledgements. Physical insertion
geometry and interaction belong in portable policy; native resource ownership,
renderable ammo props and completion belong to each game adapter.

## Native rig and portable arm targeting

ArmIk accepts canonical world poses, validated parent topology, arm roles and
wrist/pole targets. It generates pose edits without native addresses or timing.
Bc2Rig alone resolves native names and palettes, converts handedness, validates
bind/skin consistency and prepares byte-exact palette edit plans with owner/pose
identity. It distinguishes the native evaluated IK palette from ordinary world
bones and keeps SIMD padding unchanged. Neither layer applies game memory edits.
TrackedRig also supplies body-frame shoulder/pole anchors, preventing native gun
aim from forcing the other wrist through reach clamping. ArmIk accepts optional
shoulder origins and keeps lengths from current native animation. See
HAND-COUPLING-20260930.md for native verification and remaining headset limits.
TrackedRig supplies portable grip calibration with independent wrist rotation
and translation; RetargetRigSubtree keeps native child animation in an attachment.
Bc2RigPublication now owns the verified synchronous native palette-copy boundary.
Private immutable palettes are packed into native request memory without editing
animation sources. Caller-owned weapon output feeds native root/effect consumers.
Original input deadlines, actor generation, equip/space identity and exact source
checks guard publication. See HANDS-20260930.md for binding evidence and limits.


## September 30 controller and first-person integration

ControllerInput/AimFrame and action policies remain portable. OpenXrInput samples
poses/actions in the same calibrated space/time as the eyes; the separate v3
shared-memory input lane preserves its original QPC deadline independently of
pending frames. InputPacket v2 is 288 bytes (v1 is still readable); FrameRequest v2 is 248 bytes.
Bc2Gameplay owns native input discovery, ownership/epoch guards, gather/update
hooks and angular aimer setters. Controllers and MotionAim are opt-in.
The renderer uses a local upright body base for HMD views while preserving native
gun yaw/pitch. A verified first-person transform plus per-eye context/projection
scopes renders native gun/arms without replaying simulation. Native offsets and
first-person classification stay in BC2; projection retarget math stays shared.
Horizontal roomscale body-follow now has an opt-in BC2 locomotion binding and
first headset acceptance. Live hand/weapon publication is opt-in and native-tested; headset acceptance is pending. See CONTROLS-20260930.md for measured evidence and limits.

The September 28 artifact repair adds portable depth-preserving projection
retargeting in math/ProjectionOverride.h. BC2 discovers and observes its own
mesh/terrain projection overrides, then stages them in the existing exact-write
scope. Native jobs and simulation order stay unchanged. Pass GPU inventory is
opt-in; captured correction passes, headset confirmation remains pending.


The async IPC request lifetime is150ms (cap200), independent of the synchronous
wait budget (cap50). This permits multi-native-frame rendering and GPU fence
publication at60FPS without blocking the XR thread. Original-pose presentation
and250ms retained age remain unchanged; latency/timeout telemetry records the
handoff. See DELIVERY-REPAIR-20260928.md.

## September 27 implementation status

BC2 connects through the shared D3D11/protocol-v2 bridge to x64 OpenXR. The presenter
now supports nonblocking IPC and bounded reuse of its last complete image pair
with the ORIGINAL source poses/FOV. Source validation and private scratch copies
precede swapchain replacement. Invalid/focus/space transitions reset retention.
These policies are independent of BC2 offsets and apply to future game adapters.

Native camera-binding correction now passes bounded BC2 campaign checks: distinct
per-eye matrices, controlled projection shifts, moving poses and exact restoration.
BC2-specific discovery/role matching remains in NativeProbe. The reusable
ExactWriteBatch applies validated constant ranges together and restores only its
exact writes; it contains no native engine addresses. Native update/job order stays
unchanged. Complete temporal-history and render-subsystem isolation remains
unverified, and production capabilities stay zero pending integration gates.
The September 27 combined native/OpenXR retest received positive user feedback
for the image, with a direction-dependent black-dot artifact still open. See the
latest HANDOFF checkpoint for the exact scope and remaining integration gates.

## Contract versus engine implementation

The shared core has no Windows, DirectX, OpenXR SDK, native process, vtable or
Refractor dependency. Math functions with `Lh` names explicitly mean canonical
row-vector left-handed matrices; they are not assertions about Frostbite's
native layout. OpenXR poses use metres, +Y up, -Z forward. A verified adapter
must convert native handedness, matrix layout, units and depth convention.
`WorldFrame.farPlane` is an absolute distance, not Refractor's far delta.
The renderer must handle culling and any reverse-Z/native projection conversion.

`FvrCore` and `FrostbiteDiscovery` are separate static libraries. `BC2Adapter`
is a DLL with a fixed-width, versioned C metadata entry point. The current ABI
reports identity/status/capabilities, with an optional BC2-specific C discovery export; render and input callbacks are not
exported yet. `IRenderAdapter`/`IStereoSink` are internal C++ contracts. Extending
the native callback ABI requires a versioned struct, explicit sizes, same
architecture, and explicit resource ownership. Never expose C++ vectors,
exceptions, STL objects, or borrowed game pointers across a DLL/IPC boundary.

The executable and native game module may be Win32. The established 2142 design
used an x64 OpenXR presenter to avoid 32-bit runtime limitations. Preserve that
separation where necessary, but define a distinct Frostbite protocol and named
objects rather than silently reusing BF2142 IPC names or layout. The presenter
and frame producer must agree on protocol, GPU adapter, resource generation,
texture format, synchronization, reference-space generation and timing.
FvrD3D11 now implements independent-process GPU texture transfer. FvrFrameChannel supplies versioned tracking/ticket IPC with deadlines, GPU feedback and peer-exit handling. Its native game producer remains pending. FvrOpenXR implements the host and projection submission path; headset acceptance is pending. See D3D11_BRIDGE.md and OPENXR_HOST.md.

## Required invariants

1. One native simulation/animation advance supplies both eye passes. A second
   call to a Frostbite top-level frame/tick is not an acceptable render adapter.
2. Save native state once; restore after either-eye failure and before external
   publication. An ownership/device change invalidates the entire pair.
3. Both eye images belong to the same native frame and device generation.
   They cannot alias the same resource slice. Never substitute menu rendering
   when world stereo is unavailable.
4. OpenXR head/eye/controller timestamps share a runtime clock. Recenter changes
   the reference-space generation; stale generations do not reach gameplay.
5. Recoil remains in native weapon/simulation behavior. The viewing and movement
   basis may remove recoil only after the corresponding engine behavior is
   understood. Both hands, HUD and body must share the same turn basis.
6. Native capabilities start disabled and are granted independently from
   verified signatures, executable section ownership, ABI, object identity and
   relationships. Hashes identify evidence; a new hash alone is not a hook.
7. Rig profiles supply bone names/indices, parent topology, palm axes and weapon
   attachments. Restore only exact previous mod writes before native animation;
   apply after native animation. Never treat the previous solved pose as the
   authored arm length source. Respawn/skeleton replacement changes generation.
8. Input outputs are semantic requests. BC2 owns key/button mapping, stance
   support, interaction rules, authoritative fire matrices, and ownership.
   No Refractor keycodes, item IDs, native prone toggle or vehicle seat numbers.
9. Diagnostics are on-demand developer tools. No per-frame logging/scanning,
   startup registry changes or background polling is installed.

Frostbite engine integration and BC2 game rules remain separate so another
Frostbite game can provide its own bindings without duplicating the VR policies.
Frostbite versions are not assumed binary-compatible.

The BC2 native observer has now established RH/padded camera conventions and
a D3D11 renderer for the installed profile. FrostbiteCamera explicitly converts
those conventions; see BC2_NATIVE_DISCOVERY.md. An opt-in visibility-stage pose pulse established visual camera control and exact scoped restoration. Production camera/stereo capabilities remain disabled. The independent D3D11 bridge is hardware-tested but is not connected to native rendering.


## Portability and visibility

BC2 is the first adapter, not a binary definition of Frostbite. A BF3/BF4 port
reuses FvrCore, tracking/ticket transport and graphics/XR modules where the
verified APIs fit. It supplies its own executable discovery, native camera
layout/depth/scale, ownership/lifecycle, visibility scheduling, eye target and
render-state implementation, plus game input and rig profiles. No port inherits
BC2 offsets or a capability just because both games use Frostbite.

IRenderAdapter::PrepareViews receives a StereoViewSet before eye rendering.
The shared core can enclose both finite eye frusta in a symmetric conservative
cone, including asymmetric views and angled eye poses. stereoVisibility is a
separate required capability. A later engine may instead provide per-eye
visibility; the optional cone does not dictate that implementation. If native
visibility was prepared earlier, PrepareViews must validate the exact owner,
frame and tracking sample already used. BC2's visibility and draw callbacks run
on different threads; no game object or GPU call belongs in an unverified
cross-thread shortcut.

runtime/IFrameProvider.h is the generic frame-source boundary. IPC and native
production have no dependency on the OpenXR host header or SDK. RemoteFrameProducer supplies a single graphics-thread pump. StagedFrameProducer
now acquires an immutable request before visibility and accepts a complete pair
after native restoration on the graphics thread. Its local owner/frame/device
key and connection generation reject stale completion and native-frame replay.
The staged transport passed separate-thread x86-to-x64 GPU tests; the BC2 native
provider remains unconnected. Native view factory lifecycle callbacks and the
11:35:09 game exit must be resolved before enabling per-eye native views.

## Desktop presentation pacing candidate

The shared graphics/D3D11PresentationPacing.h forwards DXGI Present arguments
and errors while selecting zero monitor sync only for an adapter-authorized
active desktop chain. BC2 validates its renderer, swap chain, device and native
wrapper. The bounded -UncapMirror diagnostic is opt-in; native settings and job
order are unchanged. Native checks pass; headset response remains to be tested.

## September 30 body-follow feedback

RoomscaleFollow owns horizontal physical displacement accounting, actor/reference
resets, lean radius and bounded velocity requests. It has no native dependencies.
Bc2Gameplay reads the verified CharacterEntity position and converts the request
to ordinary on-foot axes; BC2 retains its collision response. Actual displacement
is published atomically with the upright view base and removed from that base
before head/eye composition. Manual joystick travel remains game locomotion.
The opt-in BodyFollow pilot does not grant wall-safe HMD motion, vertical collider
tracking or production roomscale capability. Live step/return evidence is in
CONTROLS-20260930.md. Diagnostic pose observation remains a separate option and
confirmed native animation jobs can run on several threads.


## September 30 independent tracking and physical torso

Shared TrackedRig, TrackedBodyFrame and PhysicalTorsoFrame separate wrist tracking,
physical shoulder anchors and native collision-body movement. Tracking loss, action
re-arming and weapon-flick edges are independent per hand. Equip changes preserve
anatomical calibration; owner/skeleton/space changes reset it. Native reload-root
sway must not drag stationary physical wrists, and a lagging roomscale collider
must not pin shoulders behind the physical torso. BC2 alone binds character position,
input enums, weapon identity and animation publication. Native sources stay untouched.
See HAND-TRANSITIONS-20260930.md for three passing native runs and headset limits.

## October 1 native firing events and shared muzzle history

PlaceFireAtMuzzle preserves the final native aiming/spread basis and replaces only
origin, AFTER native authored offsets. FiringPoseHistory shares the first published
muzzle between consumers of the same adapter-verified event, keeping original
tracking deadlines and opaque owner/equipment/space identities. Its storage and
behavior are independent from diagnostic record capacity and contain no BC2 APIs.

BC2 binds both client effects and local-server firing, proves player pairing through
constructor ID/indexed-array links, and matches the context seed already used by
both native spread paths. Its muzzle comes from a named, topology-validated
attachment in the private published rig pose. Independently sampled recoil poses
proved insufficient on SPAS-12. Immutable publications and bounded guard-read retry
avoid borrowing animation memory or blocking on its mutex. Only caller-owned XYZ
output bytes change; sources, native angles, padding and animation remain intact.

The opt-in campaign -MuzzleFire pilot passes the shotgun timing check and a full
BC2XrHost test through a separate explicit test-only XR runtime: 507 native pairs,
34 muzzle writes, 18 beyond the log limit, zero fallback/source/restoration errors.
The real system runtime is not selected. Native executable bindings remain BC2-local;
BF3/BF4 require their own owner/event/attachment/callback proof. Production metadata
capabilities remain gated, and headset/impact acceptance is not established. See
FIRING-ORIGIN-20260930.md and reports/muzzle-fire-20261001.json.


## October 1 explicit two-hand support

Shared SupportGrip owns contact/squeeze hysteresis, ownership and tracking recovery,
and minimum-rotation alignment. It changes only gun-hand grip/aim orientations.
BC2 publishes deadline-bound anatomical contact from its native evaluated wrist
and weapon root alongside the immutable muzzle snapshot. Gather uses a private
input copy so native aim, visible weapon and muzzle consumers agree, without
altering raw XR input or dragging the other wrist. No additional hook, game offset
or native animation write is introduced. See TWO-HAND-SUPPORT-20261001.md.

## October 1 headset-recovery correction

OpenXR uses shared RecenterPolicy on tracked head/focus recovery and frame gaps,
then publishes one new reference generation to eyes and input. BC2 rig ownership
changes only with the actor. Its observed soldier eye height and collision actor
position define a shared renderer/hand base; portable TrackedRig maps absolute
head-relative grip positions, bind-derived anatomy and stable gun attachments.
Portable BindAnatomyFrame and explicit subtree leaf preservation contain no game
indices. BC2 alone recognizes the measured collapsed SPAS jntWpn_7 leaf, preserving
its original bytes and retaining all other native validation. Arm IK reads only
its participating branches. See HEADSET-CALIBRATION-20261001.md for failed prior
headset feedback, final native evidence and pending headset acceptance.


## Equipment settling and shared aim after headset feedback

TrackedRig owns native attachment settling/persistence; TrackedAimFrame owns XR
aim-to-body math. The BC2 adapter gates absolute weapon orientation to verified
XM8/SPAS and supplies its observed -Z barrel axis. Hand placement and gun aim
share the same eye/body reference as rendering and native controller aiming.
SupportGrip still modifies only a private gun-hand orientation copy; the BC2
palette publisher seats the left wrist only while explicit support is held.
Contact always uses the unsnapped tracked target. Source animation stays exact.

OpenXrHost consumes optional EXT user-presence independently from focus/tracking.
Presence loss invalidates retained imagery and controls; return triggers a shared
space/reference reset after a short settle. Runtimes without the extension retain
the previous focus/tracking/frame-gap fallback. No engine addresses enter this
policy. Headset acceptance and further Frostbite adapters remain separate.


## Batch weapon profiles and authored acquisition

WeaponProfile is portable policy data: stable identity/revision, optional model
axes, independent aim/support/translated-muzzle evidence. Missing axes do not
prevent a separately verified non-aim capability. Bc2WeaponProfiles owns exact
native asset lookup and static profile lifetime; profile pointers stay in-process,
never in IPC. Existing SPAS/XM8 IDs and axis behavior are preserved.

Bc2WeaponCapture observes validated native rig sources before VR pose edits.
It retains at most32 item/actor/rig/space groups with96 samples each, reports
capacity losses, and uses monotonic capture sequence plus episode identity.
Stable fingerprints include named topology and bind transforms. Source units
are explicit. The collector does not install offsets or grant write capabilities.

weapon_profile_pipeline.py audits one or many captures offline. Exact names or
capture-hash-bound legacy maps establish persistent IDs; matrices are converted
to local metre-space relations and tested for stability/conflicts. Candidates
remain separate from the runtime registry and per-feature verification. Other
Frostbite adapters provide their own acquisition/identities and native evidence;
they can reuse the policy and audit conventions, not BC2 memory offsets.

A raw-valid tracking flag is not sufficient to edit the shared torso: after
TrackedRig's plausibility checks, both final hand targets must still be tracked.
Otherwise the untracked arm remains exactly native.

## Physical weapon-mode interaction and launcher pilot

Portable SightFlip consumes a verified local hinge/contact, semantic physical-item
identity and grip intent. It owns angle/detent hysteresis, cancellation, timeouts
and request-correlated native acknowledgement. Duplicate tracking cannot commit;
an already acknowledged expected mode transition survives the temporary contact
gap until a fresh sample. Shared policy has no engine offsets or native slot IDs.

BC2 supplies exact scoped-XM8 family discovery, native selector/edge/boolean-reader
proof and read-only ordered switch-map resolution. Entry action33 activates its
launcher; the validated current map uses action36 to return to its rifle. These
are requests through original input consumption, never equipped-pointer writes.
The physical family identity survives that expected native equipment change.

Named source jntWpn_9 contact is measured against an approximate11cm ladder segment,
after source rig fingerprint/name/parent validation. Raw free-hand targets remain
independent of visible support attachment; nearest contact prevents grip coupling.
Native sight animation performs the completed flip without palette overrides.

Final native gesture/grip/aim checks pass; headset feel remains pending. The shared
rifle flash remains unchanged in launcher mode, so grenade muzzle translation stays
disabled. See LAUNCHER-PILOT-20261001.md and launcher-sight checkpoint/summary.

## Headset-driven sight input timing repair

The first headset sight toggle failed while manual recenter and launcher grip
were accepted. Shared SightFlip must preserve arming across idle duplicate
packets. SightFlipPackets pairs raw intent with the actual published contact
generation; BC2 retains native ownership/deadline validation and mode dispatch.
Current release/tracking loss overrides buffered intent. Real controller timing
must be tested alongside ideal geometry fixtures. See
LAUNCHER-INPUT-TIMING-20261001.md for regression evidence and remaining checks.


## October1 sight geometry lifetime correction

Native equip readiness is adapter-owned: unsettled palettes cannot establish
interactive hinge geometry. Shared SightFlip accepts validated geometry updates
only while idle, preserving arming and unique request IDs; an active manipulation
keeps its hinge fixed. The adapter refreshes from the same publication paired with
input, rather than retaining the first session-wide native transform. Bounded
gesture summaries retain native/configured hinge and angle/dwell extrema.
See LAUNCHER-GEOMETRY-20261001.md for evidence and validation status.

## Fixed sight grasp after headset toggle acceptance

SightGrasp is portable immutable hinge/contact math. BC2 supplies measured
finger-base/palm geometry and named rear/front sight snapshots; visible wrist
attachment and continuous pre-detent motion are applied only to private palettes.
The native policy/animation remains authoritative at dispatch. Preview identity
cancellation is atomic before the contended pose publication and selects the
ordinary tracked palette on release. Contact always uses the raw hand, preventing
a snapped pose from maintaining its own grab. See SIGHT-GRASP-20261001.md for
native evidence, accepted toggle feedback and the pending new headset test.

## October 1 hand roles and reload coordination

HandPose generates only a validated hand subtree from adapter-provided reference
geometry, flexion axes/ranges, curl/pinch targets and a resolved wrist. Free and
MechanismGrip are independent of current weapon animation; WeaponSupport preserves
the native shape. Bc2HandPose gates exact named bind metadata and supplies controller
anatomy/authoring. Bc2RigPublication owns role choice, post-IK private-palette writes
and existing preview/base lifetime guards. Raw gesture contact stays independent
of displayed hand seating. See HAND-ROLES-20261001.md.

ManualReload emits semantic physical-operation requests and waits for exact native
acknowledgements, under immutable plan and owner/equip/space guards. It neither
mutates ammo nor provides BC2 staged-reload bindings; no runtime enables it yet.

## October 1 hand input, immutable support and future adapters

GripAttachment is engine-free item-local contact ownership, keyed by actor,
generation, item, skeleton, space and unique physical-grab token. Bc2Gameplay
retains support through ordinary Reload; Bc2RigPublication uses the fixed contact
for the rendered wrist and raw-controller distance test. Native animation sources
remain untouched. Identity/tracking/action/release rules still bound its lifetime.

FingerCurlOverlay changes a verified index branch relative to current animation.
Bc2HandPose provides measured right-hand anatomy; the rig publisher composes this
after arm solving, including a fresh preview solve. The weapon wrist and other
branches are preserved. Mesh trigger motion has no verified binding.

OpenXrInput supplies optional ThumbTouch/IndexTouch sensor availability and state.
InputPacket v2 packs masks in the old reserved word without changing 288-byte size;
v1 accepts only the original zero word. HandTouch affects the Free role only and
preserves analog fallback. Native injected touch evidence does not prove actual
headset sensor delivery. Right Free is prepared but not runtime enabled.

BodyInventory is a portable snapshot/request/ack policy, with dynamic item
generation, stable category slot preferences and native reconciliation. It does
not hide/equip BC2 meshes or manufacture item instances. ManualReload likewise
remains a portable coordinator while staged native dispatch is under research.
Read FROSTBITE-PORTING-ROADMAP.md for existing interfaces, missing capability seams
and proposed per-title implementation/acceptance gates.

Manual left-trigger ADS was removed on user feedback; raw left-trigger analog
remains available to the pose layer. Bc2InputBinding still supports native Zoom
from an internal semantic request. A future OpticProfile/eye-box controller must
validate native ADS state and isolate relevant magnified scene/reticle rendering
without reintroducing unwanted weapon visual effects. No such optic runtime is
enabled. See OPTICS-ADS-ROADMAP.md; preserve XM8 and exclude ACOGs from that task.


## Portable feed and hand ownership foundations — October 1

HandInteraction is a fixed two-hand semantic ownership arbiter, separate from
HandPose presentation. Exact actor/equip/space/item/contact tokens permit explicit
transfer only after a replacement validates. Failed acquisitions retain valid
existing ownership; stale releases, same-packet lease extension and replayed
unavailable-input intents are rejected. The adapter must supply authoritative
fresh samples and call Update even without gestures.

FeedMechanism is a stateless query over a reconciled physical snapshot for a
profiled belt-feed family. It chooses a candidate physical operation and checks
same-input weapon-local contact geometry. It does not mutate a cover, reserve a
resource, dispatch native reload, grant fire readiness or acknowledge ammunition.
ManualReload remains a separate native request/ack coordinator. Neither contact
success nor NoPhysicalStep is a native acknowledgement.

Both foundations are tested but not wired to BC2 runtime. Native bindings,
resource authority, gesture recognition and private presentation remain adapter
integration work. The offline optic inventory likewise reads saved evidence only;
it makes no rendering or ADS changes. See FEED-MECHANISM-20261001.md,
HAND-INTERACTION-20261001.md and OPTIC-INVENTORY-20261001.md.

## October 1: exclusive runtime hand ownership

HandInteraction now arbitrates BC2 support and sight roles. Portable From APIs
separate authoritative current safety from a recorded original contact packet.
BC2 alone identifies actor/rig/equip/space and the verified XM8/launcher physical
family. Both contacts use one immutable publication; claim acquisition precedes
policy commit/native dispatch, without changing attachment or shot math.

SightOwnershipRecovery authorizes only an attempt at a new reservation after
exact native acknowledgement and expiry-only loss. SupportOwnershipRecovery
requires newer real contact, unchanged grasp and exact dependency lineage.
Neither revives tokens or deadlines; native state remains engine-authoritative.
See [integration/evidence](HAND-OWNERSHIP-BC2-20261001.md). Reload/body-inventory/
optic bindings remain separate and disabled.

## Sight continuation and local-server reload observation — October 1

The sight visual policy consumes a separately captured raw palm/wrist and fresh
pre-IK target, after the existing policy confirms native mode acknowledgement.
It never feeds displayed IK back into contact or sends another mode request.
Native animation is a rate-bounded fallback; exact family/owner/space/deadline
and claim validation stays in the adapter. Mode-specific grips are preserved.

Bc2ReloadServer proves the local client/server player and selected item chain,
then supplies a200ms server firing lease to the existing original-once observer.
Server branch2 is explicit, with its own player/soldier/item identity and counters;
its item+10 is not mislabeled as a client+3C/+40 wrapper. Client and server callbacks
run on different threads. No lock spans an original native call; no gate is enabled.
Native weapon/finger capture shares one immutable evaluated pose and records
optional inverse binds. Offline BC2 tools derive shell grasp/rail candidates;
chosen interaction tolerances remain distinct from measured asset geometry.

## October7 presentation lifetime evidence

The192 immutable fallback journal classified two exact pose expirations,7.99ms and
7.93ms before first packing. Current input, ammo reserve, selected meshes and family
were still fresh; original/current targets matched. The portable target takes the
minimum of original geometry input, native observation and exact gun/hand claim
lifetimes. Transaction authority and render continuity therefore require separate
fresh composition, not extending a cached target or replacing its timestamps.
The selector still restores ordinary native palette on any expired proof.

Diagnostics record raw shot-clock ticks despite the current shot_deadline_ns label;
interpret using QueryPerformanceFrequency, not as nanoseconds. This label error is
confined to the journal and does not affect freshness checks or native selection.
Headset/eye-texture and all-weapon acceptance remain separate from the three actual
completed native magazine transactions.
