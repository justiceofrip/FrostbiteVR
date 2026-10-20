# BF4 adapter: current state (2026-10-20)

The only BF4 state document; overwrite it when the state changes. BC2 state stays in
[CURRENT.md](CURRENT.md). Start with [HANDOFF-BF4.md](../HANDOFF-BF4.md); stereo detail in
[BF4-STEREO.md](BF4-STEREO.md). Branch `bf4/vr-adapter` (pushed; no PR or release).

## Evidence levels (same vocabulary as CURRENT.md)

- **Actual-game check**: unattended runs on the real bf4.exe (build 179547, sha256
  `9d9d1a3f...c6c2aa`), AMD RX 9070 XT, Steam -> EA app launch, campaign only, no headset.
- **Emulated**: Bf4EmuHost (WARP or GPU), fake OpenXR runtimes, launcher emulated sessions.
- **Live, simulated headset** (developer, 2026-10-19): the real bf4.exe in a campaign level
  with the real FvrXrHost on `FvrSimOpenXr` (below): proves the game and host side end to
  end (poses reaching the camera, eye crops, labels, keys, mouse, pad, depth), not optics,
  comfort, a real runtime's timing or real controllers.
- **Headset acceptance**: theater mode (game screen on a virtual screen, laser on the
  menus) works in the player's headset (2026-10-19, SteamVR). Head tracking was seen in
  three sessions (09:16-10:19; rotation was inverted and the gun oversized, both fixed in
  0.4.16); everything built since 0.4.17 is unseen in a headset.

## At a glance (installed: 0.4.111-local)

