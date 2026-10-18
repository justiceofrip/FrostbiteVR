# Selected campaign knife native adapter — 426 candidate

The opening `Knife_Equipable` now connects the existing `SelectedMeleeAttack`
trigger/jab policy to the already verified action37 input transaction. It reuses
the actual right-hand `GunHold`, the420 mesh/wrist calibration, the current input
epoch and a fresh full rig identity. It creates no chest draw or second hand
ownership policy. All existing menu, mechanic, equipment and native-playing
gates remain prerequisites; a suppressed semantic trigger cannot obtain another
route through the raw trigger value.

Recognition uses the original controller grip in the fixed tracking frame.
Current body mapping places the same measured blade tip and axis in native
world space. The search cone's native forward basis is built directly from
that physical vector; applying the camera matrix reflection to it would point
the cone backwards. Original input/render deadlines gate the input commit and
cannot be renewed to admit another attack. Once published, the immutable intent
can reach native acquisition after newer continuous controller packets arrive.

## Native boundary

The adapter uses418's verified acquisition and attack interfaces plus419's
measured selected-melee owner chain. It does not reuse the firearm reader's
effects+D4 callback relationship or the chest reader's original-firearm anchor.
Both client firing copies, the server item/effects/firing object, exact slot0
configuration, inventories, actor and owning player are checked independently.

Five independent listeners are installed only after the measured live code and
vtable checks pass. They do not register another shared firing observer and do
not hook the existing Step, Update or Compose dispatcher:

| Boundary | Verified x86 interface | Behavior |
| --- | --- | --- |
| Acquisition | `0x578500`, secondary `effects+0xD0`, no arguments | Bind the exact published intent at original caller `0x6DD770`; observe the native cached weak target after return |
| Primary search | `0x577790`, thiscall with attacker, transform, ShotConfig, scalar; `ret16` | At acquisition return address `0x578552`, pass an aligned local copy with the measured blade basis/origin |
| Movement retry | `0x577D40`, same four arguments; `ret16` | At acquisition return address `0x578573`, pass the same blade copy; keep the engine's internal movement adjustment and internal search calls unchanged |
| Attack | `0x57ABD0`, secondary effects receiver, float plus two words; `ret12` | Require the same effects/actor/item and acquired weak target; exact original return completes the one pending intent |
| Damage observation | `0x6C4C00`, soldier receiver plus two arguments; `ret8` | Only inside the matching attack at caller `0x57AF15`, observe health before/after the original call while the same cached weak target remains valid |

The effects transform, native source arguments, configuration, range, collision,
damage descriptor and health are never written. Only a caller argument points
to a local transform copy. Native SIMD padding is retained. The blade origin
must remain within two metres, scaled to world units, of the native acquisition
origin; that is an admission limit, not an increased attack range. This narrow
mapping requires the current authored Shot InitialDirection/InitialPosition to
be neutral. Non-neutral data disables motion mapping and preserves the stock
trigger fallback. Trigger intents always retain stock aiming; only physical
strokes substitute the blade transform.

The retained418 proof ends the damage function five bytes before its epilogue.
426 additionally read the exact installed image (SHA256
`3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258`):
`0x6C53DC` restores0x128 stack bytes and `0x6C53E2` returns8. The full2021-byte
receiver SHA256 is
`0aa21373f22fd900e5e02c1b2ddd1959b786736a7bcc4e01b4a6052f324c08c3`.
The318-byte retry function SHA256 is
`c1f9f58bb714c317b47b63e31820a814f0c9f5cb5ca08b45103152a68b97c706`.
All twelve registered proof fingerprints were checked offline against that image.

## Completion and evidence

The existing portable policy owns the only pending intent. Input-buffer
publication is recorded separately from native acquisition. Neither elapsed
time, idle animation, sound nor an input bit completes an attack. Missing or
uncertain native evidence leaves that intent pending/unknown; holsters, vehicle
Use and unrelated equipment never consult this latch. Known actor retirement
can close its old intent. The native windup/recovery and cached-target rules
remain unchanged.

