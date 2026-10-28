# Restart emulation —327 source,327/328 private scenarios

The326 tracked-menu release fix is retained.327 adds graphics-thread observation
only when the private scene diagnostic is compiled. Normal player builds contain
no automatic controller sequence or graphics observation. No speculative driver
patch or graphics-thread guard was introduced. Both full normal builds pass244
suites: x86 137.72s, x64 39.86s. The pinned327 headset candidate is local only.

Two different actual-game input sequences passed after fresh launches:

| Scenario | Result |
| --- | --- |
|327: restart while holding a removed original rifle magazine | New actor and VR views recovered; original-magazine return, pump, shell and support grip passed |
|328: restart while a shotgun still requires its pump stroke | Old cycle retired; new actor recovered; rifle magazine return, new pump cycle, shell and support grip passed |

Each run used the native VR menu pointer, three clicks with balanced down/up,
no desktop input, and consumed240 distinct stereo pairs. Independent server and
both client observations confirm the native magazine operations. Neither run
reported a presentation fallback during its post-restart magazine interaction.
Support returned133.53ms and128.05ms after shell completion respectively. All
test processes exited normally, their hooks were disabled, and BC2 remained alive.

The328 retirement happened with the new shotgun selected internally. Drawing
the rifle afterwards does **not** prove rifle-first native-cycle retirement.
That specific branch remains unverified in-game; do not change its audit flag.

## Graphics investigation remains open

326 crashed at `amdxx32.dll+0x1066ab` after confirming restart.327/328 passed
without a graphics behavior change, which is consistent with an intermittent
fault. The observed native view draw, immediate-context indexed draw and matching
swapchain Present boundaries used one thread per run, with no captured foreign
overlap or observation overflow. This does not observe every graphics API call
or prove that all engine/driver/resource threading is correct. Neither pass is
a fix for326, death recovery or sustained campaign stability.

Local evidence: `bc2vr-recovery/scene327-native01/recovery-audit.json` and
`bc2vr-recovery/scene328-native01/recovery-audit.json`, their raw traces, graphics
thread observations, matching build receipts and exception-watcher completion.
The failed326 evidence remains preserved separately. No publication or new
headset acceptance is implied.

## Next combined headset acceptance

Use a fresh game and the pinned normal player build, with the headset connected.
Check the previously reported human-input failures together:

1. Rifle support grip; remove and return a magazine; fire empty and manually
   reload. Check visible chest ammunition and immediate support regrip.
2. Draw/stow both shoulders repeatedly. Squeezing an empty hand away from its
   shoulder must not draw the previous weapon. Repeat a ground pickup.
3. Fire SPAS, retain the fore-end, pull rearward then forward, and fire again.
   Insert a shell and immediately support-grip; confirm hands and holsters remain
   usable. Emulation cannot approve physical comfort or visual hand placement.
4. Use the VR pause menu and pointer. Checkpoint restart is last because the
   intermittent graphics crash remains unresolved; save the outcome either way.

Existing309 pistol support/reload/fire and305 pistol-only hip evidence remain
separate. A third native carry slot is not enabled.59 contact variants/33 meshes
and18 magazine-prerequisite configurations are not59 completed reloads. Knife
motion remains a private prototype; chambering, full belt-fed loading, broader
manual-action enrollment and the recorded visual polish remain open.
