# Fork weapon-loading report: independent audit340

2026-10-11. The report is useful research, not an accepted player feature. The
installed795745 executable has SHA256
`3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258`.
Addresses below were checked against this image and, where stated, its live
reflection metadata. No REST server or unlimited-ammo setting was enabled.

| Claim | Local evidence and disposition |
| --- | --- |
| Missing pistol definitions explain some unavailable campaign weapons | Archive tables confirm M9 definitions in multiplayer common archives and installed async mesh/animation bundles. The SP02/common survey did not find that weapon definition. Useful for an isolated test-range loader. |
| Adding common bundles is a working solution | Not established here. Both-common and base-common-only additions for the actual SP03 saved mission crashed during loading. No VR mod was injected. Do not ship this manifest change. |
| Signature decoding means no integrity check anywhere | The named routine decodes40 hex digits into20 bytes. That alone does not establish absence of downstream validation. |
| LoadBundle can be called as a simple two-argument function | The inspected implementation returns with12 stack bytes consumed and uses an output/refcounted result. Ownership and full ABI must be implemented before a native call. |
| Settings offsets can expose useful developer controls | Most reported Client/Server field offsets match live reflection. Read-only values and type layouts were captured. Settings mutation still needs field/type/lifetime verification. |
| The supplied ServerSettings class layout is correct | Correct live ServerSettings is vtable`0x142dad4`, size`0xcc`. ClientSettings is`0x142daf0`, size`0xfc`. The supplied`0x142db0c`/`0xa4` layout is NetworkSettings. |
| `0x4af980` starts a REST server | It points inside an instruction in this image. `0x4af950` contains route registration; server initialization and network binding remain unverified. |
| The settings setter is a general console executor | Verified as a settings wrapper. Arbitrary command dispatch is not established. |

The native equip flow reads resource-manager global`0x154c444` and invokes its
virtual slot`+0x70` with output storage, bundle path and flag1. Preserve the
returned object's lifetime; do not invoke a guessed fire-and-forget signature.
`US_hg_M9-3.dbx` also identifies the three-round-burst M9-3 (`wcSmg`,20 rounds),
not the ordinary M9. Asset identifiers must determine test coverage.

## Actual loading experiments

Private evidence is under `bc2vr-recovery/`:

1. `fork340-inspection`: installed image disassembly, archive inspection, typed
   resource survey and `settings-verified.json`. Baseline survey read750473216
   bytes with no read errors and found none of the13 selected multiplayer assets.
2. `fork340-mounted-start01`: added targets beneath SP02, then used Continue.
   Gameplay loaded but no selected multiplayer resources appeared. The saved
   mission was subsequently identified as **SP03**. This is a non-applicable
   control, not a failed test of loading those bundles into SP02. Client.Level's
   default SP01 value does not identify the active saved mission.
3. `fork340-mounted-start02`: both common archives targeted to SP03. Continue
   queued; no stable local soldier within40 seconds; process then exited.
4. `fork340-mounted-start03`: only base mp_common targeted to SP03. Same failure.

Both SP03 crash reports record EIP`0x5ac6d8`, ECX0, while accessing`[ecx+0x50]`
in the weapon asset loading path. The40-second exception watcher had already
finished, so its zero-exception count is not a crash-free result. Original crash
reports and profile backups are retained. These results concern this saved SP03
mission, not a fresh SP02 new-game fixture or every possible bundle-load route.

`tools/bc2_test_bundle_mount.py` provides bounded private prepare/apply/restore
fixtures with exact executable, archive and manifest checks. It refuses external
manifest conflicts and restores exact original bytes. Five deterministic tests
pass, including base-only selection and refusing to overwrite an external edit.
Every real experiment restored the original manifest SHA256
`48ea9a14908089144d249debd958b327e04aab25db116b8805ad081cd8ceffaf`.
`depth340-baseline-start` subsequently reached a stable local soldier with the
unmodified manifest, using the same Continue helper and no OS input.

Next loading work should investigate dependency/domain ownership or a separate
new-game test range. Repeating the same failed manifest additions or enabling
unverified debug services will not expand accepted weapon coverage.
