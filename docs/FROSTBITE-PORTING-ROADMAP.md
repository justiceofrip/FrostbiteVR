# Frostbite VR porting roadmap

As of 2026-10-01. This is a developer plan, not a supported-games list.

The intended product is one shared VR core with independently verified game
adapters. BC2 is the working reference. BF3 is the proposed second adapter, BF4
then Hardline follow, and later games begin with separate feasibility studies.
Sharing the Frostbite name does not establish compatible offsets, native APIs,
rigs, render hooks, or graphics resources.

## What is proved, and what is proposed

BC2 has native stereo rendering connected to the OpenXR host, controller/roomscale
integration, and headset acceptance of selected gun grips and the physical
launcher sight interaction. The user accepted the free-left-hand animation, while
launcher hand placement still needs polish. Acceptance is feature-specific: the
new right-index/touch/support-retention candidate needs a headset check, grenade
muzzle translation remains disabled, and known rendering/lifecycle issues are
not declared solved. See the [current checkpoint](HANDOFF.md),
[hand-input candidate](HAND-INPUT-RELOAD-20261001.md), and
[weapon pipeline](WEAPON-PROFILE-PIPELINE.md).

The physical SPAS consumer now has repeated actual native insertion evidence,
using scripted controller motion through Gameplay, real reserve ownership and
the authored shell/rail (trace231748-438:6/24 to7/23 to8/22 loaded/reserve, two
distinct native transfers). Headset feel remains unverified. The run delivered
all240 pairs with one recovered request timeout; full acceptance stays open. Shared chest
anchors and dynamic shoulder selection are integrated behind `-BodyInventory`;
their native/headset acceptance is pending. Reversible weapon hiding and true
holstering are still being validated. See [reload](MANUAL-RELOAD-20261001.md),
[body consumer](BODY-INVENTORY-CONSUMER-20261001.md), and the
[current checkpoint](HANDOFF.md).

No BF3, BF4, Hardline, BF1, BFV, 2042, or newer native binding is verified in this
repository. The interfaces and milestones below distinguish existing source from
proposed adapter work. Older sections of README/architecture/transfer documents
retain historical checkpoints; use the latest feature evidence and actual source
rather than treating every historical status sentence as current.

## Reuse map

| Layer | Existing reusable work | Work belonging to each title |
| --- | --- | --- |
| VR math and policy | Stereo/projection/culling math, tracking, recenter, comfort/movement, action edges, arm IK, grip and aim policies in [include/fvr](../include/fvr) | Coordinate basis, physical scale, scene/owner identity, collision and native movement semantics |
| Render contract | [FrameCoordinator / IRenderAdapter](../include/fvr/runtime/FrameCoordinator.h), complete-pair submission, restoration and failure regressions | Safe native scheduling, eye targets, visibility, first-person/world projections, temporal history, original-state restoration |
| Frame transport and XR | [StagedFrameProducer](../include/fvr/ipc/StagedFrameProducer.h), [FrameProvider](../include/fvr/runtime/FrameProvider.h), fixed-width IPC, OpenXR host, original-pose presentation and input sampling | Native graphics thread/device ownership and resource handoff; backend compatibility must be established |
| Graphics | [D3D11 bridge](../include/fvr/graphics/D3D11FrameBridge.h), epochs, GPU completion and pair ownership | Current [sharing protocol](../include/fvr/graphics/SharedTextureProtocol.h) is DXGI/D3D11 and supports specific RGBA/BGRA 8-bit formats. A different backend/format needs proven interop or a new backend; no DX12/Vulkan/HDR adapter exists here |
| Hands and interactions | [HandPose](../include/fvr/interaction/HandPose.h), [HandTouch](../include/fvr/interaction/HandTouch.h), [SightFlip](../include/fvr/interaction/SightFlip.h), grip/IK math, reload and inventory transactions | Skeleton topology/binds, hand anatomical calibration, native support animation, mechanism contacts/axes, native operation acknowledgements |
| Discovery and evidence | [PE validation](../include/fvr/engine/PeImage.h), bounded signature matching and [BindingValidation](../include/fvr/engine/BindingValidation.h), telemetry/audit patterns | Signatures, ABI, object relationships, ownership, lifetimes, call sites, and exact executable evidence |
| BC2 implementation | A reference for solving and testing integration problems in [src/games/bc2](../src/games/bc2) | Native addresses/types, x86 calling conventions, view/cache layouts, input IDs, skeleton names/indices, palette hooks, weapon modes and item categories stay BC2-specific |

