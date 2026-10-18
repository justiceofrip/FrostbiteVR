# Opening Nambu100 authored alignment candidate

The opening SMG has no entry in the three-item native weapon feature catalog.
Its native yaw/pitch still follow the XR right-controller aim. Before this
candidate its displayed gun followed wrist calibration without a measured model
axis. A reference-space change recalibrates that wrist orientation. This is a
credible source of a visible gun/shot mismatch, not proof of the reported offset.

`profiles/opening426/CombinedGrips.h` preserves the prior202 and SCAR339 entries
and adds the exact Nambu100 authored binding. It uses the existing
`AuthoredWeaponAimFrame` and `TrackedRig` consumers; no new per-name orientation
branch or native weapon capability is added. The right hand remains attached to
the measured grip. The independently enrolled support contact remains separate.
Null persistence is optional metadata; live item/data, exact selected mesh, rig,
owner and original input/configuration leases still establish the binding.

The installed SP_01 configuration references the Nambu100 mesh, HandsIkPose and
shared ske01 skeleton. The exact source closure, generated binding and reviewed
axes are retained in `profiles/opening426/nambu-grip-bindings.json`,
`reviewed-model-axes.json` and `NambuAuthoredGrip.generated.h`.

The assembled mesh was inspected in two orthographic views. Its barrel points
along canonical -Z and its sights sit above the receiver along +Y. The lateral
magazine extends +X, which would make a whole-mesh principal-axis estimate an
inappropriate barrel measurement. The fixed flash basis is identity; its
canonical position is `[0.000631691, 0.109905012, -0.884455979]` metres. The right
wrist contact is `[-0.049723890, 0.041787350, -0.228854671]` metres. Both hands and
the required weapon controls are static. The typed primary Shot fields are raw
InitialDirection `[0,0,1]`, InitialSpeed `[0,0,600]` and zero InitialPosition.

Private small review artifacts are in
`bc2vr-recovery/overnight426/crash/nambu-aim-geometry.png`,
`nambu-aim-geometry.json`, `nambu-firing-fields.json` and `nambu-static-flash.json`.
The review metadata binds their SHA256 values. Extraction read installed assets
in memory; no raw asset or mesh payload was copied.

## Validation and native firing boundary

The added `Bc2AuthoredGripTests` group uses the measured profile, null persistence,
different initial wrist rotations, two world scales and a reference change. It
compares the displayed barrel with independently computed native controller
yaw/pitch, checks the grip relation and rejects the old-space or wrong-mesh
binding. Root reports the 16 focused suites and both normal full builds passing
289/289 suites for source `1d2e623ff56c7781259ffaa23f3dc7e2c918d5dc0e19d55c59d0f034e1314557`.

Frozen normal426 still lacks Nambu `TranslatedMuzzle`: that composition adapter
leaves its output origin and all orientation bytes unchanged. Authored visual
axes are therefore not full muzzle-origin or projectile-impact acceptance. A
native one-shot comparison must separately establish the stock origin offset,
rendered barrel direction and native composition axis. The existing composition
observer cannot measure the final projectile direction or its spread.

Existing `check_weapon_alignment.py` compares barrel and native shot evidence for
its original supported fixture; `check_muzzle_fire.py` and
`check_native_xr_muzzle.py` check translated origin, unchanged native orientation
and matched client/server event poses. They explicitly do not prove impacts.
For Nambu, retain its exact current asset/configuration identity and calculate a
read-only mapped candidate even while native translation is unsupported; do not
classify it from a guessed model axis or promote its profile to NativeVerified.

A minimum native comparison holds the same physical controller poses across a
reference change, first samples aim without firing, then records separated
native single-shot events before and after. Each event needs raw controller aim,
reference/body heading, selected item/configuration, model-axis descriptor,
tracked weapon/flash, original/output shot matrices, native token, client/server
path, original generation/space/deadline, and origin-write status. Start with the
first settled shot in each reference and report subsequent recoil/spread
separately. No extra recenter command or game input was issued by this subtask.

The425 crash lost `native-trace.json`, including its per-shot matrices. Its host
log contains no manual recenter; it records two automatic recenter counts and
reference spaces2 through6. This cannot establish when the reported offset
occurred or whether recenter would fix it.

## Read-only composition evidence limits

`compare_authored_shot_alignment.py` reports the pre-spread native composition
axis against an exact, unexpired render candidate. The native source is the
original phase2 invocation, with current equipment identity and its native token;
the retained render frame may precede newer input. `observed_candidate_unexpired`
confirms the existing owner/space/deadline checks, not that this frame supplied
the native aim consumed by that invocation. Original and current render generations, observation
QPC, deadlines and remaining QPC ticks are reported. Candidate age and consumed
native input generation are unknown. No field is relabelled as same-shot proof.

