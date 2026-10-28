# Executable rejection reported during checkpoint 436

On October 21, a tester screenshot reported that the launcher rejected
`BFBC2Game.exe` from a Steam installation. The exact fingerprint must come from
`launch-check.json`; the screenshot transcription is not a verified executable
identity. No previously verified matching build was identified in the repository
review, and the actual executable has not been inspected.

Installed release 0.1.12 admits this one verified executable fingerprint:
`3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258`.
The rejection establishes a different executable identity. It does not establish
that every Steam installation is incompatible, that this file is corrupt, or
that its native interfaces differ. No new hash was admitted.

`Test-Bc2GameInstallation` writes `data/runtime/launch-check.json` even on this
failure, before OpenXR preflight, game startup, asset preparation or native hook
attachment. **Report feedback > Save diagnostics** already includes that record.
It contains the exact computed SHA-256, file-version string, launcher version,
found/supported flags and error. Feedback also includes package identity. This
is enough to identify the admission failure and recover the complete fingerprint
without transcribing a screenshot.

Current feedback does **not** include PE machine type, timestamp, SizeOfImage,
file length, section layout or native signature/ABI checks. Hash and file-version
equality alone cannot establish hook compatibility. A future read-only local
inspection must verify the applicable original code and ownership relationships
before a new executable is admitted. This investigation made no production
changes, downloaded no executable and preserved rejection before any hook.
## Follow-up

The user clarified that the tester got running by switching to the Origin/EA
version. This is a reported workaround, not acceptance of the original Steam
executable. Its exact fingerprint, PE metadata and native signature compatibility
remain unverified. Track the original Steam-build admission separately; do not
close it as fixed or loosen native hooks based on the alternate installation.

The subsequently supplied `C:/Users/outca/Downloads/launcher-last(2).log`, labeled
"steam version" by the user, is 711 bytes and reports successful asset
preparation plus "BC2 VR is connected; native VR input received." It contains
no error, executable hash, store metadata or post-start gameplay telemetry.
Therefore it demonstrates a reported Steam startup reaching native-input
readiness, but cannot be matched to the rejected screenshot's executable or
used to establish sustained gameplay. No original game bytes were supplied.

### October 26 — exact repeated Steam fingerprint and separate activation report

The newly supplied `launcher-last(4).log` contains the complete rejected hash:
`22e96001382eb847d3ab0a02b59ec7bb79ba47e04146e74c6c49e38465f38c96`.
The user identifies this tester as using Steam. This build is not admitted by
public0.1.12; no matching real executable was found in the inspected local
evidence. Recovery files named SteamLibrary are synthetic path fixtures, not
evidence of a tested Steam executable. The earlier successful log still cannot
be matched to this hash. No compatibility check or allowlist was bypassed.

Keep this separate from the other, explicitly non-Steam Pimax tester. Their old
`launcher-last(3).log` failed managed-cache layout validation. That proves the
resolved launcher/game paths overlapped a forbidden location, not that their
actual game installation was in the wrong place. Their later screenshot reports
"Original BC2 exited during window activation." Published0.1.12 emits this
before its headset host and native adapter start. The ownership lookup also
returns null on identity mismatch or inaccessible process properties, so the
message does not establish an actual crash. The new session's activation and
process evidence are needed; deleting the game is not a diagnostic requirement.

A launcher-only diagnostic correction now distinguishes observed exit from
unverified ownership and includes bounded, redacted activation logs in saved
feedback. It passed44 startup,47 feedback and125 portable-session checks using
synthetic processes/files. Its first portable fixture run exposed an uninitialized
mock variable under StrictMode; that evidence is retained, and only the fixture
initialization changed before the passing run. No native code, executable
admission, game, headset or installed launcher changed in that work. This
does not establish a fix for the tester's startup. Receipt:
`performance436/launcher-launch-triage-r49/validation.json`.

At the user's subsequent publication request, only the three diagnostic changes
were ported onto the exact public0.1.12 payload and released as
[0.1.13](https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.13). The published
package separately passed50 session,44 startup and47 feedback checks plus
fresh-install/update/rollback checks. Native binaries, executable admission and
the asset-cache recipe are byte-identical to0.1.12. The Steam and Pimax root
causes remain unresolved; release notes explicitly preserve those limits.

### October 26 — 0.1.13 live-game verification failure

`launcher-last(5).log` is168 bytes and reports "Unable to verify the original BC2
process after window activation. No VR adapter was attached." The user confirms
BC2 is running only on the monitor. This selects0.1.13's unverified/live-or-
unavailable branch; it does not prove game exit or headset incompatibility.
The host and native adapter have not started on this branch.

An offline diagnosis exercised the exact published Runtime.Core functions,
instead of mocking both functions as the Session coordinator tests do. A missing
initial Path is accepted into the identity and later rejects the same PID/birth
when its Path becomes available. Unavailable StartTime/Path also rejects. This
is a reproduced synthetic condition, not the tester's established root cause.
Two hidden, self-terminating Windows PowerShell32/PowerShell64 processes passed
immediate identity capture, fresh lookup and string-preserving JSON roundtrip.
No BC2, headset or desktop input was used. All19 final checks passed.
The first fixture incorrectly expected a throwing Handle ScriptProperty to
reject; PowerShell evaluates it as null and the published function still returns
the object. That assumption was corrected in the fixture only.

Evidence: `performance436/launcher-identity-r50/result.json`, SHA256
`dc3b02bf79afeeb191a27651039298d8350a14dd23ce53fbc96318d4fab06799`.
No production code, installed package or release changed. The next necessary
tester records are session.json (game identity, activation_process_outcome) and
activation.json (passed/error and verified window samples), or activation.log
if JSON is absent. A passed activation plus a retained live process would narrow
the issue to the fresh .NET lookup. Do not bypass identity or adopt another PID.
