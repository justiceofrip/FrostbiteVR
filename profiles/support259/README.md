# Authored support contact enrollment —259

The failed258 headset run exposed a capability mismatch: SCAR magazine data was
enrolled while its support contact was still gated by three legacy weapon
profiles or the two compiled full-hand authored profiles. The new support-only
registry supplies the exact static left wrist without changing gun/right-hand
placement, firing, native ammunition, or reload capability.

Six static anchors cover twelve existing magazine configurations: SCAR_sp,
SCAR_sp_s, F2000_sp, three XM8C variants, three MG36 variants and three XM8 LMG
variants. AEK/XM8 retain their existing full-hand authored bindings; the accepted
SPAS support transform is included verbatim from checkpoint202. M416's three
variants are explicitly deferred: their exact left-hand ancestor controls are
varying, so this static-reference pipeline cannot admit them. MG36's right wrist
also varies, but its left reference is constant; support-only enrollment never
retargets that right wrist.

`support-bindings.json` contains derived transforms and source resource, clip,
skeleton and binding hashes, not game asset bytes. Current repository readers
re-evaluated installed resources using the bounded archive loader. Source joins
are retained privately under `bc2vr-recovery/support259`:

- `verified-bindings.json`: SP common, SCAR and F2000.
- `arsmg-verified-bindings.json`: MP common, XM8C and deferred M416.
- `lmg-verified-bindings.json`: MP common, MG36 and XM8 LMG.

For regeneration, use `bc2_authored_grip_bindings.load(...,
reload_references_only=True)` with exact configurations, mesh bindings and the
decoded hand-pose batch. MP common uses the existing `ArchivePool/readers`
bounded loader. Feed explicitly selected names to
`bc2_authored_support_bindings.derive(..., enable_contact=True)` and merge the
disjoint resulting profiles before calling `header`. Missing or varying left
references are errors. Source documents and generated profile digests are pinned
in the JSON. A class label alone is not a runtime binding.

Each emitted pose retains its original selected-mesh snapshot, equipment and
input deadline. Output, contact reads and both Pack callbacks independently
validate it against the current exact configured mesh and owner. A new snapshot
cannot extend the old pose's lifetime. The contact boolean only lets the existing
support-grip policy evaluate the measured target; it cannot acquire a hand claim.

Focused x86/x64 support-consumer tests pass, including actual support policy
acquisition, immutable grasp, changed configurations, owner/space replacement and
original deadline expiry. The baseline SCAR enrollment probe failed as expected.
The generator suite verifies the emitted registry matches its digested rows.
Actual-game259-01 acquired/released SCAR_sp support from the measured contact.
The later combined sequence failed at shell support return, independently of
SCAR support. Other enrolled configurations and headset placement remain
unverified; this is not whole-roster acceptance.
