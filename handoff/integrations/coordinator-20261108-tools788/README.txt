Launcher temporary-file inspection correction.

Actual private79/04 reached campaign world, then the test runner mistook a
disappearing sim/state.json.tmp for a linked path. All owners exited normally;
native retirement and exact profiles/settings restoration were proved.
An ordinary concurrent rename reproduces the same failure without links/mocks.
FileInfo.Attributes can return -1 after disappearance, which contains all flags.
Re-query only that invalid sentinel with File.GetAttributes. Missing file/parent
continues ancestor inspection; real links and other filesystem errors still fail.
20 focused path checks passed, including disappearing parents and replacement
junctions. No native source changed; retain native79's360x86/357x64 checks.
Evidence: WT reports/coordinator-20261108-feedback788/live-temp-path.
This source fix is not installation or LMG/headset acceptance; installed77 remains.
