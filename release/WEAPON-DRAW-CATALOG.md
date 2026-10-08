# Weapon draw metadata catalog

This diagnostic matches submitted indexed geometry against metadata derived from
installed mesh sections. It uses the existing bounded draw capture and asynchronous
readback. It does not enable a weapon, change rendering or prove animation ownership.

Generate a catalog from explicitly named installation-relative archives:

```powershell
python tools/bc2_weapon_draw_catalog.py --game-root "D:/Games/BC2" `
  --archive Dist/win32/async/weapon/ru_rgl_aek971-00.fbrb `
  --archive Dist/win32/async/weapon/sp_rgl_xm8_scoped-00.fbrb `
  --output draw-catalog.json --header GeneratedWeaponDrawCatalog.h
```

The output contains section names, hashes, palette bone hashes and archive
provenance. It contains no vertex/index bytes. Unsupported resources remain gaps.
Repeat `--archive` for a batch. Each selection retains the existing archive,
decompression and memory bounds; the compiled catalog is capped at 2,048 sections.
The capture keeps its stricter 2 MiB vertex-buffer cap (the reusable CPU
fingerprinter permits 4 MiB). Selected index data is capped at 400,000 bytes;
record, pending-copy and per-frame limits are unchanged. Larger buffers are
reported as rejected evidence, not interpreted by relaxing the capture limit.

Configure an existing native diagnostic build with
`-DBC2_DRAW_CATALOG_HEADER=<absolute-path-to-GeneratedWeaponDrawCatalog.h>`.
The existing explicit reload draw diagnostic then uses that compiled catalog.
Without this option it retains the previous SPAS shell and ACOG dot filters.
No extra game hook, runtime switch or feature-admission flag is introduced.
Build matching native binaries before running a diagnostic; adding the header
alone does not alter an already running DLL.

Each captured draw reports its catalog match status (`0` not candidate, `1` no
match, `2` unique, `3` ambiguous, `4` malformed), total matches and up to 16 matching
catalog indices. A truncation flag is explicit. The catalog lists resource,
variant and section identity. Equal signatures across resources or variants remain
ambiguous, regardless of the selected-weapon label. The hashes cover ordered
indexed position and local skin bytes, not textures, materials or a GPU skin remap.

The report also retains the existing native request/world/view/frame and producer
cohort. Those fields and a geometry match are separate observations. In particular,
`catalog_match_native_association_verified` remains false. A future calibration
receipt must additionally join the collector's exact equipment/rig/source-palette
sample to the packed palette and the actual draw's skin remap/consumption. The
current named shell/optic producer path does not establish that join for AEK.
Neither nearby timestamps nor a shared skeleton is sufficient.

This step supplies batch mesh identification through the real capture consumer.
It does not establish AEK grip, support-hand, muzzle, magazine contact or holster
geometry. Previously mislabeled AEK pose calibration remains quarantined.
