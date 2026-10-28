# Reusable body inspection and actual-game material evidence

The next local headset candidate is361, with357 preserved.359 introduced
optional material rendering;360 supplied additional actual-game visual evidence.
361 makes the working inspection input reusable without changing native gameplay
code. Its local launcher exposes the partial material as an explicit preview.

## What360 established

The original neutral look-down capture completed32 pairs but displayed no body
ammo: default stow completed, and current ammo publication requires a held gun.
That was retained as an insufficient visual check. A private right-shoulder draw
selected the SPAS and displayed its shell. It did not validate SCAR materials.
The corrected left-shoulder draw selected the observed SCAR slot.

The corrected run completes240 stereo pairs,239 paired depth copies and238
compositions, with no composition failure, missing geometry, expired/invalid
prop or render failure. One startup eye attachment is rejected; this is not
all-frame coverage. Default stow and subsequent draw produce two inventory
commits and no cancellations.410 fresh per-eye prop observations match the
exact SCAR magazine geometry assigned by the material sidecar.18 saved images
show changed pixels. The reviewed gallery shows the chest magazine with dark
surface detail in both eyes, alongside the original near-arm geometry.

The native trace SHA256 is
`89ebafc756961fdb2a628b2f65e51a643cd07859aa76af054a1db88e420dd55c`.
Private evidence: `materials360-view2-native01/material-visual-review.json`,
`depth-analysis/audit.json`, `depth-analysis/comparison.png`, and the saved
receiver-variant receipt. Native359 and the isolated receiver are separately
pinned. The two earlier insufficient captures remain available.

This is a partial static base-color conversion, not full native shading or
headset acceptance. Native color-space/pass selection, complete materials and
ammo visibility while empty-handed remain open. No material sidecar has been
silently enabled in the player package.

## Reusable receiver input

`BC2NativeIpcProbe` accepts an optional measured tracking-space contact:

```text
--body-props-cache <private-geometry-cache>
--body-props-materials <optional-private-material-cache>
--body-look-down --body-inspection-contact -0.2 -0.12 0.18
--static-pose --async --pairs 240 --capture-poses --request-lifetime 150
```

These are receiver arguments, not a standalone game launcher. Supply its normal
new output directory and connect a matched native producer. Confirm the contact
against the current inventory before the run: the example is the left-shoulder
contact used for SCAR in360, not a universal weapon-to-shoulder assignment.

The schedule waits for normal empty hands, approaches the supplied contact,
squeezes once while stationary there, then returns the right hand in front.
The240 pairs span14seconds. Only the explicit right grip is added; triggers,
sticks, buttons and touch actions stay neutral. No native inventory or ammo call
is made by the receiver. Native equipment ownership determines what is drawn.
Bad coordinates, incompatible action scripts and wrong pair counts reject.

`body-inspection-input.jsonl` records each published controller packet.
`body-inspection-result.json` records the requested contact and input count.
Its single-edge field describes the schedule; inspect the packet log and native
receipts to establish what actually happened. Successful process exit or a
loaded material cache is never sufficient visible-prop evidence.

Both full361 builds pass245 suites (x86:172.60s; x64:75.04s). Seven actual
receiver argument checks reject malformed/incompatible input before creating an
IPC output directory. The shared schedule tests run on both architectures.

The schedule has receiver regressions across shoulder/hip contact positions,
one grip edge at the contact, continuous motion, preserved tracking identity,
rejected fire input and invalid-coordinate nonmutation.361's extracted schedule
retains360's measured path. A separate normal361 actual-game run completes240
pairs,239 depth copies/compositions and18 visibly changed images. Its884 recorded
controller packets contain exactly one grip edge at the contact and no fire,
reload, menu or selection-button input. Default stow and draw commit twice with
no cancellations; no magazine command is submitted. The reviewed gallery again
shows the detailed SCAR chest magazine. Native dependency digest remains
identical to359. Evidence: `materials361-native01/inspection-audit.json`; trace
SHA256 `bc146dba902171040ac9ce931416721be4dfe37c67e4aea255244f0619c5a15b`.

## Reuse across material families

The offline family audit groups75 resolved mesh/material pairs into107 exact
pixel-bytecode families across their passes. The two359 recipes are only partial
reviews. Two of the largest unreviewed families each occur across eight meshes.
Use these groups to review shader conversion once and then bind each section's
measured coordinates/parameters. Equal shader bytes alone do not establish active
pass, color space or identical parameters. This inventory counts graphics work,
not supported weapon reloads. Private report: `material360-family-audit/families.json`.

## Startup investigation

Both359 failures have a zero instruction pointer with
`amdxx32.dll+0x1066ab` on the faulting stack. The Continue helper had restored its
slot/protection with no active callbacks. Its source delegates save-body ownership
to the native Continue/event path; no helper-owned allocation was found being
freed under the queue. This is not a proof that startup code is fault-free, and
the graphics fault does not identify its producer.

Three later launches reached campaign after a30-second menu dwell. The first
startup observer rejected a90-second duration, so its menu baseline was not
observed; a60-second observer was attached after startup. The following two
launches confirmed observer attachment before the menu dwell and each completed
the60-second observation with zero exceptions. All used the same Continue helper.
Timing and observation differences are retained; the sample does not establish
a crash-rate bound or a fix. No driver or graphics-setting workaround was applied.

Owned360/361 game sessions are closed after native hook detachment. The next
local361 package retains357/358 pump and combined gameplay evidence; this material
inspection does not re-establish every mechanic.357 is preserved separately.
The local launcher accepts `-PreviewMaterials`; normal session startup accepts
`-BodyPropMaterials <explicit-cache>` only alongside body inventory. Preview
materials stay optional, private installed assets and are not public release data.