Even [FrostbiteCamera](../include/fvr/engine/FrostbiteCamera.h) contains the
observed BC2 padded camera conversion, not a universal Frostbite layout. Existing
x64 builds and a 64-bit OpenXR host do not prove a 64-bit game's native hook ABI.
Keep native pointers inside a matching-architecture adapter.

## How the current ammunition/body pipeline carries forward

The reusable unit is a mechanism family and its interaction contract. A future
BF3/BF4 adapter supplies measured content and native operations to the same shared
policy. A weapon profile does not itself authorize changing ammunition or hiding
a native mesh.

| Shared pipeline stage | Existing reusable source | New title must supply |
| --- | --- | --- |
| Hand ownership | `HandInteraction.h`: one shared claim per hand, release, cancellation and generation checks | Actual actor/equipment/space generations and original tracked samples |
| Ammunition supply | `AmmoSupply.h`: acquire a representation of existing reserve and resolve the exact reservation | Fresh authoritative reserve counts and verified native completion |
| Guided insertion | `ReloadInsertion.h` and `ManualReload.h`: contact, alignment, insertion progress and mechanism sequencing | Authored prop/grip/rail geometry, native reload phase and mechanism-specific operations |
| Body equipment | `BodyAnchors.h` and `BodyInventory.h`: chest/shoulder zones, item assignments and draw/holster transactions | Actual carried item identities/categories, native selection, visible/hidden presentation and input suppression receipts |
| Hand appearance | `HandPose.h`, `HandTouch.h` and arm IK: open/curl/touch poses and independent targeting | Skeleton roles, bind calibration, native palette publication and exact preservation of unrelated animation |
| Validation | Deterministic policy tests and the evidence/audit structure | Title-specific capture decoder and native trace fields, both-eye pixel checks, then headset acceptance |

The BC2 implementations (`Bc2PhysicalReload`, `Bc2ReloadFlowRuntime`,
`Bc2BodyInventory`, `Bc2WeaponVisibility`) are reference adapters. Their native
offsets, input numbers, mesh hashes and x86 call sites stay local. The saved-trace
auditors currently parse BC2 evidence; another game needs a decoder or a versioned
canonical export, not renamed BC2 JSON that appears to pass.

For BF3, bring up one ordinary weapon and one verified reload family end to end.
Use that second real consumer to identify useful common adapter services. Then
batch-capture additional weapons and configure measured grips, prop poses and
rails. Manual exceptions should be limited to distinct mechanisms or asset
differences. BF4 can reuse the resulting shared policy and tools while verifying
its own native bindings. No BF3/BF4 binding or per-title time saving has yet been
measured, and no all-weapons reload claim follows from the first SPAS pass.

## Adapter seams to finish

1. **Turn the discovery module into a usable runtime boundary.**
   [ModuleApi](../include/fvr/engine/ModuleApi.h) currently exports metadata through
   `FvrGetModuleInfo`, not render/gameplay callbacks. The
   [BC2 discovery module](../src/games/bc2/Bc2Module.cpp) still reports zero
   capabilities; the working opt-in native probe is a separate integration path.
   BC2 does not directly implement `IRenderAdapter`. First document how its native
   callback stages satisfy that contract, then extract the smallest common runtime
   service needed by BC2 and BF3. Do not force an asynchronous native job graph
   into synchronous callbacks or undertake a speculative full rewrite.

