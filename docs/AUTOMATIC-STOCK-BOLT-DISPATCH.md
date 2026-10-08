# Automatic magazine stock-bolt data: shared disabled descriptor extension

The installed BC2 executable's ordinary AutomaticFire2 route bypasses the stock
bolt states even when a weapon's assets contain nonzero BoltAction values. This
removes one shared extraction deferral without requiring a physical charging
handle. It does not enable a new weapon or grant native callbacks/ammunition.

Read-only verification matched the existing whole-function fingerprints for
Update, Step, Timed, transition notification and transfer against executable SHA
`3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258`.
The reflected FireLogicData BoltAction offset is 0x44; its Time/Delay/ZoomHold/
FireHold members are 0/4/8/9. PrimaryFire's FireLogic begins at 0x0c, consistent
with the existing native reader. Metadata TypeInfo slots initialize at runtime;
this offline proof validates linked field-array offsets, not a live TypeInfo.

The decisive decoded paths are:

- Step return state 1 at 0x6e6359 compares FireLogicType 2 and jumps past hold logic.
- Firing-entry helper 0x6e1fa0 selects next 9 for type 2, 13 for type 3, 5 otherwise.
- Step state 6 at 0x6e6625 enters BoltHold 7 only for type 1.
- Step state 9 at 0x6e66f6 repeats automatic state 9 for type 2; its timing helper
  reads rate data at primaryFire+0x84, not BoltAction.
- Only ordinary states 7/8 read the proved bolt hold flags/delay/time offsets.
- Magazine states 10/11/12 use reload timing; rtMagazine state 12 returns to 1.

The new optional `--automatic-stock-bolt-proof` descriptor CLI input checks an
exact build-specific static proof document. It preserves configuration values,
configuration digests, exact asset paths and all other deferrals. The registry
exporter can materialize these records **disabled**. A zero-bolt family review
cannot enable a stock-bolt candidate; it raises an explicit error. Default output
without the proof remains unchanged. No C++ production state machine changed.

Installed descriptor metadata reclassifies 22 rows under this data-only rule.
The measured AR/SMG/drum batch grows from 12 to 21 exact paths across 7 names:
9A91, AKS74u, UZI, M416, XM8 Compact, MG36 and XM8 LMG. Each x86/x64 probe uses
the real registry/geometry/physical consumer for partial and empty insertions,
ammunition conservation through MOCK native receipts, retirement and denial of
changed path/capacity/bolt values. Missing empty-control evidence still rejects
manual empty admission. Separate production-disabled probes reject all 21 rows.
Forty-three Python regressions cover the original tools and seven new proof cases.

Geometry/main grip/support/native mesh identity and current firing cohorts remain
separate requirements. Virtual callbacks, external/restored state and actual
per-weapon native empty-control/transfer acceptance are unverified. No static
proof permits calling state7/8 or disabling their stock behavior. Rifle charging
handle interaction remains deferred. The earlier accepted scoped-XM8/AEK baseline
and separate belt-fed mechanism evidence are outside this measured input set.

Root integration should add the new helper to the existing source allowlist and
retain it alongside both modified exporters. `source-operations.json` records
four exact tool operations; new helper/tests/document are separate payloads.
All private fixture headers and executable disassembly stay outside source release.
Canonical files, prepared binaries, the frozen 115352 candidate and 2142 source
were not changed. No process was opened, game launched or native input sent.
