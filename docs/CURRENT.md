# Current state — shared normal integration / public 0.1.15

Updated November 13, 2026. This replaces old next-step instructions; prior evidence remains in
[history/](history/README.md) (search it by run number; never read a log whole).

## Active work

Private **0.1.98** remains installed and setup-validated, with0.1.97 as fallback. Native pistol-slide sample-time correction and its passing builds remain unchanged. Actual98/11 and13 acquired the bolt rifle and ordinary held grip;11's retained grip references pass preparation admission.13 stopped before firing because its clearance retained the previous50-degree aim; the recorder now rejects that contradiction before issuing a clearance.17 focused checks pass, including unchanged13 data. The pickup reader also loads correctly under isolated embedded Python, with15 checks passing and the unchanged exact-PE check reused.12 failed before game startup;11/13 closed normally, and all three restored profiles/settings. These tool corrections do not establish full bolt-magazine/front-loader reloads or a new installation. [Current delivery and acquisition](../handoff/integrations/coordinator-20261113-weapon-acquisition/README.txt), [retained family checks](../handoff/integrations/coordinator-20261113-family-runtime/README.txt).

Optional performance mode is off by default. Actual92/01 verified1x native render/depth targets, all three source slots and visible gameplay at simulated1024x768; originalMSAA3, profiles and launcher settings were restored after normal game exit. This qualifies the settings lifecycle and native path, not headset resolution, FPS or physical allocation. Four native depth-eye rejections remain recorded. [Current evidence](../handoff/integrations/coordinator-20261113-family-runtime/README.txt).

Weapon work is organized by shared reload family, with registered profiles checked in bulk. The next useful result is complete reload/fire/switch behavior on a representative family route. Individual gun names identify evidence, not separate implementation jobs or whole-family completion. [Family coverage and remaining gaps](WEAPON-COVERAGE-CURRENT.md).

Actual90/01 rendered visible gameplay through the optional three-source pipeline and retired normally after435 seconds, restoring profiles/settings/environment. Its retained timing window shows mean publication-to-copy-submission delay12.636ms versus89's48.495ms, with143 copies completing in the same iteration after submission before the XR wait. These are unmatched diagnostic windows, not a headset FPS comparison. Native89's8x depth correction and six hardware cases remain applicable. The path remains off by default; higher headset resolutions are not established and existing memory ceilings still apply. Actual88/01's depth-admission failure and unproved native retirement remain preserved. [Scope and evidence](../handoff/integrations/coordinator-20261112-multisample-pipeline/README.txt).

Installed-normal87/08 completed opening-breech selection, preparation shot, all five manual reload stages, a later shot and return to the rifle. The observed counts were1/7→0/7→1/6→0/6, then rifle21/210; the native server-shoot helper accepted the later shot. The complete reload driver finished with no pending input or held mechanical grip. Its strict three-copy firing audit cannot cover the later shot because that recording window had ended; projectile impact and headset reliability remain unproved. The343-second session retired normally and restored profiles/settings. Earlier86/01's driver timeout remains preserved. [Shared underbarrel changes and exact evidence](../handoff/integrations/coordinator-20261112-underbarrel/README.txt).

Native83's shared mechanical-binding-gap recovery and audio-worker shutdown correction remain included. Earlier83/03 and83/06 driver failures are preserved;87/08 supplies the later complete opening-breech route above.

Actual91/02 completed all five belt-feed stages and a subsequent native shot: all three ammunition copies changed200/487 to199/487 under the same Fire token. Four recording drops limit completeness. Actual92/01 then completed ordinary held-weapon swaps rifle→LMG→rifle with exact target lifetimes, neutral release and unchanged rifle21/210; movement afterward reached the courtyard. Empty-hand QJU88 stowing remains deferred. Headset reliability remains open; do not repeat the passed refill/angle sweep or make each LMG a separate project. [Current evidence](../handoff/integrations/coordinator-20261113-family-runtime/README.txt), [earlier refill evidence](../handoff/integrations/coordinator-20261108-tools801/README.txt).

The shared observation corrections pass the combined offline checks, including the retained Open and completed lid-closure recordings. The [shared entry update](../handoff/integrations/coordinator-20261112-shared-entry/README.txt) removes the holster command’s old report imports, makes staging use explicit installed versions/fresh destinations, and gives the canonical runner a recorded storage-ceiling option. These tooling changes leave native binaries unchanged. Full session and gameplay acceptance remain separate from the scoped offline checks. Current local receipts and active assignments: `reports/coordinator-20261108-feedback721/CURRENT-STATE.txt`. Private83 packets bound to installed77/76 are historical after the83 installation and must not be reused.

