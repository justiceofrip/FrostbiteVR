CHAT25/CHAT26 integration-only native recipe; see ../HANDOFF.txt for outcomes.
Scene01/game19 setup ran and stopped at foreground admission before input or
native attachment. FinishScene closed normally and restored all three profiles.
Neutral/Motion were not run. Preserve these failures; a new integration needs
fresh output/process/lease identities and reviewed rebinding of these wrappers.
Lease: chat25-chat26-20261028-integration01-12ad90001f4a4986aee8b08654985b14.
Root alone runs this after normal x86/x64 builds and frozen runtime receipt.
No OpenXR instance or BC2XrHost is used; this is a D3D11 synthetic-input check.
Headset perception, controller ergonomics and full task acceptance remain separate.

Root preparation:
SetupScene.ps1 now provides Start/Capture/Resume for the current user's SP01 save:
 -Operation Start -OutputName <unused gameNN> creates scene01/profile-before,
 preserves GameSettings.bin/ini, temporarily sets1920x1080windowed/MSAA0, and calls
 the observed starter. It makes one foreground request but sends no menu input.
 -Operation Capture -CaptureName menu saves the exact foreground client image.
 Root reviews it before -Operation Resume -CaptureName menu -ReviewedImageSha256
 <actual> -U <reviewed> -V <reviewed>. This permits one ordinary Resume click only.
 A separate uniquely named campaign capture establishes visible loading outcome.
 FinishScene.ps1 checks original helper exits and every retained probe against the
 latest -NativeCleanupReceipt/-NativeCleanupReceiptSha256, closes the game normally,
 and restores all three profiles exactly after no game/helpers remain. Stock-only
 cancellation before probe attachment omits the native cleanup parameters.
 These setup/finish files were statically parsed, not run during preparation.

The lower-level launch/calibration steps remain:
1. Preserve all three current BFBC2 profiles in a fresh G: folder's profile-before/
   and profile-before.json ({profile_path,files:[{name,sha256}]}). Root owns exact
   restoration after normal game exit. This starter never writes those profiles.
2. Establish a reviewed SP01 first-guard checkpoint/menu path; no SP03 seed,
   historical pointer, old approach result or saved direction is admitted as current.
3. StartObservedGame.ps1 -OutputName <unused gameNN> -ProfileBackupReceipt <path>
   -ProfileBackupReceiptSha256 <actual>. It pins the existing StartMenuGameR4,
   verifies backups, sets both observation flags before game launch and restores
   caller environment. Use its actual process-knife-observation.json and hash.
4. Root reaches and verifies the guard with existing reviewed loading/navigation
   tools. This recipe supplies no screen coordinates, teleport or guessed setup.
   Current inventory must be selected slot0 Knife_Equipable/action37/logic0/-1 ammo.

Common RunKnifeObservation.ps1 parameters (all values from current receipts):
 -BuildReceipt <repo>/reports/integration-chat25-chat26-20261028-01/normal-runtime/build-receipt.json
 -BuildReceiptSha256 <actual> -SourceSha256 <native closure> -BuiltSourceCommit <40hex>
 -ProcessReceipt <current process-knife-observation.json> -ProcessReceiptSha256 <actual>
 -Output <repo>/reports/integration-chat25-chat26-20261028-01/native/<unused run>
 -AllowBoundedImages

First run: -Mode Neutral, plus common parameters. It sends neutral input only,
keeps pose/controller journals, disables receiver image files and observes60s.
Native probe retains eight bounded images:512MiB raw ceiling plus128MiB reserve,
640MiB total per native session. Every wrapper storage admission includes the
entire bc2vr-task-worktrees root alongside Hotfix018 and the canonical roots;
StorageBudget runs before creation/launch and refusal is never bypassed.
Inspect native cleanup and current owner before using calibration evidence.

Derive the current axis with the existing G: Python and pinned offline tool:
 G:\Unity\VRMMO\bc2vr-recovery\overnight426\release\stage-0.1.8-01\runtime\python\python.exe -B
 G:\Unity\VRMMO\bc2vr-recovery\overnight426\knife\DeriveDirection426.py
 --native-report <neutral/native-trace.json> --input-journal <neutral/receiver/scene-input.jsonl>
 --weapon <current inventory weapon pointer> --output <native/new-direction.json>
Tool SHA256:10ad4707586a870eb0571d24f9acdb1aed481b0ccdaf2ac6f60c2f12dc62fec1.
Optional DeriveForward.py --direction <new-direction.json> --output <new-forward.json>
uses the same reviewed rotation calculation; it grants no current rendered-aim proof.

Motion: -Mode Motion plus common parameters, -PriorCleanupReceipt <neutral cleanup>,
 -DirectionReceipt <new-direction.json> -DirectionReceiptSha256 <actual>.
Optional -RotationReceipt <new-forward.json> -RotationReceiptSha256 <actual>.
Add -SingleJab for exactly the first12cm motion and withdrawal, then neutral to8s;
without it the existing second motion at5.2s remains. Trace lasts11s in either case.
SingleJab forbids approach. Existing bounded approach options remain available only
for the two-jab fixture, based on fresh measured setup, never arbitrary navigation.
Both observers must be enabled and the export observation hook present. Preserve
sound Starts/Stops, original-scope fields, temporal ambiguity/loss, native acquisition,
attack/damage/health and progression separately. No sound/body-hit inference from labels.

Retain completion.json, retained-probe-cleanup.json, evidence-summary.json and all
raw native/input reports. Receiver failure can coexist with valid native cleanup.
No force termination or automatic retry. Root verifies helper exit and exact native
retirement before normal game close and all-three-profile restoration. No installation
or headset acceptance follows from this emulated diagnostic.
