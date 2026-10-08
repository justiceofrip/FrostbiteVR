# Frostbite VR: Bad Company 2 single-player preview

This is an unpublished work in progress. User tests have demonstrated native
stereo, tracked weapons/support hands, roomscale movement, recentering, the pause
menu, manual shell/magazine insertion, chest ammunition and boat head aim/fire.
The latest headset run accepted pickup recovery but still reported automatic
reload on empty SPAS/AEK weapons and an unspecified feeling of instability.

The shared empty-reload guard now includes the missing shell path. Separate
monitor checks verified SPAS and XM8 stayed empty after firing all loaded rounds.
AEK's intermittent cause remains unproven. The next focused headset test checks
empty firing, deliberate manual reload and transitions through pickup/holsters.

This combined source passes 166 C++ suites on each architecture and 695 Python
tests. Its additional weapon data, body display batch, horizontal AEK sight and
vehicle-reticle work are offline candidates. New native weapon registrations,
the GP30 sight and reticle producer remain disabled. No exact-package headset
acceptance or complete campaign compatibility is claimed. See [FEATURES.json](FEATURES.json)
and [HEADSET-TEST-CARD.md](HEADSET-TEST-CARD.md).

This preview renders native stereo game views through an x86 BC2 module and a
separate x64 OpenXR host. It supports the tested 32-bit executable and Direct3D 11
renderer. You need your own installed game; no game executable, assets or saves
are included. Multiplayer is not implemented.

The current default launcher enables tracked weapons/support hands, launcher
sight interaction and SPAS shell reload. XM8 magazines, physical body inventory
and boat head aim/fire still use explicit development switches. Physical weapon
slots are the intended normal controls; thumbstick selection and the legacy
pouch are development fallbacks while supported-item coverage grows.

## Setup

1. Extract the entire preview into a writable folder outside the game installation. Keep its directory layout.
2. Use 64-bit Windows 10 (version 1703 or later) or Windows 11, Direct3D 11, a working OpenXR headset runtime, tracked controllers, and Python 3.9 or later for desktop window recovery. The Windows minimum follows the helper's [DPI API requirement](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setprocessdpiawarenesscontext); your headset runtime may require a newer version. SteamVR/Steam Link was used for headset checks; other runtimes and controller profiles are not certified. Python is not bundled.
3. Open PowerShell in the extracted folder and configure the installed game path:

   ```powershell
   .\Setup-BC2VRPreview.ps1 -GamePath 'D:\Games\Battlefield Bad Company 2'
   ```

   Setup reads and checks the executable, then creates only `config\local.json` inside this preview. It does not patch or copy files into BC2. Only the executable hash recorded in `FEATURES.json` is admitted by this preview setup; a different hash needs compatibility verification.
4. Start BC2 normally on the monitor, select its Direct3D 11 renderer and a windowed 16:9 display, and load campaign gameplay. The current launcher does not load a mission or create a VR main-menu environment for you. Then connect the headset and select a working OpenXR runtime.

If PowerShell blocks scripts, use your normal per-process script policy or review and unblock the extracted scripts. This package does not alter system execution policy, runtime registration, game settings or save data.

## Launch and stop

```powershell
.\Start-BC2VRPreview.ps1
```

Keep this terminal open. The launcher prints a unique session folder under `reports`. If Python is not on PATH, use `-Python 'C:\Path\To\python.exe'`. The game may be placed on the left monitor without activating its window. Python recovery failure is logged and does not itself abort native cleanup.

You can check the packaged desktop helper without opening the game or changing any windows:

```powershell
python .\tools\game_window.py --preflight
```

Normal window recovery uses Python's standard library and the included `read_bc2.py`. It reads the installation path from this package's `config\local.json`. Pillow is only needed for the optional developer screenshot command; launch and stop do not require it.

To stop only this mod session, open another PowerShell window in this preview folder and use the session folder printed at launch:

```powershell
.\Stop-BC2VRSession.ps1 -ReportFolder '.\reports\native-xr-session-<timestamp>'
```

Stop requests a graceful host exit. Let the launcher finish collecting native cleanup evidence before launching again. BC2 and SteamVR remain open. Closing BC2 also ends the host. Do not rebuild/replace the package while a session is active; after an update, restart BC2 because loaded native DLLs remain resident until the game exits.

## Controls

| Action | Touch-style controller input |
| --- | --- |
| Move | Left thumbstick |
| Snap turn | Right thumbstick left/right |
| Fire | Right trigger |
| Interact / enter or exit a seat | Left primary button (X); release between presses, including after a seat change |
| Crouch | Left secondary button (Y) |
| Sprint | Left thumbstick click on its own |
| Jump | Right primary button (A) |
| Ordinary reload where available | Right secondary button (B) |
| Stow / draw a supported weapon with BodyInventory | Release right grip, reach its assigned shoulder/back zone, then squeeze right grip |
| Development weapon-selection fallback | Right thumbstick up/down; return to center between selections |
| Support grip | Left grip near a supported weapon contact; release to let go |
| Recenter | Hold both thumbstick clicks for one second, then release |
| Pause menu | Left menu button; if reserved by the runtime, hold X+Y for half a second. Keyboard Escape also works. Aim the menu laser and use trigger to select. |

For the SPAS, collect a shell from the legacy left-side ammo pouch (about 23 cm left and 55 cm below the head) with the free hand, move it to the **bottom loading port**, then push inward along the magnetic guide. The experimental body-inventory switch offers both recentered chest and belt contacts on one supply: chest about 16 cm left, 25 cm below and 10 cm forward; belt about 23 cm left, 55 cm below and 2 cm forward. The action is complete only when the native ammunition count changes; a nearby shell or capture tap alone does not prove a reload. The game remains responsible for firing and ammo accounting.

