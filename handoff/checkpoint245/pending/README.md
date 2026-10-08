# Unmerged checkpoint245 work

These frozen source payloads are saved for the next agent; they are not built
into checkpoint245. Each original manifest and payload hash is preserved.
Rebase changes deliberately; original bases differ and full-file replacement
can undo later fixes. Local evidence paths in manifests are historical and are
not available from this repository. No compiled/game assets are included.

- **physical-recovery**: shared pump/bolt recovery adapters and convergence.
- **native-recovery**: BC2 native Update guard and recovery channel; depends on
  physical-recovery. Default OFF, no actual-game recovery acceptance. Preserve
  checkpoint245's recording-window initialization when rebasing Runtime.
- **sampled-magazine-rails**: shared curved insertion path with measured M9/MP443
  calibration and tests. No pistol native admission or chambering implied.
- **ordinary-bolt-diagnostic**: records the first physical cancellation, with
  tested adapter fixtures. It diagnoses the M95 Unlock failure; it is not a fix.

The separate MP443 provisioning harness and incomplete pistol composition were
not merged. Do not delay interrupted-pump recovery to enroll more weapons.
