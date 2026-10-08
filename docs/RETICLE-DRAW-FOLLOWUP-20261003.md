# Reticle draw follow-up — October 3, 2026

The floating ACOG dot is still unresolved. This change fills specific evidence
gaps in the existing bounded observer; it does not suppress native rendering.
It is separate from sniper magnification and the native ADS lens-filter observer.

The saved 12:52:43 video frame at 7 seconds was reviewed again: the dot is visible
forward of the broadside scope. Six saved native captures contain 901 exact
ACOG reticle-section matches, but none contains a submitted-optic producer or
matching packed-optic constant. Later successful producer records in those runs
describe the SPAS shell instead. Existing evidence cannot authorize a reticle skip.

## Connected evidence

- The immutable palette publication now carries its original selected ACOG
  configuration snapshot and physical equipment generation. The existing native
  actor/request/paired-pack verifier still supplies the actual 48 submitted bytes.
- At an admitted count12/stride68 candidate, the collector asks the native adapter
  for the actual owned eye camera, current owner/equipment and selected metadata.
  This happens at that DrawIndexed call, before the original call. The owner and
  native view/request/frame relationships are checked around the reads.
- Original/current metadata must retain the complete native owner, sole configured
  state, mesh identity, slot, data identity and original leases. A fresh same-owner
  publication cannot extend an expired old palette or selected snapshot.
- Completed geometry and all bound VS constant-buffer captures feed the existing
  aperture math only after exactly one packed-optic match. Missing/incomplete or
  duplicate matches are reported separately. Each eye uses its own camera.
- The report retains the packed optic matrix, eye-world/bind positions, original
  metadata deadlines, equipment epochs, classification and missing-proof bits.
  No game vertex/index/texture bytes are exported.

The configured ACOG mesh is deliberately distinct from submitted skin-instance
ownership. `selectedMeshIdentityVerified` is not set by this patch. A geometry or
matrix match alone does not prove which native instance issued the draw. The
new missing bit `1024` always records that outstanding GPU-instance link, and
`draw_suppressed` remains false. This is not an enabled visibility fix.

| Missing bit | Meaning |
|---:|---|
|1|Exact reticle geometry not established|
|2|Current per-eye draw scope/camera unavailable or changed|
|4|Exact native request/paired palette producer unavailable|
|8|Original palette lease expired or malformed|
|16|Original/current selected ACOG configuration unavailable or changed|
|32|Current actor/equipment/space differs from the source|
|64|Submitted optic bytes unavailable|
|128|One or more bound VS constant buffers were not captured completely|
|256|No unique packed-optic constant match|
|512|Invalid submitted skin matrix|
|1024|Actual selected GPU skin-instance ownership remains unverified|

## Next native check

After review and a complete x86/x64 rebuild, use the existing bounded stream
observer with a selected scoped XM8, hand publication, selected-mesh observation,
and `PassEvidence`; the established hand flag also enables the verified palette
worker observer. Keep the run at most 15 seconds. `OpticFilterObserve` provides
neutral tracked input and selected-mesh observation without the animated hand
fixture; use it with PassEvidence for a monitor evidence run. No launcher or
script invocation has been changed here, and none was run by this agent.

Inspect `reload_draw_evidence.draws[].reticle_aperture_observation` alongside the
worker/producer report. Required results are exact reticle geometry in both eyes,
matching original producer/configuration and a unique complete CB matrix match.
Use the captured slot/byte offsets and exact matrix to establish the native
palette upload/subrange and mesh-instance association; a second timestamp or
another matching matrix is insufficient. If worker association is absent, its
existing typed status must identify the failed owner/view link before proceeding.

Only after that association is established can the native indexed-draw consumer
skip a fully Hidden reticle. Visible, Partial, Invalid and every missing-proof
case must call the native draw exactly once. Partial edge clipping needs separate
fragment/stencil work. Through-scope and broadside views require both-eye HMD
checks, with switching/recenter/reconnect and housing/glass preserved.

## Offline validation

Both architectures pass eight new actual-producer/observation groups, six existing
producer groups and the existing aperture suite. The new cases exercise 20 exact
owner/view/equipment transitions, selected-state changes, original expiry despite
renewal, duplicate/incomplete constants, bad matrices, independent eyes and partial
visibility. New helper/tests compile with `/W4 /WX`; existing native translation
units compile at `/W4` with their pre-existing warnings. No game, UI, injection,
GPU or headset check was performed, and current test binaries were not replaced.

Integration must rebuild all users of the extended internal C++ observation
structures together. There is no IPC/NativeProbeConfig layout change. The normal
continuous session has no new capture path or resolver work: the existing explicit
15-second diagnostic admission remains required.