| Feature | State | Evidence |
| --- | --- | --- |
| Theater screen + laser menus | on | headset; live sim: laser Resume and mod menu clicks |
| Head-tracked campaign view (6DoF head) | on | headset (earlier build); live sim: direction, labels 100 % |
| Three frame channels + next pose in the capturing frame | on | live sim: 52.9 pairs/s at 117.7 game fps (2: 44.8), labels 525/525 |
| VR pad reaches BF4 (controller scan: no WMI freeze, stand-in pad) | on | live sim: polled ~150/s, walked 5.2 m, bursts; stand-in polled alone and first |
| Controller aim (mouse look), aim dot, two-handed aim, snap turn | on | live sim: gain learned = measured, 20 deg -> 19.6 deg; ADS and firing; gains kept through a death camera |
| Gun in the hand (BF4's rig drawn at the controller, barrel along the aim) | on (default) | live sim: follows hand position and turn, true size, held through ADS, firing |
| No crosshair in the head-tracked view (HUD split, centre mask) | on | live sim: eye pairs without it, also mid-burst |
| Gun follows aim (turned render), reach limit | option | live sim: gun/laser on the aim, labels 335/335; past the reach gun and dot stop together |
| BF4's pause/death/end menus in a level on the theater screen | on | live sim: pause -> screen -> laser RESUME -> head-tracked, twice; restart checkpoint |
| Recenter: both stick clicks 1 s (or mod menu) | on | live sim: recentered twice, no stick click reached the game |
| Comfort vignette, walk toward body/gun | on / body | live sim (vignette seen while walking) |
| Holsters (keys 1/2/3, G, F) with pulses | on | live sim: 1, 3, grenade, knife work; 2 not seen (no sidearm) |
| Manual reload (magazine gesture; B tap/hold) with markers | on (player setting) | live sim: B, belt, magwell -> BF4 reloads |
| HUD corners (ammo, minimap insets) | on | live sim (eye snapshots) |
| Physical crouch | off | live sim: pulses both ways, view lower |
| 3D from depth (depth buffer) | off | live sim: depth found, parallax direction right |
| 3D: alternate eyes (each frame from one eye, true parallax) | off (option) | live sim: gun and walls at their depths, labels 327/327, 52 pairs/s |
| BF4 split stereo (developer) | unsupported (retired) | live sim: crashes BF4's one-view world renderer |
| Arms from the body (arm IK by palette rewrite: shoulders at the body, hands on the gun) | on (default, "Arms") | live sim: both arms solved every pass, all arm draws agree < 0.5 mm, turned 60 deg no sleeve across the view, reload/ADS; no frame-rate cost (116/116 presents/s) |

## Works on the real game (actual-game checks)

- Session-scoped `dxgi.dll` proxy loads the adapter inside bf4.exe; Present/ResizeBuffers
  hooks adopt the 1920x1080 fullscreen swapchain (format 28). Launcher restores the game
  folder, `user.cfg` block and profile key after every run (11 runs, no leftovers).
- `user.cfg` is executed (PerfOverlay.DrawFps visible in snapshots).
- **Menu pointer**: the same PointerEvent -> Win32PointerSink path the VR laser uses drove
  BF4's real menus (Campaign tab -> Battlefield 4 Campaign -> Resume) into a level.
  `GameMenuPointer` is now Enabled with that evidence.
- nvapi64.dll shim + core NVAPI emulation answer every public query BF4 makes.
- Developer research channel (developer sessions with `research_dump_image` only):
  snapshots, clicks, `dumpimage` (decrypted bf4.exe image for offline RE), background
  vtable scan, read-only `peek`. It never writes game memory (a `poke` command was removed
  after the policy guard blocked it). Added 2026-10-19: `viewcopies <0..3>` (which view
  block transform copies get the head pose; research only), `depthprobe` (installs the depth
  capture, reports it and writes `vr-snapshot-depth.bmp`), `mouse dx dy` (SendInput counts,
  camera yaw/pitch before and 20 frames after), `sendkey <scan>` (a key as the holsters
  press it) and `setting <id> <n>` (a mod menu item as if chosen in the headset). Added
  later: `viewdump <name>` (the next submitted view block, 0x1020 bytes as the game
  submitted it, to `<research>/<name>.bin`; `build/scratch/pytools/viewblock.py` lists its
  transforms, FOVs and differences between dumps) and `aimtrace <name>` (the controller
  aim's recent updates as CSV; Controller aiming), `devicechange [event]` (a WM_DEVICECHANGE to
  the game window, BF4's controller scan). Added 2026-10-20 (observe only: hooked calls
  proceed unchanged): a shader census from swapchain adoption (D3DReflect on each
  CreateVertexShader; `shadernames` writes every constant buffer, variable and resource name
  with counts and each layout, `shaderdump <word>` saves matching bytecode; BF4's shaders keep
  their reflection: 4214 in the prologue, 1189 layouts), `drawprobe <name>` (one frame of draws:
  shader layout, bound constant buffers read back, targets, camera position, bone palettes near
  the camera, whole `viewConstants` on each camera change; shared context hooks
  `Bf4ContextHooks`) and `fpview <0-3>` (the experiment that proved the gun route: the
  first-person pass with the render's projection, then moved 10 cm down / right).
  Fixed 2026-10-19: a session used to replay every command already in `cmd.txt` at start
  (old menu clicks, keys, settings); one replayed click landed on the home screen's
  multiplayer quick-match tile ("Failed to join game session", nothing joined). The channel
  now starts after the highest id in the file (`LastResearchCommandId`, unit-tested); the
  simulated headset likewise ignores one-shot lines already in its control file.
- Live input facts (2026-10-19, prologue): BF4's mouse look turns 0.00140 rad per count on
  both axes, linear (200 and 100 counts alike, exact return), +x turns right, +y looks
  down; the controller aim's first estimate (0.003) is 2.1x that, so it starts at about
  half speed until it learns (it learned 0.001400 live). SendInput scan codes reach the
  game: V switched AUTO -> SINGLE and back, 3 drew the underbarrel launcher, 1 the rifle; 2
  did nothing (no sidearm in that loadout, as far as the HUD shows).

## Simulated headset: live sessions without one (2026-10-19)

- Regression pass (session 51, 0.4.97-local, prologue; `build/scratch/regress.ps1`, not in the
  repository): the pad polled; the left stick walked 4.8 m; the trigger fired 6 rounds; the left
  trigger aimed down the sights with the gun held in the hand; B reloaded (button mode; 21 in
  the magazine); key 3 drew the underbarrel launcher in the hand and key 1 the rifle; 3D
  alternate eyes on and off again (pairs stop with it).

- `tools/simxr/SimOpenXrRuntime.cpp` -> `FvrSimOpenXr.dll` (x64, developer only, never
  packaged or registered): an OpenXR runtime the XR host loads through the developer
  `xr_loader` setting. Unlike the test fixtures it has no script and no end: 90 Hz on the
  QPC clock (the clock the input path expects) until the host, a `stop` line or a
  `simxr-stop` file ends it; Quest-2-like asymmetric eyes, 1440x1584 per eye, 64 mm IPD.
  `%FVR_SIMXR_DIR%\simxr.txt` sets head and hand poses, triggers, grips, sticks and
  buttons (re-read when it changes); `snap` writes the submitted eyes side by side (and
  each quad layer) as BMP; `simxr-ack.txt` reports frames, layers and haptic pulses.
  `tools/simxr/Sim.ps1` edits the file and turns snapshots into PNGs.
- Pose-label check: `sig A|B` records a fingerprint of the left eye at two head poses,
  `flip A B` alternates the head between them every frame, and each new pair is classified
  by its pixels against the pose it is labelled with. Mislabelled pairs (pixels from
  another pose) are exactly what makes the world swim in a headset.
- Live results (0.4.44-local and 0.4.45-local, prologue, two sessions of ~10 and ~6 min):
  - World lane on by default in the level; 2 bridges; 21,468 pairs in the first session,
    0 bridge or resampler failures, 1 cancel. Host: 53,159 frames at exactly 90 Hz, all
    with valid tracking; request-to-pair latency 27.3 ms mean (0.4 % over 50 ms, max
    122 ms); 52 timeouts in ~21.5k requests; oldest re-shown pair 244 ms (limit 250).
  - Rate: 44.8 pairs/s at 118.6 game fps with frame sync 2; about 52 pairs/s with 1.
  - Labels: head flipping 90 degrees every frame, 355/355 pairs at frame sync 2 and
    396/396 at frame sync 1 showed exactly their labelled pose (0 mismatched, 0
    ambiguous). Frame sync 1 is therefore safe at this frame rate; the default stays 2
    until a headset session agrees (it is in the developer menu).
  - Head turned 30 degrees right: the view turns right; the aim dot stays on the gun's
    aim (moves left). The game's own crosshair stays at the frame centre (the head), so
    in VR only the aim dot shows where shots go. The first-person gun follows the head.
  - Controller aim: a 20 degree hand turn put the aim 19.6 degrees right (measured in the
    eye's tangent space); +-60 / +-30 degree sweeps learned 0.001400 / -0.001400 (the
    measured mouse gain); snap turn moved the body exactly 30 degrees; two-handed aim
    engaged and steered by the line between the hands (front hand 12 cm right: 21.6
    degrees right, 3.8 down). Found and fixed live: letting go of the two-handed grip made
    the overshoot guard grow the pitch gain sevenfold (Controller aiming, below); the same
    sequence twice on the fix: gains unchanged, 0 guards.
  - Holsters: left hand over the left shoulder + grip drew the underbarrel launcher (HUD
    1/11 SINGLE), right hand over the right shoulder back to the rifle; hover pulses
    reached the host's haptics.
  - Magazine reload: B tap dropped the magazine, left grip at the belt took one, bringing
    it to the magazine well seated it and BF4 played its own reload (20/105 -> 21/104).
  - HUD corners and the comfort vignette (while walking) appear in both eyes as designed.
  - Physical crouch: with the head 0.4 m low the view sat lower with crouch on than off,
    and standing restored it; in a later session the log showed the pulses both ways
    (`crouch=crouched crouch_toggles=1`, then `standing ... 2`).
  - 3D from depth: see that section (depth found, parallax direction right).
  - BF4's split 3D (third session, 0.4.47-local): reproduced the headset "freeze" in
    0.1 s and caught it as a crash in the one-view world renderer (Stereo, below).
- Play-flow sessions (2026-10-19 evening, Baku; navigation by the VR laser only):
  - 0.4.57-local (session 11): left trigger aims down sights: the holographic sight comes
    up with its dot where the hip crosshair was, BF4 asks for FOV 0.785 -> 0.698 and the
    render stays at 2.10 (no zoom in VR); the learned gain followed BF4's ADS sensitivity
    (0.0014 -> 0.00084 rad/count, no guard), 2 guard steps on leaving ADS, no runaway.
    Right trigger fires (20 -> 15 -> 11 rounds, impacts on the aim dot). Three faults
    found: past the FOV budget the gun stopped short of the aim (fixed: reach limit);
    "Game menu (pause)" left the head-tracked view on a magnified piece of the pause menu
    and controller aim drove its cursor to the edge (fixed: Game menus in a level); the
    both-sticks recenter did nothing and drew the knife (fixed: recenter gesture).
  - 0.4.58-local (session 12): hand 15 deg up with the head level: BF4's crosshair, gun and
    aim dot together about 5 deg up (Quest-like eyes reach little upward); pause through
    the mod menu -> theater screen in 0.28-0.29 s with the whole pause menu, cursor left
    centred, laser click on RESUME (224, 247) -> game resumed, `game menu closed`, aim
    re-engaged (twice); both sticks held 1.3 s with the right one pressed first ->
    `recenter: both sticks held`, view recentered (checked by a second recenter at a fixed
    head pose shifting the view ~30 deg), rifle still in hand. One 10 s stall (no Present
    from about 21:40:40 to 21:40:50) swallowed one laser click on the mod menu; 20 s with
    the menu open later did not repeat it. Cause unknown (Open, below).
  - 0.4.59-local (session 13; 0.4.59 also carried a developer window-shift experiment,
    removed again): HUD corners in a turned render: hand 20 deg either side, the eyes'
    outer edges show dark where the second minimap / ammo panel had been. Pitch gain seeded
    from the learned yaw gain (-0.001680 = 1.2 x 0.001400). Pause -> RESTART CHECKPOINT ->
    its confirmation dialog (OK) by laser on the theater screen -> the checkpoint reloaded
    -> `game menu closed`, aim engaged and the world lane back in the level. The lens
    shift for a wider gun reach does not work (BF4-CAMERA-RE.md).
  - 0.4.62-local (session 15): "Wide reach": hand 15 deg up with the head level, crosshair,
    gun and dot together ~4-5 deg up with On, ~9-10 deg with Wide. Death by a grenade
    (left hip grab, thrown at the feet): BF4's campaign shows no menu on death; the view
    fades and the checkpoint reloads by itself, then the head-tracked view and aiming came
    back. That death broke the aim's mouse gain (fixed above). A second 10 s game stall
    (23:04:12-22, no Present, every thread idle-waiting; the window thread in its normal
    GetMessage) began as a command of mine started after a pause, like the first; the
    watchdog and Present now log who has the foreground when the game loses it or stalls.
  - 0.4.63-0.4.65-local (sessions 16-18, Baku; the same grenade death each time): session
    16 (probe build): the death camera also swings the yaw both ways for seconds, partly
    the way steering pushes; overshoot guards fired between probes and grew the yaw gain
    sixfold. Session 17 (with the anchor cap and `aimtrace`): three guard firings still
    grew the yaw gain 5.8x; the trace showed every one passing the guard's tests, so the
    guard is now armed only after the game's FOV changes (Controller aiming). Session 18
    (0.4.65-local): through the death, the fade and the checkpoint reload the gains stayed
    0.0014 / -0.0014 with 0 guards (`foreign_turns=2`); an ADS round trip learned
    0.000839 in ADS and 0.001400 after it, 0 guards, and the trace shows no overshoot past
    the target after any step. The 10 s pauses on these reloads had the game itself in the
    foreground (loading), not another window.
  - 0.4.66-0.4.75-local (sessions 19-28, prologue/Baku): recoil (Controller aiming); no aim dot
    by default; key 2 still nothing (no sidearm); then the first-person pass research and the
    gun in the hand (that section). Navigation in these sessions used the research `click`
    (the same pointer path) after the laser's offsets varied with the panel's placement.

## Gun in the hand (2026-10-20, built; live sim; the default)

- Finding (live, sessions 20-23; research `shadernames`, `drawprobe`): BF4 draws its
  first-person meshes (arms, weapon, their effects) in a pass of their own inside the G-buffer
  pass, with their own `viewConstants` (every shader binds them at b2; names from the shaders'
  reflection: `viewMatrix`, `projMatrix`, `viewProjMatrix`, `crViewProjMatrix`,
  `projectionKxKyKzKw`, `cameraPos`). That view has the game camera's rotation (not the
  head-tracked render's), a camera position in a first-person space near its origin, and a
  projection at the game's own FOV (cot 2.414: 45 deg at hip; cot 5.14 aiming down sights),
  uploaded through a 1 MB ring buffer (map no-overwrite, bound with an offset). The meshes are
  posed in that camera's space, so the gun is fixed on screen and drawn for 45 deg however wide
  the render is; that is why it sat in front of the face, oversized, whatever the hand did.
- Route (`Bf4ContextHooks` + `Bf4FirstPersonView`, GPU side, nothing in BF4's memory written):
  when BF4 binds b2 (vertex and pixel stage) with a block that is a rigid perspective view not
  at the render's scale whose rotation is exactly (or, with recoil and camera shake, within
  ~4.5 deg of) a game camera rotation the view override recorded for a recent submit, the bind
  proceeds with a constant buffer of ours: the render's projection (true size) and the view
  composed with M = hand x grip-at-rest^-1, with viewProj, the camera-relative viewProj, the
  projection terms and the camera position kept consistent. The whole first-person rig (gun,
  arms, their first-person effects) then sits where the right controller is: grip position,
  aim orientation, so the barrel points where motion aim sends the bullets. The hand is taken
  relative to the head of the same submit (`CameraRequest.handInHead`, recorded with that
  submit's rotations, so a frame never gets another frame's hand).
- Grip at rest (`GunGripAtRest`, live): the rifle's root bone sits at (0.066, -0.022, -0.254)
  in BF4's first-person camera space, bore along (-0.029, 0.019, -0.999); the hand holds the
  pistol grip ~8 cm below and ~6.5 cm behind the root.
- Aiming down sights: BF4 animates the gun ~10 cm toward the eye line; the receiver (the
  largest rigid mesh of the pass, read from its b0 `worldMatrix`) is tracked each pass, its hip
  pose held while the game's FOV is zoomed, and the rig moved back by the difference (live:
  9.5 cm, the gun stays in the hand). Hip bob, sway and recoil stay visible.
- The game fires from the eye while the barrel is drawn at the hand: the aim goes from the eye
  to the barrel's line 20 m out (`kGunConvergeMeters`, `MotionAim::ConvergedAim`), so shots
  cross the barrel's line there instead of running parallel a hand's distance (~30 cm) away
  (off by 20 cm at 5 m, ~0.5 m at 50 m: under a degree either way). With the left grip
  steadying the gun the barrel is drawn along the line between the hands, as the aim goes
  (`MotionAimOutput::barrel`; emulated test: the right controller 11 deg off, the barrel on
  the line). Unit test: the convergence geometry and the controller converging on it.
- With the gun in the hand the render is not turned (full sharpness, no reach limit: the aim
  follows the hand anywhere) and +0x00 keeps the game's value. Mod menu "Gun: BF4's place /
  On the aim / Aim, wide / In the hand" (default In the hand; id `gun_aim`, values 0-3). Log: `gun in the hand: on/off`,
  `first_person_view ... hand=exact/near/missing by_game= passes= weapon=(...) zoom_held= shift=`
  every 600 frames (`missing` also counts perspective spot-light shadow views, left alone).
- Live (simulated headset, sessions 24-28): every first-person bind of the pass matched and
  replaced (diff 0.0000 by the game rotation; the render rotation never matches); hand moved
  left/up and turned 25 deg: the rig followed; hip and ADS frames show the gun at the hand;
  full-auto bursts: three eye pairs mid-burst with the gun at the hand (before the shake
  tolerance, frames drawn while firing showed BF4's own gun).
- Muzzle effects (live, session 38, the gun held 40 cm low and 25 cm right): the first-person
  muzzle flash is drawn at the hand-held barrel's tip (it belongs to the first-person pass and
  moves with it). Not yet looked at: tracers and smoke BF4 may spawn as world particles at its
  own muzzle. One frame mid-burst showed BF4's crosshair: fire spreads its arms out of the old
  centre box (fixed: the plus-shaped mask, No crosshair below).
- Firing effects (live, session 63, the gun held low right, then low left): the muzzle flash and
  the rifle's laser beam start at the hand-held barrel, and the laser dot sits on the bullet
  impacts. Ejected casings do not follow the gun: they fly out right of centre from BF4's own
  gun position whichever side the gun is held. They are world-space draws of their own, only
  while firing: in the main G-buffer pass one 240-index instanced draw per casing (`dii n=240
  i=1`, a vertex shader that binds only `viewConstants`, 9 inputs, 49 instructions) and the
  same in the shadow cascades. Moving them with the hand would mean rebinding the main view
  for exactly those draws (needs a stable identity for that shader, e.g. a bytecode hash);
  not built. No tracers were seen in first person (prologue rifle).
- Quick knife and grenade (live, session 62, 0.4.105-local, keys F and G): both play and the
  view stays intact; the grenade count went 3 -> 2 -> 1 across the in-hand and BF4's-place
  runs, and every first-person bind stayed matched. The whole rig stays on the controller
  through these animations, so the stock throw and slash arms appear where the hand is rather
  than where BF4 puts them in front of the eye (the throw's arms spread across the lower view
  at the pull). Acceptable for short actions; anchoring the rig to the head during throws
  needs a reliable "not holding the gun" signal first.
- Gaps: the ejected casings (above); the grip at rest is the
  prologue rifle's (other weapons may sit a few cm off in the hand); the left hand stays on the
  gun, not on the left controller (the arms reach from the body: next section); shots cross the barrel's line 20 m out rather than
  running along it (under a degree either way); not seen in a headset.
- Evidence: WARP checks in Bf4Emulated (the first-person block gets the render's projection
  and a moved view, the world's passes through, pixel stage too, off passes everything through;
  hand mode by the game rotation only; receiver tracking picks the largest rigid mesh; weapon
  part test), emulated session (the request carries the hand relative to the head, no turn, no
  reach limit, no dot), live as above.

## Arms from the body (2026-10-20, built; live sim; option "Arms")

- Problem: with the gun in the hand the whole first-person rig follows the controller, so the
  shoulders swing with the gun (live, sessions 56 and 64: the gun turned 60 deg, the support
  arm's sleeve swept across the lower view).
- Finding (draw-probe palettes, sessions 58-64; scratch `fp_rig.py`, `fp_classify.py`): BF4's
  first-person arms skeleton has fixed bind-space joints (the points two bones map alike in every
  pose): shoulders (+-0.1641, 1.4745, -0.0396), elbows (+-0.3729, 1.2601, -0.0125), wrists
  (+-0.5089, 1.1205, 0.1609) (+x: the left arm). The 34695-index depth mesh and the 19431-index
  G-buffer mesh each hold both arms (clavicle, two upper-arm and three forearm bones, the hand and
  fingers per side); other meshes (sleeves, the right hand) hold parts. Palettes order bones per
  mesh, and the weapon's palettes carry copies of the arm bones: the same pose, a few cm apart
  during reloads, or stale ones metres away.
- Route (`Bf4ArmIk`, pure, core tests; `Bf4FirstPersonView`, GPU side, nothing in BF4's memory
  written): in the first-person pass each bone palette BF4 binds at vertex b0 (>= 1 KB) is read;
  each arm's chain is found where an upper-arm bone shares the shoulder with another bone and the
  elbow with a forearm bone that shares the wrist with another (1 mm). Chains near the
  first-person camera (shoulder within 0.6 m) vote; the most voted is the pass's reference (ties:
  nearest the previous pass's). Every palette's bones are classified against it (5 mm): upper
  arm (maps shoulder and elbow), forearm (elbow and wrist), clavicle (shoulder), hand (wrist);
  fingers and weapon bones map none and stay. One solve per pass, in the head's space: the torso
  goes to the body frame (the head's yaw, level, at the eye), the shoulder follows 35 % of the
  hand's move from BF4's hold (up to 8 cm, the clavicle turning about the neck's base), the
  elbow is solved on BF4's elbow side (two bones, lengths from the chain), out of reach the
  shoulder slides along the line to the hand, the clavicle takes half the upper arm's swing.
  The hands stay where the gun puts them. Each classified bone S becomes C S with
  C = headFromPass^-1 D headFromPass, and the bind proceeds with our copy of the palette.
- The body frame: the holsters' estimator (`Bf4BodyGestures`: the head's heading, followed
  only past 37 deg at 4 rad/s), carried with each request's hand (`CameraRequest.bodyInHead`,
  recorded per submit like the hand); without it the head's yaw, level (`BodyInHead`).
- Cost: BF4's ring is write-combined memory; plain loads took 61.9 us per 2.9 KB palette, SSE4.1
  streaming loads 3.1 us (live, session 68), now used for every ring read of the first-person
  view (view blocks and receiver too). Per pass: read ~30 us, find/solve/rewrite ~180 us, upload
  ~3 us (~10 palettes); 116 presents/s with or without (session 67, before streaming: 99 vs 119).
- Mod menu "Arms: With the gun / From the body" (id `arms`; default From the body; gun in the
  hand only). Log: `arms: from the body`; `first_person_view ... arms=read/rewritten bones=
  solved= switches= slide=(left right) arm_us_per_pass=read N (streaming) work N upload N`.
- Live (simulated headset, sessions 64-68): every pass solved both arms; all arm draws of a pass
  agree within 0.5 mm (depth prepass and G-buffer); turned 60 deg left or right the arms reach
  from the body (no sleeve across the view); the head turned 30 deg either way with the gun ahead:
  the shoulders stay; reloads (the left hand to the magazine) and aiming down sights keep
  working. Session 64 found a stale weapon copy leading the first arms draw (fixed: the distance
  gate); aiming 40 deg up smeared the shoulder skin (eased: the clavicle's share).
- Gaps: the left hand stays on the gun (not on the left controller when it lets go); the
  skeleton's joints are the prologue rig's (another rig finds no chains and stays as BF4 draws
  it); aiming steeply up still shows the near upper arm large; not seen in a headset.

## No crosshair in the head-tracked view (2026-10-20, built; live sim)

- BF4's hip crosshair and hit marker sit at the frame's centre, which in the head-tracked view
  is wherever the head looks: meaningless there (the barrel shows the aim; the player wants no
  crosshair). Its visibility lives in the weapons' shared `HudData`, not a setting.
- Found (research `drawprobe` with target identities): BF4 composites the scene into the
  swapchain buffer (a draw reading the render-resolution target) and then draws the HUD straight
  onto it (draws reading BC3 UI atlases), all on the immediate context.
- `Bf4HudSplit` (a `Bf4ContextHooks` listener; render targets tracked through
  OMSetRenderTargets): before the first HUD draw after the composite it copies the buffer (the
  scene without the HUD); at Present, while the world lane runs, a plus-shaped mask around the
  centre is put back from that copy, before the frame is captured: a square of 10 % of the
  frame's height, and bands 4 % thick reaching 15 % each way along the axes. A 20 % box (the
  first version) let the crosshair's arms out mid-burst: full-auto fire spreads them along the
  axes to ~10 % from the centre (live, sessions 38 and 41); a 44 % plus hid an objective marker
  near the eye line (session 42). The rest of the HUD (minimap, ammo, markers off the axes)
  stays; the theater view keeps BF4's crosshair. Only our copy and that mask of the presented
  image are written (the flat mirror shows it too). Log: `hud_split ... copies= restores=
  no_composite=`.
- Evidence: WARP check (the centre square and the arms come back from the scene, off the axes
  it stays as the HUD left it, one copy and one restore); live (session 30): eye pairs without
  the crosshair, 2359 copies and restores in 2360 frames; session 43: still and mid-burst eye
  pairs without it, the objective marker left of the centre kept. Markers on the axes near the
  centre (one looked at straight) are still hidden.

## Gun follows aim: the barrel on the bullets' line (2026-10-19, built; live sim; an option since 2026-10-20)

- Problem (player, 2026-10-19): with controller aim the bullets go where the controller
  points, but BF4's first-person gun stayed in front of the face. Live experiments on the
  simulated headset (research `viewcopies`, head turned and flipped every frame): BF4 draws
  the gun at rest relative to the camera that renders the frame (`+0x40` of the submitted
  view block), whatever the soldier aims at; `+0x00` (a soldier-relative camera, about
  600 units from `+0x40` in the prologue) only feeds its weapon-lag motion. So no single
  write moves the gun alone.
- Route (input/render only, no new native write): the override renders each frame with the
  camera turned to the game's aim at the head's position, so BF4 draws its gun along the
  bullets' line (and its own crosshair on the aim); `+0x00` keeps the game's value so head
  motion does not swing the gun. Each eye ray is rotated back into that render by the eye
  resampler (`FrameView`, a pure rotation: exact, no depth needed), so the world matches the
  head; depth stereo rotates each eye's offset too. The turn is latched per pose id and
  recorded with it, so every submit of a lease shows the same world and the capture uses the
  matching rotation.
- Cost: a turned render must still cover both eyes, so it is wider. Coverage counts each eye's
  lens ellipse (runtimes mask the rectangle's corners; the rotated corners would cost far
  more); the turn stops at a 2.1 rad (~120 deg) vertical budget (`kGunAimFovBudget`). Quest-like
  eyes (head-only 1.88 rad): full follow up to ~20 deg sideways or ~7 deg down (up/down costs
  most: the vertical FOV is already the limit); the centre is up to ~22 % less sharp then.
  Lens corners that fall just outside take the frame's edge colour.
- Reach (2026-10-19, session 11 finding): beyond the budget the gun used to follow only part
  of the way while the bullets went on along the controller (live: hand 15 deg up with the
  head level, gun and BF4 crosshair a third of the way to the aim dot; the menu pose 42 deg
  left/21 up barely moved them). Now the aim itself is kept inside the reach: the
  controller's direction is brought back toward the head, along the turn between them,
  until the render can turn all the way with 0.05 rad to spare (`LimitAimToRender` in
  `Bf4MotionAim.h`, judged with the lease's head pose and eyes). Barrel and bullets then
  stop together at that edge; looking toward a target brings it into reach. Log:
  `reach_limited=` (updates whose aim was held back). With "Gun follows aim" off the aim
  follows the hand fully (the gun then stays in front of the face; the aim dot shows the
  shots). Live (0.4.58-local): hand 15 deg up with the head level put crosshair, gun and
  aim dot together about 5 deg up. The upward reach is small for Quest-like eyes (their
  vertical FOV is already the render's limit); a wider budget would cost sharpness.
- Mod menu "Gun: ... On the aim / Aim, wide": Wide lets the turned render grow to
  2.3 rad instead of 2.1 (`kGunAimFovBudgetWide`; the centre up to ~39 % less sharp than the
  head alone needs, against ~21 %), so the gun follows farther from the gaze before it
  stops. Unit and emulated tests (the aim stops farther up with Wide, still all the way
  followed).
- A turned render brings BF4's own HUD corners (minimap, ammo panel) into the eyes' outer
  edges, skewed (seen live at 20 deg: a second minimap at the left eye's edge). With HUD
  corners on they are shown once, in their insets: the eye resampler draws their source
  regions dark in a turned render (WARP check; fails without it).
- Mod menu "Gun: On the aim" (the default until 2026-10-20). Log: `aim_pairs=` (theater_link),
  `rendered_fov=` (view_override).
- Evidence: unit tests (rotation math, ellipse coverage, budget, the override's aim-turned
  write at the head position with `+0x00` untouched, per-pose latch, exact restore); WARP
  checks (an eye's centre ray lands where the turned render shows it; depth stereo with the
  turn); live on the simulated headset: hand 15 deg right -> the rifle swings onto the aim dot
  with BF4's crosshair; head 20 deg right with the hand straight -> the gun's laser still
  ends on the aim dot; pose-label check 335/335 with head flips of 25 deg and the aim off
  centre; 3D from depth coherent with the turn; hand 50 deg down while looking ahead hits
  the budget (partial follow, as designed). Not seen in a headset yet.

## Game menus in a level, recenter gesture (2026-10-19, built; emulated; live sim)

- Found live (session 11): pausing through the mod menu's "Game menu (pause)" kept the
  head-tracked view, because BF4 keeps rendering the world behind its pause menu. The
  headset then showed a magnified, gun-turned piece of the 2D menu with RESUME out of view
  and no screen for the laser; controller aim also pushed the menu's cursor to the edge.
  Pad Start does not close the menu again; pad A (RESUME is preselected) did.
- Signal (Win32 only, no game memory): BF4 shows the system cursor in its menus and hides
  it in play (live: pause menu `CURSOR_SHOWING` with a cursor handle; playing, flags 0 and
  no handle). With the game window in front and a campaign level loaded, 4 frames of a
  shown cursor switch to the theater screen (the laser clicks there, the triggers are not
  the pad's, controller aim and body gestures stop); 4 frames hidden switch back. Covers
  the death and mission-end screens too. Log: `game menu on screen: theater view` /
  `game menu closed`; status `game_menus=` / `game_menu=`.
- Recenter by holding both stick clicks 1 s (as the mod menu says) never worked in BF4: the
  shared XR host takes that gesture only in roomscale mode, which BF4's host does not use,
  and nothing set the adapter's request. Live, the hold did nothing but drew the knife (BF4's
  pad: left click sprints, right click melees). The adapter now runs the framework's
  `RecenterGesture` on the controller sample, and `StickChord` keeps both clicks from the
  game while they are held together (a lone click reaches it after 80 ms, or as a 60 ms tap
  when released sooner). Log: `recenter: both sticks held`; status `recenters=`,
  `recenter_gestures=`, `stick_chords=`.
- Evidence: unit tests (`StickChord`, reach limit) and emulated campaign sessions (game
  menu on/off with flicker rejection, no mouse or pad trigger into the menu, front end not
  counted; both-sticks recenter with a staggered press and no pad click leaking; reach
  limit through the real core, on and off). Live on the simulated headset (0.4.58-local):
  pause and resume twice, two recenters (Simulated headset, above). Not seen: the death
  and mission-end screens, a headset.

## Head tracking: the world lane (2026-10-19, built; seen in a headset)

In campaign levels the headset shows the game's world from the player's tracked head
(6DoF, both eyes from the same native frame; mono unless 3D from depth is on) instead of
the theater screen. Pieces (all on the analysed build only):

- `Bf4FrameworkGlue.cpp` `TheaterLink` carries both lanes. World: the menu lane says
  Gameplay, so FvrXrHost asks for eye pairs on the frame lane (`D3D11FrameBridge`, the
  same transport BC2 uses; controller samples now arrive through it). Each host request
  (a lease) is taken at the start of a Present; its head pose goes to the camera tagged
  with a pose id.
- `Bf4ViewOverride` (slot 5 of a copied GameRenderer vtable, exact restore) now also
  writes the FOV both eyes need (`+0xB4`, derived `+0x250`, dirty `|= 0xF`), exactly as the
  game's FOV setter `+0xB89EE0` does, and counts how many submits in a row carried each
  pose id. FOV/aspect offsets are from the setter and the view setup `+0x848B00`
  (BF4-CAMERA-RE.md).
- `Bf4WorldView.h`: a frame is captured only once `settle` submits (default 2, mod menu
  1-4) carried the lease's pose, so a pair is never labelled with a pose it was not
  rendered with; a lease that cannot be met in 140 ms (170 ms with two channels) is
  cancelled. Each eye is the part
  of the frame its FOV covers (asymmetric crop), resampled to the host's eye size by
  `Bf4EyeResampler` (game device, own device-context state, raw bits kept).
- The world lane runs only while the game submits world views (loading screens stay on
  the theater panel) and falls back to the theater panel when no pair is published for
  2.5 s (retry after 5 s). The mod menu (menu button) always opens on the theater panel and
  has "In campaign levels: Screen / Head-tracked view".
- Recentring: the first head pose of the session faces forward; mod menu "Recenter view".
  With controller aim the view is anchored to the body heading (right stick); without it,
  game yaw is kept and pitch/roll/position come from the headset.
- Evidence: unit tests (crops, decisions, fallback, FOV write and exact restore, pose
  runs, lease-only camera); Bf4Emulated world phase (the emulated host receives pairs and
  each eye carries its own crop); `Run-Bf4TheaterXr.ps1 -World`: the real FvrXrHost on the
  fake OpenXR runtime submitted 165 pairs from the emulated game, 0 rejected, 0 runtime
  errors. Live on the simulated headset (above): BF4 renders the wider FOV (1.878 rad for
  the simulated eyes), every checked pair carries its labelled pose at frame sync 1 and 2,
  44.8 pairs/s at frame sync 2.
- Frame shape (worked out, not yet seen): the frame is rendered tall enough for the
  taller eye (+3 %) at the window's 16:9, so it is about 1.7x wider than the two eyes need
  (Quest 3 numbers: frame tangent ~2.35 across vs ~1.38 seen). The outer part of each side
  is never shown: BF4's corner HUD (ammo bottom right, minimap bottom left) falls outside
  the eyes, and about 40 % of the horizontal pixels are spent there. A narrower projection
  aspect would fix both, but the view setup takes the aspect from the output size after
  copying the submitted block (BF4-CAMERA-RE.md), so writing it there has no effect and the
  renderer's own copy has no clean restore point. Open: a HUD route (render the UI pass
  separately) or a game window closer to the eyes' shape (the laser needs 16:9 menus).
- HUD corners (mod menu, default on): meanwhile the resampler copies the frame's bottom
  corners into framed insets inside the visible lower corners, at the same view tangents in
  both eyes, above the vignette. Measured on the 10:21 session's frame snapshot (16:9): ammo
  panel u 0.865-0.995 v 0.835-0.985 -> tangents x 0.38..0.82 y -0.64..-0.93; minimap with
  objective text u 0.008-0.142 v 0.705-0.985 -> x -0.82..-0.43 y -0.50..-0.96 (aspects kept;
  inside both eyes of a Quest 3, checked by rendering the real frame through the resampler).
  That session's eye snapshot confirms neither reached the eyes before. WARP check: a corner
  panel outside the eyes appears in the inset and nowhere else.
- Known gaps: mono until engine stereo engages (below); pair rate is bounded by `settle`
  frames per pair (two frame channels remove the request round trip, below), so the world
  updates slower than the headset (the compositor reprojects head rotation every frame); HUD and
  pause menu are drawn into the wide frame (pause menu: use the pad, or switch to Screen);
  first-person weapon not yet re-projected.

## Frame channels: higher head-tracked update rate (2026-10-19, built; live sim)

- Three channels (default since 0.4.55-local, launcher `--frame-channels 3`): the composite
  provider now takes 2-4 channels (one begin per XR frame; a channel begins once the
  youngest outstanding request is 0.7/(channels-1) of a typical completion old; pairs that
  complete together go out oldest first, the rest held, none dropped); the host writes
  `xr-channel-3.txt` too and the adapter serves three bridges (leases taken together are
  queued oldest first). Live on the simulated headset (prologue, frame sync 2): 52.9 pairs/s
  at 117.7 game fps vs 44.8 with two; 6,227 new pairs against 4,674 re-shown frames (two
  channels: about one to one); mean request latency 33-41 ms vs 27 ms (requests now wait
  behind others; the compositor reprojects rotation either way); timeouts 0.4 % (0.24 %);
  pose-label check 525/525 with head flips of 60 degrees and the gun following the aim.
  Emulated (`Run-Bf4TheaterXr.ps1 -World -FrameChannels 3`): 396 pairs through the real host,
  every channel delivering, 0 rejected. Unit test: three channels begin one per frame and
  three pairs completing together come out oldest first over three frames.

### Two channels (the first step)

- One frame channel holds one request: after each pair the host had to see it, give
  feedback and ask again before the game could take the next head pose (a request round
  trip of 1-2 game frames on top of the `settle` frames). The launcher now starts
  FvrXrHost with `--frame-channels 2`: the host alternates two independent channels
  (`PipelinedFrameProvider`, own textures and ticket sessions, single-slot protocol
  unchanged) and the adapter's `TheaterLink` serves two bridges as a FIFO, so the next
  request is already queued when a pair is captured. Each pair is still labelled with its
  own request's pose (oldest request first; its pose goes to the camera).
- Next pose in the capturing frame: the camera's pose used to be set at the start of each
  Present's frame work, before that Present captured, so after a capture the next queued
  pose waited one more frame and one submit carried a pose already captured. The core now
  asks the link for its current request right after publishing and, if the front changed,
  sends that pose to the camera in the same frame (`earlyPoses`): 2 Presents per pair at
  settle 2 instead of 3. The labelling rule is unchanged (a capture still needs `settle`
  submits of the pose). Emulated test.
- Emulator note: with two channels the queued request (usually channel 2) can outlive its
  200 ms in Bf4EmuHost, where each capture is a WARP software render (25 of 139 timed out
  in one run; pairs per run vary 76-237 with the machine's load). Not expected on a GPU.
- Staggering: one channel begins per XR frame and only once the other request is 0.7 of a
  typical completion old, so the queued pose is not much older than a fresh one. If both
  complete in one XR frame the older is shown and the newer held one frame (never
  dropped). Menu/recenter hands a held pair back as not consumed.
- Requests live 200 ms with two channels (one waits behind the other); the adapter cancels
  a queued lease after 170 ms (140 ms with one channel). The host keeps both channels'
  shared textures open (two-entry cache), so alternation never reopens them.
- Second token: `logs/xr-channel-2.txt`, written before `xr-channel.txt`. An adapter or
  host without it falls back to one channel. Host report: `frame_channels`,
  `channel2_*`, `pipelined_held_pairs`. Adapter log: `theater_link ... bridges=2 queued=`.
- Evidence: `PipelinedFrameProviderTests` (real shared-memory channels: one begin per
  frame, older pair first and the newer held, stagger, suspend hands back the held pair,
  input on channel 0); `Run-Bf4TheaterXr.ps1 -World -FrameChannels 2`: 237 pairs (119 +
  118), 0 rejected, vs 47 with one channel in the same emulated run (untracked capture,
  so this measures the transport, not the settle frames). Live on the simulated headset:
  both channels used, 0.24 % of requests timed out, rates above.

## Holsters and magazine reload as gestures (2026-10-19, built; emulated only)

- `Bf4BodyGestures.h`: input-only (no game memory). Body-relative holsters (the
  framework's `BodyFrameEstimator` + BF4 layout): over the right shoulder = primary (key
  1), right hip = sidearm (key 2), over the left shoulder = first gadget (key 3; the VR
  layout has no D-pad, so gadgets were unreachable before), left hip = grenade (grab, let
  go away from the hip to throw: G), chest with the left hand = knife (F), belt front
  left = magazine. A gadget in hand has no VR magazine: the gun's waits, fire is free. Keys are BF4's
  default PC bindings, sent as scan codes only into the focused game window, held 80 ms,
  and released on every outcome (key change, focus loss, detach). A grip press that starts
  in a holster is the gesture's, not the pad's; with holsters on, the right (gun) grip is
  never the pad's grenade button. Short controller pulses go to the XR host as feedback
  events on frame channel 1 (its haptic policy checks packet, space and deadline).
- Manual reload (Reloading: Manual) is now the magazine gesture when the virtual pad is
  usable: a tap on right B drops the magazine of the gun in hand on release (primary and
  sidearm tracked separately), fire (pad RT) is held off until one is seated, the left hand takes one from
  the belt and seats it at the magazine well (7 cm below, 6 cm ahead of the right grip,
  12 cm radius), which presses the pad's reload (X) so BF4 plays its own reload. Letting go
  before the well drops it. Holding B (0.4 s or more) holds the pad's X for as long as B
  is held: BF4's reload, or its interact / pick-up when something is in reach (the same
  pad button), so manual mode loses no game function; nothing is dropped by a hold. A magazine pushed into a gun
  that still has one is a tactical reload (same press; log `tactical=`). Custody persists across
  packets, menus and tracking loss; a level start is the lifetime transition. BF4's own
  ammunition and auto-reload are untouched (nothing native is acknowledged or written).
- Feedback without visuals of the gear itself: a light pulse when an empty hand reaches a
  holster it could use now (not the gun already in hand; the belt only while the gun is
  empty), and two markers drawn into the eye pair: an amber ring at the belt pouch while
  the gun has no magazine, a green ring at the magazine well while one is in the hand.
  Each eye places a marker by its own pose (`MarkerInEye`), so markers have true depth
  over the mono world. Log: `marked_pairs=`.
- Physical crouch (mod menu, default off; standing play): crouching 35 cm below the
  self-calibrated standing height pulses BF4's crouch toggle (pad B), standing back above
  20 cm toggles it off (`Bf4PhysicalCrouch.h`). The stance is believed, not read: it follows
  our pulses, the player's own crouch button (which is never overridden until the body
  changes posture) and BF4's own stand-ups on sprint and jump. Unit-tested; status and the
  periodic gestures log line: `crouch=... crouch_toggles=`.
- Mod menu: "Body holsters: Off / On" (default on). Status/log: `gestures gun=...
  magazine_out=... draws= throws= knives= ejects= grabs= seats= drops= quick_reloads=`
  (logged at each change), plus `key_sends` / `haptics_sent` every 600 frames.
- Evidence: unit tests (each holster, put-back vs throw, knife hand, grip held at engage,
  holsters off; eject/hold fire/persistence/drop/seat/per-gun/quick reload/level start)
  and an emulated campaign through the whole core (keys reach the platform and go up,
  pulses, RB/LB not pressed by gesture grips, RT held off, X on seating); hover pulses and
  marker placement/parallax unit-tested; the WARP check draws a marker ring. Live on the
  simulated headset: keys 1 and 3 by gesture switch weapons, the magazine gesture's pad X
  reloads after keyboard selection (BF4 takes both), hover pulses arrive; the left-hip
  grenade (grab, release away) threw one (HUD 3 -> 2) and the left-hand chest grab drew the
  knife (HUD melee). Not seen: key 2 (no sidearm in the prologue), holster positions on a
  real body.

## Controller aiming (2026-10-19, built; emulated only)

- `Bf4MotionAim.h`: in the head-tracked view the gun follows the right controller through
  BF4's own mouse look (input only, nothing written to game memory). The headset view is
  anchored to a body heading that only the right stick turns (`HeadTrackedViewAtYaw`;
  ~150 deg/s); each frame the game's aim, read from the camera it submits (view block
  `+0x40`, read before the override), is steered toward body heading + controller
  direction by relative `SendInput` mouse counts (only into the focused game window).
  Counts-per-radian are learned per axis from what the game actually turned (sign
  included, so inverted look and aim-down-sights sensitivity are handled). If counts
  produce no turn (pause menu, cutscene) it goes quiet for 60 frames.
- The virtual pad's right stick is released while it aims (no stick look), and the left
  stick is turned back by the aim's offset so walking stays relative to the body.
- Walk toward (mod menu): Body (default) turns the left stick back by the aim's offset so
  walking follows the body; Gun leaves the stick alone. Vehicles: the left stick is
  throttle and steering there and the adapter cannot tell a vehicle from walking yet, so
  Body would mix them while the gun aims off the body heading; switch to Gun in vehicle
  sections (emulated test through the menu).
- Two-handed aim (mod menu, default on): while the left grip is held 12-80 cm in front of
  the right grip and within 35 degrees of the right controller's aim (60 to keep it), the
  gun aims along the line between the grips, steadier than one wrist. Its grip is then not
  the pad's spot button; a grip carrying a magazine or grenade never counts. Unit-tested;
  log `two_handed=` (engagements).
- Comfort vignette (mod menu, default on): while the left stick moves the player, or the
  body turns smoothly, the eye resampler darkens each eye beyond ~35 deg from its centre
  (dark by ~50 deg), in proportion to the stick and fading in fast / out slower
  (`ComfortVignette`). Unit-tested; the WARP check draws it.
- Mouse steering is now robust to the game's input delay: half the remaining error per
  frame, with the counts of the last two frames treated as in flight. The old step (0.7,
  no in-flight term) was stable only for the emulator's two-frame lag; with three frames it
  diverged (the aim would swing ever wider). Chosen by simulation (delays of 1-3 frames,
  gain off by +-40 %: settles in 7-24 frames, overshoot <= 22 %) and checked by an emulated
  test at three delays, which fails with the old step.
- Mouse gain: the first estimate is deliberately high (0.003 rad/count): a game more
  sensitive than believed overshoots every step, a less sensitive one only steers slower
  until learned (simulated: no overshoot for true gains 0.0002-0.003). Learning trusts a
  window by how far the game turned (0.2 rad: fully) rather than by counts, so one aim
  step learns the gain; learned gains survive re-engagement. Overshoot guard: when the
  error swings past zero by more than 30 % of the swing before it, the estimate grows by
  that ratio at once, at most 4x (a threefold sensitivity jump, e.g. leaving
  aim-down-sights, settles without running away; emulated test at two delays). Log:
  `guards=`.
- Guard fix (live, 2026-10-19): a sign change the target makes (it moved more than the
  game in that update: a flick, snap turn, two-handed grip taken or let go) is not an
  overshoot and starts a new swing. Before, letting go of the two-handed grip grew the
  pitch gain sevenfold (-0.0014 -> -0.0098; slow vertical aim until relearned). Chosen with
  `tools/models/aim_guard_model.py` (delays 2-3 frames, sensitivity 1-5x, steps, ramps,
  teleports, sweeps):
  clearing the record on any fast target motion blinded the guard and ran away on moving
  targets (up to 23 rad); judging by cause keeps the original protection in every modelled
  case and removes most false alarms (sweeps at the right gain: 5 -> 0). Tests: target
  teleports at a learned gain change neither gain nor guard count (fails before); the
  sensitivity test now changes sensitivity while the hand holds and then turns quickly
  (its old form teleported the target in the same frame, which overshoots ~1 rad at three
  frames of delay whatever the guard does: the counts leave before the game shows any;
  it had passed only because the teleport misfired the guard).
- The game turning by itself (found live, session 15: a grenade at the player's feet; BF4's
  death camera sank and turned the view while the aim kept sending counts): learning took
  that for the mouse (pitch gain sign flipped, both gains collapsed to ~1e-5) and the
  overshoot guard then grew the yaw gain to its 0.05 clamp, 35x the real value, which
  survived the respawn. Now: a window in which the game turned against one-signed counts
  (compared with the counts it has applied by then, two samples later) starts a probe:
  aiming goes quiet for a moment and watches. If the game keeps turning, it is its own
  camera (`foreign_turns=`): nothing is learned and aiming stays quiet, longer each time.
  If it holds still, the counts did it: inverted look, and that axis's sign flips (a sign
  changes only this way). Once an axis is learned, one window may change its gain at most
  2.5x either way unless the next agrees; gains stay within 5e-5..0.02. The guard grows a
  gain only when the game moved the way recent counts pushed it, by no more than they
  explain. Unit tests: death camera and respawn (gains stay within 2x, aiming converges
  after), inverted look found by probing; the 3x sensitivity tests at delays 1 and 2 pass.
- Death camera, second round (live, sessions 16-17): the death camera also swings the yaw
  both ways, sometimes the way steering pushes, so guard firings between probes still grew
  the yaw gain (6x, then 5.8x with a cap). Now: (1) the overshoot guard is armed only for
  ~1.5 s (180 game samples) after the game's own vertical FOV changes by more than 2 %
  (BF4 changes its mouse sensitivity exactly when its view zooms: sights, scopes,
  vehicles; `GameFovY` from the view override), or while nothing is learned yet; (2) a
  guard never takes a gain past 4x the anchor, the gain learning last confirmed (set only
  by a window that taught something, or by seeding an axis from the other, never by a
  guard); (3) after a probe finds the game's own camera, ~2 s (240 samples) pass with
  neither learning nor guards; (4) a window teaches with 200 counts, or with 40 counts and a
  turn of 0.05 rad (an estimate that is too high sends few counts and could otherwise never
  relearn). Session 18 verified it live (above). Not modelled in
  `tools/models/aim_guard_model.py` (which covers the earlier rules); checked by the unit
  tests (death camera with yaw swing; 3x sensitivity with a FOV change at delays 1 and 2)
  and live. A sensitivity change without a FOV change (a settings menu) is now relearned
  by windows, at most 2.5x per window, instead of by the guard.
- Recoil (live, session 19, 0.4.66-local): a full 20-round magazine on full auto with the hand
  held still: BF4 kicked the view up 68 times (mean 0.0022 rad, max 0.0057) and aiming pulled
  each back; the aim stayed within 0.011 rad of the hand (mean 0.0023), gains unchanged, no
  guard, mute or probe. Unit test with two frames of delay: full auto and a 3 deg sniper kick.
- Aim trace (developer research `aimtrace <name>`): the last 4096 aim updates (~35-49 s)
  as CSV (`tick_ms,sample,game_yaw,game_pitch,target_yaw,target_pitch,mouse_x,mouse_y,
  gain_x,gain_y,guards,mutes,foreign,flips,engaged`) in `<research>/<name>.csv`; dump it
  within ~12 s of the event. Session 17's death trace is replayed by a unit test
  (`tests/bf4/data/DeathCameraTrace.h`, 796 rows, generated by
  `build/scratch/pytools/trace_to_header.py`). That replay checks the data path and the
  current code on real camera motion; it is not a reproduction (the pre-fix code also
  passes it, as its window alignment differs from the live run's).
- An axis not learned yet starts from the other axis's learned gain (x1.2, the cautious
  side) instead of the first estimate: BF4 turns as far per count on both axes (0.00140 /
  -0.00140; ADS 0.00084 / -0.00084), and with the gun's reach keeping vertical moves small
  the pitch gain had stayed at the first estimate (2x: half-speed vertical aim; session 12
  ended at -0.003). An axis counts as learned after windows worth 0.1 rad of turning; its
  own learning then takes over. Unit-tested.
- Paused game (BF4's Esc menu moves a cursor with the mouse): when a 20-sample window sent
  200 counts or more and the game did not turn at all, aiming goes quiet for 1 s, then 2,
  4, 8 s while it keeps happening; any turn resets that. (The old 1500-count threshold
  never tripped at the cautious first gain.) Unit-tested. Live (session 11) this was not
  enough: aiming re-engaged when the mod menu closed and pushed the pause menu's cursor to
  the screen edge. BF4's own menus now release aiming outright (Game menus in a level).
- Aiming stays engaged across short gaps between world-lane leases (500 ms hold). Before,
  each frame without a lease released it, and re-engaging re-anchored the body heading to
  the game's aim (a view jump by the controller's offset) and reset the learned mouse gain;
  the emulated test had held a lease every frame. Regression test with a lease gap every
  third frame (fails without the hold).
- Turning (mod menu "Turning: Smooth / Snap 30 / Snap 45 / Snap 90", default Snap 30): the
  right stick turns the body heading in steps (one per push past 0.7, re-armed under 0.3), or
  smoothly at the "Smooth turn speed" (Slow 90, Normal ~150, Fast 240 deg/s). The game's aim
  follows through the mouse within a frame or two; the head view jumps with the heading.
  Unit-tested (30 and 90 degree steps, smooth rate); log `snaps=`, `turning: ...`.
- Mod menu: "Aim with: Right stick / Right controller" (default right controller).
  Status/log: `controller_aim=... gain=... mutes=... mouse_sends=...`.
- Evidence: unit tests (convergence onto controller yaw/pitch, gain learning with signs,
  body turning, release, stall guard) and an emulated campaign session with a one-frame
  input delay (core + fake game); live on the simulated headset (above), including
  aim-down-sights (gain followed BF4's ADS sensitivity) and firing. Not seen in the real
  game: vehicles, recoil interplay.
- The gun is still the game's first-person model at the game camera (it points where the
  controller points); hands/IK are separate work.
- Aim dot (mod menu "Aim dot", default off since 2026-10-20: the player wants no crosshair,
  the barrel shows the aim; shown anyway with "Gun follows aim: Off", where nothing else
  would): BF4 draws its crosshair at the centre of the
  frame, which is now where the head looks, not where the gun aims. While controller aim is
  engaged the eye resampler draws a green ring and dot where the game's aim (its own camera,
  read before the override) crosses the rendered head view (`AimInView`, `ReticleAt`;
  ~0.6 deg, same direction in both eyes of the mono frame). Not drawn in split 3D. Evidence:
  unit tests (geometry) and a WARP GPU check in Bf4Emulated (ring and dot drawn at the aim
  point, frame untouched elsewhere, plain pass unchanged). Log: `reticle_pairs=`.

