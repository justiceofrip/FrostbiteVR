# Current state â€” shared normal integration / public 0.1.15

Updated October 28, 2026. This replaces old next-step instructions; prior evidence remains in
[the archived index](history/2026-10-27-current-through-r55-native.md).

## Active work

R56 is now applied on top of the qualified shared normal below: ten production
files, two existing regression files and two bounded diagnostic readers. Its
exact source closure is `af13daab2acb73ac73273f1a7719515b7befdbb89d3537db2b25eeba2c6f59e8`;
freeze `root-custody-normal-r56-prep/build01/source-freeze.json`, SHA
`ea6e832082030e9ea80ba993858d1496b27a2e89fb40e38fb95ca2fcbd715e32`.
Both normal architectures passed305/305; both focused custody suites and all
five reader regressions also passed. The new six-binary runtime is frozen at
`root-custody-normal-r56-prep/build01/normal-runtime/build-receipt.json`, SHA
`1bbce04a887c1b9c022d0d4aeb6f58e5df0dd1faa28b4c23fb79db8f2e7db002`,
version `unreleased-checkpoint436-custody-frame01`. Its independent audit
`build01/independent-runtime-audit01.json`, SHA
`9ded26011efb62700a5657aa2513a8839b7b514a5733810518dcbdafadda147a`,
joins all14 changes, source closure, both validations, actual18/15 consumer
compilations and all six binary/PDB GUID+age pairs. Normal flags are unchanged.
M95 five-cycle native confirmation passed as detailed below. The generic Nambu
diagnostic build completed and restored normal sources/configuration/binaries
exactly; independent audit `b805e226dd10c6dd5988e26ae6779b54398f93c5f1f65d9c77c3c4f891954305`.
Scene124 stopped before gameplay input or mod attachment because BC2 did not
retain foreground ownership. It closed normally and restored all three profile
files. Nambu acceptance remains pending; this is not a failed native reload test.
The [436 R56 source handoff](../handoff/checkpoint436-r56/README.md) and portable
`profiles/checkpoint436-r56` operation records now preserve this checkpoint for
GitHub review. No new launcher runtime was installed or published.

The R51 ammo-entry, R52 actor-retirement, R54 continuous GunHold and verified
edge-projection fixes are applied to the normal checkout: 13 production files
and four existing regression files, with other gameplay/launcher code preserved.
Normal feature flags and the reviewed native operation table are unchanged.
Private fixture, phase and payload instrumentation is excluded.

The preceding integration is retained under `normal-shared-integration-prep`:
source preparation `7c027183f6926d276da9e160c1ce6cd634f98f137f2e0b3ddde43d83cb2bb765`,
native closure `bba525ecb1db39774f245efcf52d0148edba5ef5141b5bbbb999a9484b37abb5`.
Its six-binary fallback is `build01/normal-runtime/build-receipt.json`,
SHA `fe86157e3905af017a3be52acf69507e862acb59dbfc1a30817ec0160bbb674b`,
version `unreleased-checkpoint436-shared-production01`.
Independent artifact audit `build01/independent-runtime-audit01.json`, SHA
`498b16bf37761822babdd8a3b993957eabec9aa300fdd6f7a0e24453995180e1`,
joins all17 source changes, both305/305 validations, five focused checks and all
six binary/PDB identities. The Probe PDB is retained with its DLL.
This qualifies the build pair, not additional native/headset behavior.

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
Actual death, full jeep ride and all campaign vehicle coverage remain open.

Preserve qualified performance comparisons; no guaranteed90Hz/headset performance
claim follows from diagnostic pairs. HUD flicker, knife hit/audio polish,
cutscene orientation, scope/weapon polish and vehicle reticles remain on
[the backlog](PREALPHA-BACKLOG.md). LMG manual loading, broader bolt/pistol
chambering and scope coverage remain incomplete. Physical grenade/underbarrel
reloads are deferred; full body/MP/other Frostbite ports are later scope.

## Public release and storage

[Public0.1.15](https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.15) remains
unchanged. Its launcher fixes missing image-path identity and exposes the narrow
experimental Steam option. Actual Steam rendering/equipment support remains
unverified. It retains the checkpoint434 native binaries; this private integration
is not installed, packaged or published. Public product commit13d7574bd682ae883a935a9adae52a59409b576f.

Use G: and process-local TEMP/TMP/TMPDIR. Player installs may use their selected
drive. Keep40GiB allocated/15GiB free,3GiB normal-build reserve and512MiB raw/run.
Call StorageBudget before builds/captures/sessions; never bypass a failed guard.
Earlier storage work reclaimed approximately1.217GB, including233,414,656B of
reviewed superseded raw frames and498,606,875B from67 old JSON reports
(`storage-r56-old-install-compression01/result02.json`). The latest batch,
`storage-r56-m95-compression01/run01/compression.json`, losslessly compressed
eight completed files and reclaimed355,228,926B; all content hashes preserved,
no deletion in that batch. Earlier receipts remain.
`storage-shared-normal-review01/root-cleanup-result.json` removes43 reviewed
redundant/obsolete PDBs, reclaiming651,902,976B; exact canonical or current symbols
remain. Historical duplicate restoration locations are in its manifest. Protected
390/407/408, source, logs, screenshots and all user recordings remain preserved.
Older private wrappers copied stale root PDB names; new normal/M95 recipes use
actual graph `symbols/` paths. Preserve correct current runtime symbols.

Run relevant regressions and both normal architectures for native changes;
do not repeat passed suites for docs or unchanged binaries. Read
[architecture](ARCHITECTURE.md), [AGENTS](../AGENTS.md) and
[storage policy](STORAGE-BUDGET.md). Keep temporary restoration separate from
persistent magazine, holster and owed cycle state. No second-eye simulation replay.
