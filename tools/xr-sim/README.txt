BC2 host-only xr-sim + xr-tape driver

Run from a normal Windows token with PowerShell 7.2+. The driver starts only
BC2XrHost.exe --test-scene --controllers and the official Python tape checker.
It does not start BC2, supply IPC/game lifetime arguments, automate desktop input,
or edit Windows runtime registration. The sandbox execution label is irrelevant;
the elevation guard checks the actual effective Windows token.

Invocation:
  pwsh -File tools/Invoke-Bc2XrSimulation.ps1 `
    -OutputDirectory 'G:\Unity\VRMMO\FrostbiteVR\reports\xr-host-NEW'

Use the current checkout's own reports path. Supply these absolute paths on the
command line or in ignored build/toolkit/xr-tools.local.json (CLI wins):
  HostPath, LoaderPath, RuntimeManifest, TapeManifestDirectory,
  TapeLayerName, TapeChecker, Python
TapeLayerName must be XR_APILAYER_XRTAPE_recorder. The manifest directory must
contain exactly one matching api_layer; library_path is resolved relative to
its manifest. Host, loader, runtime and recorder must all be PE x64. The driver
does not discover, download, install, compile or register these prerequisites.

Example local config shape (replace values with installed absolute paths):
  {"HostPath":"G:/.../BC2XrHost.exe","LoaderPath":"G:/.../openxr_loader.dll",
   "RuntimeManifest":"G:/.../xrsim_runtime.json",
   "TapeManifestDirectory":"G:/.../layer-x64",
   "TapeLayerName":"XR_APILAYER_XRTAPE_recorder",
   "TapeChecker":"G:/.../tools/xrtape_check.py","Python":"G:/.../python.exe"}

The default host duration is 12 seconds (8..30); the total child-work watchdog
is 35 seconds (15..60, at least duration+10). Fresh output is mandatory. Storage
admission reserves 128 MiB before capture/session creation, including one 320x240
stereo composition. The official encoder always writes left/right PNG, a 640x240
SBS duplicate and JSON; all four are retained. It has no separate SBS-off switch.
The raw estimate includes the duplicate and JSON overhead. Output growth is
polled against 128 MiB; this watchdog is not an OS filesystem quota. Tape is
capped at 6000 frames; hitting that cap fails, never passes as a complete run.
Temporary paths stay under G: project storage; original caller TEMP/TMP/TMPDIR
and PYTHONDONTWRITEBYTECODE are restored. Only child ProcessStartInfo environments
receive XR_RUNTIME_JSON, XR_API_LAYER_PATH, XR_ENABLE_API_LAYERS, XRSIM_DIR,
XRSIM_HEADSET_CONFIG, XRTAPE_DIR and XRTAPE_MAX_FRAMES. ArgumentList preserves spaces.

The driver creates an explicit 320x240 asymmetric stereo boot profile, verifies
runtime/system/profile/PID identity, then scripts head and grip poses, grip values,
trigger down/up, focus loss/return and one capture. Each command.txt update is an
atomic rename, spaced beyond the documented 16 ms timestamp granularity. The
driver checks exact cmdSeq advancement, lastCmdError and committed frame state,
and retains raw acknowledgement text and snapshots in commands.json.

Negative control: both hands become invalid after the positive capture and remain
invalid until normal host exit. With at most two in-flight frames allowed after
the settled simulator endFrames boundary, the host must count at least 30 later
input frames without raising tracked_hand_frames past that boundary. A later
focused/head-valid host diagnostic row must also report input_hands_tracked=false.
This is an actual host admission check, not a fabricated or edited trace.
This phase runs at 30 Hz so frame waits cross the host's existing 20 ms diagnostic
row threshold; it does not measure performance. WOF-compressed ordinary input
files are admitted through the existing storage helper; symbolic links and
junctions remain refused.

Success also requires advancing host frame/input/hand counters, normal owned
process exit, a new x64 recorder header for that exact PID/executable, complete
trace/footer, xr-sim runtime identity and all required official stereo/contract
checks. The full official checker runs; no failures are filtered. Its optional
depth check may SKIP when no depth layer is submitted. Startup/unfocused empty
submissions may fail never_submits_zero_layers: retain and investigate that
failure; do not trim frames or change it into a pass. Failed runs keep their
trace, state, stdout/stderr, commands and result.json. Cleanup uses only retained
owned Process handles, never process names, process trees or game processes.
result.json reports host_wiring_status, capture_status and tape.conformance_status
separately. An overall failure does not erase a successful host-only wiring check.

Contracts inspected locally (read-only official source):
  xr-sim ca14f5b1c549cf49dca49233250b8f3cdad013f4
    docs/CONTROL.md, USAGE.md, HEADSET_PROFILES.md
    src/xrsim_control.cpp, xrsim_frame.cpp, xrsim_compositor.cpp
  xr-tape 419c30d64d72ff9a41f00701ffe39c3c94bf3ac3
    docs/SCHEMA.md, CHECKS.md, layer/xrtape_layer.cpp, tools/xrtape_check.py
The receipt hashes the actual selected binaries/manifests/checker. Source build
provenance belongs in the coordinator's tool-install receipt; source contract
pins alone do not prove which sources produced an arbitrary supplied binary.

Evidence limit: this exercises the real BC2 XR host and its diagnostic scene
under a simulated OpenXR runtime. Tape records API submission geometry, not
pixels or controller actions. Simulator input receipts plus host counters prove
the stated host plumbing; they do not prove native gameplay reloads, physical
headset comfort, tracking quality, performance or all-weapon acceptance.