Native73 adds knife render recovery from the same still-live hand claim and separate passive current-hold observation; strict Held and attack gates remain unchanged. Actual593's ordinary native Draw/Return remains valid, but strict Held, rendered visibility and attack/damage remain unproven. Actual630 reached gameplay and genuine Empty, then refused its first knife command. Actual636 stopped during the preceding holster release, before invoking the corrected knife test. Both closed normally with profiles/settings restored. [Native73 delivery and evidence limits](../handoff/integrations/coordinator-20261107-native73/README.txt).

Actual93/03 completed native Draw1 → Attack2 → Return3 from one measured120mm/120ms physical jab with zero trigger input. The attack receipt and both required context copies completed; the knife was visible while held and removed on return. Damage against a target and headset feel remain unproved. The detailed recognition window was overwritten, so this does not separately prove that the bounded sample-gap recovery ran. The179-second session retired normally and restored profiles/settings. Earlier failures remain preserved. [Current evidence](../handoff/integrations/coordinator-20261113-family-runtime/README.txt).

Actual87/05 completed23 ordinary movement/head-turn pulses across eight invocations, all with acknowledged neutral release. Both-eye review shows the player leaving the house and walking into the courtyard. This establishes the campaign navigation path; C4 objectives and a mission-permitted vehicle seat have not yet been reached. Navigation41 and reload79 offline checks pass. Native input/source deadlines remain unchanged. [Scope and checks](../handoff/integrations/coordinator-20261112-campaign-tools/README.txt).

Actual94/01 resumed the retained first-mission beach checkpoint and used the new navigation observation with the scripted knife selected; both-eye captures show movement from water onto shore. Root firearm snapshots were valid in this run, so the missing-firearm case remains qualified offline only. A180/-180degree tool comparison and later command-preparation expiry stopped further progress; controls released cleanly. The888-second session retired normally and restored profiles/settings. C4 remains open. [Navigation batch](../handoff/integrations/coordinator-20261113-navigation/README.txt).

Actual97/03 continued inland through24 movement pulses and the ordinary Y crouch toggle, visibly passing beneath the fallen log. Both transient navigation refusals recovered through fresh same-owner observations in the actual game. The tool-only correction passed nine focused ownership/deadline checks; native97 is unchanged. The907-second bounded session retired normally and restored profiles/settings. C4 was not reached. [Current navigation result](../handoff/integrations/coordinator-20261113-navigation/README.txt).

The chest knife accepts a deliberate physical jab while held, with no trigger press required; actual93/03 now confirms the native attack dispatch. The optional stock trigger attack remains. The jab uses BC2's original melee targeting; tracked blade-tip collision is disabled. Release grip to return the knife. Target damage, headset feel and visibility stability remain unproved. Earlier inventory allocation improvements remain included without an FPS-gain claim. [Jab implementation](../handoff/integrations/coordinator-20261107-native75/README.txt), [current native result](../handoff/integrations/coordinator-20261113-family-runtime/README.txt).

Actual93/02 loaded the known SP03 boat checkpoint, accepted ordinary X exit, resolved the player back to their soldier, resumed on-foot movement and drew the shoulder shotgun with8/24 ammo visible in both eyes. The392-second session retired normally and restored profiles/settings. Seat switching, broader vehicle controls and C4 charge placement remain open. [Current vehicle evidence](../handoff/integrations/coordinator-20261113-family-runtime/README.txt), [opening C4 repair](../handoff/integrations/coordinator-20261104-opening-charge/README.txt).

Actual95/01 isolated the seat-switch refusal to its binding check. Actual96/01 then established that the keyboard concept was exactly unbound after all23 reads and other prerequisites passed. Private97 repairs this supported semantic-action path, but a successful seat transition remains unproved. Both diagnostic sessions retired normally and restored profiles/settings. [Exact record](../handoff/integrations/coordinator-20261113-navigation/README.txt).

The installed magazine handler now accepts a settled bolt-action rifle when its magazine needs work. It preserves each rifle’s empty well across switches, including before a new native binding arrives; an owed bolt stroke stays with its owner. This covers 17 M95, M24, GOL and SV98 configurations. A complete manual bolt-rifle magazine reload has not yet been demonstrated in BC2. [Weapon-family coverage](WEAPON-COVERAGE-CURRENT.md).

