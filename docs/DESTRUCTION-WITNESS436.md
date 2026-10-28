# Destruction stereo witness (private diagnostic, not a rendering fix)

The reported symptom is destroyed building geometry apparently fixed to one
eye/view direction. The user identified the left eye; another tester's captured
footage does not establish which eye or the same cause. The desktop mirror uses
the left commit. No fracture/visibility/shader root cause has been proved.

This source adds an opt-in observer without changing native draws, camera
overrides, simulation, culling, bridge lifetimes, deadlines or public launchers.
It observes indexed, non-indexed, both instanced, Auto and both indirect draws.
Every original call still executes once with unchanged arguments. Indirect
arguments are identified but their buffer payload is not read in this version.

## Activation and bounded controller protocol

Only the injected native child receives `FVR_BC2_DESTRUCTION_WITNESS=1`.
The normal absent flag adds no capture work or extra draw hooks. The session
must use a fresh output directory. No existing witness or request file may be
present when it starts. Run the normal storage guard before launching and budget
all startup images as well as this observer; do not run it during performance
measurement. The diagnostic itself may add substantial GPU/CPU work.

After arranging each actual native scene, atomically replace
`destruction-request.txt` beside `native-trace.json` with two decimal integers:

```text
1 600
```

This example requests pair 1 and the draw-ordinal window starting at 600.
It is an example, not an established destroyed-wall draw. Ordinals are zero
based within each native eye Draw callback. Window selection is fixed by request
1, must be repeated unchanged for requests 2 and 3, and may be 0..100000. Each
window records at most 256 draws. Skipped prefixes and overflow are counted;
no shader/target deduplication hides distinct instances.

Wait for `destruction-status.json` to show `copied` or `failed` at that sequence
before requesting the next. `copied` means pending copies have terminated, not
that all evidence is complete or that destruction has been identified. Check
each payload's ready flag, copy error and omission counts in the final report.
Sequence 1/2/3 is single use; failed pairs cannot be retried under the same ID.
The intended three observations are:

1. Before destruction, pose A.
2. After an observed native destruction event, pose A.
3. The same destroyed object, controlled yaw/lean pose B.

The controller must separately record map/object/event and input/pose evidence.
This observer cannot identify a building or prove that damage occurred. Do not
claim an emulated destruction reproduction from an arbitrary quiet scene.

## Ownership, output and limits

Pairs require exact world, native request, host request, source slot, native
frame/device epoch, channel/tracking/space identity and both view pointers.
Missing/mismatched second eyes fail instead of borrowing a later frame.
Each eye includes its expected native primary camera bytes, actual shader and
buffer bindings, draw arguments, viewport/target/depth descriptions and the
exact color texture accepted by the bridge. Payloads use staging copies and
nonblocking `Map(DO_NOT_WAIT)` on the immediate-context owner. No Flush, wait,
native-buffer map, pipeline binding or file IO occurs in graphics callbacks.

Per-eye payload quotas are independent: at most 128 constant-buffer copies and
1,398,101 bytes per eye. The total allocated payload region is at most 8 MiB.
VS/PS constant buffers are captured whole or explicitly omitted. GS/HS/DS
shaders are identified but their constants, D3D11.1 constant subranges,
per-instance vertex payloads and indirect argument contents are not captured.
Those gaps prevent claiming complete shader/instance coverage.

Three pairs contain at most six 4-byte/pixel color images, each no larger than
8,418,816 pixels with axes at most 16384. With eight existing startup color
images, raw color is at most 471,453,696 bytes. The additional 8 MiB buffer and
8 MiB metadata ceilings yield 488,230,912 bytes, below the 512 MiB capture limit.
Do not add raw depth or more fixture images without recalculating that budget.
The `.raw` images retain their recorded DXGI RGBA/BGRA format and tight rows.

After callback drain, `destruction-witness/witness.json`, `buffers.bin` and
`pair-N-eye-E.raw` are written once. Buffer offsets refer into `buffers.bin`;
only ready payloads are meaningful. Pending private staging refs are not
released from the worker on single-threaded devices. They are explicitly
reported and retained until game exit. Completed copies are released by Poll
on the owning graphics thread. No native target or bridge retirement lease is
retained by this observer.

## Validation and remaining live step

`SceneDrawWitnessTests` covers exact identities, mismatched/reentrant eyes,
sequential request bounds and aggregate storage limits. The explicit WARP target
`SceneDrawWitnessGpuTests <new-absolute-output-directory>` covers distinct
instances, all draw-kind records, exact per-eye snapshots despite later buffer
updates, unchanged bindings, wrong thread rejection, ordinal selection,
per-eye budget exhaustion, and partial-stop reporting. It is not an automatic
CTest because it creates a real D3D11 device and bounded capture files.

These sources have not been built or run by the implementing agent. Root owns
focused/full builds and the next justified actual-game observation. First use
must establish a bounded ordinal window containing the visible defect. If the
window misses it, report incomplete coverage; do not infer a camera fix from
unrelated draws. Compare actual GPU constants with expected camera data first;
only then investigate instance/history ownership or native visibility lists.
