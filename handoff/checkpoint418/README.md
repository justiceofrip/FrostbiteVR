# Checkpoint 418: opening-knife input candidate

**Release and desktop promotion remain blocked.** The last headset run was the
failed 416 session; 417/418 have no headset acceptance. Runtime 414 / launcher
0.1.2 remains installed. No public release was made.

418 retains [417's completed-cycle recovery](../checkpoint417/README.md) and
adds the [verified selected-knife input route](../../docs/archive/SELECTED-KNIFE-INPUT-418.md).
Physical stabbing, enemy damage, death/restart distortion and full campaign
acceptance are unfinished. Invocation 8748's exact failure cause remains unknown.

| Evidence | Result |
| --- | --- |
| Source | `6178748ef4f7decacd26f36035d05662c37df1a6a565a163264621468bc47679` |
| Frozen receipt | `d145f3395acac3b945badeefdf46c80e251ae2a8017f78e49d36d7428b4be607` |
| x86 probe | `23be32da5047a6ce7db3ecf1fe9bfdbef494db10cf2609fb5cbcb4ae2bd28edb` |
| Focused input suites | 3/3 passed |
| Full normal builds | 286/286 passed on x86; 286/286 on x64 |
| Native input smoke | Inconclusive: receiver stopped at 216/240 pairs without its input-start/final receipt |

`knife-smoke01` rejected configuration 10 before controls started; it provides
no gameplay-input evidence. In `knife-smoke02`, the probe exited 0 with hooks,
lists and callbacks restored. The external watcher recorded 10 Melee37-high
samples, both selected-knife copies progressing `2 → 5 → 6 → 2`, and Fire8
remaining neutral. These observations do not replace the incomplete timed-test
result. No enemy-health/damage or atomic native-gather witness was captured.
The receiver supplied both grips at `[0,0,0]`, so its images do not establish
headset hand/knife placement. No further 418 live run was made.

The desired 417 rifle/SPAS regression remains untested in the actual game: the
current continued save contains only the two knife entries. Automated recovery
regressions are retained separately from this selected-knife observation.

[validation.json](validation.json) pins the build, tests, both native attempts,
supplemental observations and unchanged rollback. Lossless compression of 493
generated build files saved 646,861,333 bytes; all file hashes were unchanged
and no files were deleted.

Private receipts and normal binaries:
`G:\Unity\VRMMO\bc2vr-recovery\overnight418`.
The 417 source snapshot and frozen binaries remain separate and intact.
Use `Build-Checkpoint.ps1 -OperationProfile checkpoint418` for current source;
old profile receipts cannot validate the new source closure.