Destruction update, November4: the user reports that the previously repeatable building-rendering bug appeared fixed on the same level in their latest headset test. They remain uncertain; the exact tested build and level were not identified. Treat this as positive headset feedback and an apparently resolved scene-specific issue; preserve the existing correction and watch for recurrence. Broader scene coverage remains unconfirmed.

Actual38 ordinary holstering reached Empty, drew QJU88 and opened its cover; container removal then lost its claim without an accepted operation. Actual37 XM8 launcher fired1/7->0/7 and visibly damaged a wall, but reload admission remains unresolved. Actual104 physical sight selection committed and a native shot joined HUD1/5->0/5; no new facade damage or projectile impact was established. Both104/106 closed normally with native retirement and exact profile/settings restoration. Underbarrel reloads, broader LMG coverage, destruction rendering, performance and headset acceptance remain open.

The user's 0.1.22 headset session confirms AN94 manual magazine reload and smooth firing. It reports support-grip drops, T194 shell insertion/QJU88 reload failures, underbarrel failure and HUD flicker; [feedback](archive/2026-10-31-bc2vr-0.1.22-buglist.md). Cadence58's Fire-continuity correction remains included. [Test card](../handoff/integrations/coordinator-20261031-feedback59/TEST.txt).

Offline57's shared magazine/feed reload connections, bounded hitch protection, fewer rig reads and deferred frame-feedback service are retained. [Prior receipt](../handoff/integrations/coordinator-20261030-offline57/receipt.json) and [actual pipeline](../handoff/integrations/coordinator-20261030-offline57/PIPELINE.txt). All-weapon reload completion, measured FPS, aim and destruction acceptance remain open.

Feedback56's anatomical feed contacts, reviewed visual axes, skin-decal/soot correction and host copy poll are retained. [Prior receipt](../handoff/integrations/coordinator-20261030-feedback56/receipt.json). Its native menu observation did not establish campaign reload or headset acceptance.

Integration55 supplied0.1.19's weapon/ammo/underbarrel wiring and depth-target reuse, retained in the current candidate. [Integration receipt](../handoff/integrations/coordinator-20261030-integration55/receipt.json). No headset acceptance or measured performance gain is inferred from setup validation.

Headset52 enrolled 77 names/181 configurations in private0.1.18; [receipt](../handoff/integrations/coordinator-20261030-headset52/README.md).
The user confirmed multiweapon two-hand grips and reliable swaps. [Feedback53](archive/2026-10-30-bc2vr-0.1.18-buglist.md)
records reload/ammo, muzzle, stow and underbarrel/sight failures; C4 was not tested. The weapons job remains unaccepted.
Wiring54's shared ammo/feed, measured muzzle/underbarrel connections and depth-target reuse
first shipped in 0.1.19 and remain included. [Original source evidence](../handoff/integrations/coordinator-20261030-wiring54/STATUS.txt).

The retained launcher identity and 49-part cache checks are recorded in [Evidence08](../handoff/integrations/coordinator-20261029-evidence08/README.md).
[User headset feedback on 0.1.17](archive/2026-10-29-bc2vr-0.1.17-buglist.md): C4 progression blocker, severe performance/choppy fire, destruction and cutscene bugs; interactions improved. Acceptance remains open.
Runtime05's stock black captures and cleanup remain in its [diagnostic handoff](../handoff/integrations/coordinator-20261029-runtime05/README.md).

Runtime02's guarded desktop tools, stock black failure and exact cleanup remain in
its [handoff](../handoff/integrations/coordinator-20261028-runtime02/README.md).
BC2 is visible with the headset connected. The desktop failure does not establish
VR failure or require repair before a headset check; do not repeat unchanged desktop tests.

CHAT07/CHAT23 source is combined with finished CHAT25/26: chest reserve continuity
and grenade/reload hand-claim/rearming regressions are retained. The [batch receipt](../handoff/integrations/coordinator-20261028-batch01/README.md)
records the original305/305 builds and evidence limits. Headset failures remain open;
other cards' exact prerequisites are recorded once in the coordinator state.

