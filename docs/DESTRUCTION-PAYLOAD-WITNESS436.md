# Selected-draw payload witness — source candidate, October 26, 2026

This is a small supplement to the existing destruction witness, **not a rendering
fix or a claim that the reported wall has been reproduced**. It is isolated in
`tools/debug/` and has no CMake target or NativeProbe caller. Root compiled both
isolated x64 targets and ran their plan/WARP checks successfully on October26.
It must not be included in the Steam/Pimax launcher hotfix or the
frozen R48 runtime. Root owns integration review and any actual-game run.

Validation is retained in `build/x64/scratch/destruction-payload01/validation.json`.
Both tests exited0; no native/game/XR execution occurred. The check includes the
independently reviewed predicated-copy refusal. Actual affected-draw association
and production integration remain outstanding; the rendering bug is not fixed.

## Question it can answer

The retained R45 analysis found that native visibility, Prepare and Draw run for
each eye. Known camera-context restoration exists, but the saved draw witness
does not retain per-instance vertex bytes, buffer-SRV contents, indirect draw
arguments, or predication bindings. Therefore correct VS/PS camera constants
alone cannot distinguish stale geometry payloads from a bad camera consumer.

`D3D11SceneDrawPayloadWitness` can capture the requested native buffer byte ranges
at one selected draw in each eye, before the original draw executes. It records
the complete `SceneWitnessKey`, actual arguments, input-layout identity, topology,
shader identities and predicate binding. The original draw is still the caller's
responsibility and must execute exactly once, unchanged. The helper never binds,
updates, maps or retains a native source buffer. Private staging copies execute
in the original context's command order; later native updates cannot silently
replace the captured bytes.

Sources are IA vertex/index buffers, explicit VS/PS/GS/HS/DS buffer-SRV slots,
and the actual intercepted indirect-argument buffer. Offsets are relative to
the IA binding offset, the SRV's first element, or the indirect argument block.
The helper records actual absolute copy offsets. Structured/raw and enumerated
typed buffer formats have bounded element extents. Textures and unsupported
formats retain resource/view identity plus the complete SRV descriptor and are
explicitly `UnsupportedView`; no texture payload or guessed byte stride is used.
Indirect arguments capture exactly16 or20 bytes according to draw kind.

## Bounds and ownership

- One instance, one pair, at most8 explicit ranges per eye, each4-byte aligned
  and no larger than64KiB. Internal CPU payload is1MiB; Snapshot temporarily adds
  a second1MiB caller-owned copy, so peak CPU payload is2MiB. Private GPU staging
  is at most1MiB. Metadata adds less than128KiB including the snapshot. Saved
  payload is at most1MiB. No color/depth image is duplicated.
- Selection is immutable. Both eyes require the same complete world/request/
  frame/source/tracking identity, verified native owner/request, distinct view
  pointers, and a positive predicted timestamp. Draw argument shape and ordinal
  must match the explicit plan. Pointer equality is not substituted for payload
  equality. Missing draws, mismatched epochs and changed argument shapes remain
  separate failures; no next-frame retry supplies a missing eye.
- All D3D calls run on the first immediate-context owner thread. Another thread
  is rejected before calling the context. The helper holds no native source,
  bridge source, view or retirement references after `Observe` returns.
- `Poll` attempts at most16 staging maps per callback with `DO_NOT_WAIT`. It never
  Flushes, loops waiting on a query, or writes files. Pending is not Ready.
