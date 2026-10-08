# Native shell visibility and scoped private-palette presentation

The saved raw rig captures establish that the SPAS shell is rigid and nonhidden
at ordinary idle, but native animation collapses it during held reload and for
part of the return to idle. This would make a valid physical shell disappear if
all native hiding were copied into its owned preview.

## Saved evidence

The bounded offline audit is `reports/native-shell-visibility-20261001.json`.
It saves source hashes and per-sample classifications, not extracted assets.
`hidden` and the matrix basis come from the same immutable pre-IK rig snapshot.
Phase classification requires consistent surrounding reads of all three native
firing copies with matching actor, item, actor generation and space, at gaps of
at most 40ms. This is a phase bracket, not an atomic firing/rig snapshot.

| Trace | Relevant observation |
| --- | --- |
| 203628-362 | All 76 SPAS samples are rigid/nonhidden and bracketed by ordinary idle; zero hidden-leaf poses across 1,579 tracked poses. |
| 200458-557 | All 75 SPAS samples are rigid/nonhidden at idle. The separate exact-section audit establishes actual submitted shell geometry and both-eye packed-palette matching. |
| 183633-048 | Hold window 3,237,093–3,237,437ms. Samples 62 and 63 at 3,237,234/3,237,343ms are hidden while all three surrounding branches remain held 11→12, with 6 loaded/18 reserve. One other held sample is rigid. |
| 192127-784 | Four hidden and two rigid samples occur during confirmed held intervals. Hidden samples 62–64 precede the first transfer; sample 69 occurs while reheld at 7 loaded/20 reserve. |
| 203915-110 | Its sample ring retains only the later period after hold/completion: 96 rigid samples, only four with surviving phase brackets. Its 379 hidden-leaf poses establish an earlier hide interval, but that ring cannot locate it. |

In 183633 and 192127, respectively 13 and 12 bracketed idle-after-transfer
samples are still hidden. The first rigid sample after the final hide occurs
about 1.4 seconds after the last recorded transfer. Firing-state hold therefore
does not freeze shell animation visibility. These observations establish native
bone hiding, not whether an unmodified shell produces visible screen pixels.

## Candidate behavior

The explicit visibility permission is set only inside `BuildReloadPreviewPalette`
after actual selected-mesh, exact SPAS profile/rig, current AmmoObject/GunHold
claims, source identity, and the appropriate genuine reserve/native-cycle lease
have passed. A feature toggle alone cannot reach this permission.

The presentation binding verifies the exact named shell leaf and its retained
raw world/evaluated `1e-4` collapsed basis. It places that shell using the measured
grasp and current physical targets. For palette-plan construction only, a scoped
private `RigSnapshot` copy removes this one shell index from the hidden exclusion
list. Native source bytes, identity, bind transforms, evaluated transforms and
all other hidden exclusions remain original. The generic `BuildRigPosePlan` is
unchanged and continues rejecting every hidden-leaf write.

The result carries the exact permitted index to publication's final hidden-leaf
assertion. A separate `owned_shell_visibility_poses` counter records published
uses. Raw contact still reports the real native hidden status; it never lies
that native animation showed the shell. The gameplay item should not be dropped
solely because that diagnostic flag is false.

The actual Pack path now calls tested `SelectReloadPackedPalette`. Loss of the
claim, focus, coherent shot guard, cycle or original deadline returns the exact
accepted base palette, including its native collapsed shell and opaque padding.
A malformed/missing base returns no override and Pack uses the original native
palette. No native animation time, source transform or ammunition is changed.

## Validation and remaining work

Both architectures pass 13 preview cases, 10 composition cases and five existing
presentation cases (28 total). Regressions cover immutable original rig/bytes,
exact known shell shape, permission off, wrong asset/binding/claim/native owner,
unrelated hidden leaves, original SIMD padding, and actual Pack selection back
to the hidden base after release. The native x86 publication source compiles.

This candidate remains isolated for root review and integration. No native game
actions or global setting changes were performed. Actual visual continuity of
the owned shell still needs an integrated native/headset check; pixel visibility
and manual-reload usability are not claimed from these offline tests.
