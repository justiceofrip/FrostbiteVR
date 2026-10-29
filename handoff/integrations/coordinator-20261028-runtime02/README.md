# Coordinator runtime02 — October 28–29, 2026

The shared foreground blocker is now separated into two observed conditions.
The tool sandbox uses another Windows desktop; its foreground query returns no
window. The new `game_window.py --desktop-context` records desktop/input ownership
and refuses game operations before process access when that ownership is unproved.
The same diagnostic on the active Default desktop passes. Package preflight remains
independent of desktop/game access. Native source and the batch01 binaries are unchanged.

Window helper tests pass33/33; three targeted package checks pass. The first test
interpreter lacked Pillow and had a package-specific `_pth`; its five skipped image
tests and two package-import failures are retained. The available Pillow interpreter
and canonical full Python resolve those environment issues without source changes.
Independent source review found no actionable issue.

Runtime02 scene01 used a fresh original process and preserved all three profiles.
Direct activation failed; a manual-reply recheck found ChatGPT foreground, and a
bounded45s wait found the browser foreground. The existing launcher activation helper
then passed one burst/four stable observations with both input queues detached and no
keyboard/mouse events. A later window/foreground postcheck refused capture; a
read-only follow-up found another application foreground and unchanged game geometry.
The position-only alignment recheck initially reused a transformed coordinate; that
private-wrapper error was corrected and retained as a failed attempt. No menu image,
gameplay input or native attachment was accepted. Scene01 closed normally and restored
all three original profiles. Launcher0.1.12 and settings remain unchanged.

Windows has one foreground window across monitors. The user is using the other
monitor; continue offline work and request a short uninterrupted test window only
when the diagnostic is ready. Do not repeatedly activate BC2 while the user works.

The user subsequently authorized one five-minute window. Scene02 ran from05:07:55
to05:11UTC: startup's initial identity guard refused, then an independent read
matched the recorded PID/path/birth. The activation helper found BC2 already in
foreground; two bounded captures remained black. Read-only Nambu preflight rejected
`Read out of bounds`, with no weapon samples. No menu click, gameplay input or
diagnostic attachment occurred. The user was not watching, so these captures do
not distinguish game startup from capture failure. A close request was accepted,
exit observed, and all profiles restored; exit code was unavailable, so the receipt
does not qualify a normal-close success. The future helper now retains the process
handle; six offline cases protect explicit-null versus missing/invalid close data.
The agreed window ended; further game interaction needs another agreed window.

Saved SteamVR logs join the prior `XrResult=-2` episode to no connected HMD. Prior
successful sessions used Steam Link/vrlink. VirtualDesktop was already blocked during
those successful sessions, so no driver or OpenXR registration change is justified.
The user confirms the headset is unavailable; no new XR preflight is warranted.

Lossless compression of66 current generated symbol files reclaimed669319168 bytes;
every content hash stayed unchanged. Build admission retains the3GiB reserve and
unchanged40GiB/15GiB limits. The Nambu preparation passed actual batch01 validation.

Local evidence: `reports/coordinator-20261028-runtime02`. Scene recipes bind the
current integration lease, exact game lifetime, fresh names, bounded captures and
profile restoration. The scoped Nambu diagnostic build passed and restored the
normal build's exact configuration and outputs. Its fixture enables three probes
and excludes grenade/knife/contextual opt-ins, so its result cannot qualify CHAT07
combined normal interactions. Build receipt SHA256:
`273b0504dfa002ead9a5ee7b92edc2ff7867875cf84e95857cb255cbec17625c`.

The first binding check rejected the generated capability-header path. An
append-only validator verifies that the one path substitution leaves every
capability byte and source macro unchanged; the original failure is retained.
The reviewed binding passed without another build. The original build recipes
and `magazine/README.txt` remain frozen historical inputs.

Three old documentary pins also required review: archive links and the R56
coverage introduction changed, while selected Nambu admission remained unchanged.
`AdmissionReviewed.py` substitutes only those exact reviewed document pins; the
original plan and all other admission predicates remain intact. Actual offline
metadata/geometry admission passed, with runtime authority still false.

Before a future agreed game session, use `magazine/RunMetadata.ps1`,
take `NativeScene.ps1 -Operation SnapshotModules` after stock setup and before
attachment, and preserve the exact process identity. `Finish -NativeRunDirectory`
requires independent remote-thread exit, disabled/drained hooks, original receiver
exit and the known module set. CLI success alone is insufficient. Cleanup records
normal close separately and can resume profile restoration after an observed exit;
an already absent process is never labeled a successful normal close. Seven
synthetic cleanup cases pass, including refusal of missing/undrained/wrong-lifetime
evidence. The first fixture failure exposed singleton-array unwrapping and is
retained alongside the corrected result in `offline-validation.json`. This is
offline validation, not proof of actual native retirement.
No new installation, native equipment acceptance or headset acceptance is claimed.
