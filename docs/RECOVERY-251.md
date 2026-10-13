# Recovery251: mechanism cleanup must preserve the next hand owner

Actual250 completed two rifle magazine cycles and interrupted pump recovery,
then failed shell insertion. The shell acquired its AmmoObject claim after
advancing processing time inside Gather. Pump cleanup replayed an earlier time
through its cancelled normal recognizer, causing the shared hand arbiter to
reject the reversed clock and clear both hands. A new adapter regression
reproduced that exact failure before the fix.

PhysicalWeaponCycle now exposes explicit physical cleanup: release only its
current exact Mechanism claim, clear presentation, and cancel an unfinished
gesture. Inactive cleanup never updates the global arbiter or touches another
consumer's claim. Pending native transactions retain their original deadlines
and require exact completion/rejection. Neutral regrip remains supported. The
BC2 pump refreshes only processing time after preceding shell/resource work;
controller observation timestamps, deadlines and input sequences are unchanged.

Native recovery diagnostics now retain validated guarded-update/step totals
across reload and selection handoffs and count each accepted recovery release
once. The combined checker distinguishes ordinary from recovered releases and
still requires actual completion and ammunition conservation; historical250
remains failed. No native authority derives from these diagnostic totals.

Seven focused C++ suites and all240 full suites pass on both architectures
(full x86 tests129.11s, x64 tests38.85s). Eleven Python status-checker tests pass.
Frozen source: `280f164d3e459989a94c7269a68bcf23b92bba3eda8b83f414371f5b2636a597`.
Actual251 failed before shell loading. The first preflight stopped before
injection on a changing external ownership read; the second passed the same
strict preflight. Both initial magazine cycles completed with2058 paired
presentations and zero fallbacks. Native invocation16150 (trace row11834)
restored clientB from6/6/7 into7/6/8, timer0.498921, conserving7/24 rounds. The
existing common snapshot-progression validator accepts this captured receipt,
but the service restricted that path to M95. It cancelled the SPAS cycle as
Restore failure7 before any holds or physical stroke. The251 cleanup fix was
therefore not reached in the actual game. Hooks drained and the game remained
responsive; scene rows42/42 flushed. Trace SHA256:
`58f02ca2147e0475ce1b3aa037aae093f8ad7bdc0ee3c2e84f2387bb864fa662`.
There is no new headset acceptance. Scene/checkpoint reload crashing is separate
and unresolved. Source remains local; published GitHub checkpoint is245.
