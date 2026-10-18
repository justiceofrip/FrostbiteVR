# Checkpoint423: opening cloud correction proved for one frame

The private one-frame experiment corrected the first mission's cloud stereo
mismatch in actual BC2. Three measured cloud regions changed from271-283px
disparity in the retained422 baseline to0px in423 frame64146. Nearby characters
retained stereo depth. Root reviewed the exact same-frame eye pair.

Additional retained controls from the same423 run strengthen this result:
after the one-frame override had ended, receiver pairs216 and239 again showed
265-288px cloud disparity. No extra game test was needed. Root also reviewed
the later pair239. The measurements are in
`G:/Unity/VRMMO/bc2vr-recovery/overnight423/within-run-cloud-controls.json`.

Both left-eye scopes restored both packet matrices exactly: eight12-byte writes,
no unresolved write, clean native detach, references8 to8. The bounded fault
recorder reported zero exceptions. Four focused suites and288/288 suites on each
architecture passed. Exact source, binaries, logs and image hashes are recorded
in validation.json. Source94be29d4, frozen receiptad4fe93c.

This is a default-off single-frame diagnostic, not an installed gameplay fix.
No headset, actual death or full-campaign acceptance is claimed. Installed
runtime414/launcher0.1.2 and GitHub remain unchanged. The first fresh launch
exited during native loading before VR injection; its cause remains unknown.
The second launch and full diagnostic completed, then BC2 was closed normally.

Next:424 extends the verified correction across frames with bounded ownership,
world replacement handling and exact restoration. Do not reuse an old frame's
packet or weaken stale-owner checks. All output and temporary data stay on G:.