CHAT25/CHAT26 knife regressions and optional original-call sound context are
integrated; gameplay recognition, damage and sound forwarding are unchanged.
Both normal builds passed305/305 plus four focused suites each. The
[integration handoff](../handoff/integrations/chat25-chat26-20261028-integration01/HANDOFF.txt)
pins six binary/PDB pairs and preserves the foreground-blocked game setup and
OpenXR instance failure (`XrResult=-2`). No knife episode or headset check ran;
the game closed normally and all three profiles were restored. Install09 now
includes this source in0.1.17; native single-jab/audio acceptance remains pending.

CHAT33 launch diagnostics and placement checks are integrated: setup/Play reject
protected game-folder placement early, and Steam reports separate requested opt-in
from native-profile refusal. Seven canonical suites pass; native source/artifacts
are unchanged. The [integration receipt](../handoff/integrations/chat33-20261028-integration01/README.md)
retains the repaired restart-fixture failure and a fresh SteamVR OpenXR instance
failure (`XrResult=-2`). Install09 includes these changes while preserving settings;
actual Play/headset checks remain pending. Steam native compatibility is unverified.

CHAT06 vehicle-transition retention regressions are integrated: nine ammunition
cases and four pump/bolt cases pass in both normal configurations (two suites
per architecture). Runtime source and the CHAT04 artifact receipt are unchanged.
The [integration receipt](../handoff/integrations/chat06-20261028-integration01/README.md)
records another OpenXR instance-creation failure (`XrResult=-2`); no game/install,
launcher0.1.12/settings unchanged. Native permitted entry/exit, equipment lineage,
partial reload and owed-cycle recovery, and headset acceptance remain pending.
The tests protect retained debt; they do not prove successful return on foot.

CHAT05 regression coverage is integrated: nine pickup resource cases and scoped
MP443 hip stow/draw preserve the tested ownership boundaries. Both normal
architectures passed the two affected suites; the runtime source, operation
records and prior CHAT04 binary receipt are unchanged. The [integration receipt](../handoff/integrations/chat05-20261028-integration01/README.md)
records a fresh OpenXR instance-creation failure (`XrResult=-2`), unchanged
launcher0.1.12/settings and no game/install. CHAT05 still needs native stow/release,
pickup/replacement, owed-cycle and headset evidence; diagnostic CPU hide/show
does not qualify real props or firing.

CHAT04 original-magazine tracking suspension is applied on R56: lost input drops
transient pose/hand authority while retaining the exact original and its rounds;
recovery requires fresh same-owner evidence and a new hand claim. Both normal
architectures passed305/305 with unchanged normal feature settings and operation
records. The [integration handoff](../handoff/integrations/chat04-20261028-integration01/README.md)
pins the source, six binary/symbol pairs and retained failed episodes under
`reports/integration-chat04-20261028-01`. OpenXR preflight failed at instance
creation (`XrResult=-2`); that run left0.1.12 unchanged. Install09 now deploys0.1.17;
startup/equipment smoke remains pending. No native/headset acceptance follows
from these builds. CHAT04's replacement-magazine tracking loss, M95
left-root recovery, support/pump and native missing-render checks remain open.

The preceding R56 runtime and symbols remain as a fallback under
`root-custody-normal-r56-prep/build01`; its [source handoff](../handoff/checkpoint436-r56/README.md)
and `profiles/checkpoint436-r56` preserve the reviewed operation records.
M95 five-cycle native confirmation passed as detailed below. Scene124's Nambu
check stopped before gameplay input or mod attachment because BC2 did not retain
foreground ownership; it closed and restored all three profiles. Nambu acceptance
remains pending. That earlier run did not install or publicly release a runtime.

The R51 ammo-entry, R52 actor-retirement, R54 continuous GunHold and verified
edge-projection fixes are applied to the normal checkout: 13 production files
and four existing regression files, with other gameplay/launcher code preserved.
Normal feature flags and the reviewed native operation table are unchanged.
Private fixture, phase and payload instrumentation is excluded.

The preceding shared integration's build identities and independent artifact audit remain in
`normal-shared-integration-prep/build01`. Its normal builds qualify the build pair, not additional native/headset behavior.

Root owns builds, game launch/input and native sessions. Scenes120 through124
are closed; native/provision helpers retired and all three original profiles
restored. Scene122 MP443 and scene123 M95 passed their scoped native audits.
Original scene120 M95 failure and scene121 two-hand refused postflight remain
preserved; the later M95 pass does not rewrite the earlier failure.
Agents prepare disjoint source/reviews only. Keep BC2 on the left monitor;
respect input guards and do not touch BF2142.

## New actual-game qualifications

