# Checkpoint425 — opening cloud correction verified with emulated tracking

The per-frame cloud correction now passes a bounded actual BC2 opening-scene
run. The disabled control reproduced265–287px horizontal cloud disparity.
The active run reduced all three sampled cloud regions to0–1px at two visible
post-restart moments about8seconds apart. Nearby characters retained49–54px
stereo disparity. Root inspected both stereo pairs and the retained phase sheet.
This does not flatten the whole view.

## Implementation and regression

425 corrects424's native completion wrappers: returning inside __try was being
classified as abnormal completion. A shared Windows leaf records successful
return explicitly, runs cleanup, then returns the saved result. The actual
production leaf is covered by a real Windows SEH regression for nested normal
calls, exception propagation, TLS restoration and callback lifetime cleanup.
Frame epochs, owner identity, packet checks and exact restoration remain intact.
See ../../docs/WEATHER-COMPLETION-425.md and
../../docs/WEATHER-TRANSLATION-RUNTIME-424.md.

Four focused suites and288/288 full suites on both x86 and x64 passed. Frozen
source:645bf3e64ffd8098298c7210784c3d847ca7243a4a94baf0e92a819d2e300a18.
Frozen build receipt:c18b3c68fee54e0007a6f6cdcfd31106b043f700dc54a505afda024ddcb4366d.
The existing x86/x64 build trees were reused; only changed text and the four
small runtime binaries were frozen under the private recovery directory.

## Actual-game evidence and limits

- Active425 opening02:240 stereo pairs;64 corrected,176 skipped because one or
  both admitted weather packets were absent.128 scopes applied/restored exactly,
  512 translation writes, zero restore failures or rejected publications.
- The first retained correction belongs to the pre-restart world; selected and
  latest belong to the new world. All12 retained packet snapshots restored
  exactly; only translation words12–14 changed, by the approximately64mm eye gap.
- One Entire Mission restart completed. Hooks, lists, callback contexts and the
  exact current survivor order restored; request references returned8→8.
- Both the successful active run and disabled control recorded zero exceptions.
  Both diagnostic games closed normally after detachment.
- Retained intermediate samples show a black interval between visible boat
  scenes. Aggregate counters cannot establish that every skipped epoch falls
  within that interval. No all240-frames-corrected claim is made.
- The old observer checker incorrectly demanded two independent per-eye matrices
  from adjacent packet fields. Revised checks use the measured kind1/8
  environment-entry→world-setter provenance plus exact paired-image identity.
  They preserve the distinction between root-camera inference and render-eye
  attribution. Actual422/423 captures and13 negative/positive regressions cover
  that correction. Raw old helpers and failed reports are retained.

The first active425 attempt crashed during loading at a null callback with an
amdxx32.dll return address, matching two424 startup faults that happened before
VR attached. Its final runtime/restore report did not survive. The later control
and active passes do not identify or resolve that intermittent fault, and do not
exonerate the mod. No speculative graphics/driver changes were made.

No headset, death, full campaign, general LOD or knife acceptance
is claimed. The distant-hill comparison rectangle overlaps sky and is excluded
from terrain-depth claims. The correction remains explicitly opt-in through
BC2_WEATHER_TRANSLATION=1; the experiment flag must be absent. Installed
runtime414, launcher0.1.2, the pinned420 build entry and GitHub remain unchanged.

## Evidence and resume

All exact paths and hashes are in validation.json. Private work is under:
G:/Unity/VRMMO/bc2vr-recovery/overnight425/.

Use the frozen normal-build receipt to identify the candidate. The fresh-game
startup and finite opening runner are pinned in the private harness receipts;
they use the left monitor and native UI, with no mouse/keyboard/focus commands.
The updated observer checker applies only to future runs; historical results
were not rewritten. Do not treat existing parent feature metadata as new425
acceptance.

The additional moving-view check below completes the short synthetic sweep.
Next useful work is investigation of the intermittent load fault and, when
requested, headset validation of this private candidate. Do not repeat the
passed static or short motion checks without a new question.

Storage stays on G: under the existing40GiB generated limit. All77 raw files from
this checkpoint were compressed losslessly, reclaiming412.43MiB with hashes
unchanged. No425 source, logs, images or user recordings were deleted.

## Moving-view supplement

The same frozen binaries passed a separate eight-pair synthetic head-motion run
with the observer disabled and runtime correction enabled. All eight frames
were corrected: 16 scopes applied and restored, 64 translation writes, zero
skips, rejected publications, restore failures or watched exceptions. No scene
recovery occurred. The exact seven native views and request references 8→8
restored, both helpers exited successfully, and the game closed normally.

All eight delivered view matrices differ. Independent analysis measures a
relative yaw change of −0.175000009 radians and camera-center displacement of
0.245032 meters, consistent with the receiver's short sweep. Raw requested head
poses are not journaled, and synthetic eye offsets remain along tracking X
rather than rotating with yaw. This is useful runtime/moving-camera coverage,
not headset geometry or comfort acceptance.

Root reviewed the eight stereo pairs and full first/last views. Continue had
progressed to the nighttime shore with trees and searchlights. The old boat
cloud rectangles overlap different content there; their failed image-match
thresholds are preserved and are not used to claim either boat-cloud success
or a regression. Opening02 remains the measured boat-cloud comparison.

The first motion setup was rejected before report creation/hooks: the frozen
normal build requires explicit resource-magazine/body-inventory options. The
corrected setup adds those existing options; the receiver still sends no
controller actions. Failed receipts, the original review and its correction are
retained. No native code or binaries changed for the motion supplement.

See motion-validation.json for exact hashes. The original validation.json is
unchanged. The 23 additional raw images were losslessly compressed, saving
41.93 MiB with checksums unchanged. Final generated allocation is 37.271 GiB,
below the existing 40 GiB limit. All diagnostic games are closed; installed
runtime, launcher and GitHub remain unchanged.

## Desktop promotion for the user's test

After the validation above, the user explicitly requested425 on the desktop
launcher. Launcher0.1.2 now selects
G:/Unity/VRMMO/bc2vr-recovery/overnight425/headset-launch425-private/.
It reuses the headset-used416 menu-visible scripts, with frozen425 binaries and
the cloud correction set in BC2's environment before process creation. Observer,
delay and one-frame experiment are absent. Historical audio evidence remains
attributed to416. Installed-worker validation passed against105 pinned files.

The user launched session20261018-130945-946 while deployment was being checked.
Its session reports running/MainMenu/native probe started; the probe, trace and
host hashes match frozen425. This session is left running for the user. No new
headset acceptance or crash fix is claimed. The original validation records
describe pre-promotion state and remain immutable. Exact414 rollback settings
are in overnight425/launcher-promotion/settings-before.json; launcher package,
desktop shortcut, native binaries and GitHub were not rebuilt or replaced.

Metadata note: runtime_binary_root in the new private dependency manifest still
names its416 parent, while both executable script paths and binary pins select
425 correctly. The idle guard blocked a cosmetic metadata correction after
the user launched; no active-session files were changed. Correct and repin that
informational field after the user finishes. It is not consumed by launch code.