Runtime's `aim_evidence.effective_aim` is copied from `Tracking` after the shared support
policy's correction. Its barrel-to-aim
angle cannot measure the residual against the raw physical controller while
two-hand correction is active. An available pre-support input can itself come
from a diagnostic receiver, so it is not labelled physical hardware evidence.
Native writes and supported-weapon admission are
unchanged; Nambu remains unsupported for muzzle translation.

The existing discovery binds server composition at `serverShoot+0x15a`, return
address `0x579f1a` in the installed image. This precedes spread generation at
`0x57a51f`, velocity rotations through `0x57a724`, and inherited velocity addition
at `0x57a73e`–`0x57a7ce`. The complete retained instruction listing is
`bc2vr-recovery/vehicle-reticle-native-producer-20261003-candidate/shot-disassembly.txt`
(executable SHA256 `3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258`).
The final velocity is copied to local `[esp+0x30..0x38]` at
`0x57a7d2`–`0x57a7e6`. At `0x57a9dc`–`0x57a9e0` their pointer is laid on the
stack for virtual dispatch at `0x57aa0b`, where it is argument4 of11.
The intervening `0x556e90` call at `0x57aa01` is an effects owner/context helper:
it accepts one output pointer, returns that pointer in EAX and cleans4 bytes at
`0x556fae`. It does not consume the pending velocity argument. The complete
receiver review is retained in `bc2vr-recovery/overnight426/crash/projectile-endpoint-review.md`
and `.json`. These are investigation sites only. The final virtual receiver,
live owner/token association and readonly hook have not been enrolled by426.
Existing mounted observers record queue growth or an unspread configured ray,
not final infantry bullet velocity.

## First native 426 observation

The finite synthetic-input Nambu run in
`bc2vr-recovery/overnight426/nambu-native01` completed on 2026-10-18. Its native
report SHA256 is `500d12d5c4e4dab64c1379b10863574b3e40ad3c9c6b49493cf6879ef1994a27`.
`aim-comparison.json` accepts four exact-configuration observations: client and
server composition for native tokens 501380 and 503105. An independent vector
calculation, input-timestamp comparison and artifact hashes are retained in
`aim-independent-audit.json`.

| Measurement | Recorded result |
| --- | --- |
| Pre-spread composition axis versus rendered authored barrel | Less than 0.000024 degrees in all four observations |
| Effective versus same-generation pre-support aim | Zero for the first pair; 0.000910 degrees for the supported second pair |
| Composition origin versus displayed muzzle | About 0.862 m apart; 85.55–85.56 cm behind along the composition axis and 10.46–10.56 cm perpendicular |
| Composition origin versus candidate body translation | Within 3.26 mm on client and 0.082 mm on server |
| Native output mutation | None; all output matrix bytes unchanged and no origin writes |
| Reference spaces | One; no recenter comparison |

These data establish close directional agreement and an untranslated origin at
this observation boundary. They do not establish final bullet direction or
impact. The body translation closely follows the stock camera, but the exact
camera consumed by each native invocation was not separately joined. The second
token also joins the actual own-Update postreload shot and ammunition decrement
in the independent reload audit; that joins firing, not the consumed aim packet.

All four candidates equal the latest published render generation and retain
76.36–83.51 ms of their original lease (measured QPC frequency 10 MHz). Nevertheless,
the client candidates are generations 576 and 1339 while the diagnostic Fire
packets are 577 and 1340. Client composition follows those Fire packets by 3.43 ms
and 2.14 ms. The server candidates match 577 and 1340. The matching candidate input
timestamps are 15.11–16.49 ms before composition; these are diagnostic preparation
timestamps, not physical controller sample ages. The static synthetic pose makes
the observed axis comparison useful, but this gap remains material for moving
hands and cannot be relabelled as same-shot input provenance.

The session also passed an independent audit of 18 clean-stop predicates:
240 published/consumed stereo pairs, zero camera/GPU failures, hooks disabled,
callback/list/camera/clock restoration, request references 8→8 and the exact seven
surviving views restored with the owned eye removed. Zero scene recoveries were
attempted. `stereo-cleanup-audit.json` and `retained-probe-cleanup.json` bind the
report and bootstrap to PID 26896, process creation 134368038403330872 and the exact
retained probe. This is clean-stop evidence, not death/reload crash acceptance.

## Translated-muzzle candidate implementation

The new source implements an explicit candidate through
`BC2_MUZZLE_ENROLLMENT_HEADER=profiles/opening426/NambuMuzzle.generated.h`.
It is not part of the frozen normal426 runtime and is not native-verified.
The existing `PlaceFireAtMuzzle` operation and event history support the measured
Nambu flash; the new enrollment extends admission without adding a name-only
capability or changing the authored grip source. Build and actual-game validation
of this candidate remain pending.

