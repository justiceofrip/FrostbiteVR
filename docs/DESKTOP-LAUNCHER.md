# BC2 VR desktop launcher

The desktop application is an interpreted PowerShell 7.5+ / WPF application.
It does not compile the mod, run diagnostic fixtures, provision weapons, or
publish releases when the player presses Launch. Windows and an installed
licensed copy of BC2 are required. SteamVR/Steam Link still need a connected
headset. This is an unreleased singleplayer preview, not campaign acceptance.

## Installed local version

On the development PC, **BC2 VR** on the desktop points to
`G:\Unity\VRMMO\BC2VR-Launcher\Start-Launcher.ps1`. Version 0.1.2 is installed
there, without another copy of the native binaries or game-derived assets.
The shortcut uses the existing PowerShell 7.5 runtime. Removing that runtime
requires installing PowerShell 7.5+ and recreating the shortcut.

The local profile references the validated normal412 quick launcher under
`../bc2vr-recovery/overnight412/headset-launch412-private`. Launch starts SteamVR
if needed, then uses that pinned path to start BC2, continue the saved campaign,
wait for the level and attach the normal VR session. Existing active sessions
are not duplicated. No synthetic controller driver is enabled. The game window
continues to use the existing left-monitor recovery.

- **Launch campaign in VR:** connect the headset first. The game starts if it
  is closed; an existing campaign can be attached.
- **Restart BC2 before launch:** normally close the old game, then continue
  campaign with a fresh process. First use Stop VR if a session is active.
  A previous injected process needs a restart; it is never force-killed here.
- **Stop VR:** use the existing session's identity-checked graceful stop path.
  BC2 and SteamVR remain open. The injected module stays mapped until BC2 exits.
- **Check setup:** verify the pinned build/dependencies and free-space reserve.
  It does not prove the headset is connected or an eye image is being presented.
- **Open logs:** opens `data` beside the installation. `launcher-last.log` and
  `operation.json` are overwritten for each operation. Native session evidence
  stays in the original session folders; the launcher does not duplicate it.
- Closing the launcher after startup leaves VR running. Closing during a worker
  operation minimizes the window until it completes, preserving its output pipe.

Native session storage limits remain unchanged: 40 GiB generated output limit,
512 MiB raw capture limit per run and 15 GiB free plus session working space on
the output and temporary volumes. The launch path now inventories this large
development tree once and hands that admission to the three known startup
stages. Each stage is single-use, bound to the same invocation/process ancestry,
expires after 180 seconds and rechecks current free space. Standalone/native
build calls still perform fresh inventories. See [storage budget](STORAGE-BUDGET.md).
The complete inventory improved from 27.08 to 7.50 seconds in the local timing
check, and real child handoff checks took 0.62 seconds. These measurements do not
establish a new cold launch time or headset image.

The user's C: drive is reserved for the OS. Since version 0.1.1, process-local
TEMP/TMP/TMPDIR to G: before any worker starts; native admission further uses
the repository's G: build/scratch. Children inherit the real paths. The former
requirement to free C: was unnecessary and is removed; actual G: headroom and
generated-output limits remain enforced. No Windows/user environment settings
were changed. Installers require choosing a data-drive location and refuse the
Windows drive instead of defaulting to LOCALAPPDATA.

## Updates

The feed is fixed to `justiceofrip/FrostbiteVR`. Check for updates reads the
latest 30 GitHub release records, including prereleases when the checkbox is
enabled. Source-only checkpoints are skipped. No compatible release was
published at verification time; nothing was uploaded by this task.

Each installable release must attach both:

1. `bc2vr-update.json`, schema `fvr.desktop.release.v1`.
2. `BC2VR-X.Y.Z-win64.zip`, exactly matching its size and SHA-256.