These are emulated-controller mechanical or paired-image results, not new
physical-headset acceptance, all-weapon coverage or campaign completion.
All paths below are under `bc2vr-recovery/performance436`.

- **M95_sp:** R56 `m95-custody-r56-prep/run01` passed5/5 ordinary manual bolt
  cycles through empty on all three native firing paths; final5/40 includes
  stock magazine refill. Independent audit
  `5eabaa559f5a1d8f1c7e7798b86adf41e23ff060120bb5d9c5fdbfaed2f47ee7`.
  All45 accepted current-contact rows join exact custody roots and original
  100ms input deadlines; release-to-ready12.2824–17.4457ms. All45 had valid raw
  poses: zero accepted missing-raw rows, so native frame-gap continuation remains
  unproved.37 record-lock drops and238 receiver timeouts are retained; no full
  callback, smoothness, manual magazine, all-sniper or headset acceptance.
- **F2000 + SPAS:** R54 `ammo-retirement-r52-integration-prep/combined-pump03/run01`.
  Six authority-joined magazine operations across all three firing copies;
  original return, replacement and post-holster return completed. SPAS fired
  8->7, completed manual pump on all three native paths, and loaded one shell
  7/24->8/23. Support returned; both shoulder stow/draws and final rifle firing
  completed. Audit `a7b65d1551a505830faf759cde871d16f40005ea55f713c226dfbf4f47447cf9`.
- **SCAR_sp_s:** `campaign-magazine-batch-r55-prep/single-magazine02/run01`.
  Preparation shot30/210->29/210; four native transfers29/210->0/210->29/210
  ->0/210->30/180; postfire29/180 in all three copies. Fresh support returned.
  Audit `e20b4d6bf7e9b1dcfaff7433de099a247335e9806f3b679f24123dee104eb500`.
  Seventeen record-lock drops are disclosed; not exhaustive callback capture.
- **MP443_sp:** `campaign-magazine-batch-r55-prep/mp443-single-magazine04/run01`.
  Exact normal-source, flags-only diagnostic:17/85->16/85 preparation shot,
  four authority-joined transfers16/85->0/85->16/85->0/85->17/68, then16/68
  postfire in all three native copies. Fresh support returned;31 client and31
  server post-detach samples stayed16/68. Independent audit
  `0a57a521fdfcd8eded71516f0e056efa8ad4a3cbe5fc4d1378d7fb990a800960`.
  Nine record-lock drops disclosed; all32032 retained rows finished. Native and
  receiver cleanup confirmed. Hip holster, slide/chamber, empty-fire suppression,
  other pistols and headset feel are not qualified by this episode.
- **AEK971_sp empty magazine:** `campaign-magazine-batch-r55-prep/empty-magazine02/run01`.
  Exactly30 server shots30/210->0/210; all three copies remained0/210 for5.046s;
  manual empty removal and refill30/180; postfire29/180, stable after detach.
  Original checker exit1 remains preserved: two native client prediction
  rollback/replay pairs were mistaken for reloads. The reviewed saved-evidence
  successor verifies immutable restore inputs, matching server shots, bounded
  replay and per-shot convergence. It retains all empty-dwell/refill/owner gates.
  Corrected audit `6fc76bd07f4a84fec9b06a7e8c27926a1a703098b0e649944f9eb01b1c784489`;
  independent audit `8284ee65319f3581ed45293c2c4700cd3c7aa0a659853f7c0b4cef96e25f76c4`.
  Twenty record-lock drops; retained dwell gaps <=14.337ms. No native rerun was
  needed. That episode did not cover two-handed empty firing or last reserve.
- **Two-handed AEK native evidence:** `campaign-magazine-batch-r55-prep/two-hand-empty03/run01`.
  Same support3/root2 through30 shots and5.026s at0/210; ordinary support release
  preceded authority-joined manual refill30/180 and one postfire29/180. All three
  native copies agree; later31 client and31 server drain samples retain the same
  owner and29/180. Independent audit `independent-native-audit01.json`, SHA
  `d3fa6aeda107808df469f42bba159f676e6402aaf1daf011ca01f953342b1b2b`.
  Original runner/checker exit1 remains: the initial postflight rejected differing
  client/server chain bookends and saved no samples. The differing field is unknown;
  no replacement passing receipt is implied. Seventeen record-lock drops; no
  last-reserve or headset claim. Scene121 closed/restored normally.
