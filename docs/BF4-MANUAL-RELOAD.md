# BF4 manual reload for every gun: research and pipeline

Evidence level: **static analysis only** (reflection tables and code) of the decrypted
research image of bf4.exe build 179547, 2026-10-19. Nothing here has run in the game.
RVAs are relative to `0x140000000`. Reflection offsets come from the game's own field
tables (`tools/bf4probe/bf4_reflection.py`; code cross-references with `tools/bf4probe/bf4_xrefs.py`), not from forum offset lists.

Goal: manual reload as a per-gun option (roadmap M5, then all guns), with button reload
(already working through the virtual XInput pad) as the default and the fallback for any
gun that is not classified or not verified.

Rules that shape the design (AGENTS.md): magazine custody, ammunition transactions,
holster ownership and owed pump/bolt steps persist across frames until they actually
complete; completed native transfers are never rolled back; stock reload/cycle animation
may be suppressed only through a verified adapter; campaign only (R5), and AI soldiers
must never be affected.

## 1. What the game already says about every gun (reflection)

Data assets, one per weapon blueprint (shared by every soldier who carries that weapon,
AI included: `UnlimitedAmmoForAI` lives here, so **never write these**):

| Class | Field | Offset | Notes |
| --- | --- | --- | --- |
| WeaponFiringData | PrimaryFire (FiringFunctionData*) | +0x10 | |
| | ReloadWholeMags | +0x4F | a reload throws away the rounds left in the old magazine |
| | DisableReloadWhileSprinting / AbortReloadOnSprint | +0x50 / +0x51 | |
| FiringFunctionData | Shot / OverHeat / Sound | +0x60 / +0xF0 / +0x160 | |
| | FireLogic (FireLogicData, embedded) | +0x168 | absolute offsets below |
| | Ammo (AmmoConfigData, embedded) | +0x220 | |
| | UsePrimaryAmmo / UnlimitedAmmoForAI | +0x248 / +0x249 | |
| FireLogicData (abs.) | BoltAction.BoltActionDelay / BoltActionTime | +0x184 / +0x188 | plus Hold/Force/UnZoom/ReturnToZoom flags +0x18C..+0x190 |
| | ReloadInputAction | +0x1BC | input concept id |
| | ReloadDelay / ReloadTime | +0x1D8 / +0x1DC | |
| | ReloadTimerArray | +0x1E0 | float array (staged reloads) |
| | ReloadTimeBulletsLeft / ReloadThreshold | +0x1E8 / +0x1EC | threshold = fraction of the reload at which ammo moves |
| | ReloadLogic / ReloadType / FireLogicType | +0x200 / +0x204 / +0x208 | enums below |
| | HoldOffReloadUntilFireRelease / ...ZoomRelease / ForceReloadActionOnFireTrigger | +0x218 / +0x219 / +0x21A | |
| AmmoConfigData (abs.) | MagazineCapacity / NumberOfMagazines | +0x220 / +0x224 | capacity counts a chambered round for ReloadType 2 (see 2.2) |
| | AutoReplenishMagazine / StartUnloaded | +0x240 / +0x241 | |
| WeaponMagazineModifier | MagazineCapacity / NumberOfMagazines / InitialNumberOfMagazines | +0x18 / +0x1C / +0x20 | attachments (extended mags) |

Enums (values from the reflection tables):

- ReloadType: `rtSingleBullet`=0, `rtMagazine`=1, `rtMagazineWithPossibleShorterReload`=2
- ReloadLogic: `rlWeaponSwitchCancelsUnfinishedReload`=0, `rlReloadUnaffectedByWeaponSwitch`=1
- FireLogicType: `fltSingleFire`=0, `fltSingleFireWithBoltAction`=1, `fltAutomaticFire`=2,
  `fltBurstFire`=3, `fltHoldAndRelease`=4, `fltDetonatedFiring`=5