The manifest contains `version`, `asset`, `bytes`, `unpacked_bytes`, `sha256`.
The zip has `package.json`, `app/Launcher.ps1`, `app/Worker.ps1`, and
`app/Launcher.Core.ps1`. Each file is listed by path and SHA-256 in package.json.
The package schema is `fvr.desktop.package.v1`; its `runtime_entry` is either
null (launcher-only update preserving the user's local runtime profile) or a
verified file below `runtime/`.

Downloads are restricted to HTTPS GitHub release hosting, including every
redirect. Asset origins must belong to this repository. The updater rejects
path traversal, links, duplicate paths, unlisted files, excessive expansion,
wrong hashes and incorrect sizes before selecting the package. The trusted
origin is the GitHub repository/release account; a hash in that same release
is an integrity check, not an independent publisher signature.

BC2 and the mod must be closed for installation or rollback; SteamVR may stay
open. A per-installation lock prevents concurrent launcher operations. Downloads
and staging stay on the installation volume. The active-version record is
atomically replaced only after verification. Failed verification leaves the
current version selected. Updates retain current and previous versions, remove
their own staging/download files, and preserve settings and the local runtime.
Rollback verifies the previous package before selecting it. Reopen the launcher
after update/rollback so UI and worker code come from the same version.

Limits: 512 MiB download, 1 GiB expanded package, 12,000 zip entries, five HTTPS
redirects and five minutes per download. Persistent version storage is bounded
to two versions; game saves, captures and development trees are never pruned by
this updater. Interrupted-process leftovers are addressed in the recovery note
below, rather than silently deleting arbitrary folders.

## Creating an installer/hotfix

From a reviewed source checkout:

```powershell
pwsh -NoProfile -File tools/Build-DesktopLauncher.ps1 -Version 0.1.1
```

The builder calls the repository storage guard before writing a package, with
actual scratch routed to the repository's data drive. A source installation
using `Install-BC2VRLauncher.ps1 -FromSource` copies only interpreted text files;
normal updates use the verified versioned zip transaction.

The output includes the zip, update manifest, `Install.cmd`,
`Install-Package.ps1`, `Install-BC2VRLauncher.ps1`, `Start-Launcher.ps1`, and
`Launcher.Core.ps1`. Distribute those installer files together. `Install.cmd`
checks for PowerShell 7.5+ and asks for a data-drive folder, then installs there
with a desktop shortcut; it does not require admin rights. PowerShell
itself is a prerequisite, not silently downloaded. For a custom location, invoke
the installer with `-InstallRoot`, `-PackageZip`, and `-ManifestPath`.

To include a future reviewed native hotfix, supply `-RuntimeRoot`,
`-RuntimeFiles` (JSON array of explicitly reviewed relative filenames), and
`-RuntimeEntry`. The builder never walks the whole development tree. Known game
archives, meshes, game executable, local configuration, asset caches, recordings,
PDBs and dumps are rejected. Review the allowlist regardless of extension.
Required third-party license files must be included by that reviewed runtime.

The runtime entry is a PowerShell adapter with this contract:

```powershell
param(
    [ValidateSet('Launch','Stop','Validate')][string]$Action,
    [string]$SettingsPath,
    [string]$StateRoot,
    [switch]$Fresh
)
```

The adapter owns native prerequisites, supported executable checks, process
identity, graceful stop, campaign startup, bounded output, and owned-game asset
extraction/cache creation. It must throw on failure, return after startup, and
put persistent state/cache outside its immutable version directory using
StateRoot. User game_path is in SettingsPath. It must never silently enable
synthetic diagnostics or treat a controller publication as gameplay proof.

**Not yet a portable public native runtime:** the current personal 410 path
uses private, absolute-path Continue helpers, a Python runtime and locally
extracted body caches. Those are intentionally not smuggled into the updater
package. A reviewed portable runtime adapter/dependency closure and clean-machine
install test remain necessary before advertising a public one-click BC2 mod
installer. Launcher-only updates work with the installed local profile today.

## Recovery and validation

On a force-terminated update, active.json still points to the last committed
version. Do not manually remove that version. Abandoned `staging-*` and
`download-*.zip` entries are private installer output, but inspect them and
ensure no launcher worker is running before removal. Never remove `data`, game
files, or the referenced local runtime. The next release can add age-bounded
orphan cleanup; ordinary successful/failed operations already clean their own
staging/download files.

`tests/Test-DesktopLauncher.ps1` verifies package transaction, rollback, retained
versions, preserved settings, concurrency, archive rejection and active-game
blocking. The UI smoke path exercises its actual Check setup button/worker and
renders a preview, without starting the game. Public feed lookup was exercised
against GitHub and reported no compatible release. Native binaries and hooks
were not changed or rebuilt for this task. Full game/headset launch remains
untested for this desktop UI. The previous unused C: reserve blocker is fixed.

GitHub release API reference:
https://docs.github.com/en/rest/releases/releases#list-releases
# Installed412 follow-up

Personal launcher0.1.2 now points to frozen normal412 binaries. Its installed
worker/dependency validation passes without launching BC2 or SteamVR. Previous
launcher0.1.1 and exact settings backups are retained. The new release package
is launcher-only (12,957 bytes), not a public native runtime installer. Reopen
the desktop launcher to load the new app files. A resident previous native DLL
requires the existing **Restart BC2 before launch** option; the update does not
silently close a running game.

Launch storage inventory now runs once, using restricted one-use handoffs for
the verified helper, session supervisor and trace stage. The full inventory
measured27.08s before and7.50s after; child checks took0.64s. End-to-end game/XR
startup has not been remeasured. See [412 notes](CHECKPOINT-412.md).

