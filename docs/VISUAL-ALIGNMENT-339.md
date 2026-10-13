# Local339: controller alignment and fixed shotgun parts

The headset337 feedback separates three visual problems. The SCAR's right hand
was seated correctly on the gun but the pair did not follow the real controller
angle. A gray body-ammo magazine appeared prominently beside the rifle. During
the SPAS cycle the folded stock/rear ring separated from the receiver. The user's
fourth shotgun picture is the normal assembly reference.

339 adds the SCAR's exact authored controller/model axes through the existing
grip registry. Its measured static support contact is unchanged. The established
AEK/XM8 entries are included unchanged and their native capability priority is
preserved. This is an authored visual alignment, not new projectile or ammunition
admission. The regression starts the controller at different rotations and checks
that the same final tracked pose produces the same gun/right-hand pose.

The pump presentation now also writes the exact SPAS folded stock, stock end and
rear ring relative to the placed receiver. These are separate from the moving
fore-end. Installed mesh ownership and HandsIKPose supply the three parts;
the authored BoltAction clip keeps them fixed relative to the receiver (less
than0.2mm translation variation in the sampled reference). Their names, parents,
full rig fingerprint, selected pump calibration and original input lifetime gate
the private palette. Hidden parts are not revived. Trigger, bolt, shell, hands,
native ammunition and cycle completion are not controlled by these extra writes.
338's fore-end rest override remains included.

The assembly regression introduces opposing stock/stock-end offsets, changes
weapon orientation and world scale, composes the actual private palette and checks
that fixed pieces stay with the receiver while original native bytes, pump and
unrelated parts remain untouched. This is a candidate correction for the visual
report; the screenshot alone does not prove where the original deformation began.

Magazine investigation: the installed SCAR magazine measures approximately
25.5 x166.0 x83.3mm. Its computed chest placement is near the torso, rather than
an oversized asset. Host body-prop rendering uses its own depth buffer and cannot
occlude against the game's weapon/arms. The user's screenshot is consistent with
that limitation, but does not establish every contributing cause. No magazine
size adjustment or depth fix is included in339. Textured body-prop rendering and
native scene-depth association remain open.

Private references: `bc2vr-recovery/visual339-references` contains measured part
bounds, reviewed SCAR axes, the typed firing direction, static flash reference and
orthographic views. Public headers contain derived transforms/hashes only.

Both full normal builds pass all244 suites (x86:147.33s; x64:48.64s).
One input-driven BC2 run then passes three magazine cycles, shoulder swaps,
one physical pump, one inserted shell, support return and subsequent rifle fire.
The shell-to-support delay was129.416ms. Six native ammunition commands have
matching server and both-client evidence. The final rifle count is28/180 after
two native shots, independently verified on all three state branches.

The initial offline audit expected exactly one rifle shot. The native diagnostic
already allowed one or two shots from its automatic-fire pulse. The audit now
checks the observed shot count and each branch's actual decrements; the initial
failure is retained. No production change or duplicate game run was needed.

The trace records414 resting and119 moving pump pose pairs, with zero pump or
magazine presentation fallbacks and no pump reach/pose/source rejections.
The host consumed240 stereo pairs and compared pixels for40. These counters
confirm execution, not correct appearance: there is no per-frame GPU bone
readback, and12 recording-lock drops prevent a lossless callback-coverage claim.
The mod detached and its hooks drained; BC2 stayed open. Evidence and the initial
audit are in `bc2vr-recovery/visual339-native01`.

Normal player binaries are frozen separately from the private scripted-input
build. Checkpoint339 and its headset stage retain source/binary hashes and these
audits. Restart BC2 before the next player attach because the current process
contains a detached diagnostic DLL. No339 headset acceptance, magazine visual
fix, sustained campaign acceptance or crash fix is claimed.
