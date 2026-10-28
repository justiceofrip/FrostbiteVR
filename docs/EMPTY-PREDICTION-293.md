# Candidate293: replay the final predicted shot without duplicating bolt debt

The292 actual-game emulator completed four ordinary M95 cycles and failed on
the fifth, before a held/advancing lease was published. ClientB Restore8295
copied an unchanged native snapshot that restored0/45 to1/45, followed by its
own Update8296 replaying1/45 to0/45. The earlier positive-load prediction rule
explicitly excluded this empty path. The failure was Restore, not a missed
physical stroke. Hooks drained, the game stayed alive, and the original engine
subsequently refilled to5/40. No manual magazine claim is made for that refill.

Trace: `pipeline-runtime-20261005/root-monitor/m95-empty292-01/native-trace.json`.
SHA256: `e58d10f737ce53245df9c13ee19042a05ecd834e328688857b0715aa6955f367`.
Exact fields: `tests/M95EmptyReplay292Fixture.h`.

The shared rollback validator now accepts an explicitly selected empty boundary
6/5-or-6/1 with zero loaded rounds, then exactly one restored cartridge and
unchanged reserve from the original copied snapshot. It only withdraws the
affected client's tentative shot vote. The original cycle, shot number and
deadlines remain; its own subsequent replay and the other branches' own shots
are still mandatory. No hold, release, Ready or ammunition command is created
by Restore. Any already-started hold, open own Update, owner change, malformed
snapshot, different counts or late operation remains rejected.

Connected tests now cover positive and empty counts for both configured pump
and bolt classes and both clients, including repeated rollback/replay and all
existing negative cases. The prior implementation failed these tests at the
same state transition (`test-temp/empty-replay293-before-fix.log`). This extends
one shared prediction rule rather than adding another per-weapon input path.

291's first five-cycle native launch was rejected before gameplay input because
the recorder still required one/two cycles.292 centralized the bounded recording
plan and passed17 focused suites on each architecture before the real-game run.
The separate setup timeout is retained; fresh on-foot evidence permits proceeding
to provisioning without repeating a vehicle-exit input. All failures remain
local evidence. Normal290/headset290 remain frozen until a newer candidate has
native acceptance and full normal builds.293 native acceptance is pending.

## Subsequent293 acceptance

Five ordinary-controller M95 shots and physical bolt cycles passed in the actual
game, including the final round. Ammunition went 5/45 to 0/45; the stock native
magazine reload then produced 5/40. That final refill is an explicit native
fallback, not a manual M95 magazine reload. Each bolt closure reached native
Ready in 38-62 ms, with original guarded clocks, all three branch receipts,
custody return and paired presentation for all four physical motion phases.
There were no presentation fallbacks or evidence rejections. Hooks drained;
the game remained alive. Full callback coverage and headset acceptance are false.

The shared prediction validator now recognizes the engine restoring the final
predicted shot and replaying it. Only that client's tentative shot vote is
withdrawn; the owed physical cycle, original deadlines and ammunition authority
are retained. Exact failed292 callbacks and connected pump/bolt regressions
cover both clients and malformed histories. No native count, state or timer
write was added. Earlier291 recorder rejection and292 native failure remain saved.

Both normal x86 and x64 builds pass all 243 suites. A local normal player build,
source archive and checksum-verified headset package are being saved; private
input drivers are disabled there. Nothing is published or launched in the headset.
Prior290's eight-shot SPAS/pump/empty-shell/shoulder/F2000 sequence and279's
native-menu pointer/checkpoint recovery remain separate preserved evidence.

Campaign playthrough/death recovery, rifle-reload interruption across actor
replacement, native pistol/slide and belt-fed loading, M95 manual magazine
loading, broader family enrollment and visual headset acceptance remain open.
Seventeen compiled magazine prerequisites are not seventeen accepted weapons.

Evidence: `pipeline-runtime-20261005/root-monitor/m95-empty293-01/concurrent-timing-review.json`.
Trace: `8271f6b4b477e511b139f603d59aa97bafb24dc468205a80b8ba20adb9a196c3`.
Source: `3e317aa671ebd5c596f33eeb28291651705d0592f8598080e063901d5f7a6071`.
See [final-shot prediction](EMPTY-PREDICTION-293.md) and
[last-round bolt/fallback ownership](archive/LAST-ROUND-BOLT-291.md).