2. **Unify capability evidence.**
   Preserve the existing signature + ABI + identity + relationship gates.
   `runtime::Capabilities` requires stereo visibility; `FrostbiteEvidence` does
   not yet expose that separate gate. Reconcile these and report per-feature
   status: unavailable, investigated, native-verified, headset-accepted, with
   build/evidence references and a reason when disabled. Reload, empty hands,
   body inventory, individual mechanisms, HUD and vehicles need their own gates.
   One successful rifle or stereo test must not enable every capability bit.

3. **Define a gameplay snapshot/command boundary.**
   This is proposed, not an exported interface today. Supply canonical poses and
   units, scene/actor generation, tracking-space generation, authoritative held
   item and loadout revisions, validated rig roles, and timestamped action state.
   Accept semantic commands and return exact native acknowledgements. Input,
   contact geometry and rendered poses must refer to coherent samples. Never use
   a native pointer or equipment-array slot as a persistent item identity.

4. **Preserve frame ownership across native stages.**
   `WorldFrame` and `FrameLease` already carry owner/frame/device/space identity.
   Capture one tracking sample before visibility, render both eyes without another
   simulation or animation tick, restore on every outcome, then publish a complete
   pair. Keep the original rendered poses with delayed images. `PairConsumed`
   means GPU consumption, not successful headset presentation. Loading, resize,
   device loss, respawn, focus loss and reconnect must invalidate the right state.

5. **Normalize capture and profile data.**
   The current collectors and offline parsers understand BC2 trace fields and
   asset identities. Add a versioned canonical capture adapter for each title;
   retain provenance, units, source timestamps, topology/bind fingerprint,
   native-before-VR poses, settling status, and rejected/dropped counts. Do not
   label unparsed or missing data as a successful capture.

Keep the C DLL/IPC boundary fixed-width and versioned: no STL, exceptions,
allocator ownership, or engine pointers crossing it. Internal C++ policy
interfaces need not become a public binary ABI merely to share source.

## Bring-up sequence and acceptance gates

| Gate | Deliverable | Evidence required to proceed |
| --- | --- | --- |
| 0 — Environment and baseline | Exact title/build manifest, executable architecture, chosen graphics settings and controlled test route | Installed build identified, reproducible unmodified run, suitable campaign/offline/local test environment confirmed. Read-only investigation first; discovery must not enable hooks |
| 1 — Native discovery | Read-only camera/owner/renderer/rig inventories and binding report | Unique bounded signatures plus ABI/object/relationship checks; measured units and coordinate conversions; observed lifecycle/thread ownership. Hash matching alone is insufficient |
| 2 — Native stereo | One repeatable scene rendered through the title's adapter | Distinct actual eye images/projections, correct visibility, one simulation update, complete pairs only, exact native restoration. Exercise failure after each stage; inspect moving geometry, first-person meshes, scopes, effects and temporal artifacts |
| 3 — Headset presentation | Existing host/transport reused or a new backend verified | Source-pose/image agreement; frame age, dropped pairs, GPU waits and failures reported. Repeatable steady run plus rapid turns, focus changes, reconnect/recenter, loading and device transitions. Native fixture success and headset acceptance are separate results |
| 4 — Basic embodiment | One validated ordinary weapon family, independent hands, roomscale and native action bindings | Controller/weapon/shot agreement; no source-palette feedback; correct support release. Test equip, pickup, reload, death/respawn and owner changes. Roomscale must respect native collision rather than only displace the camera |
| 5 — Content expansion | Batch profiles and mechanism-family bindings | Feature-specific evidence per item/mode; native acknowledgement for physical operations; unsupported items retain a defined fallback. Add optics, HUD, vehicles and scripted cameras as separate capabilities |
| 6 — Developer release | Reversible package, sample adapter, evidence manifest and compatibility table | Relevant deterministic regressions and architecture builds pass; tested payload identified; no game binaries/assets or private data included; limitations and recovery procedure reproducible |

Record frame timing distributions, resolution and refresh rate with each run.
A powerful GPU improves available budget; it does not establish correct visibility,
pose timing, synchronization or native lifetime handling. Keep failed runs and
fault-injection results alongside passing evidence.

