# Native pose-copy lifetime correction, checkpoint 419

This candidate closes a concrete source-level gap in model-pose copying. It is
not a confirmed explanation of the checkpoint 416 death/restart distortion.
No camera restoration failure was observed in that failed headset run.

The generic pose path previously validated source bytes at initial admission,
then checked equipment before a later native copy. It could discover a source
change only after passing the private pose to the original packer. Specialized
reload/pump paths already had additional checks; the generic path lacked the
same boundary protection.

Each native copy now checks the captured soldier/weak/weapon addresses, actor
and equipment generations, tracking space, input sequence and original/current
deadlines. It compares the current source pointer, bone count and palette bytes
with the original admitted snapshot, then checks the copied guard metadata
again. Newer input from the same lifetime is accepted. This is not an atomic
native lifetime guarantee or a validation of every skeleton field.

When validation fails, BC2's original packer receives its current native source,
exactly once. No old snapshot is substituted, no native animation data is
written back, and later valid poses can resume normally. The change does not
reset equipment, ammunition, pump debt or interaction ownership. It adds at
most one 64 KiB source read per copy and reuses scratch capacity between the
two copies. A brief stock-pose fallback and the read cost require live review.

The existing rig test target now covers successful copies, source mutation,
actor/equipment/recenter changes, expired evidence, changes during the source
read, and invalid source/count bounds. It checks unchanged native bytes and
current-source fallback. The two native copy indexes do not imply left/right
stereo eyes. The three focused rig/reload-palette/tracked-rig suites passed.
Both full normal x86/x64 builds passed 286/286 C++ suites. Source `3258fd8d`
is frozen in private receipt `0c71f474`. A subsequent bounded normal-game knife
trigger check passed input/cleanup checks and delivered eight image pairs with
visible scenery in both eyes. Its 991 paired rig copies recorded zero source
changes, copy-boundary rejections or pack failures. This validates an ordinary
copy path, not the changed-lifetime fallback or the reported death distortion.
The fixed synthetic hand poses do not establish headset calibration or knife
animation acceptance. Audit `8762d0ca` and root visual review `870b7bbc` retain
those limits separately from the frozen pre-test build receipt.

An additional review found that replacing the main camera within the same
world/request is not covered by the current owner-recovery trigger. The existing
retirement logic assumes a detached request and cannot simply be reused for
that case. This remains a documented investigation; no permanent blanking guard
or speculative same-request recovery was added.

Checkpoint 418's knife input check is retained separately. Its visible pose
deformation is confounded by synthetic hands at the head; it did not demonstrate
a pose-lifetime transition. Do not claim this change fixes that image or verifies
knife damage.
