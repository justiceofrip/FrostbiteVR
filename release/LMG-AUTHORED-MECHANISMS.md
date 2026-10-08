# Authored LMG mechanism data

The later [drum-contact follow-up](DRUM-CONTACT-FOLLOWUP.md) resolves the strict
MG36/XM8 LMG extraction rejection below using measured coupled motion and a
separately verified authored insertion direction. The original report below
records the predecessor criteria. Both drum outputs remain private geometry
candidates; neither enables a native LMG reload.

The offline pipeline now resolves **30 exact configuration definitions, 14 native asset names and seven body models**. These are configuration and mesh counts, not 30 different guns. The generated data includes part-relative motion, baseline and peak transforms, candidate rotation axes, and complete same-frame left wrist plus 15 finger transforms where a stable contact interval exists. **No native LMG manual reload is enabled.**

| Body model | Authored geometry/motion identified | Complete hand/part contact candidate | Remaining blocker |
|---|---|---|---|
| PKM | `jntWpn_12` top cover; `jntWpn_11` co-moving cover component; `jntWpn_6` box; measured `jntWpn_4` slider, 111 mm | Box | Native stages/transfer/restore; other contacts |
| M249 | `jntWpn_3` top cover, 2.584 rad; `jntWpn_6` box; separate moving feed parts | None meeting current sampling criteria | Contacts and native stages |
| M60 | `jntWpn_9` cover, 1.589 rad; `jntWpn_6` box; separate feed parts | Cover | Box/feed contacts and native stages |
| MG3 | `jntWpn_3` top cover, 2.584 rad; `jntWpn_6` box; separate feed parts | Box and `jntWpn_19` feed-part candidate | Native stages and contact validation |
| Type 88 (`QJU88` asset) | `jntWpn_3` top cover, 2.584 rad; `jntWpn_6` box; articulated feed parts | None meeting current sampling criteria | Contacts and native stages |
| MG36 | `jntWpn_6` double drum; authored removal path | Drum, diagnostic candidate | Stricter magazine contact check still fails |
| XM8 LMG | `jntWpn_6` double drum; authored removal path | Drum, diagnostic candidate | Stricter magazine contact check still fails |

The cover/box/drum descriptions above come from inspection of CPU-rendered exact mesh parts and their authored motion. Bone names alone did not establish the roles. The machine-generated coverage deliberately leaves semantic roles unset. Native interaction order, authoritative closed/open states, ammunition transfer and cancellation have not been demonstrated by this extraction.

The seven meshes contain single-weight rigid vertices in the inspected LOD; small feed pieces move as separate joints. This is evidence about these assets, not permission to render every belt as one rigid object. The tool records mixed triangles and weighted sections, and refuses to present them as an independent rigid part. Missing reload tracks remain gaps; private previews omit the corresponding geometry rather than inventing animation. Six models have at least one such missing part track.

These are partial clip evaluations, not a claim that every animation track decoded. For example, M249 has 70 decoded tracks and two unsupported `D3I1K16uC16u` tracks (`jntWpn_4` and `jntWpn_5`). The observed geometry bound to `jntWpn_5` is omitted; no identity-pose fallback is substituted. Each model's relevant missing-track set remains in the coverage report.

All 30 authored configurations use the native `rtMagazine` bulk-reload enum. That enum does **not** distinguish a detachable drum from a belt box. Physical feed-family classification remains separate from native ammunition transfer. Body ammunition placement is shared VR policy; it is not extracted from these weapon animations.

## Reproduce locally

Run `inspect_lmg_common.py`, the weapon/mesh inventory tools and `bc2_authored_hand_pose_batch.py` first. Then pass their local outputs to the shared extractor; `--asset` accepts exact native names and can be repeated:

```powershell
python -B tools/bc2_authored_mechanism_geometry.py --game "<installation>" --metadata "<mp-lmg-metadata.json>" --inventory "<weapon-inventory.json>" --hand-poses "<hand-poses.json>" --output "<private-output>" --asset PKM --asset M249 --asset M60 --asset M60_sp --asset M60_sp_s --asset MG3 --asset MG3k --asset MG3_sp --asset MG3_sp_k --asset QJU88 --asset QJU88_sp --asset QJU88_sp_k --asset MG36 --asset "XM8 LMG"
python -B -m unittest discover -s tests -p test_bc2_authored_mechanism_geometry.py
```

`mechanisms.json` retains exact configuration/mesh/animation/skeleton provenance, per-part matrices and contacts. `coverage.json` contains metadata and explicit gaps. Neither file grants a native capability. Generated matrices, raw geometry and previews stay in private local output; the source package contains the extractor, tests and the compact `LMG-MECHANISM-COVERAGE.json` report only.

## Bounds and interpretation

The Granny clip reader now projects only the root `ArtToolInfo` and `Animations` members it consumes. Some assets expose the same large track graph again through root `TrackGroups`; decoding both exceeded the existing 100,000-object budget. Projection avoids that duplicate expansion while retaining the original object, value, array and byte bounds. Unselected root payloads are not claimed to have been validated. Full `Resource.read()` behavior is unchanged.

Motion sampling is bounded to 30 Hz, 15 seconds and 128 rigid parts. Each part is measured relative to its actual parent, so a moving parent is not mistaken for a local hinge. Contact candidates retain wrist, fingers and item from one authored frame and require a stable 200 ms interval. They are diagnostic geometry; spline interpolation and hand-skin contact still need validation. The stricter shared magazine checker uses 60 Hz and requires at least 100 mm of carried separation throughout the interval. Its rejection of MG36/XM8 LMG remains visible; the looser mechanism result does not bypass it.

Next integration is shared feed-mechanism data binding plus native stage/transfer/cancel evidence. No separate per-gun state machine, runtime admission, or headset acceptance is introduced here.