## Proposed title order

This order is an engineering recommendation, not an estimate of relative difficulty.
Engine branding supports the family relationship; it provides no binary compatibility
guarantee. Verify the actual installed renderer and executable architecture rather
than deriving either from marketing names or minimum requirements.

| Priority / title | First investigation and useful test entry | What must remain unknown until measured |
| --- | --- | --- |
| 1 — BF3 | Use it as the second consumer that proves the shared-core boundary. Start with an accessible controlled infantry campaign checkpoint, after environment verification. EA identifies it as Frostbite 2 in its [product page](https://www.ea.com/games/battlefield/battlefield-3) | Native view scheduling, graphics path, owner and rig bindings, source animation access, resource sharing and current launch/access constraints |
| 2 — BF4 | Compare the new adapter with BF3 and extract only demonstrated common mechanisms. Its [PC manual](https://akamai.cdn.ea.com/eadownloads/u/f/manuals/GAME-BF4/b4-pc-en.pdf), pp. 7 and 47, documents campaign and an offline Test Range; verify availability in the installed build | Whether any BF3 structures survive, renderer/temporal differences, first-person meshes, loadout/vehicle transitions and native action semantics |
| 3 — Hardline | Begin in a controlled campaign scene. EA's [release announcement](https://news.ea.com/press-releases/press-releases-details/2015/EA-and-Visceral-Games-Announce-Battlefield-Hardline-Is-Available-Now/default.aspx) identifies its campaign and Frostbite 3 | Native layouts and game-specific tools/actions. Sharing the engine label with BF4 does not authorize reusing BF4 bindings |
| 4 — BF1 | Begin with one infantry role in the officially described [War Stories](https://www.ea.com/en-gb/games/battlefield/amp/news/the-single-player-of-battlefield-1); expand role/vehicle coverage separately | Rendering/backend compatibility, animation ownership, mechanism families and scripted-camera transitions |
| 5 — BFV | Apply the same discovery gates to a selected [War Story](https://www.ea.com/games/battlefield/news/war-stories-overview) | All native bindings, renderer paths, hand/weapon rigs, testing access and lifecycle behavior; no inherited BF1 capability flags |
| Separate feasibility gate — 2042 | Establish a suitable supported testing environment before native integration. EA's [published briefing](https://www.ea.com/en-au/games/battlefield/battlefield-2042/news/battlefield-briefing-answering-your-reveal-questions) explicitly describes Solo against AI as online | Current environment/access constraints and all technical bindings. Solo, private or Portal must not be assumed to mean offline or unprotected |
| Discovery only — BF6 / future titles | EA documents a [BF6 campaign](https://www.ea.com/games/battlefield/battlefield-6/news/battlefield-6-campaign); that is a possible investigation entry, not proof that this adapter strategy is available | Installed architecture/backend, supported test environment, integration access and every native capability |

Prefer offline/campaign/local controlled testing wherever available. This roadmap
contains no anti-cheat bypass, protected multiplayer deployment, or modification
of public sessions. If a suitable supported environment is unavailable, record that
as the feasibility limit and continue with offline source/tooling work.

Other Frostbite franchises can use the same investigation process, but are not
implicitly supported by a Battlefield adapter or by this priority list.

## Scale hands and weapons through data and mechanisms

The unit of implementation should be a validated rig or mechanism family, with
small per-item bindings, rather than a new VR implementation for every gun.

1. **Calibrate each rig family once.** Validate topology and inverse binds, derive
   anatomical palm/finger frames, and bind semantic hand roles. Reuse IK and finger
   policies while retaining native authored weapon-support animation. New topology
   or bind fingerprints require validation, even when joint names match.
2. **Capture loadouts in batches.** Adapt the existing
   [weapon pipeline](../tools/weapon_profile_pipeline.py), collecting original
   settled attachments before VR posing. Separate equip episodes and physical
   item generations; reject ambiguous poses. Pipeline output remains a candidate,
   never automatic runtime enablement.
3. **Gate each feature independently.** [WeaponProfile](../include/fvr/interaction/WeaponProfile.h)
   currently gates aim alignment, support grip and translated muzzle only. Reuse
   a verified family binding where its relationships hold; review exceptions per
   asset and mode. An underbarrel launcher sharing a rifle mesh does not prove its
   projectile origin, handle, or native selection relationship.
4. **Bind native mechanism families once.** [SightFlip](../include/fvr/interaction/SightFlip.h)
   and [ManualReload](../include/fvr/interaction/ManualReload.h) supply portable
   intent/acknowledgement policy. The adapter supplies verified pivot/contact
   geometry, native operation and authoritative state. Magazine changes, individual
   rounds, breeches and cycling actions need different native evidence. Moving a
   bolt/pump bone alone does not chamber a round; `CycleAction` is a semantic
   operation, not a universal native pump binding. Never fabricate ammunition.
5. **Keep body inventory authoritative.** [BodyInventory](../include/fvr/interaction/BodyInventory.h)
   assigns body slots from adapter category preferences, allowing a shotgun-on-back
   preference without hard-coded gun names. Refresh on pickups/replacements and
   reject stale draw intents. Empty hands require verified weapon hiding and fire
   suppression; weapon and arm meshes sharing a palette is not proof that hiding
   one is possible. Native equip slots may remain internal to physical interactions.

This reduces repeated implementation while retaining per-family calibration and
per-feature evidence. Capacitive touch is optional pose input, not an invented
button action; controllers without a sensor must expose that absence explicitly.

## First actionable development batch

- **Framework task:** document BC2's staged native callback mapping to the shared
  contracts; reconcile capability reporting and add a minimal second-adapter
  skeleton with every native feature disabled by default.
- **BF3 discovery task:** produce the exact-build/environment manifest and bounded
  read-only owner/camera/renderer inventory. Report evidence and missing bindings
  before proposing native writes.
- **Offline tooling task:** define canonical capture input and run existing
  policy, ownership, frame-pair and profile rejection fixtures against synthetic
  second-title data. Keep BC2 schema translation in its adapter.
- **Integration task:** implement only the bindings needed for Gate 2, retaining a
  known BC2 checkpoint and regression results. Broader weapon, HUD and vehicle
  work follows demonstrated stereo/ownership stability.

Agents can parallelize discovery analysis, portable fixtures, capture normalization
and profile review. Give one owner the live native experiment and shared runtime
files at a time. Every adapter handoff should include build identity, environment,
capability/evidence matrix, tested scenes, native restoration results, remaining
unknowns and the next bounded experiment.

## Focused subsystem plans

- [Local full-body IK](FULL-BODY-IK-ROADMAP.md): body targets and native mesh/palette
  ownership, preserving current arms/hands and native locomotion.
- [Flatscreen visualizer and networking](FLATSCREEN-VISUALIZER-NETWORK-PLAN.md):
  shared pose semantics, remote actor binding, interpolation and a measured
  decision on pose transport versus native server integration.
- [ADS overlay optics](OPTICS-ADS-ROADMAP.md): magnified native views and reticles
  inside the relevant physical optics. Preserve the accepted XM8 optic and keep
  ACOGs outside this work, rather than treating every zoom-capable weapon alike.

These documents are research and implementation plans. None grants new native
capabilities to BC2 or another Frostbite adapter.

[Belt-fed reload planning](BELT-FED-RELOAD-PLAN.md) extends the shared mechanism
approach with explicit cover/container/belt semantics while preserving native
ammunition authority. It grants no native LMG capability.


Portable implementation follow-up: [feed prerequisites](FEED-MECHANISM-20261001.md)
and [hand ownership](HAND-INTERACTION-20261001.md) are now tested foundations.
The [optic evidence tool](OPTIC-INVENTORY-20261001.md) audits existing capture gaps.
None supplies a native binding for another Frostbite title. The current product
priority is the [single-player BC2 release](SINGLEPLAYER-RELEASE-PLAN.md).
