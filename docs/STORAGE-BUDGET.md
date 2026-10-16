# Development storage budget

Development output has a default **40 GiB combined ceiling**, with **15 GiB
free headroom** left on both the output volume and Windows TEMP/TMP volumes.
Raw image captures have a **512 MiB per-run planned limit**. A build reserves
3 GiB of planned output/scratch; a normal session reserves 640 MiB, including
512 MiB for the native probe's maximum eight 4096x4096 RGBA snapshots. These are
admission estimates, not a filesystem quota or a promise about runtime growth.

`Build.ps1`, `Build-Checkpoint.ps1`, `Start-NativeTrace.ps1` and
`Start-BC2VRSession.ps1` check the budget before creating their outputs or starting
build/game processes. They fail with a useful storage error instead of silently
consuming the last free space. The guard never deletes evidence or changes game
state. Session reports retain their successful admission receipt.

The build scripts check the original temporary-file volumes before switching
process-local `TEMP`/`TMP` to `<build>/scratch`, and restore both variables even
on failure. Linker PDB output is explicitly set to `<build>/symbols`, replacing
stale paths cached by earlier checkpoints. Reuse these same build directories.
Runtime DLLs and diagnostic tools retain their debug symbols. In the normal
`RelWithDebInfo` configuration, executable targets whose names end in `Tests`
omit their duplicate linker PDBs; shared compiler symbols remain. `Debug`
builds keep test PDBs, and `-DFVR_TEST_DEBUG_SYMBOLS=ON` explicitly restores them
in `RelWithDebInfo` when debugging a test. This changes link output only, not
native feature selection or gameplay. The first build relinks existing tests;
it does not require another copy of the build tree.

The measured roots are this repository's `build`, `reports`, `dist`, `release`
and `test-temp`, sibling `bc2vr-recovery`, existing same-parent `bc2vr-preview-*`
workspaces, an existing same-volume legacy `BC2VR`
directory, and the requested output directory. Nested roots count once. Windows
reports both logical bytes and disk bytes through `GetCompressedFileSizeW`; the
40 GiB ceiling uses disk bytes, so retained losslessly compressed evidence does
not consume its former uncompressed budget. Non-Windows accounting conservatively
uses logical bytes. Hard-linked copies can still count more than physical use.
API errors reject admission rather than pretending a file occupies zero bytes.
Directory junctions and file links are rejected; a file with the verified WOF
compression tag is allowed without following a link. New external output roots
must be passed with `-AdditionalRoots`. See Microsoft's
[file-size API](https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-getcompressedfilesizew)
and [attribute-tag query](https://learn.microsoft.com/windows/win32/api/winbase/ns-winbase-file_attribute_tag_info).

Preview workspaces are counted in full, including their small source snapshots;
this deliberately avoids excluding large generated assets inside those siblings.

## Private diagnostic wrappers

Historical frozen wrappers do not automatically inherit launcher changes. Every
new or resumed private build/capture wrapper must call this helper **before any
directory creation, snapshot copying, process launch or capture**:

```powershell
. (Join-Path $repository 'tools/StorageBudget.ps1')
$rawBytes = [long]$width * $height * 4 * 2 * $savedPairs * $imageStreams
$admission = Assert-FvrStorageBudget -RepositoryRoot $repository `
    -OutputPath $runDirectory -Operation Capture `
    -RawCaptureBytes $rawBytes -PlannedBytes ($rawBytes + 64MB)
```

Count both eyes and every native/composed image stream. Include texture row
pitch/padding if the writer retains it. Pass the same bounded frame count to the
capture program; the helper does not intercept later file writes. Save only the
event window needed for the current question. Prefer small reviewed crops and
contact sheets for long-term visual evidence. Do not create another multi-GiB
full-frame sequence for an unchanged passed check.

The standard trace launcher has no raw-frame count or dimension options. Its
estimate uses the normal native probe's eight fixed snapshots and existing
4096x4096 capture bounds. Additional receiver image sequences and private
scene-depth builds are not covered by that estimate and must budget every saved
image through their own wrapper. This is not a new native image-writer limit.

Concurrent writers are not reserved atomically. Serialize large build/capture
jobs, and run a fresh admission immediately before each one. Do not silently
raise the limits or start another process to bypass a refusal. If an exceptional
run needs a larger explicit budget, record the requested limit, estimate and
reason in that run's admission receipt; prune completed generated data first.

## Retention

- Reuse the working x86/x64 build trees. Keep a current runnable checkpoint and
  one prior recovery checkpoint, not a full build tree for every experiment.
- Keep source/patches, configuration/provenance receipts, concise test results,
  small diagnostic logs, and the still-needed failing visual samples.
- After visual review, retain selected lossless samples/crops and the review;
  prune superseded bulk raw image sequences. Record what was pruned so a report
  never implies raw evidence is still available.
- Object files and superseded PDB/build directories are regenerable. Preserve
  symbols for a current unresolved crash before pruning its build.
- Preserve binaries referenced by a current launch/test receipt, even when
  they live in an older `build` directory. The Continue and direct-level launch
  helpers are runtime dependencies; directory age alone does not make them
  disposable. If a retained helper must be rebuilt, keep its original receipt
  and record the new binary hashes in a separate receipt before using it.
- Never remove game installation data, user recordings, save/profile backups,
  source, Git history, or an active run as an automatic retention operation.

Cleanup requires an exact reviewed manifest and bounded path validation. The
budget guard performs no automatic cleanup and does not authorize one.
