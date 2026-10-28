# Checkpoint 414: lower-chest ammo placement

This follows 413 and changes the shared ammo body contact to (-0.16,-0.55,+0.06)m.
Magazines and shells display at the primary contact in recentered body mode;
the alternate belt pickup remains available. Native operations, audio and
per-weapon grasp calibration are unchanged.

Both full builds passed all 284 suites. Focused validation passed four suites,
including 6,048 physical poses across 18 registered magazine configurations with
independent stereo/pickup expectations. Actual installed mesh bounds passed.

Actual BC2 with emulated controllers delivered the XM8 magazine at the lower
chest in 239 paired views and the SPAS shell in 188 paired views. Measured grab
contacts matched the shared anchor within 0.061 mm; the XM8 mesh stayed
46.1–62.3 cm below the head. Both runs drained their hooks, left the game
responsive and recorded zero compositor errors. One captured rifle view was
also inspected visually. The geometry-only diagnostic uses an untextured prop;
this is not a headset comfort or texture-acceptance claim.

The first collection rejected an unsupported no-image option before injection.
The next drew the SPAS from the right shoulder, so its rifle postflight correctly
failed; a separate shell audit preserves that useful evidence without relabeling
the original result. A fresh run drew the XM8 from the left shoulder and passed
the complete rifle collection and placement audit. No firing or reloading was
requested. See [compact validation record](validation.json).

The desktop launcher now selects checkpoint 414. The exact 413 profile, binaries,
audio cache and source remain available for rollback. No headset acceptance or
complete campaign playthrough is claimed.

See [implementation and evidence](../../docs/archive/CHEST-AMMO-414.md).
Build using `Build-Checkpoint.ps1` and `profiles/checkpoint414`. Keep 413 source,
normal binaries and desktop profile for rollback; launcher app-version rollback
is separate from choosing a local game profile. Private evidence stays on G:
under `bc2vr-recovery/overnight414`; no game meshes or raw captures are included.

Source is published on `checkpoint/414-chest-ammo` and tag `checkpoint-414`.
Remote main's independent build/documentation changes remain separate.
