# Tracked muzzle firing — October 1, 2026

Status: opt-in campaign prototype implemented and native-verified for XM8_sp_s and

SPAS12_sp. No headset retest or downstream projectile-impact measurement yet.

Death-crash and LOD work remain explicitly deferred.

## Behavior and launch

`Start-BC2VRSession.ps1 -MuzzleFire` enables tracked hands, body follow, controllers

and translated muzzle placement. `Start-BC2VRTest.ps1 -MuzzleFire` is the bounded

headset equivalent. Ordinary `-Hands` retains its previous firing behavior.

No game installation files or global desktop input are changed.

The final native client firing-effects and local-server shot matrices receive the

same tracked muzzle position for the same native shot seed. Only twelve XYZ-origin

bytes in each caller-owned output change. Native orientation, recoil/spread basis,

SIMD padding, matrix inputs and animation sources remain untouched. Native code

continues projectile creation exactly once. Other firearms/special shot paths,

nonlocal ownership, vehicles and unavailable/expired tracking use native behavior.

This is not multiplayer coverage or impact/collision acceptance.

## Shared framework and adapter boundaries

PlaceFireAtMuzzle replaces the origin AFTER BC2 composes its authored ShotConfig

rotation/translation. Applying at the earlier builder would double the muzzle

translation. Rotating by an animated weapon delta would add idle/equip/recoil tilt

on top of native aiming; the discarded candidates were observation-only.

FiringPoseHistory retains the first valid published muzzle for a verified event.

It uses opaque actor/owner/equipment/space/event keys and original monotonic

tracking deadlines. Expired events never acquire a newer pose. Weapon/owner/space

changes and invalid tracking invalidate history. Its fixed 32-slot storage is

separate from diagnostic logging; writes continue after the log fills. It has no

BC2 addresses, OS APIs or native memory writes. The adapter serializes access.

BC2 resolves jntWpn_Flash by name AND weapon-subtree ancestry. It publishes an

immutable muzzle snapshot from the same private evaluated pose as the rendered

gun. Readers avoid the animation mutex, validate current tracking/ownership and

retry at most three times if the input guard advances during their read.

Client/server callbacks can arrive after different recoil animation evaluations.

The SPAS-12 exposed a 10–12 cm mismatch in the independently sampled candidate.

Both callbacks use context+0x1c to seed native spread; observed same-shot seeds

matched, while separate shots differed. Matching those seeds within the original

pose deadline eliminates that mismatch without freezing animation or changing

native spread. Every enabled path also verifies local campaign ownership.

## Installed-build evidence (preferred VA, image base 0x400000)

- ServerShoot 0x579dc0 and ClientShoot 0x81c170: thiscall, four arguments, ret 0x10;

  return value preserved. Server matrix builder 0x6cddf0 remains observation-only.

- Native final composition 0x53db40: thiscall(lhs,out,rhs), ret8, EAX=out. Normal

  client/server return sites 0x81c364 /0x579f1a; targeted/custom-origin branches are

  excluded. Matrix-copy observer 0x403810 is read-only.

- Client/server context+0x1c reads at 0x81c653 /0x57a492 feed spread seed thunk

  0x6cc0e0, then seed setter 0x4e44c0. Discovery verifies both call relationships,

  field reads, seed code, hook ABI and live instruction bytes.

- Player constructor 0x6ee400 stores its ID at +0x154. Server creation 0x573750

  passes that index through 0x572140 and stores the player in manager+0x6c[index].

  Server context is 0x1566b18; getter 0x547bf0. Pair validation checks vtable,

  capacity, both IDs, exact indexed entry and unchanged reads. ID zero is valid;

  failed reads are not zero IDs. Reflected actor/data/control links are additional

  requirements. Player+0x14 and actor+0x24 are NOT identity shortcuts.

## Verification

All final checks preserve foreground on the right monitor, with BC2 on the left.

The x86/x64 suites pass 33/32. Actual-executable discovery rejects 80 mutations.

The x86 ABI fixture checks argument cleanup, return values and unchanged sources.

Portable tests cover bounds, nonfinite inputs, exact basis preservation, event

matching, deadline expiry, identity resets and more events than history capacity.

| Check | Reports | Result |

| --- | --- | --- |

| Unmodified final-composition observer | native-trace-20261001-042104-040 /native-ipc-20261001-042103 | 240 pairs; zero firing writes |

| Initial rifle origin writes | native-trace-20261001-042241-083 /native-ipc-20261001-042240 | 2 client /4 server writes; tracked hand moved 22.38 cm |

| Shotgun event-timing repair | native-trace-20261001-044944-288 /native-ipc-20261001-044943 | 2 client /2 server writes; exact shared event muzzle; 22.35 cm hand travel; 240 pairs |

| Final full XR host, test-only runtime | native-trace-20261001-050140-968 /native-xr-muzzle-20261001-050140 | 507 fresh pairs; 12 client /22 server writes; 18 writes after log capacity; zero origin fallback |

The final host test drives the REAL BC2XrHost and OpenXR action path through an

explicit separate FvrNativeCampaignTestXr.dll. It selects the rifle, reloads,

fires two bursts, moves its tracked hand and loses right tracking. It does not

load SteamVR or select a system runtime. 79 native left-only control samples had

zero untracked firing. Host presentation: 1030 frames, including 523 reused pairs;

129 blank frames in the fixture duration, which includes native startup. There

were zero rejected pairs, async timeouts, XR validation errors, GPU failures,

source changes or restoration failures. Hooks retired when the actual host exited;

BC2 remained responsive during the 15-second post-cleanup check. The unchanged

original XR fixture also passes with zero errors. This is not a comfort or visual

headset test and does not validate physical projectile impacts.

Superseded evidence: 044040-720 had valid shotgun writes but FAILED client/server

muzzle agreement; 044502-292 observed shared seeds with no writes; 045850-317 had

one missing-snapshot/native-origin fallback. 045224-815 correctly rejected a

receiver executable as a continuous XR host before activating hooks. These are

not final passes. The receiver-only lifetime option was removed.

Recheck saved evidence without touching the game:

```powershell

python tools/check_muzzle_fire.py --native reports/native-trace-20261001-044944-288 --receiver reports/native-ipc-20261001-044943 --writes --weapon SPAS12_sp

python tools/check_native_xr_muzzle.py reports/native-xr-muzzle-20261001-050140

```

`Test-NativeXrMuzzle.ps1 -Python <python.exe>` repeats the full native fixture. It

requires the inspected campaign SPAS-12/XM8 loadout and reserve ammunition, performs

read-only preflight, sends only native controls, never activates BC2, and stops only

its own host on failure. It is an explicit firing test, not an offline unit test.

Summary: reports/muzzle-fire-20261001.json. Exact source/binary checkpoint:

reports/muzzle-fire-20261001-0505/checkpoint.json. Earlier accepted headset payloads

remain preserved separately. Physical two-hand support, further weapon classes,

wall/impact behavior and headset feel remain future work.


The subsequent explicit grip increment combines this firing path with supported
aim; see TWO-HAND-SUPPORT-20261001.md for native checks and remaining headset limits.
