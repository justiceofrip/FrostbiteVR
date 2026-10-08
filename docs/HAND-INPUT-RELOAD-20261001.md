# Hand input and reload grip — October 1, 2026

The user accepted the preceding free-left-hand animation and improved launcher
sight mechanics in headset session 105939-315. That session was ended on explicit
user authorization; its evidence is preserved in
reports/headset-hand-roles-feedback-20261001-105939/acceptance.json.
This candidate adds support retention, right index movement and optional touch
input. These additions have native/offline evidence; their headset appearance
and feel remain untested.

## Implemented behavior

- Left trigger no longer requests native ADS/Zoom. Its analog value still drives
  finger posing. Native Zoom remains available as an internal semantic capability
  for future verified optic-specific behavior. No postprocess, LOD or material
  layer was globally disabled; the reported XM8 blur/detail cause remains unknown.

- Ordinary Reload no longer explicitly cancels support grip. Portable
  GripAttachment captures the authored wrist relative to the item for one grab
  token. Both visible attachment and distance testing use that contact through
  native animation. Release, tracking/identity loss, switching, use and other
  existing cancellation rules still apply. The raw hand stays independent.
- FingerCurlOverlay applies an analog right-index curl to the current authored
  pose after arm solving, including mechanism-preview poses. BC2 binds the right
  finger anatomy once per verified skeleton, preserving the gun wrist and other
  branches. Neutral trigger adds no rotation. This does not move the gun's
  trigger mesh; its native mesh/animation binding is still unknown.
- Free-left-hand thumb/index touch sensors now augment analog posing. Unsupported
  sensors retain the previous analog behavior. Support/mechanism shapes stay
  role-specific. Right free-hand anatomy is prepared but empty hands are disabled.
- The shared BodyInventory policy now handles authoritative inventory snapshots,
  stable category-preferred slots, pickup/replacement identities and acknowledged
  holster/draw transactions. It is tested but not connected to native gameplay:
  BC2 still needs a verified gun-hide/presentation boundary.

The support fix addresses two identified causes: the explicit Reload cancellation
and an authored contact that moved with reload animation. It is not a claim that
every possible support interruption is resolved.

## Touch transport

ControllerState records sensor availability separately from touched state.
InputPacket version 2 retains the 288-byte layout, placing active/touched masks in
the former controller reserved word. New readers accept version 1 only with its
original zero reserved word. Version2 rejects unknown bits and touched-without-
active masks. Update the host and native payload together.

OpenXR profile bindings request optional trigger and thumb-surface touch inputs.
If optional touch paths are rejected, the profile retries without them. This is
an optional pose input, not a gameplay button. Actual SteamVR controller sensor
delivery still needs a headset test; the native fixture injected touch samples
and recorded both touched/untouched states after transport. See the
[OpenXR interaction-profile specification](https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html)
for the runtime binding contract.

## Evidence

Both required builds pass: 48 x86 and 47 x64 CTest suites. Relevant suites cover
grab identity and cancellation, packet validation/backward reading, touch fallback,
index-branch isolation and body inventory ownership/replacement.

The dedicated support/reload fixture is
reports/native-trace-20261001-112224-172 with receiver
reports/native-ipc-20261001-112223.

- One grab token stayed held through the Reload action and all four native shell
  transfers. Both inspected firing branches changed 4/8 ->5/7 ->6/6 ->7/5 ->8/4,
  conserving 12 total rounds. Transfers were 719/718/719 ms apart.
- Native weapon-relative wrist movement measured 591.8 mm while the rendered
  attachment drifted 0.194 mm. No release occurred while the scripted grip was held.
- The fixture deliberately released at 9.5 s, after the last shell but before
  native post-reload readiness. It does not prove held grip through that final
  post-sequence interval or physical pumping.
- The index overlay has 12 active and 89 neutral recorded samples. Neutral adds
  exactly no change; the active rotation matches a single overlay within
  0.00000185 rad. Wrist and other branches remain preserved.
- All 240 stereo pairs arrived; one recovered receiver timeout is retained.
  Native source/packing/cleanup checks passed. No headset or mesh-trigger claim.

