# 394: wrist contact and ordinary grenade investigation

Implementation is integrated and both full builds pass252 suites. The raised-gun
actual-game sequence passes; the separate grenade attempt stops before emitting
an action and fails acceptance. The latest headset result remains the failed390
run; this is not a campaign acceptance report.

The shared arm solver can now preserve an already admitted wrist beyond the
avatar's native reach. The BC2 adapter opts into that presentation policy for
fresh tracked hands and attachments. Upper and lower segment translations extend
proportionally; hand and finger geometry, weapon geometry, input deadlines and
interaction ownership are unchanged. Reachable poses retain their previous
output. Targets closer than the native minimum reach still report a clamp.

Focused x86/x64 checks cover exact wrist placement, normal-pose equivalence,
controller roll, rigid fingers, frame transformations, scale and fresh pump
contact. These geometric checks cannot establish whether arm extension looks
natural in the headset. The live check must observe a completed mechanical plan,
successful hand posing and both native eye copies, with a measured wrist error
of at most1mm after posing. The393 mechanical-only test remains historical and
does not count as a394 hand-contact pass.

A separate default-off grenade diagnostic supplies one ordinary input edge.
It reads the current native grenade route and all three ammunition copies,
observes stock selection/fire/return, and requires the original physical weapon
and a fresh hand token to return. It neither writes ammunition nor calls a
selection function. Any observed excess ammo loss, lost custody after settled
return, read gap or journal overflow prevents acceptance, even after recovery.

This diagnostic is not a player grenade binding. Its isolated receiver proves
neutral menu/input ownership through a pinned process, arguments and channel;
ordinary XR input does not yet publish that pending-menu fact. Normal builds
leave the diagnostic disabled. Matching injector and DLL builds are required
because the private request structure grows from1200 to1204 bytes.

The393 checkpoint-fade correction is retained. An actual death capture and a
sustained headset campaign playtest remain necessary. Temporary MSAA0 startup
fixtures, if used, must be recorded and restore the user's exact original
settings after the game exits; they are not a confirmed driver-crash fix.

## Actual-game evidence

One fresh SCAR/SPAS session completes three shots and physical pumps, two distinct
manual shell transfers, shoulder return, rifle magazine handling and rifle fire.
All27 runtime checks pass. The rig publishes357 extended-hand pairs (714 native
copies),357 authored hand poses,357 error samples and zero recorded wrist error.
There are no reach/pose/source rejections, generic-hand fallback or mechanical-only
poses. Maximum segment extension is1.46341121. This exceeds native arm length and
still requires visual acceptance; it does not stretch the weapon or fingers.

After detach,31 coherent read-only samples spanning at least2.9s show both
inactive SPAS copies idle at7/22. No post-detach reads are rejected. The earlier
393 observation gap remains in its original evidence rather than being rewritten.

The separate grenade run receives the intended single held-button gesture but
rejects admission53.97ms after pressing. Its original input still has81.89ms of
life. No native edge is sent; rifle22/191 and grenade1/3 remain unchanged in all
three native ammunition branches. Existing logs do not identify the failed
predicate. The next diagnostic will record it without relaxing admission.
Both runs detach cleanly and leave their game alive; no headset or OS input was
used. Each preserves its own complete trace and failed/passed acceptance.

A read-only grenade supplement observes target FireOnDeploy=true and
InflictSelfDamage=false. The original held rifle independently requires
FireOnDeploy=false. These belong to different objects; neither observation is
evidence of actual self-death. Health and damage consumers remain a separate
investigation before relying on a grenade to test death rendering.

The first394 campaign launch crashes before VR injection with the same AMD
graphics-worker stack family as earlier startup failures, despite MSAA0. A second
and third launch reach gameplay. Therefore MSAA0 is not an established crash fix.
Each fixture restores the exact original settings bytes, including after exit.
No global graphics-driver settings were changed.