Left trigger is not a manual ADS bind. Physical aiming works on supported optics; 2D zoom overlays from sniper/launcher ADS are not a completed physical-scope feature.

Optional tests are explicit:

```powershell
.\Start-BC2VRPreview.ps1 -ExperimentalMagazineReload
.\Start-BC2VRPreview.ps1 -ExperimentalBodyInventory
.\Start-BC2VRPreview.ps1 -ExperimentalMagazineReload -ExperimentalBodyInventory
.\Start-BC2VRPreview.ps1 -ExperimentalBoatHeadAim
.\Start-BC2VRPreview.ps1 -ExperimentalBoatHeadAim -ExperimentalBoatHeadFire
```

With `-ExperimentalBodyInventory`, SPAS and the exact scoped `XM8_sp_s` can
stow and draw through the same physical-slot system. Automatic stow requests
empty hands on initial on-foot entry and observed vehicle exit, but still needs
current native suppression and paired hide confirmation. The SPAS/XM8 loadout
prefers SPAS on the right shoulder and XM8 on the left. There are no visible
stowed weapon meshes yet. Combined headset behavior and actual exit-to-empty
hands still need verification.

With `-ExperimentalMagazineReload`, positive partial XM8 magazines can be returned
unchanged while held, or replaced using the ammo supply. Full-magazine removal
and same-original return are now enabled too: monitor run 001346 completed a
30/183 return with no ammo change or native reload. Its interaction receipts
passed, but the strict observer audit remains incomplete because of two missing
after-boundaries and three record-lock drops. Full-magazine headset handling is
unverified. The positive-loaded, zero-reserve route is implemented but has no
native or headset acceptance. Releasing a removed magazine does not create a
persistent world drop or stored magazine.

Physical removal from an empty weapon remains disabled. Ordinary B reload is
preserved only after a fresh matching idle-empty observation with no active or
retiring VR cycle; uncertain state cannot bypass ownership. That fallback has
deterministic coverage, not live-game acceptance.

These switches do not indicate headset acceptance. Boat head aim is limited to the observed PBLB driver/front-gun relationship. Use head direction to aim and the right trigger to fire with both boat switches enabled; aim-only mode leaves firing disabled. Monitor checks verified camera response and visible firing, but headset comfort, combined steering and other vehicles remain unverified.

## Evidence and remaining limits

Scoped-XM8 monitor run 015859 verified hide/free hands/show, restored GunHold and
ordinary post-draw firing. Counts changed 22/191 to 20/191; all three native
copies recorded both shot events and the server supplied both muzzle receipts.
The first shot has matching client/server effects; the second lacks a recorded
client effect. Positive native firing and incomplete effect pairing are separate
verdicts. Both-eye samples were reviewed; headset acceptance of this admission
remains pending.

SPAS pause/resume run 022310 showed fresh input rearming, a new hide request and
both free hands after resume. Root reviewed both eyes and the desktop. Its 31-check audit passed with 240 pairs,
zero timeouts and 15.240 seconds of post-detach stability. Final Recovering state
followed source withdrawal and session stop; this does not prove exact packet
expiry. No checkpoint restart, death or headset action was tested.

Historical October 2 monitor checks repaired a specific teardown fault and
completed two controller-free checkpoint replacements (240 pairs, 11 loading
timeouts, clean detach, 15.221 seconds observed afterward). A later feature-enabled
neutral-input run also completed two replacements (240 pairs, 288 timeouts,
including 277 boat waits; clean detach, 15.231 seconds afterward). Those dated
receipts do not establish a fix for the user's later headset restart/death
blackscreen, active reload transitions or current package frame pacing.

Ground pickup and temporary-held firing have portable groundwork but their BC2
native adapters remain disabled. Manual pump/bolt cycling, all-weapon reloads,
full-body IK and multiplayer are unfinished. The floating optic red dot, some
LOD artifacts and animation polish remain open.

## Prepare local chest and back geometry

With Python 3.10 or newer installed, run this manually from the extracted preview folder after setup:

```powershell
.\tools\Prepare-BodyAmmoAssets.ps1 -Python python.exe -GamePath "D:\Games\Battlefield Bad Company 2"
```

Use your installation path. The helper reads installed archives and writes `build/body-ammo-assets.fvrprop` locally. Keep this generated cache private; do not redistribute it. Preparation does not launch BC2 or enable rendering and does not establish in-game visibility. Setup and launch defaults are unchanged.

## Troubleshooting and removal

If the headset stays in its runtime home, inspect the current session's `runtime.stderr.log` and `session.json`. A loaded campaign, the configured executable path, working OpenXR runtime and no other active BC2 mod session are required. After removing/reconnecting the headset, let tracking stabilize and recenter. If the game or mod remains in a bad state, stop the mod and restart BC2. The reported restart/death blackscreen is unresolved; retrying a checkpoint is not a verified repair.

Reports are local diagnostics and can contain file paths, process/module information and game images. Review them before sharing. They are never automatically uploaded. Package manifests list the exact distributed bytes; they are integrity records, not a digital signature or proof of headset acceptance.

For removal, stop the mod, close BC2, and delete the extracted preview folder when you no longer need its reports. The launcher does not install a service or replace game files. Preserve any reports or local configuration you want to keep before deleting that folder.
