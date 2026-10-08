# Continuous test and pause checkpoint

User explicitly requested no countdown and will Alt+F4 BC2 when finished.
Start-BC2VRSession.ps1 starts the faster desktop-pacing build with150ms async
request lifetime. The XR host observes an exact BC2 process handle, and the
native probe observes an exact host process handle. Closing either side ends
VR. When only the host exits, the live game restores its native state/hooks.
When the game exits, its process owns and releases the injected resources.

No periodic reinjection or artificial session duration. Existing bounded scripts
remain available. The shared XR host accepts a stop callback; zero duration is
rejected without one. Process handles are validated by executable name and
retained, preventing PID reuse confusion. The wire config remains1168 bytes:
bit0x400 interprets its duration union word as hostPid. Other modes keep the old
duration limits. Rendering, deadlines, pose ownership and native job order are
unchanged from the headset-tested pacing candidate.

Native one-second telemetry reports world calls, published/consumed/discarded
pairs and failure counters with UTC timestamps. The worker writes it without
blocking the graphics hook. This supports investigating the user's clarified
middle-of-test interruption, not a supposed progressive degradation.

All23 x86 and22 x64 suites pass. Real-GPU/test-XR presentation, explicit stop
callback, missing-stop-source rejection, wrong process identity and held-handle
child-exit checks pass. PowerShell launchers parse cleanly. See
reports/continuous-session-20260928/checkpoint.json for exact code/binaries,
active process identities and report paths. The test is left running for the
user; development pauses at their request after it ends.
