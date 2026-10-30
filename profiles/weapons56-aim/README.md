# Authored visual aim enrollment

`Aim.generated.h` supplies exact configuration/mesh/rig entries for the existing
model-to-XR-aim conversion. `enrollment.json` retains per-configuration binding,
mesh/LOD/skeleton/animation, visual review and typed firing metadata digests.
No game asset or preview image is included.

The frozen table contains 193 exact configurations, 80 asset names and 44 mesh
families: 30 LMG rows and 163 independently reviewed non-LMG rows. Of the existing
181 firearm origin profiles, 180 join this table; Nambu100 retains its separate
authored-grip axis reference. This is source enrollment, not native acceptance.
The 13 additional rows are 12 exact underbarrel configurations and M2CG_sp with
direct typed firing evidence. Another 32 underbarrel configurations remain
unenrolled because the frozen packet lacks their exact typed firing join.

The independent reviews are `profiles/weapons56-axis-review/review.json` and
`profiles/weapons56-axis-completion/review.json`, each pinned by digest in the
enrollment. The generator rechecks all source files, images,
exact mesh/LOD/configuration/rig joins and typed fields. Five AN94 rifle rows use
the exact reviewed host mesh from the underbarrel pose: their earlier own-pose
assembly failures remain recorded and grant no grip/animation capability.

The LMG review in `reviewed-meshes.json` records direct inspection of seven
private CPU geometry previews, including their omitted triangle counts in the
enrollment provenance. The projection is explicit: barrel toward image right
corresponds to canonical -Z, and sights above the receiver establish +Y up.
Every configuration separately matches the reviewed mesh and LOD content, rig,
skeleton, root convention and typed firing record. The earlier muzzle records
remain origin-only (`direction_claim: none`); they do not authorize visual axes.

`BindAuthoredAim` requires the current verified configuration path/pointer and
unique mesh in the sole configured state. Retain the immutable binding with the
published pose and check `AuthoredAimCurrent` at publication and every consumer,
including both eye packs. Refreshed snapshots cannot renew either source lease.
The profile's rig fingerprint must match the current rig at admission and use.

Use `AuthoredWeaponAimFrame(binding, TrackedAimFrame(effectiveInput, body))` only
when existing accepted/native or authored-grip alignment has not already supplied
orientation. Feed that orientation into the existing `TrackedRig::Update` while
keeping its current authored/native grip source and support-corrected input.
This profile grants no grip, support, reload, ammunition, native fire or muzzle
capability, and does not modify native recoil/spread. Native accuracy, submitted
mesh alignment and headset acceptance remain open.

The bounded metadata generator is `tools/Build-Bc2AuthoredAimProfiles.py`; run
with `--source` pointing to the private `lmg-authored-mechanisms-candidate`,
`--reviews profiles/weapons56-aim/reviewed-meshes.json`,
`--muzzle profiles/weapons54-muzzle/enrollment.json`, and
`--output profiles/weapons56-aim`. Add `--check` to verify frozen output without
rewriting it. The complete table also requires
`--batch-review profiles/weapons56-axis-review/review.json`.
It never reads or extracts game archives.

`Bc2AuthoredAimTests.cpp` exercises exact configuration/rig/lease rejection and
the existing tracked-rig consumer with preserved sockets/native child relations
and support-corrected aim. Deterministic tests are not native/headset acceptance.