## 3D: alternate eyes (2026-10-20, built; live sim; option)

True stereo without a second render per frame: each frame is rendered from one eye, the eyes in
turn, and each pair shows the new frame in its eye and the other eye's latest frame, turned onto
the new frame's pose (the approach of alternate-eye rendering mods).

- The eye is chosen on the render thread, per frame (`Bf4FirstPersonView`, a
  `Bf4ContextHooks` listener; it toggles at each Present): every main view bound in the frame
  (`viewConstants` at the render's projection scale, rigid perspective; ~16 binds a frame live)
  gets a camera moved half the eye distance (the headset's, else 32 mm) along the head's right
  axis, and the first-person pass with the gun in the hand is seen from the same eye. The CPU
  camera (the view override) stays at the head, so culling, lighting and the pose labels are
  the mono lane's; BF4's deferred lighting reconstructs positions with the head's camera (3 cm
  off, not visible). The frame's eye goes to the frame lane with the Present (`laneEye`).
- First try, discarded: moving the CPU camera to an eye per lease. BF4 draws the first-person
  rig into the G-buffer before the frame's main view, and which submit a render frame shows is
  not predictable (live: 1-4 submits behind the newest, sometimes skipping one; predicting "the
  previous + 1" missed 14 %), so the rig's G-buffer draw came out at the other eye: a lit gun
  beside a dark silhouette in one eye.
- Frame lane (`Bf4FrameworkGlue`): every frame known to show its pose (`settle` submits) is
  kept as its eye's (`EyeResampler::Keep`, one copy a frame); a pair takes the new frame's eye
  from it and the other eye from that eye's kept frame (at most ~200 ms old, else this frame
  in both), turned by the rotation between the two frames' cameras
  (`AlternateEyes::staleRotation`, the view override records each submit's camera axes).
  3D from depth and "Gun follows aim" (a turned render) are off with it.
- Live (simulated headset, session 50, prologue): one lit gun in each eye in every pair; the
  hand-held gun 40-70 px of near parallax against the walls' ~6-10 px (a ~1440 px eye),
  pose-label flip check (head yaw alternating 0/10 deg every frame) 327 pairs, 327 matched,
  none mismatched; 52.4 pairs/s at 117 game fps (as mono); 1-2 % of pairs had no recent kept
  frame. Log: `alternate_pairs= alternate_one_frame= kept_frames=` (`theater_link`),
  `eye_views= eye_frames=` (`first_person_view`). Session 57: BF4's pause menu (Esc) took the
  view to the screen and RESUME brought the alternate-eye pairs back, the gun lit in both eyes.
- Evidence: core test (requests carry the option and the half eye distance; the camera stays
  at the head); WARP checks: the resampler (kept frames, the other eye turned onto the new
  camera, one-frame fallback) and the first-person view (the main view at the left eye, the
  next frame's at the right, BF4's first-person pass untouched without the hand, the hand-held
  rig seen from the frame's eye). Not seen in a headset: comfort (each eye refreshes at half
  the game's rate; moving things are a frame apart between the eyes).
- Gaps: BF4's HUD markers and the corner HUD are drawn at the frame's centre camera in both eyes
  (at infinity); with "Gun: BF4's place" the first-person rig is not moved to the eyes.

## 3D from depth (2026-10-19, built; emulated only; opt-in)

Native split stereo is unsupported on PC (below), so this route gives depth without a second
render: both eyes are cut from the one mono frame and each is shifted by its own parallax
using the frame's depth buffer (depth-image-based rendering; no simulation or render replay).

- `Bf4DepthCapture`: finds the frame's main depth buffer through the game's own
  `ClearDepthStencilView` calls (MinHook detours on d3d11.dll's implementations; the
  calls always proceed): the largest depth target (single- or multisampled) with the
  backbuffer's aspect. The D3D11 runtime resolves a context's vtable entries lazily and
  immediate and deferred contexts have their own implementations, so the install resolves
  them first (one clear of a 4x4 target of ours on each) and hooks each implementation it
  sees, re-checking at every Present. With two or more clears of it a frame (BC2 clears
  again before the first-person weapon) a copy is recorded just before each clear on the
  same context (in order, even on deferred contexts) and the one from before the last
  clear holds the world; with one clear a frame the live buffer is copied at Present and
  nothing before the clears (live, that pre-clear copy was a 65 MB MSAA copy every frame
  for nothing; now `copies=0`). A target picked before the backbuffer's shape was known is
  dropped once it is.
- Live (simulated headset, prologue): BF4's main depth is 2688x1512 (the render
  resolution: 1920x1080 at 140 %), R24G8_TYPELESS (44), 4x MSAA, cleared to 1.0 (standard
  depth), once a frame: the first-person gun is drawn into the same buffer (nearest
  0.12-0.13 m), so the depth at Present has the gun. The front end uses a single-sample one.
  `depthprobe` snapshot (sample 0 drawn into a float target under our own context state,
  log scale): gun, holo sight, rubble, girders and wall, aligned with the frame; centre
  3.3 m. With 3D from depth on, the holo sight shifts right in the left eye and left in the
  right eye (crossed: in front), about 110 px between the 1440 px eyes, i.e. about 0.5 m;
  edges at depth jumps show doubled outlines (disocclusion, as expected of the method).
- Eye resampler: each eye samples the frame at its own tangent + b/z (b = signed half eye
  distance, z = view distance from the depth with the view's near/far, +0xBC/+0xC0, read
  live as 0.06 / 20000; reversed depth when the buffer clears to 0), by a four-step
  fixed-point search; nearer than 0.4 m counts as 0.4 m (the weapon, about where a VR gun
  is held; nearer would mean a very strong per-eye shift). The aim dot is drawn where the source point under the aim lands in each eye, so it sits on the surface it covers (no double image on a near target); gesture markers keep their own per-eye placement.
- Mod menu "3D: Off / From depth / Alternate eyes" (id `depth_stereo`, values 0-2; default
  off). Log: `depth_capture installed= main=WxH
  format= clear= split= ... msaa_clears= hooked=` every 600 frames, `depth_pairs=`; developer sessions
  write `vr-snapshot-depth.bmp` (log scale: 0.1 m white, 200 m black; multisampled buffers
  read at sample 0) the first time it works, with the other snapshots and on `depthprobe`
  (which also logs `centre= nearest= far_plane=`).
- Evidence: linearisation and per-eye shift signs unit-tested; WARP checks in Bf4Emulated:
  near content shifts each eye its own way while far content stays (shader), and the real
  detour picks a 16:9 depth over a larger square shadow map, detects two clears a frame,
  keeps the depth from before the second clear, and uses the live buffer with one clear
  (that check found the lazily resolved vtable entry and the shadow-map pick before the
  game did). Multisampled depth (BF4's deferred MSAA) is copied as it is and read at
  sample 0 through a Texture2DMS view (pixel shader now ps_4_1: SM 4.0 cannot declare a
  Texture2DMS without a fixed sample count; the WARP check caught that compile failure,
  which would have disabled the world lane); WARP checks for a 4x MSAA depth in the
  shader, in the capture and in the snapshot (sample 0 of a 4x copy reads back 0.12 m).
  Not yet seen: how it feels in a headset (comfort, the doubled edges, the gun at the
  0.4 m clamp).

## Stereo: BF4's own two-eye render in the world lane (2026-10-19, retired)

- **Retired (live, simulated headset, 2026-10-19)**: the "freeze" is a crash. Two views
  overrun BF4's world renderer, which is built for one (a one-element view-renderer array
  at `+0x88`, a 0x34400-byte object with one 0x34000-byte view block); view 1 takes an
  unrelated member as its renderer and faults, and BF4's crash handler spins. Details and
  addresses: BF4-STEREO.md, "Split stereo is unsupported on PC". The developer item is now
  shown disabled ("Unsupported: BF4 renders one view") and the core keeps it off; the code
  below stays for reference. History of the attempt:

- **First headset session (0.4.13-local, 09:16)**: head tracking worked (world lane,
  ~22 pairs/s at ~97 game fps, FOV written 1.947 rad). 3D engaged as designed
  (`mode=1 on=1`, half eye 31 mm) but looked strongly zoomed and the game died within
  seconds (BF4's own handler, no Windows crash report).
- **Crash cause (static, confirmed)**: the two-eye loop `+0x8CAF70` appends one 0x1560-byte
  entry per eye to an array (object `+0x1570/+0x1578/+0x1580`) whose inline storage at
  `+0x1590` holds exactly one entry, with no bounds check; the second eye overwrote the next
  0x1560 bytes of the object. Fix: `Bf4EyeLoopOverride` points that object at a copied
  vtable (16 slots) whose slot 2 (`+0x8BE430`, the only way the loop is reached) gives the
  array room for 8 entries for the length of each call and restores the three pointers
  exactly afterwards. The object is found by a background read-only scan (class vtable
  `+0x1D24258` with its array in inline storage; exactly one expected), re-checked right
  before the one write. 3D is refused unless this override is in place.
- **Zoom cause**: in split mode the view's aspect is an eye half's (the view setup takes
  it from the output's `+0x60/+0x64` size), not the frame's; crops, the render FOV and the
  eye distance assumed the frame's (inferred from the zoom; the stereo probe now logs that size). Now the adapter reads the aspect the engine applied
  (`[renderer+0x60]` view 0 `+0xC4`) and uses it for all three.
- **Second headset session (0.4.14-local, 09:46)**: no crash, but the game stopped
  presenting ~2 s after 3D engaged (eye-loop override installed, side-by-side render
  confirmed, then no Present; the headset went black). The head-tracked mono view before
  it was reported "way too zoomed in" and a "motion sickness machine"; the pair rate was
  23-32 Hz at 100-200 game fps, render FOV 1.947 rad and view aspect 1.778 as intended.
  `waitRenderSync` (`+0x8288B0`): the main thread waits after every submit, so it runs
  at most one frame ahead and the settle-2 labelling rule holds. The zoom is not
  explained yet; the projection is rebuilt from the written FOV (view update `+0xB8C180`
  bit 1 -> `+0xB8CFC0`, matrix at view `+0x320`).
- **Third headset session (0.4.15-local, 10:19)**: the player found the cause of the
  sickness: every head rotation was inverted (look left -> turned right, look up -> looked
  down). Snapshots (`vr-snapshot-frame.bmp`, `vr-snapshot-eyes.bmp`) and the probe showed
  the renderer's view at FOV 1.95 rad with the real world camera position during head
  tracking, eye crops consistent with that FOV, and the gun/HUD drawn far too large.
  Fixed in 0.4.16-local:
  - Handedness: BF4's view space is right-handed (projection builder `+0xB7EAE0`: w = -z
    when view `+0xD9` is set, which it is live); camera rows are right, up and +Z behind
    the camera, as in OpenXR. The head pose now goes in unmirrored
    (`AdapterCore::XrPoseToBf4Camera`; unit test: head left/up turns the camera left/up).
  - Gun size: view update bit 4 (`+0xB8C910`) sets `+0x258 = tan(fov/2) /
    tan(defaultFov/2)` from the default FOV at `+0xB8`, which keeps the first-person gun
    at its normal on-screen size; with the 111 degree render it was ~3.5x too big in the
    headset. The override now writes the FOV to `+0xB8` too (restored exactly), so the
    scale is 1 and the gun is at true size. The HUD is screen space and stays magnified
    by the crop (~2.3x).
  - Split stereo image shift sign follows the right-handed view (left eye -separation).
  - Resume click: the logged press/release drifted 63 px (1920 wide), beyond the 2 % lock;
    the lock is now 6 %.
  - 3D depth now defaults to off (the split render still freezes the game).
- Evidence tools for the next session (developer): `vr-snapshot-frame.bmp` (the game
  frame) and `vr-snapshot-eyes.bmp` (both eye images) 5 s into head tracking and on every
  "Mark this moment for feedback"; a watchdog that logs every thread's stack (module +
  offset) after 4 s without a Present (Bf4Diagnostics; emulated end to end); a fault log
  (`faults.log`) armed while 3D is on; each laser click logged with its client position.
- Crash guard: a marker written while 3D is on and removed when it goes off; a session
  that starts with the marker present starts with 3D off (the mod menu can switch it on).
- The rest is as built: `Bf4EngineStereo` sets StereoEnable/StereoSplitScreenEnable and
  the stereo values through an exact-write journal and keeps the engine's eye distance at
  the headset's; 3D starts 10 s after head tracking; crops come from the halves with the
  engine's image shift removed; no capture until the layout is stable.
- Not verified: whether anything else in the split render has one-eye capacity on PC
  (the next session's log shows `eye_loop ... max_entries` and survives or not), HUD,
  cost, depth comfort. Per-eye horizontal resolution is half the backbuffer's.

## Headset path (emulated end to end)

- Theater link (`Bf4FrameworkGlue.cpp`, `FVR_BF4_FRAMEWORK_GLUE` ON): the game screen goes
  to the XR host on the existing menu lane (MenuChannel + D3D11MenuProducer), labelled
  1280x720 logical (16:9, the host's verified pointer mapping); the host's MenuControl
  comes back as the game pointer. The host writes `logs/xr-channel.txt`; the adapter
  connects whenever it appears.
- `FvrXrHost.exe` = the shared OpenXR host (BC2XrHost main) with `--game-exe bf4.exe`;
  the launcher starts it after BF4's PID is known; packaged with the OpenXR loader.
- Bf4EmuHost (CTest `Bf4Emulated`): the emulator plays the XR host; the panel carries the
  game image and the host's MenuControl clicks the emulated button exactly once.
- `tools/emulated/Run-Bf4TheaterXr.ps1` (explicit, GPU): real FvrXrHost on the theater
  fixture (FvrTheaterTestXr) + emulated game: 19 panel frames, 75 quad layers, panel
  centre pixel = the game's colour, 0 runtime errors.
- Real game + headset: theater mode works (2026-10-19). The world lane above is next.

## Camera and session signals (2026-10-19, live read-only probes done)

- Live probe: `[renderer+0x60]` entry 0 holds a rigid camera transform that follows the
  player; FOV `+0xB4` is in radians (0.79 in the prologue), near 0.06, far 20000, aspect
  1.778. Slot 19 returns `[renderer+0x70]` (not a camera).
- Session signals (R5) are wired (`Bf4SessionSignals.h`): campaign levels read as
  `Levels/SP/<name>/<name>`, the menus as `Levels/FrontEnd/FrontEnd`; isMultiplayer is
  noise outside a level, so it counts only inside a non-front-end level and a 1 must hold
  2 s before it locks (the lock is permanent). Campaign gameplay features now unlock.

## Controller input (2026-10-19, emulated)

- Virtual XInput pad from the VR controllers (BF4 default scheme, reload = right B ->
  pad X). Bf4EmuHost: the emulated host publishes a controller sample and the game's own
  `XInputGetState` call sees reload + left stick forward through the real hooks. Core
  tests: deadzones, reload gating by mode, neutral while the mod menu is open, Start pulse
  from "Game menu (pause)", pad released on lockdown. Live (simulated headset): walking,
  firing, reload and recenter go through it; BF4 polls it only through an XInput device its
  controller scan made (Open / blocked, stalls: `Bf4ControllerScan`).
- XR host `--theater-input` (launcher passes it): gameplay input continues under the
  theater panel. Launcher: experimental features on by default for pre-release (theater
  view is a Candidate), 3D route defaults to the 3D Vision emulation with the launch-option
  note in the window.

## Manual reload research (2026-10-19, static only)

- BF4-MANUAL-RELOAD.md: the reflected weapon data (reload type, timers, threshold,
  capacity, bolt/pump, weapon classes, AnimatedWeaponBinding signals), the native firing
  state machine (`+0x9BFAC0`, ammo transfer `+0x9BC910` at the ReloadThreshold point),
  how every gun maps to the framework's mechanism model, two ways to drive the game's
  own reload, and phases P0-P8.
- Built: `Bf4ReloadClassifier` (gun data -> WeaponMechanismDescriptor; unknowns stay on
  button reload), unit-tested. Not wired to anything.
- Superseded for play by the magazine gesture above (no native observation needed);
  the driver below stays for when the firing object is found.
- Built: input-only reload driver (`Bf4ReloadDriver`): holds fire while a VR magazine is
  out / breech open / reload running / stroke owed, presses the game's own reload, and
  acknowledges only observed ammunition. Tested against an emulated firing state
  machine and the framework's ManualReload coordinator end to end; not wired into the
  core yet (needs the VR gesture side and the player's firing object).
- Built: read-only firing census (developer sessions; ~30 s in, then every ~2 min, and
  research command `census`). It logs every weapon-firing object it finds, limited-ammo
  ones first, and client/server pair candidates, so one live session can answer the
  client vs server question. Emulated end to end; never run in the game. All research and
  probes now stop at any lockdown, and a running census aborts.
- Static answer to the client/server question: every gun has a client firing object
  (client weapon `+0x8`) and a server one (server weapon `+0xA0`); acknowledgements
  should follow the server copy (BF4-MANUAL-RELOAD.md 2.3). Correction found on the way:
  the first client vtable was an abstract base; the live classes are A (`+0x1D467A0`) and
  B (`+0x1D467E0`), both owned by one client weapon entity (`+0x1D46540`: B at +0xA0, A at
  +0xA8, active at +0x98). The census classifies holders, finds the entity and its active
  class, and logs who holds the entity (emulated end to end).
- Open: the live census result; the rest of the ownership chain to the player (R8)
  before any write.

## CI and release pipeline (2026-10-19)

- Pushed to `bf4/vr-adapter`; first GitHub runs green at `440594f`: `build.yml` Windows
  x86 and x64 (all 283/282 suites incl. Bf4Core and Bf4Emulated) and `bf4-emulated`
  (launcher tests on Linux). Fixed on the way: script permissions and line endings, a
  POSIX path bug in the launcher's profile settings resolver, and the emulator's menu
  button assuming a 1280x720 client on runners with small desktops. The release workflow
  (tags `bf4-v*`) has not run: no release is tagged until a headset session passes.

- `bf4-release.yml` (tags `bf4-v*`, so BF4 releases never collide with other games here): windows-2022 like `build.yml`, actions pinned to the same
  commits. Downloads `openxr_loader_windows-1.1.61.zip` from KhronosGroup/OpenXR-SDK-Source
  (asset name confirmed via the GitHub API) and stops unless `x64/bin/openxr_loader.dll`
  hashes to the pinned `a69729a3...0fa5f`; the local loader's provenance is not recorded,
  so the first tagged run is the actual check. Builds `FvrXrHost` too, packages with
  `make_release.py --require-xr-host` (fails without the host or loader) and ships
  `licenses/` (THIRD_PARTY_NOTICES + OpenXR, JsonCpp, MinHook, BFVR texts). Local package
  check from `build/x64`: 23 files, host + loader + notices present.
- `bf4-emulated.yml` / `run_all.sh`: the portable launcher tier only. The old Linux C++
  compile globbed BC2 sources and `tools/wincheck` lacks the theater/XInput/GDI shims, so
  both were dropped; the C++ suites run through `build.yml` (Build.ps1, Windows x86/x64).
- Bf4Emulated prints SKIP for the gamepad check when `xinput9_1_0.dll` is unavailable.

## Merged design (2026-10-18)

One BF4 design now. Kept from the earlier local files: `VrPanel` + `GdiPanelRaster` (mod
menu rendering), evidence levels (now in `Capability`), the XInput pad input plan, the
installed-build facts and camera leads (BF4-ARCHITECTURE.md). Dropped as duplicates:
`Bf4Common.*`, `ReloadMode.h`, `FeatureStatus.h`, the overlay's `LaserPointer` (the host's
framework `MenuPointer` is the pointer), and `BF4-ADAPTER.md`. Originals archived in
`reports/bf4-earlier-design-archive.zip`.

## Tests (2026-10-19)

- Mod menu layout: with more items than one column holds (8 rows on the 1280x720 panel)
  the items go into two columns; before this, rows past the eighth (Recenter, holsters,
  feedback marker in developer sessions) were drawn off the panel and unclickable. Labels
  and notes were shortened to fit half width (GDI text now ends with an ellipsis when it
  does not fit). With more items than two columns of full rows hold, rows get shorter and
  the item fonts scale with them (checked by rendering the 18-item developer menu); the
  dead "Screen distance" entry is gone (the host has no way to receive it yet). Unit
  test: every item on the panel, none overlapping; a GDI render of the developer menu was
  checked by eye.
- Build.ps1 after the gun in the hand (sessions 19-28): x64 284/284, x86 283/283;
  Bf4CoreTests 93 cases, 3480 checks (new: recoil at two frames of delay; research verbs
  `shadernames`/`shaderdump`/`drawprobe`/`fpview`; the "In the hand" request carries the hand
  relative to the head, no turn, no reach limit; no aim dot by default); Bf4Emulated WARP checks:
  shader census, draw probe, first-person view (projection, moved view, pass-through, pixel
  stage, hand by game rotation, receiver tracking).
- Build.ps1 after the FOV-armed guard and the aim trace (sessions 16-18): x64 284/284,
  x86 283/283; Bf4CoreTests 92 cases, 3439 checks (new: death camera with yaw swing,
  replay of the live death trace, inverted look by probing, axis seeding, wide reach,
  `viewdump`/`aimtrace` parsing). The emulated sensitivity test now also changes the fake
  game's FOV with its sensitivity, as BF4 does.
- Build.ps1 after the reach limit, game menus and recenter gesture: x64 284/284, x86
  283/283. Bf4CoreTests 88 cases (new: reach limit, stick chord, game menu in a level,
  both-sticks recenter, reach limit through the core; three aim-loop tests now switch
  "Gun follows aim" off, since their 30 degree aims are beyond the reach).
- Earlier, after the guard fix and the simulated headset: Bf4CoreTests 83 cases (research
  commands, target teleports at a learned gain, split 3D stays off, gun-follows-aim rotation);
  Bf4Emulated pass (theater, world lane, stereo probe, census, lockdown, gestures; WARP:
  eye resampler reticle/markers/vignette/depth stereo/HUD corners, depth capture detour
  incl. the MSAA snapshot);
  launcher 19/19; session summary 2/2; `Run-Bf4TheaterXr.ps1` 5/5 and
  `-World -FrameChannels 2` 5/5 (76-237 pairs, machine-dependent); `-World` with one
  channel 4/4 (47 pairs).
- Installed locally: runtime/bf4 0.4.111-local (the source at this commit: gun in the hand and
  arms from the body by default, no crosshair (centre mask), controller scan with its cache, 3D
  option; rollback 0.4.110-local); launcher settings
  restored to the player's after the developer sessions. Developer sessions still run the
  read-only probes and the firing census.
- Build: VS 18, x64 RelWithDebInfo, `build/x64`. Math types: `fvr/math/Vec.h` extends the
  framework's `StereoMath.h` (`Quat` = `Quaternion`; Pose is `{position, orientation}`).

## Holsters and arm IK research (2026-10-19, static only)

- Holster gestures that select weapons through BF4's own keys are built (above); what
  follows is the native route (hide the gun model, hand IK) for later.
- BF4 can hide the first-person gun itself (`Animated1pOnlyWeaponBinding.HideWeapon1p`)
  and its character IK scene op has hand IK inputs (`InEnableLeft/RightHandIK`,
  `InLeft/RightHandIKReach`, `InHideWeapon`). Plan and unknowns: BF4-HOLSTER-IK-RE.md.

## Next live session

Checklist: [BF4-FIRST-SESSION.md](BF4-FIRST-SESSION.md). Afterwards
`tools/bf4probe/summarize_session.py` turns the adapter log into verdicts (camera,
single-player signals, client/server weapon copies, next ownership link); unit-tested in
`tests/test_bf4_session_summary.py` and checked against the emulator's real log.

## Open / blocked

- Headset check of 0.4.65 (BF4-FIRST-SESSION.md). The simulated headset settled the game
  side (direction, labels, rate, aim, gestures, depth target); what only a headset can
  answer: comfort at ~45 pairs/s (and whether frame sync 1, ~52/s, still holds there),
  gun size and the head-locked gun, the HUD corners' readability, 3D from depth's feel,
  holster positions on a real body, the laser on "Resume".
- Gun in the hand (above): the remaining gaps are the ejected casings (drawn at BF4's own gun
  position, session 63), the grip at rest measured on one rifle (the underbarrel launcher looked
  right in the hand, session 41; it is drawn as one skinned mesh, so receiver tracking has no
  rigid part to follow while it is selected), and no arm IK. Reloads animate at the hand (session 41). Static leads kept for that (2026-10-19/20): the weapon offset data
  (`WeaponOffsetData`, `FirstPersonCameraData` `Offset`/`WeaponBaseOffset`) are shared assets,
  not to be written; `WeaponLagBinding` outputs only pitch/yaw/roll results (the lag is a
  rotation, no offset); skinning palettes reach the GPU as bone x inverse-bind matrices (b0
  `boneVectors`, 60 bones of 3 rows), so joint positions need the bind poses.
- Update rate: 52.9 pairs/s with three frame channels at frame sync 2 (the game allows ~59
  there: 2 Presents per pair). Frame sync 1 (label-checked clean at 118 fps) could double
  the game's limit, but BF4's main thread may run a frame ahead when GPU-bound, so 2 stays
  the default until a headset session shows otherwise.
- 3D: BF4's split route is unsupported (above); 3D from depth is the stereo route. The
  NVAPI route (sequential eye passes, emulated) needs a Steam launch option to test live.
- Laser click on "Resume": fixed on the game side. Live on the simulated headset
  (0.4.53-local) the VR laser aimed at RESUME on the campaign screen pressed and released at
  the same pixel (236, 297) and the mission loaded; the mod menu opened with the menu button
  and a laser click toggled an item. Earlier report: it did not register while other
  menu clicks did. Cause assumed: no click lock, so hand jitter while the trigger was held
  went out as a drag (that screen scrolls on drags), and the trigger also reached the
  virtual pad. Now the cursor holds its press position unless the laser moves more than
  6 % of the screen, and the pad's triggers are released while the laser points at the
  screen outside campaign play (unit-tested; live check pending; clicks are logged).
- Holsters: key 2 (sidearm) not yet seen working live (the prologue loadout has none: key 2
  did nothing again in session 19; HUD slots 3, 4, T, V).
- 10 s stalls: cause found (session 31, 2026-10-20). A 4.5 s stall in a running level, the
  game in front; the watchdog's stacks show BF4's main thread (+0x128a6 -> +0xea3e ->
  +0x626b3e, its timed-message dispatch) in `IDirectInput8::EnumDevices(GAMECTRL, attached)`
  (+0x94B6E0, on message hash 0x91F1A519; hooked by the Steam overlay) whose callback
  (+0x946830) runs the well-known `IsXInputDevice` WMI scan: `CreateInstanceEnum
  ("Win32_PNPEntity")` (+0x94A208, also through the Steam overlay) and `Next` with a 10,000 ms
  timeout, blocked in RPC to the WMI service. So BF4 itself stalls whenever it re-enumerates its
  DirectInput controllers, once per attached controller; not the VR code. A full
  `Win32_PnPEntity` scan takes 5.5 s on this PC (309 devices, with Oculus, Virtual Desktop and
  ViGEm virtual-pad buses). Developer sessions log every WM_DEVICECHANGE at the game window
  (`device change: event 0x...`): session 32 showed a burst of ~20 (DBT_DEVNODES_CHANGED and a
  device arrival) and BF4 froze 11 s right after it, the same WMI stack.
  Fixed (`Bf4ControllerScan`, sessions 35-40), replacing a first fix that broke the VR pad:
  session 34 answered BF4's game-controller enumerations with no devices, and the stall was gone,
  but BF4 makes its XInput devices only from those enumerations (`+0x946690`: every device its
  check accepts becomes an XInput device, user index = the count so far; the update `+0x94C2B0`
  then polls `XInputGetState`), so it stopped polling the pad: session 35 walked and fired
  nothing. Now:
  - BF4's `IsXInputDevice` (`+0x94A090`, binding `Input.IsXInputDevice`, a unique prologue
    pattern; the `Next(10000 ms)` call checked at +0x1A7 before hooking) is answered without
    WMI by a MinHook: raw-input device names (the same `IG_` and VID/PID test), plus Steam's
    virtual pads. Live: under 0.01 ms per check (was 5.5 s).
  - Steam's virtual pads: inside BF4 the Steam overlay lists 8 "Controller (XBOX 360 For
    Windows)" pads (VID 28DE PID 11FF) through DirectInput (a plain process on this PC sees no
    game controller at all) and answers BF4's WMI scan for them with XInput IDs; they are no
    raw-input devices. BF4 made them its XInput devices, the first one user 0, which is how the
    VR pad reached the game all along. The check now says the same for them.
  - The enumerations pass through (DirectInput's own time 214-275 ms per enumeration, live) and,
    when none of the devices is an XInput pad, a stand-in Xbox 360 pad is offered first (the VR
    pad on a PC without Steam's pads or any real pad). First, because BF4 polls one pad: offered
    after eight DirectInput pads it was made an XInput device but never polled (session 37);
    first, it is polled (session 40, Steam's pads as DirectInput pads); alone, it is polled
    (session 39, Steam's pads hidden).
  - The XInput sink keeps user 0 connected while the adapter runs (a neutral pad when idle and
    no real pad answers), since BF4 checks the connection when it makes the device.
  - One device change is posted to the game window ~600 frames after adoption if BF4 has not
    enumerated by then (it has, live: two enumerations ~8 s after start).
  - After BF4's first enumeration it is answered at once from the last list, and a worker
    thread enumerates again on its own DirectInput (DirectInput itself takes ~210-280 ms here,
    which BF4's main thread waited before); a changed list replaces the cache and the worker
    tells BF4 of a device arrival, so it enumerates the new one. BF4 enumerates on a device
    arrival with a device-interface record: live, a sent DBT_DEVICEARRIVAL did,
    DBT_DEVNODES_CHANGED and DBT_DEVICEREMOVECOMPLETE did not (Windows refuses the record posted,
    so it is sent, bounded at 200 ms, from the worker). Live (sessions 54-55): three arrivals,
    each answered from the cache in 0.0 ms on BF4's thread, the worker re-enumerating behind
    (no change); walking before and after. Research verb `devicechange [event]` (hex, default
    8000) sends one; log `controller_scan ... cached= refreshes= changed=`. The "list changed"
    path has not happened live (no device came or went).
  Live (session 38, default): BF4 polls user 0 ~150 times a second, all answered by the VR pad;
  the left stick walked 5.2 m, the trigger fired bursts; level-load device changes made two
  enumerations of 214-274 ms, no stall. The class vtable entries (in front of the Steam overlay's)
  go back on detach while they hold ours; the code hook goes with the detach worker's MinHook
  removal (exact bytes). Log: `controller_scan ... code_hook= enumerations= devices=
  with_xinput_pad= stand_ins= checks=accepted/all enum_ms=`, `virtual_pad ... user0_polls=
  states_by_user= caps_by_user=`; developer sessions log each enumeration's devices and each
  check (first 64 lines). Developer experiments (files in the research folder):
  `controller-scan-hide-steam`, `controller-scan-steam-as-di`. Emulator check on this machine's
  dinput8 (device paths, the stand-in ahead of DirectInput's devices, checks answered without
  the routine's body, Steam's pads per mode, off passes through, removal restores entry and code).
  Earlier notes:
- 10 s stalls (no Present), twice now in a running level (sessions 12 and 15), each
  starting as one of my test commands began after a pause. Session 16+ logs the foreground
  owner at stalls and at every change of the game window's focus, to tell a focus thief
  from an in-game wait. Sessions 16 and 18 stalled only during checkpoint reloads, with the
  game in the foreground (loading), so those are not the in-level stalls; no in-level stall
  recurred. First one: with the mod menu open; a laser click
  during it was lost, because the adapter runs per Present. Not repeated in 20 s with the
  menu open. The watchdog's first stack (the window thread, under the Steam and EA overlay
  hooks) is BF4's ordinary blocking `GetMessage` loop at `+0x110a2` (decoded from the
  image), so it does not explain the stall; the frame-producing thread has to be found
  among the others (one slept at `+0x21d86d`). Cause unknown; if it recurs, compare the
  watchdog stacks with a normal frame and with the overlays disabled.
- The upward reach of the turned-render option ("Gun follows aim: On") with Quest-like eyes
  is small (~5 deg with the head level, ~10-15 deg sideways; "Wide reach" ~10 deg); the default
  gun in the hand has no reach limit. A lens shift (projection window) would have avoided the cost, but
  BF4 sets that window from its tiled-screenshot state every frame (BF4-CAMERA-RE.md).
- The census holder chain matched nothing live (27 firing objects, 0 holders): redo the
  walk from the live firing vtable `+0x1D53F18`.
- Mod menu choices are kept between sessions (2026-10-20): the enabled choice and toggle items as
  "id value" lines in `runtime/bf4/menu-settings.txt` (written when they change, checked every
  120 Presents; set back at adoption), except the reload mode (the launcher's setting) and the
  developer items. Research sessions use `research/menu-settings.txt` instead, so testing never
  changes the player's choices. Core test (written, read back in a new core, the launcher's and
  developer items left out); live (sessions 59-60): Snap 45 and vignette off saved, restored.
- Mod menu in VR: opens with the menu button, drawn by the adapter (VrPanel + GDI) on the
  theater lane; live on the simulated headset it opened in a level and a laser click
  toggled an item. Not yet seen in a headset.
- Corner HUD outside the eyes (frame shape, above): insets are a workaround; the real fix
  needs the UI pass rendered separately (native RE).
- Arm IK: built for the shoulders (Arms from the body, above); open: the left hand on the left
  controller when it is off the gun, and rigs other than the prologue's (their joints).
  Earlier research notes follow. BF4's own hand IK inputs: BF4-HOLSTER-IK-RE.md. How it looks (live sim, session 56): the gun turned 40 deg left reads
  as plausible arms from below; turned 60 deg right, the left (support) arm's sleeve swings across
  the lower middle of the view. Leads (2026-10-20, developer): the draw probe now writes each
  first-person skinned draw's bound palette (`palette=[...]`, 3 rows per bone; only the bound
  constants: BF4 declares 60 bones and binds fewer data past them); the rig is several
  meshes (prologue: L77 34695 and 6369 indices, drawn three times; L108 23178/2370; L98
  22461/717/2370; L631/L738 7416/19431 in other passes), the largest with ~50 distinct bones of
  60, many matrices shared between meshes. Joints can be solved from captures as the points two
  bones map alike in every pose (scratch `fp_joints.py`); three captures pinned hinges only to
  lines. Session 58: 18 captures (idle, walk, sprint, a whole reload, aiming in and out, the
  underbarrel launcher; local research data, not in the repository). Switching to the launcher
  replaced the two meshes drawn by the weapon's G-buffer shaders (23178+2370 and 22461+717+2370
  indices -> 15762 and 14958 + 804), so those are the weapon; the arms are among the others
  (19431 + 7416 in two G-buffer passes, 14217, and the depth-only 34695 and 6369, whose
  6369-index part stays through the weapon switch). With 17 captures most bone pairs of the
  19431 mesh solve to a well-conditioned shared point. Session 61 (the gun at BF4's own place,
  so the probe reads BF4's first-person view; scratch `fp_joints2.py`): in BF4's first-person
  camera space the 19431-index mesh is the left arm: its hand and finger joints at the handguard
  (x 0.0, y -0.21, z -0.42; moving ~21 cm through a reload), a joint at (-0.09, -0.36, -0.25)
  (the elbow, likely) and one at (-0.08, -0.19, 0.0) (the shoulder end, moving ~19 cm with
  sprinting); the 6369-index mesh is the right hand at the pistol grip (wrist ~(0.10, -0.21,
  -0.17)). Left for IK: the right arm's chain, recognising arm meshes in general (index counts
  differ per mesh), learning their joints while playing, the two-bone solve and the palette
  rewrite. Not built.