Independent evidence: reports/grip-reload-audit-20261001.json,
reports/reload-native-audit-20261001-112223.json,
and the original reports/reload-state-20261001-112223.json.
The joined audit verifies counter transfers within the held grasp. Its valid
baseline passes and all 14 failure mutations reject. The external read-only state
collector retained 47 rejected racing reads rather than treating them as coherent
state. Its timestamps correlate separate observations; they are not an atomic
in-process reload/pose event trace.

The subsequent launcher regression
reports/native-trace-20261001-113015-478 /
reports/native-ipc-20261001-113015 passes both physical mode changes, native
acknowledgements and all three hand-role audits: 240 pairs, 0 timeouts,
352 preview poses, maximum resolved grasp error 0.156 mm. Existing launcher
alignment, preview and hand-role checkers pass. The first alignment invocation
omitted the launcher-first option and is retained as
hand-input-reload-launcher-wrong-start-20261001.json; rerunning with the actual
fixture option passes without code or threshold changes.

Selection preparation 112832-353 delivered 240 pairs with 0 timeouts but one 91 ms
delivery outlier. This and the reload fixture timeout remain evidence of timing
interruptions; no performance repair is claimed by this candidate.

## Shared foundations and next work

[Native pump/reload research](BC2-PUMP-RELOAD-20261001.md) identifies SPAS single-
bullet reload transfers and its native bolt-action cycle dispatch.
[ManualReload](MANUAL-RELOAD-20261001.md) provides the reusable operation/ack
coordinator. Pump rear/forward and bolt lift/rear/forward/lock can feed the same
CycleAction operation, but their recognizers, geometry, native deferral and
completion bindings still require implementation and evidence. No manual pump,
sniper bolt or physical ammo transfer is enabled by this work.

[Body inventory](BODY-INVENTORY-20261001.md) allows category-based back/hip/chest
preferences while reconciling changing pickups. Runtime empty hands, physical
draw/holster, magazine/shell objects and weapon trigger mesh motion remain
disabled. See the [Frostbite porting roadmap](FROSTBITE-PORTING-ROADMAP.md) for
reusable systems, adapter gaps and per-title bring-up.

Next headset check: right-index appearance through a trigger squeeze, free-left
thumb/index touch response, and maintained support during ordinary SPAS reload.
Confirm launcher grasp and recenter remain comfortable. Use the existing
Start-BC2VRSession.ps1 -SightFlip opt-in when the user is ready; do not claim
physical pumping or empty-hands controls are present.

## Reproduce the evidence audit

From <local-workspace>, using the installed Python interpreter:

    python -X utf8 -B tools/check_grip_reload.py --native reports/native-trace-20261001-112224-172 --receiver reports/native-ipc-20261001-112223 --reload-state reports/reload-state-20261001-112223.json --output reports/grip-reload-recheck.json
    python -X utf8 -B tools/check_grip_reload.py --self-test --output reports/grip-reload-mutations-recheck.json

These commands read saved reports and write new audit results. They do not open
or control the game. Preserve the original evidence files.

## Later user correction: remove manual ADS

After the initial hand candidate, the user reported left-trigger ADS makes the gun
look blurred or lower-detail, especially XM8, and requested no manual ADS binding.
ControllerActions no longer translates that trigger into AlternateFire.
Bc2InputBinding's native Zoom semantics remain for future verified internal use.
Focused regressions preserve left finger input, independent right Fire, Use and
tracking recovery; both complete builds still pass 48 / 47 suites.

Current payload native115047-514 / receiver115046 records nine full-pressure
free-left-hand samples and 119 semantic action records with no AlternateFire
request. Both physical launcher mode requests/acknowledgements, hand-role and
sight-preview checks pass; all 240 pairs arrived. The strict launcher audit
fails its zero-timeout requirement because two receiver timeouts recovered;
one delivered pair took 92.471 ms. That failed audit is preserved unchanged.
No repeat was run to hide these timing observations.

Evidence: reports/no-manual-ads-input-20261001.json, with separate role/sight/
launcher audits. Earlier grip/reload and zero-timeout launcher evidence uses its
own recorded pre-ADS-removal binary hashes. The candidate summary preserves both
payload identities rather than attributing those earlier runs to the new binary.

Headset confirmation of the reported visual symptom remains pending. Future
overlay optics must separate native aiming state from verified unwanted ADS
visual effects; see [optics roadmap](OPTICS-ADS-ROADMAP.md). The working XM8 optic
and ACOGs retain their rendering. No physical automatic ADS is enabled yet.