- Weapon classes (`wc*`): Assault=16, Shotgun=17, Smg=18, Lmg=19, Sniper=20, Ugl=21,
  Hgr (handgun)=22, At=23, then gadgets (Knife=28, ...). Where the equipped weapon's
  class is stored is a live-probe item.

Animation interface: `AnimatedWeaponBinding` (72 AntRef signals) includes `Reload`,
`ReloadStage`, `ReloadShotgun`, `BoltAction`, `PumpAction`, `NumberOfBulletsLeftInGun`,
`BulletsLeftInGun` and the weapon-type signals `RflType/PstlType/HgrType/ATType/ShgType/
LMGType/SnpType`. Gameplay drives the stock reload animation through these, so this is
the place to keep the stock reload out of the player's view later, and `PumpAction`
versus `BoltAction` tells pump guns from bolt guns.

## 2. The native reload machine (code)

### 2.1 Runtime firing object (one per weapon instance; layout candidates)

| Offset | Meaning (static) |
| --- | --- |
| +0x128 | WeaponFiringData* (`[+0x128]+0x10` = FiringFunctionData) |
| +0x130 | AmmoConfigData* (inside that FiringFunctionData) |
| +0x148 / +0x150 | current / next state (0..15) |
| +0x15C / +0x160 | state timer / reload total time |
| +0x17C | reload-time multiplier (per instance; the hook for fast-reload perks) |
| +0x188 | reload step counter (-1 idle) |
| +0x1A0 / +0x1A4 | rounds loaded / rounds in reserve |
| +0x1A8 | shots pending (burst) |
| +0x1BC / +0x1D0 | fire-mode index / per-mode FireLogicType array |
| +0x1C0 | magazine capacity override (-1 = none) |
| +0x1F0 | weapon modifier set (`+0x78` -> WeaponMagazineModifier) |
| +0x208 / +0x209 | flags (0x208: bit 3 reload started, bits 4/5 unlimited ammo; 0x209: bit 1 partial reload) |

### 2.2 State machine `+0x9BFAC0(firing, input, dt)` (jump table `+0x9C0608`)

- **3 ready**: fire and reload input; a reload request moves to **10**.
- **10 reload requested**: waits on the HoldOffReloadUntil* flags, marks a partial reload
  for ReloadType 0/2 when rounds remain, fires the reload event, takes the reload time
  from `+0x9BCB60` (ReloadTimeBulletsLeft for partial reloads, else ReloadTimerArray/
  ReloadTime, times the `+0x17C` multiplier) and goes to **11**, adding ReloadDelay
  when empty.
- **11**: timer = reload time x ReloadThreshold, then **12**.
- **12**: calls the **ammo transfer `+0x9BC910`**, then waits out the rest of the
  reload. For single-bullet guns it loops back to **11** per round while reserve remains
  and the gun is not full.
- **7/8**: bolt/pump action after a shot (BoltActionDelay, then BoltActionTime).
- **5/6**: firing.

Ammo transfer `+0x9BC910`: needed = 1 (single bullet), capacity - 1 (type 2 from empty:
no round chambered yet), else capacity - loaded; capacity comes from the override, the
magazine modifier or AmmoConfigData. It moves min(needed, reserve) from `+0x1A4` to
`+0x1A0`, honours unlimited-ammo flags, and applies ReloadWholeMags. It is also called
from `+0x9B6460` and `+0x9BEC80` (resupply/network paths, unconfirmed).

### 2.3 Client and server copies (static answer, 2026-10-19)

BF4 single player runs a local server in the process, and every gun has **two** firing
objects. Both callers of the firing update are virtual methods of different classes:

| | Class vtable | Update (slot) | Firing object | Evidence |
| --- | --- | --- | --- | --- |
| client weapon A | `+0x1D467A0` | `+0x9739F0` (2) | `[weapon+0x8]` | constructor `+0x968F90` creates its firing object through `+0x939E70`; its update queries the client camera manager `+0x2673D80` (the client view code's) |
| client weapon B | `+0x1D467E0` | `+0x973C90` (2) | `[weapon+0x8]` | constructor `+0x9691C0` is handed its firing object; which client class carries the local player's gun is a live question |
| server weapon | `+0x1D64680` | `+0xA26220` (41) | `[weapon+0xA0]` | constructor `+0xA18C60` creates it through `+0x939E70`; destructor `+0xA19460` |

Both client classes derive from an abstract base (vtable `+0x1D46760`, pure-virtual slots;
never on a live object). One **client weapon entity** owns both (constructor `+0x968D00`,
made by the factory `+0x96C420`, vtable `+0x1D46540`): class B at `+0xA0` (created first,
handed no firing object), class A at `+0xA8` (creates its own), and the **active** one at
`+0x98` (B at creation). It also keeps a small object at `+0xC8` that points at the entity,
B and A, and it registers itself through the client game context (`+0x2670D80`, `+0x78`).
Working guess: A is the locally predicted copy and the entity switches `+0x98` to it for
the local player; the census logs the active class live. Vtables sit back to back without RTTI, so table starts come from
the code that stores them, not from gaps. The two updates use different world globals
(`+0x2668F90` client, `+0x2714938` server).
Consequences: the input-only driver is right for both (the game reloads both copies from
the same input); acknowledgements should follow the **server** copy, the authoritative one
(BC2's lesson), with the client copy only as a faster hint. Any future write must target
both or neither. The census now confirms all this live and walks one level further up.

## 3. Classifying every gun from its own data

Each equipped weapon is classified when it is equipped, from the data above, into the
framework's existing `WeaponMechanismDescriptor` (feed, afterEmptyFeed, afterShot,
tacticalRetainsChamber). No per-gun table is needed for most guns; authored overrides
only where the data is silent.

| Data | Feed / actions | Typical guns |
| --- | --- | --- |
| ReloadType 1 or 2, capacity > 1, no bolt logic | DetachableMagazine; tacticalRetainsChamber = (ReloadType 2); afterEmptyFeed = charging handle (rifles, SMGs, LMG box mags) or slide (wcHgr) | assault rifles, carbines, PDWs, DMRs, box-fed LMGs, pistols, magazine shotguns |
| ReloadType 0 | InternalTube: one round per insertion (the native loop does the same) | tube shotguns |
| FireLogicType 1 (bolt action) | afterShot = Bolt, or Pump when the weapon uses the `PumpAction` signal / wcShotgun | bolt snipers (magazine + bolt), pump shotguns (tube + pump) |
| capacity 1, ReloadType 1 | Single: open, insert, close (or insert only) | rocket launchers, underbarrel launchers |
| FireLogicType 4 (hold and release) | Single: nock and draw | bow |
| authored only | Belt (cover open, belt seat, cover close, charge) | belt-fed LMGs: data looks like a box magazine |
| authored only | Clip semantics for a swing-out cylinder (open, speedloader, close) | revolvers |
| anything else / unreadable | stays ButtonPress | never guessed |

## 4. Driving the native reload

Ammo is only ever moved by the game's own transfer. Options (option 1 is built as
`Bf4ReloadDriver`: fire held off while a magazine is out, a breech is open, a reload runs
or a stroke is owed; the game's reload pressed at seat/close; acknowledgements only from
observed ammunition; reseat without a press when there is nothing to load; a declined
reload clears the gates so the gun is never stranded; tube guns wait for P1):

1. **Gate at seat**: send the native reload (pad X, already implemented) when the VR
   magazine is seated. Simple and works for every magazine gun, but the stock reload time
   follows the physical one. Mitigation: lower the player's own **per-instance multiplier
   `+0x17C`** (the knob fast-reload perks use) while manual reload is on, restored on
   weapon switch and detach. It never touches shared data, so AI are unaffected.
2. **Physically paced native reload (preferred if 2.3 allows)**: start the native reload
   when the VR magazine leaves the gun. Hold the player's state-11 timer above zero while
   the new magazine is not seated, release it at seat so the game's own transfer runs,
   then hold the post-transfer part for the charging-handle/slide gesture when the gun
   was empty. Tube guns gate each loop iteration on one inserted shell; bolt/pump gate
   states 7/8 on the VR cycle gesture, keeping the owed step across frames.