The generated, immutable BC2 muzzle descriptor uses the existing authored
binding. It binds the exact configuration path and source
digest, selected mesh, rig fingerprint, authored binding/axis evidence and the
reviewed flash relation. Native firing evidence and experimental-test admission
must remain separate from authored geometry. A visual binding alone grants no
write. Existing accepted profile routes remain available, subject to the new
shared live ForceSpawnToCamera guard described below.

The narrow integration is an independently admitted muzzle binding in
`WeaponShotFrame`, revalidated with the original selected-configuration and input
leases by its existing reader. `SupportedMuzzleShot` accepts that binding
alongside the legacy route after checking the actual invocation's effects,
firing data, primary configuration pointer and reviewed Shot fields. Recheck the
same captured binding at composition; observation-only `aimEvidence` strings
grant no write authority. Ambiguity, configuration changes, an old space,
missing tracking and expired original leases fall back to stock. Configuration
and equipment-generation identity participate in event-history admission. The
event deadline is clamped to the original input and selected-configuration leases,
so a newer frame cannot renew the first client/server event muzzle.

Keep the existing exactly-once native calls and 12-byte output-translation write.
Preserve the native basis, both source matrices and padding. Reuse the first
event muzzle for matched client/server consumers without renewing its deadline.
This targets the measured origin separation while preserving native spread and
other downstream behavior; it does not authorize an additional aim rotation or
an arbitrary camera-to-muzzle convergence correction.

Before normal admission, a bounded candidate test must show actual matched
client/server writes at the same event muzzle, unchanged nontranslation bytes
and inputs, zero write/source failures, and original fallback on stale or changed
bindings. Extend the focused binding/event tests for exact config mismatch,
optional null persistence, equip/config replacement, expired original leases,
reference change and client/server ordering. Existing ABI and placement tests
cover the shared native boundary and translation math; duplicate arithmetic
tests would add little.

## Camera origin versus convergence

The native01 data show parallel pre-spread lines separated by about 10.5 cm. They
do not show a deliberate angular convergence toward a chosen target. The exact
installed firing asset has zero InitialPosition, forward InitialDirection,
InitialSpeed `[0,0,600]`, zero inherited-speed fraction, one bullet per shell and
shot, RelativeTargetAiming=false, ForceSpawnToCamera=false and
ActiveForceSpawnToCamera=true. The small extraction is
`overnight426/crash/nambu-shot-fields-review.json`; it reads the already hashed
installed asset in memory without retaining asset payloads. Authored values are
not a replacement for live invocation checks.

The retained native reflection maps those flags to ShotConfig+0x4a, +0x49, +0x48.
After composition, the server path tests +0x49 at 0x57a12e and can overwrite the
composed transform/translation before launch when that branch is entered.
The +0x48 flag is tested inside that path. The candidate shared muzzle gate
excludes +0x4a, MuzzleExplosion at +0x40 and +0x49. Generated ordinary-shot
admission also binds the reviewed live flag combination;
neither ignoring the downstream branch nor requiring every camera-named flag to
be false describes this Nambu configuration correctly. Existing XM8/SPAS live
flag combinations have not been re-observed here; if they enter that camera
override branch the new common guard deliberately falls back to stock origin.

The smallest decisive next observation copies the actual native launch origin
and post-spread/inherited vector at the reviewed endpoint, with exact owner,
configuration, token and per-invocation projectile ordinal. Compare that line to
the event-latched muzzle/barrel and retain the pre-spread comparison separately.
The candidate includes this bounded read-only observer. It verifies fingerprints
of the complete reviewed server function and helper before installing a one-output
pointer `__thiscall` hook, calls the original exactly once and preserves its return
and output. It copies the prepared launch arguments both before and immediately
after that helper, retaining at most64 records. It does not copy ownership-bearing
context payloads. The observer is disabled and removed only after callback drain.
Telemetry distinguishes the observed helper return and later server-Shoot return
from an unobserved virtual-launch return. The verified instruction flow reaches
vtable+0xe8 immediately afterward, but the receiver's stored velocity, eventual
impact and actual virtual return remain unverified.
For recenter acceptance, repeat separated settled shots with the same controller
pose on both sides of an actual reference change. For physical acceptance, check
near and farther impacts and a muzzle near cover: a camera-origin shot can pass
an obstruction that is in front of the displayed muzzle. A visual tracer alone,
or a single spread-displaced impact, cannot distinguish those cases. Keep Nambu
unverified for translated origin until the candidate write and downstream tests
provide their own evidence.
