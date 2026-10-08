# Single-player release target

Updated: 2026-10-01T12:30:41.197952+00:00. User direction: release a single-player build first,
then potentially develop multiplayer. This is a preparation plan, not a release
announcement, publication instruction or claim that the current candidate is ready.

## Current accepted increment — October 1

The user accepted the repaired native pause menu and controller laser in
session171944. Exact payload and feedback are saved in
[menu acceptance](../reports/menu-headset-accepted-20261001-171944.json).
This closes the current pause-menu failure; VR-first startup, settings, campaign
transitions and clean packaging remain distinct requirements. User requests
continued implementation toward launch, including active manual reload work.

## First release scope

Prepare a clearly bounded campaign build for the verified BC2 executable and
rendering path. Publish a support matrix for exact tested weapons, modes and
interactions rather than implying every campaign item is already calibrated.
Preserve the working stereo, recenter, tracked SPAS/XM8 handling and launcher
interaction while completing stability and normal gameplay checks.

Ordinary reload must work when ammunition is available. Physical shell handling,
pumping, bolt manipulation and belt-fed steps can be enabled family by family
after native bindings and headset validation. Their portable groundwork must not
be advertised as enabled reload features or block an otherwise useful scoped build.

## Requested comfort and launch experience

Added from the user's October1 requests. These are backlog items, not implemented
features or authorization to publish a GitHub release now. Current reload and
interaction work remains the priority.

- [ ] **BC2-themed mod installer for the GitHub release.** A polished BC2-themed
  interface for game detection/path selection, prerequisites, install/repair,
  version/status, launch and uninstall. Preserve game files and settings with
  explicit backups and recovery. Validate the actual packaged payload through a
  clean install, repair and removal before release.
- [ ] **VR executable opens into a BC2 VR menu environment.** Desired flow:
  launch the mod â†’ enter a BC2 environment in the headset â†’ use the main menu â†’
  start/continue campaign. Users should not need to load campaign on the monitor
  before launching VR. Reuse installed BC2 content/native rendering where the
  adapter supports it; distinguish the environment from the current pause-menu
  quad. Requires game bootstrap, frontend/title ownership before a soldier
  exists, controller/laser input, campaign/load transitions and recovery.
- [ ] **In-headset VR settings menu.** Accessible from the startup environment
  and pause flow. Provide comfort/control settings, recenter and height options,
  with persistent configuration, defaults and clear feedback. Exact options are
  to be designed against implemented capabilities; unsupported options stay out
  of the user interface. Reuse shared XR/settings/pointer components so future
  Frostbite ports can supply their own styling and native bindings.

Current native pause-menu open/close and pointer Resume checks are groundwork
for this flow, not completion of the VR startup environment or settings menu.

## Evidence needed before packaging a candidate

- Repeatable startup, shutdown and recovery with Steam Link/SteamVR reconnect,
  headset removal, recenter and tracking loss; controls documented accurately.
- Campaign load, checkpoint transition and death/respawn checks. Investigate
  reproducible failures with a baseline comparison; do not assume a previous
  death crash was caused by the mod or dismiss it without evidence.
- Stable world stereo and controller/gun/shot alignment on supported items.
  Preserve timing outliers and known visual limitations in release notes.
- Support-grip acquisition, intentional release, native reload with available ammo,
  firing/cycling and launcher transitions. The latest headset run accepted the support-loss repair; launcher sight
  handoff still needs headset validation. Preserve earlier drop evidence and
  explain any recurrence rather than clearing historical failures.
- Explicit, tested behavior for unsupported weapons, vehicle/mounted states,
  menus and unavailable tracking. Do not promise a fallback before testing it.
- Package only project binaries/configuration/templates/documentation required
  to launch the mod. Include setup, controls, supported build, known limitations,
  recovery/removal instructions, version and exact payload hashes. Do not bundle
  game assets, executable, machine-local config or credentials.
- A clean-install/package smoke test and a bounded user headset acceptance run
  against that exact payload. Portable tests or a native monitor check do not
  substitute for headset acceptance.

## Work that may follow the first scoped build

Physical inventory and empty hands, broader manual reload families, detailed
finger and weapon animation, physical magnified scopes, full-body visibility and
additional weapon polish remain separately gated features. Prioritize them by
their effect on campaign usability rather than requiring all of them at once.

Multiplayer is a later phase. Retain the
[visualizer/network research](FLATSCREEN-VISUALIZER-NETWORK-PLAN.md) and
[Frostbite porting plan](FROSTBITE-PORTING-ROADMAP.md), but do not add a server or
network dependency to the single-player build. Gameplay authority, compatible
hosting and remote actor/pose bindings need their own evidence.

## Coordination

[Agent status](AGENT-STATUS.md) records completed and current assignments.
Workers develop independent portable components or offline tools; the coordinator
reviews integration and owns live game testing. Hand ownership is now integrated into existing support/sight interactions and
monitor-validated; see HAND-OWNERSHIP-BC2-20261001.md for the remaining headset and
delivery limits. Feed prerequisites and optic inventory remain foundations.
No publication or multiplayer implementation occurs as part of this batch.
