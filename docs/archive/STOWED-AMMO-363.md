# Empty-hand ammunition display — local363

This work adds an availability display while both hands are empty. It does not
create a held weapon, hand claim, magazine reservation or ammunition command.
Only the native-selected, physically holstered item's already verified geometry
and complete ammunition reading may supply this display. Other carried weapons
are not assumed to have readable reserve ammunition.

## Contact and ownership

The active reload consumer now exposes its actual supply contact. Resource
magazines use the configured chest contact; the existing SPAS shell path uses
the belt contact. Stowed and active displays use that same contact and measured
part-to-grasp transform. Drawing a rifle must not relocate its ammunition prop.

A separate display-only cache retains a complete reserve reading through a
deferred or temporarily mismatched cohort read, only until that reading's
original expiry. Rejected data, changed native owner, draw, and lifecycle reset
clear the cache. A new complete zero-reserve reading replaces the previous
positive reading immediately. This cache is not used by reload requests, native
ammunition accounting, transaction acknowledgements or the numeric HUD.

The existing free-hand/hide receipt, exact selected mesh, input generation,
recenter space and native owner checks still apply. Both stereo eyes must retain
the same typed source and first-eye transform. A held/stowed transition starts a
new pair; the two source types cannot substitute for each other midway through
a pair. No second simulation tick is introduced.

## Failed362 evidence retained

Both full362 builds passed245 suites. Its actual-game stationary capture did
not pass display acceptance: only64 of92 stable pairs had ammunition in both
eyes. The magazine also used the belt position while the held-rifle display
used the chest. The moving shoulder capture was insufficient for visual
acceptance because the approaching right arm obscured the ammunition.

These failed runs remain at `stowed362-native01` and
`stowed362-passive-native01`. Exact compiled source is archived with verified
hashes at `inspection362-validation`.362 was not promoted to a player stage.

## Validation status

Both full363 builds pass245 suites (x86:141.08s; x64:81.05s).
Deterministic checks cover rifle and shell placement at two world scales, head
rotation, recenter invalidation, reserve expiry and replacement, missing hide
proof, geometry/owner changes, both-eye retention and held/stowed transitions.
The stationary actual-game run delivers96 pairs,95 paired depth copies and95
compositions, with no render/composition failure.90 of92 stable empty-hand pairs
contain the magazine in both eyes, versus64 of92 in362. Two ammo-only omissions
remain (pairs6 and66); do not describe this as all-frame or flicker-free coverage.
18 saved images have visible prop pixels. The reviewed images place the textured
preview at the chest. One default-stow commit, no cancellation and no magazine
command were observed.

The separate shoulder-draw run delivers240 pairs. All30 stable stowed pairs and
all200 stable drawn pairs contain the same magazine geometry in both eyes. Its
median eye-relative translation differs by0.0047mm across the draw, with identical
rotation. Two inventory commits, no cancellation and no magazine command were
observed. The reviewed images retain the same chest placement. These results
validate display continuity, not a new reload or hand-fit acceptance.

Private evidence: `stowed363-passive-native01/stowed-audit.json` and
`stowed363-native01/stowed-transition-audit.json`. Trace SHA256 values:

- Stationary: `b31ea4790d27bacd19797a9b29329b8b9c003350acec954c1ad7e25e85edd8ec`
- Draw: `d65d717f67be5107c346bd089907039c363b4b675f9bde0afeb9bb5833f087b2`

The last saved player stage remains361.363 is not promoted: its menu check
accepted all three pointer clicks but crashed after checkpoint confirmation,
before a replacement actor was observed. The fault is an execute access violation
at address0 with `amdxx32.dll+0x1066ab` on the stack and a zeroed command-record
prefix. It matches the earlier326 restart and359 startup signature. This does
not identify its producer or establish a driver defect. The observer captured
first- and second-chance dumps. No game or mod process remains.

Exact compiled363 source is archived at `inspection363-validation/source.zip`.
Crash evidence and matching-PDB symbol candidates are in `menu363-recovery01`.
The normal363 binaries and build receipts remain frozen separately. No GitHub
publication or new headset acceptance is claimed.

This display work does not expand native weapon/reload enrollment.357/358's
combined rifle, holster, pump, shell and immediate-support evidence retains its
original scope. Human controller fit, pump feel, complete material conversion,
intermittent startup/restart crashes and remaining campaign mechanics are open.
