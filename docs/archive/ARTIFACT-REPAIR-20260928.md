# Surface artifact repair - September 28, 2026

The reproduced boat-decal/terrain projection defect is corrected in bounded
campaign captures. The original September 27 headset success remains the last
user-verified headset build. The new correction still needs headset confirmation
that it clears the user's direction-dependent black dots.

## Cause and correction

The main camera binding correction did not include projection-only contexts made
by worker jobs for depth-biased mesh decals and terrain effects. GPU evidence
showed the affected left-eye draws combining LEFT view with RIGHT projection.
This differs from both a correct left camera and an entirely right-eye camera;
our earlier two-camera checker marked these layouts unknown.

Native projection setter 0x5c7f20 allocates a 64-byte projection at context+0x20.
It is reached from the independently verified camera setter, returns its native
allocation in EAX and cleans one stack argument. The mesh depth-bias call returns
to 0xc446e8; the terrain effect call returns to 0xa13e27. Their unique instruction
patterns and relative links are verified on disk and against live code. No hash
or absolute-address-only gate authorizes these writes.

For those two call paths only, the probe records the resulting projection binding
and native frame after the original call. It requires the effect's clip X/Y/W
coefficients to match a tracked eye, and keeps every native clip-Z coefficient.
The existing Prepare/Draw scope now stages the left-eye replacement alongside the
main camera constants. It validates pointer, original bytes, frame completion,
capacity and overlap before any write. Both success and failure restore exact
writes. Capture/restore conflicts reject publication. Native worker scheduling,
visibility and simulation order remain unchanged.

`math/ProjectionOverride.h` contains the portable row-vector matrix operation.
Native signatures, caller identity, allocator/frame ownership and scope timing
remain BC2-specific. This is a reusable lesson for other Frostbite adapters, not
an assertion of binary compatibility with later games.

## Evidence

All paths below are under `reports/`.

- Broken control: `native-trace-20260928-122551-335`, receiver
  `native-ipc-20260928-122550`. Three sampled left-eye passes use the mixed camera
  (1164, 9 and 174 indices). First two-camera matching alone missed the defect.
- First corrected capture: `native-trace-20260928-122937-649`. Same scene and
  shader identities: 306 recognized VS samples now match their intended eye,
  zero wrong-eye/hybrid matches. Thirty layouts remain unknown and are excluded.
  `decal-comparison.png` shows the missing left-eye bullet marks back on the boat.
  Six derived bindings per asymmetric frame; 180 writes in 30 scopes, all restored.
- Final compiled build and opt-in diagnostic:
  `native-trace-20260928-123847-523`, receiver `native-ipc-20260928-123847`.
  Eight received pairs; 306 recognized samples pass, 30 unknown, no overflow or
  unavailable buffer samples. 180 derived writes, zero capture/correction/restore
  failures. Hooks disabled, camera/list/callback/ref/pool restoration passed;
  game alive and responsive with no new crash after 15.2 seconds.
- Moving synthetic poses: `native-trace-20260928-123447-625`, receiver
  `native-ipc-20260928-123447`. 32 pairs, 780 derived writes in 130 scopes,
  zero failures and clean cleanup. Saved images vary with tracking; this is not
  physical-scale or headset-latency acceptance.
- FOV controls: `native-trace-20260928-123633-143`, receiver
  `native-ipc-20260928-123632`. 16 pairs, 218 derived writes, zero failures.
  All 16 sampled eye images have the expected 0/+48/-48-pixel shifts at width480.
- Builds: 23 x86 and 22 x64 suites pass. Three Python checker regressions pass.
  New checks cover depth-preserving projection math, unrelated/invalid matrix
  rejection, mixed-camera negative controls, signature/link/ABI mutations and
  projection setter argument/return preservation. Alignment padding warnings
  remain in native diagnostic structs; no new shadow warning remains.

Exact source/binary hashes and a source snapshot are in
`reports/black-dot-20260928/summary.json` and `verified-source/`.
The September 27 known-good checkpoint is untouched.

Earlier experiments are not acceptance results: Prepare-at-Draw was inconclusive
and reverted. Several wide capture attempts delivered zero/one IPC pairs at the
50ms deadline while their native captures completed; the deadline was not changed.
No game crash was observed in these attempts. Full pass GPU capture is now opt-in
because copying hundreds of buffers can distort the timing under investigation.

## Repeat checks / next headset test

```powershell
& <local-workspace>\Test-NativeStream.ps1 -Seconds 8 -Pairs 8 -Asymmetric -StaticPose -Async -PassEvidence
# Use the report directory printed by the command:
python <local-workspace>\tools\analyze_pass_projection.py <native-trace-directory>
```

The checker distinguishes correct eyes and both mixed-camera hypotheses. It only
claims recognized sampled VS constants; it does not prove all shaders/effects.
The pass inventory samples the first occurrence of each VS/PS/target/depth
combination, is bounded, and does no GPU wait on the game's graphics thread.
Normal `Start-BC2VRTest.ps1` leaves this heavier inventory disabled.

When the headset and SteamVR are connected and campaign is loaded:

```powershell
& <local-workspace>\Start-BC2VRTest.ps1 -Seconds 30
```

Check the boat marks and gentle head turns for the original dots, while verifying
that fusion and flicker remain as good as the September 27 result. No headset
session was started during this repair. Production capabilities remain zero;
other effect paths, temporal history, HUD/viewmodel, controllers and IK retain
their existing validation gaps. BC2 was left running and minimized as requested;
inactive diagnostic DLLs remain resident until the game exits.