Continuous newer packets retain a published intent without changing its source,
pose or deadline. Focus, tracking, menu, hand/item or input-epoch interruption
revokes further blade substitution. Exact matching native acquisition and attack
can still be observed on the stock path after interruption; missing callbacks
remain unknown. A null acquired target followed by its exact original attack
return completes a miss and permits a freshly rearmed jab. The installed adapter
suppresses selected-melee Fire even when the current render/hold source is
unavailable, so missing evidence cannot bypass the shared trigger/jab latch.
There is no native per-attack event token in this measured boundary. Association
is the next matching acquisition on the retained owned effects while the shared
latch excludes another VR melee request. Publication alone is not proof of native
consumption; `observed_ns`, `deadline_ns`, `staged_ns` and `acquired_ns` retain the
original timing for review of that gap.

`gameplay.selected_knife_native` reports bounded records with intent/input,
effects/attacker, weak handle/target, acquisition/attack times, original and
blade transforms, copied-argument counts and exact callback return observations.
The damage hook records `damage_calls`, `health_known`, `health_before` and
`health_after`. A valid nonzero damage callback with a decrease is separate
evidence from an animation or successful attack return. A null target/miss must
not be presented as damage. Actual campaign progression still requires game
observation.

Geometry failure indices are:1 missing render receipt,2 missing current body,
3 current rig unavailable,4 receipt/mesh/rig lifetime mismatch,5 non-neutral
authored shot offsets,6 controller-to-world mapping failure,7 native origin
copy rejection. Install failure1 denotes code/vtable admission,2 typed native
bindings,10–14 hook creation and20–24 hook enable. `verified` preserves successful
admission in the final report after hooks have been removed.
`cleanup_failed` distinguishes failed hook rollback from safely unavailable
installation. Record failure10 denotes interrupted blade admission and11 an
overlapping matching attack observation; overlap never completes the intent.
Gameplay gather callbacks drain before the selected adapter removes its hooks.

The expanded `SelectedKnifeRendering` test has ten groups, including real
measured layout fixtures, stale/changed ownership rejection, exact weak-target
reads, native-forward mapping, scale/head-motion separation and source/padding
preservation, delayed acquisition across continuous newer packets, a mocked
native miss followed by another jab, and genuine source interruption. These are
deterministic tests with mocked native memory, not execution of the native hooks. Build,
first-mission hit/miss, campaign progression and headset acceptance must be
recorded separately; this document makes no runtime acceptance claim.

## Bounded emulated input

The existing input-smoke receiver accepts `--selected-knife-motion-smoke X Y Z`
with an explicit unit blade direction in OpenXR LOCAL coordinates, `--static-pose
--async --pairs 240 --no-image-files`. It sends no trigger, squeeze, inventory,
movement or menu button. Two12cm strokes occupy2000–2120ms and5200–5320ms of an
eight-second stream, with slow retraction and neutral rest between them. Pair
requests continue through7.5s to keep normal rig receipts available. Actual
publication times, source inputs and delivered stroke travel are recorded in
the existing durable `trigger-input.jsonl`; publication success is explicitly
separate from miss, damage, campaign or headset acceptance.

The direction must come from the measured blade and the actual calibrated wrist,
not a guessed forward axis. Existing `rig_publication.hand_evidence` contains
`right_wrist_matrix`, `eye_base` and `grip` for a neutral-session derivation:
recover wrist-in-grip with `KnifeWristCalibration`, evaluate
`KnifeMotionFromGrip`, then reflect its canonical Z component once to obtain
LOCAL translation. Native selected-slot/path verification and a new current
render receipt remain mandatory in the gameplay consumer. Root controls native
Entire Mission restart and target placement; this receiver never starts a game,
selects an enemy or writes target state.

The selected-knife receiver uses explicit `--selected-knife-neutral` (static,
async,240 pairs), `--selected-knife-trigger-smoke` (the existing bounded trigger
schedule), or the motion flag above. These modes host the existing native menu
observation lane and apply the shared menu ownership policy before publishing
input. Sequence, epoch, original deadline, owner and tracking-basis generation
are recorded with the actual packet. Unknown, pending, menu-owned or expired
observations grant no selected-knife gameplay admission. These fixtures send no
menu commands. Ordinary scene-neutral mode remains unchanged because an opening
restart can supply its separate menu sidecar.

The first426 neutral run reached the exact selected `Knife_Equipable` and closed
cleanly, but used ordinary scene-neutral input without that menu lane. Its native
menu report had zero reads and no installed observer; every authored-grip receipt
was absent and native `prepared` remained0. The packet's default Unknown menu
owner correctly prevented selected-knife source admission. That run establishes
neither a failed native attack nor a usable blade calibration. The dedicated
receiver correction requires another actual-game check; production admission
and the authored-receipt direction requirement are unchanged.