- Bound predication yields `PredicatedNotCopied` before any staging allocation
  or copy. D3D11 predication also controls resource copies, so a successful Map
  would not prove that a copy executed. The helper never changes predication or
  waits for its result. This is an explicit draw-coverage gap, not empty geometry.
  See [Microsoft's SetPredication contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-setpredication).
- After both eyes finish and all copies terminate, `Snapshot` copies a plain CPU
  result for a worker to serialize. `CloseOnGraphicsThread` cancels remaining
  private copies explicitly; cancellation is not successful readback. Close on
  the graphics owner before module shutdown. Foreign-thread destruction without
  Close quarantines at most this one bounded diagnostic instead of releasing
  COM resources on a single-threaded device's wrong owner.

The captured bytes are **not yet verified instance semantics**. An IA slot can
contain per-vertex or per-instance data; its input-layout pointer does not reveal
that distinction. Requested ranges must be derived from the selected draw's
observed stride/offset and independently inspected shader/input layout. Likewise,
a shader-resource binding does not prove that an executed instruction reads it.
Predication identity/value is recorded without reading or waiting for its result.

## Smallest useful next scene

1. Use the campaign save/map where the player saw the wall. Record exact mission,
   destruction event/object, camera orientation, and whether the artifact appears
   before damage. A similar-looking mounted-seat polygon is not the same fixture.
   Preserve an actual affected stereo pair joined to native/host frame identity.
2. Identify the suspect draw family in that pair. Use the existing ordinal-window
   witness and shader/target/argument records. A no-op world with no affected
   fragments does not justify a payload run. Fix the selected ordinal separately
   for each eye, expected arguments, and requested binding ranges before arming.
   If no suspect range is known, the blocker is draw association, not a reason
   to copy every game buffer.
3. For the first actual payload run, take **one pair** at that selection and join
   its exact key+eye+ordinal to the existing camera/constants/color evidence.
   Add at most1MiB saved payload and64KiB saved metadata to the existing capture
   budget; allow2MiB peak CPU payload and at most1MiB additional GPU staging.
   Do not duplicate the six images or rerun a
   three-pair sweep merely because the older wrapper assumes one.
4. Compare the executed draw's indirect count/start/base/first-instance fields,
   IA binding offsets/strides, SRV first/count, and ready bytes between eyes.
   Record uncovered texture/format/range outcomes explicitly. If the selected
   draw vanishes or changes shape, retain that outcome; do not silently capture
   another object at the same ordinal.

Interpretation:

- Missing draw or different indirect counts: inspect that object's actual native
  visibility/job producer before camera changes.
- Equal world-space geometry payloads with wrong/hybrid known view constants:
  trace the exact shared camera-context producer and restore that contract only.
- Differing camera-relative instance bytes despite correct constants: identify
  the layout and writer/frame owner before changing gather scheduling. Differences
  alone can be legitimate eye-dependent data.
- Buffer data correct but texture-SRV payload absent: this probe has located the
  next exact resource; add only its bounded consumed subresource in a successor.

An asymmetric projection comparison remains useful: retained R45 mounted
projections were equal and therefore could not expose a mixed-eye projection.
The frozen generic receiver's existing `--asymmetric` option can help only after
the same affected scene is reached; it does not combine with vehicle controls.

## Prepared checks and integration boundary

`tests/SceneDrawPayloadPlanTests.cpp` covers bounded selection/ranges, SRV-view
limits, overflow, indirect-size contracts, exact draw shape and native identity.
`tests/SceneDrawPayloadGpuTests.cpp` is an explicit WARP-only target: it verifies
distinct per-eye source snapshots despite later updates, IA/SRV binding survival,
bounded SRV extents, changed native request, wrong-thread rejection, missing draw,
shape mismatch, and a known-false occlusion predicate under both polarities. That
case proves zero copied bytes, explicit refusal and unchanged predicate/value.
The GPU harness Flushes its own synthetic device to finish a test; the helper
does not. Neither test launches a game, reads game files, or
creates output files. Neither has been compiled/run by the author.

Root can add explicit temporary build targets for the two tests and link the GPU
test with `tools/debug/D3D11SceneDrawPayloadWitness.cpp` and `d3d11.lib`, then run
once after storage admission. No broad native suite repetition is needed for
these currently unlinked files. Only after review should a diagnostic-only hook
forward the selected draw plus its real indirect-buffer parameter, call EndEye,
Poll and owner-thread Close, and serialize the plain snapshot with status fields.
The normal absent diagnostic flag must add no capture work or extra draw hooks.

### Standalone compile recipe (prepared, not executed)

The existing x64 cache points at MSVC14.51.36231 under Visual Studio18 Community.
This uses the same developer-shell setup as `Build.ps1`, but does not configure
CMake, touch native targets or run305 suites. Run in a separate PowerShell child
so the compiler environment is process-local. Storage admission precedes output
creation. The32MiB reservation covers these two small executables/objects/logs;
no PDB/debug build or image output is requested.

```powershell
$ErrorActionPreference='Stop'
$repo='G:\Unity\VRMMO\FrostbiteVR'
$out=Join-Path $repo 'build/x64/scratch/destruction-payload01'
. (Join-Path $repo 'tools/StorageBudget.ps1')
$null=Assert-FvrStorageBudget -RepositoryRoot $repo -OutputPath $out -Operation Build -PlannedBytes 32MB
if(Test-Path -LiteralPath $out){throw 'Preserve prior diagnostic build output; use a fresh numbered directory.'}
$null=New-Item -ItemType Directory -Path $out
$env:TEMP=$out;$env:TMP=$out;$env:TMPDIR=$out
$vs='C:\Program Files\Microsoft Visual Studio\18\Community'
& (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $vs -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$compiler=Join-Path $vs 'VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64/cl.exe'
& $compiler /nologo /std:c++20 /EHsc /O2 /MD "/I$repo\include" "/Fo$out\SceneDrawPayloadPlanTests.obj" "/Fe$out\SceneDrawPayloadPlanTests.exe" "$repo\tests\SceneDrawPayloadPlanTests.cpp" /link /INCREMENTAL:NO *> (Join-Path $out 'plan-compile.log')
if($LASTEXITCODE){throw 'Plan compile failed; preserve plan-compile.log.'}
& $compiler /nologo /std:c++20 /EHsc /O2 /MD /DNOMINMAX /DWIN32_LEAN_AND_MEAN "/I$repo\include" "/Fo$out\" "/Fe$out\SceneDrawPayloadGpuTests.exe" "$repo\tests\SceneDrawPayloadGpuTests.cpp" "$repo\tools\debug\D3D11SceneDrawPayloadWitness.cpp" /link /INCREMENTAL:NO d3d11.lib kernel32.lib *> (Join-Path $out 'gpu-compile.log')
if($LASTEXITCODE){throw 'GPU compile failed; preserve gpu-compile.log.'}
```

Only then run each prepared executable once, redirecting output to its own log
and checking its exit code. The WARP test bounds both staging readback and its
synthetic predicate query with separate3-second deadlines. This is64-bit
synthetic evidence, not an injected32-bit
native/game test.

For later CMake integration, the header-only plan check can follow
`SceneDrawWitnessTests` near line117. The GPU target can follow the explicit
`SceneDrawWitnessGpuTests` near line278 under `WIN32 && BUILD_TESTING`: two source
files above, include directory `include`, compile definitions `WIN32_LEAN_AND_MEAN`
and `NOMINMAX`, and library `d3d11`. It needs no `FvrCore` object library, DXGI
function imports, `d3dcompiler`, OpenXR, game adapter or native-operation profile.
Keep the GPU test an explicit target, absent from default CTest. Do not link the
new helper into `BC2NativeProbe` until its separate integration review.