- **Destruction edge rendering:** `diagnostics-r50-edge-correction07-prep/run01`.
  Baseline phase06 reproduced a detached left-eye brick ring/strip. The exact
  edge caller0x5D7F28 is now retargeted through existing derived-projection logic,
  preserving depth and exact restoration. Root/independent review found the
  bricks attached in both delivered eyes, pair3 and pair239(3.781s apart).
  Witness pixels equal delivered pixels. Runtime audit
  `a62794104015d7e761b7851e07a4f6b986736b7ab593866eab28e9c280dda16c`:
  482 successful scopes,21 added contexts per left scope, zero projection/GPU
  failures. This covers one matched damaged-house fixture; no exact masonry
  VB/IB association, all-scene or headset claim.

## Next distinct checks

1. M95's prior2/5 failure remains at `m95-shared-validation-prep/run02`:
   expired rendered contact at input927 preceded mechanism/root loss and timeout.
   R56 focused suites, normal builds and the distinct5/5 native recheck now pass.
   Direct native missing-render-frame coverage and manual magazine loading remain
   unqualified; preserve the failed episode and current scoped success separately.
2. Diagnose the refused two-hand postflight read from saved evidence without
   weakening owner/deadline/ammunition gates or replacing the original failure.
3. Resume the prepared Nambu check when the game can retain focus, then continue
   representative shared-mechanic coverage using the qualified generic
   SingleMagazine flags and exact existing selectors. MP443 variant23 is now
   natively checked above; variant22 remains Nambu100. Verify campaign mounts
   before choosing the next SMG or magazine-fed LMG. Configured metadata and
   selector tests alone do not establish a loaded asset or native acceptance.

## Remaining campaign and presentation gaps

R47/R48 recovered paired color after actual Last Checkpoint replacement and
kept controller publication alive, but strict restart qualification still fails
one first-replacement-request cancellation. R48 retry request184 was cancelled
before either eye capture, then185 succeeded; do not call it a complete pass.
Evidence: `restart-fast-run-r48-retry-prep/run01`, independent audit
`59a3933f63ea5a4cb61962b2bfd2d2d9c576e34f7c6a44af01fe9bffa6e59644`.
The known-seat camera Wait is a candidate cause, not proven per-request evidence.
CHAT02's [R3 diagnostic integration](../handoff/integrations/chat02-20261028-integration01/README.md)
preserves rejected health baselines;18/18 canonical tests pass. No runtime change
or new actual-death/recovery/headset evidence; the bounded live baseline request,
full jeep ride and all campaign vehicle coverage remain open.

Preserve qualified performance comparisons; no guaranteed90Hz/headset performance
claim follows from diagnostic pairs. HUD flicker, knife hit/audio polish,
cutscene orientation, scope/weapon polish and vehicle reticles remain on
[the backlog](PREALPHA-BACKLOG.md). LMG manual loading, broader bolt/pistol
chambering and scope coverage remain incomplete. Underbarrel manual reloads are
active work; physical grenade reloads, full body/MP/other Frostbite ports are later scope.

## Public release and storage

[Public0.1.15](https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.15) remains unchanged with checkpoint434 binaries and experimental Steam diagnostics. Steam rendering/equipment support remains unverified. See Active work above for the private development installation; public product commit13d7574bd682ae883a935a9adae52a59409b576f.

Use G: and process-local TEMP/TMP/TMPDIR. Player installs may use their selected
drive. Keep40GiB allocated/15GiB free,3GiB normal-build reserve and512MiB raw/run.
Call StorageBudget before builds/captures/sessions; never bypass a failed guard.
Retained storage manifests preserve earlier compression and exact redundant-file removal evidence. Feedback100/101 removed only expanded package replicas after checking every file against retained ZIPs; current/fallback runtime symbols, sources, failures, game captures and user recordings remain. Closed sessions and idle fixtures/runtime files were losslessly compressed with all hashes preserved. Recorded42GiB private-session/package and44GiB normal-build exceptions require fresh admission; default policy is unchanged. Use actual graph `symbols/` paths, not old wrapper root-PDB names.

Run relevant regressions and both normal architectures for native changes;
do not repeat passed suites for docs or unchanged binaries. Read
[architecture](ARCHITECTURE.md), [AGENTS](../AGENTS.md) and
[storage policy](STORAGE-BUDGET.md). Keep temporary restoration separate from
persistent magazine, holster and owed cycle state. No second-eye simulation replay.