3. Calling `+0x9BC910` directly: rejected. It would bypass the state machine, events and
   networking.

Either way: the transfer is acknowledged by observing loaded/reserve change on the
authoritative copy (the framework's `ManualReload` acknowledgement). A native cancel
(weapon switch with ReloadLogic 0, sprint with AbortReloadOnSprint, death) cancels the
VR transaction without returning or inventing ammo. The VR pouch count is read from the
native reserve, including ReloadWholeMags losses.

Stock animation: the stock reload plays through the AnimatedWeaponBinding `Reload`/
`ReloadStage`/`ReloadShotgun` signals. First version: let it play (the BC2 headset run
found residual stock animation tolerable). Later: suppress it for the player's
first-person presentation only, never the gameplay signal.

## 5. Pipeline and phases

| Phase | Work | Exit evidence |
| --- | --- | --- |
| P0 (done) | Button reload through the virtual XInput pad | in-game check pending |
| P1 | Read-only weapon probe. **Built 2026-10-19:** the firing census (`Bf4FiringCensus.h`, `RunFiringCensus`) copies the game's heap with ReadProcessMemory on a low-priority thread, keeps objects whose `+0x130` is exactly their own FiringFunctionData + 0x220 and whose local fields are in range, and logs them with limited-ammo objects first and client/server pair candidates (same data, same counts). Runs ~30 s into developer sessions, then every ~2 min, and on the research command `census`; stops at any lockdown. It then finds every pointer to each limited-ammo firing object, classifies the holder as client weapon (`+0x8`, client vtable) or server weapon (`+0xA0`, server vtable), and finds who holds those weapons (nearest vtable-looking object start and field offset): the next links of the player -> weapon chain. Emulated: finds a planted pair, marks an unlimited AI copy, tells planted client/server owners apart and follows the client owner to a holder object at field +0x40, among hundreds of stale pointer copies. Still live: run it during normal empty, tactical, tube-shotgun and bolt reloads | layout and states match 2.1/2.2; client/server answer |
| P2 | Pure classifier (section 3), **done 2026-10-19**: `Bf4ReloadClassifier` emits the framework's WeaponMechanismDescriptor, checked against its validator and plan selection (ReloadClassifierTests). Also done: the input-only reload driver `Bf4ReloadDriver` (section 4 option 1) and the emulated firing state machine `tests/bf4/FakeFiringMachine.h` (states 3/10/11/12, threshold transfer, partial-reload math, single-bullet loop, bolt states). ReloadDriverTests cover tactical and empty magazine changes, owed charging/bolt strokes persisting across frames, no-reserve reseat, a declined reload, refusal of a second concurrent request, weapon switch, single-shot launchers, unsupported tube guns, and the framework coordinator completing a whole plan; a mutation (never block fire) is caught | tests |
| P3 | Magazine guns (largest family), option 1 or 2 per P1 | one rifle, then one pistol, in a mission |
| P4 | Tube shotguns (per-shell gating) | one pump shotgun |
| P5 | Bolt/pump cycling after shots | one bolt sniper, one pump shotgun |
| P6 | Single-shot launchers, underbarrel, bow | one of each |
| P7 | Authored Belt and cylinder profiles | one belt LMG, one revolver |
| P8 | First-person stock-animation suppression, magazine props on weapon meshes, holster/pouch integration (M4) | headset review |

Dependencies: R5 (campaign signals) for any write; R8 (player soldier/weapon path) to
find the player's own firing object for writes (the census finds candidates without R8,
but a write needs an ownership chain, not a heap scan); the framework's ManualReload, DetachableMagazine,
ReloadInsertion, WeaponCycle/PhysicalWeaponCycle, BoltWeaponCustody, AmmunitionLedger
and FeedMechanism carry over unchanged. Their native evidence must be redone for BF4.
