# Shared mesh layouts312

The shared asset reader previously inferred Half4 positions from every48-byte
vertex stream. Installed M1911 geometry instead stores Float3 positions and moves
the skin bytes four bytes later. Its depth-only stream corroborates the Float3
layout. The old reader rejected the asset before authored grip extraction.

The decoder now validates the complete section under each supported layout and
requires exactly one interpretation. Finite positions, packed fourth components,
normalized weights, authored influence count, palette membership and triangle
bounds remain required. Ambiguous data is rejected. Downstream bounds, magazine,
body geometry and draw fingerprints consume the explicit decoded position format;
none guesses the skin offset from vertex stride.

A fresh read of323 installed weapon archives derives321 mesh entries, up from315.
The six recovered entries cover M1911, Nambu100 variants and two grenade meshes.
Every previously derived skin/index signature remains unchanged. The M136 mesh
still fails weight validation and remains unresolved. These are asset extraction
results, not new native reload capabilities. No game geometry is included in
the source or generated public headers.

The grip batch now has59 measured variants and33 meshes. All58 prior contact
records remain identical. M1911 adds its own constant authored support contact,
primary-hand aiming and explicit pistol-only hip metadata. Its native manual
reload/chambering, body-render enrollment and headset acceptance are not granted
by this data change. MP443_sp remains the pistol with prior native reload evidence.

Validation so far:86 Python geometry/equipment checks,14 authored-support checks
and11 focused C++ interaction checks pass. The Python suite also exposed stale
catalog expectations that omitted the already-enrolled MP443_sp; those assertions
now include its existing ammunition row and16 configured body models.

Local evidence: `bc2vr-recovery/mesh312-research/` contains the exact archive
identities, metadata inventory, before/after comparison and authored references.
`profiles/support312/` contains derived contacts only. The previous311 player
package and its actual-game combined audit are preserved. Both full312 builds pass243 suites each (x86:144.57s, x64:38.05s).
There has been no new312 native or headset test.
