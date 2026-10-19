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
| Acquisition | `0x578500`, secondary `effects+0xD0`, no arguments | Bind the exact published intent at ordinary FireLogic0 caller `0x6E1FCA`; observe the native cached weak target after return |
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
All thirteen registered proof fingerprints were checked offline against that image.

### Ordinary acquisition caller correction — 427

The original426 guard incorrectly required the generic notification return
`0x6DD770`. In the actual `fallback42701` run, server Update received Fire with
notifications enabled while idle, and its listener array contained the exact
`effects+0xD0` interface once. Acquisition entered once and was rejected by that
caller guard; attack entered once at the expected `0x6DD9E5` but was rejected
because acquisition had not been admitted. The old diagnostics did not retain
the rejected acquisition's raw caller, so they do not by themselves identify it.

Static inspection of the exact image establishes the ordinary path: Step's
state2 block loads context+0x24, passes the current firing object, and calls
`0x6E1FA0` at `0x6E64AD`. This helper selects the listener begin/end according
to that notification flag and calls listener slot0 at `0x6E1FC8`; its return
address is `0x6E1FCA`. The selected reader already requires FireLogic0. The
separate FireLogic4 inline return `0x6E1D0A` and generic `0x6DD770` remain
rejected. No input pulse extension or copied-input bridge is introduced.

The complete168-byte helper through its three `ret4` exits has SHA256
`61afd1de72f817bed677f42f1d3e1bacc627f1202c4b45c60d7f55acec3b5645`;
the12-byte Step argument/callsite has SHA256
`f32536a078da48975fb1e8cc11fbcd6d6d4e0a2bd70f62ff19adb8284da65d4d`.
Both are now required live fingerprints. The shared production caller predicate
has a focused positive and wrong-path regression. Each raw Acquire/Attack entry
also records a single atomic caller/self pair before admission, including
rejections; its independent counter snapshots are not a per-call event record.
Actual acquisition at the corrected boundary, completed miss/rearm, damage and
opening progression still require a new game run. The earlier successful input
publication and client animation establish none of those outcomes.

The subsequent427 fallback run observed raw acquisition at `0x6E1FCA` and attack
at `0x6DD9E5`, admitted both and completed its valid empty target with pending0.
It also recorded aggregate acquisition failure8. Since the later attack required
the same populated target optional, this was not an unreadable target; a current
or retained-owner check failed during acquisition. The next physical-motion run
reached server Fire but rejected acquisition at the owner check, leaving the
second jab pending. Neither run proves a hit, and no ownership condition was
relaxed based on this evidence.

Sparse `owner_failure_records` now retain the first four failed checks at
acquisition admission, current mapping, acquisition return, search mapping or
attack admission. Each records the exact first scope stage and expected/observed
value or failed read/type, raw source/before/after flags and inventory, and up to
four named differences per comparison. Phase1/2 means the first/second scope
failed;3/4 is a bracketed links/effects mismatch;5/6 is a retained source
effects/links mismatch. Both current reads must still agree on all owner links;
the context-only comparison described below also uses the existing0x18 mask.
The two additional inventory
pointer reads (`later_inventory248`/`later_inventory24c`) occur only after a
rejection and are explicitly later observations, not an atomic retry. Ordinary
Update sampling does not collect these detailed records.

`acquisition_failure` preserves the first branch previously collapsed into8:
1 target unreadable,2 retained owner rejected,3 required blade copy absent,
4 an earlier acquisition/search failure. `acquisition_prior_failure` preserves
that earlier code before the aggregate8 overwrites it. Last-completed telemetry
keeps both values after the action is cleared. These fields diagnose rejection;
they grant no native completion or new attack.

The r9 actual-game `motion42702` receipt isolates a bracket comparison defect:
acquisition was admitted with two native searches and two copied blade arguments,
then `attack_retained` rejected only `clientSoldierFlags` changing3 to19(0x10).
Every other bracket link matched; inventory947691680, client effects889736480 and
player flags9 remained unchanged. The original attack was passed through on the stock
path, but the rejected observer could not complete the VR intent. No enemy damage
or campaign progression was established.

Callback ownership now applies the same existing0x18 context mask consistently
both between its two scope reads and against the retained source. Static evidence
shows independent0x08 assignment/clearing in0x890C90 and0x10 set/clear at
0x8BE119/0x8BE2B7; the native inventory getter selects+248/+24C only with bit0.
The prior retained audit and new exact-executable check are recorded privately in
`headset391-investigation/soldier-publication-flags-audit.json` and
`overnight426/knife-context-r10/context-evidence.json`. All pointers, typed
relations, membership, bit0 and every other flag remain exact. The initial
`ReadNative` publication bracket is unchanged. Rejected diagnostics retain raw
flag values; normalization never retries a failed read or changes native memory.
This correction still requires actual enemy damage, opening progression and
headset verification. The following run establishes repeatable native misses.

## Actual r10 two-jab miss recovery — October19

`motion42703` used frozen source
`17f0b0647fa09f063d7becb2acff27ea0bc69b50b0f511a9c4dc3fe0edd3ddf7`
and build receipt
`dc7350aa5c18ccf3e5c5a2a6d7a07e66196aaf25bae4e056b40bb751418eebfb`.
The independent offline audit passed27 existing fixture/cleanup checks and25
additional source, binary, exact input, server identity and rearm checks. It is
retained at `overnight426/knife/motion42703-independent-audit.json`, SHA-256
`c1e77d90b4f2733c47ba3e57c34ba660702c8b260a12916aa88815fd1fcc240b`.

Physical intent serials1/2 join exactly to successful IPC input generations114/308
at2063/5266ms. Each native source timestamp lies inside its corresponding IPC
commit interval; each has fresh gameplay-owned menu evidence, tracking basis1,
and zero trigger/buttons. Both callbacks match the independently typed server
attacker1023882080 and melee effects889602592, distinct from client actor947773392
and client selected weapon106817280. Preflight and postflight mapping agree.

Both rows have acquisition and attack returned, two searches/two blade-argument
copies, failure0, target/weak handle0 and damage calls0. No owner boundary failed;
unknown and pending both finish0. The first completion/pending0 is already visible
in periodic telemetry at282512330823300ns, at least2249.8355ms before the second
intent source. Input248 publishes the baseline grip again at4203ms. Intent sources
are3203.3467ms apart. This verifies completed-miss rearm through actual native
callbacks, not mocked policy outcomes or an elapsed-time completion rule.

Trace/receiver exits are0, hooks disabled, references restored8 to8, no pending
owned references, and exact survivor order restored. The receipt binds the loaded
probe copy and all frozen binary hashes. The periodic read-only Update sampler
observed only one server Fire packet; it can skip under lock contention, while
the two raw acquisition/attack pairs provide the direct callback evidence. No
enemy was acquired, so this run proves neither damage, a kill, opening progression
nor headset acceptance. Callback attribution still uses the next exact retained
owner acquisition; the engine exposes no per-publication event token.

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

The expanded `SelectedKnifeRendering` test has thirteen groups, including real
measured layout fixtures, stale/changed ownership rejection, exact weak-target
reads, native-forward mapping, scale/head-motion separation and source/padding
preservation, the exact ordinary acquisition caller gate, delayed acquisition across continuous newer packets, a mocked
native miss followed by another jab, genuine source interruption, sparse owner
failure attribution, and the measured context-only callback bracket transition.
All16 context combinations pass both current/retained callback readers; all six
other flag bits and ten changed-owner cases remain rejected. These are
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
