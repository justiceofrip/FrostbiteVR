# Native reload emulation and empty-ammo hitch correction — 212

Checkpoint 212 fixes a second timing rejection in the shared empty-ammo guard.
211 corrected a held reload's Update-delta admission; the automatic-reload
inhibition at the inner Step boundary still rejected frames longer than 50 ms.
Executing BC2's actual Update and Step instructions reproduced automatic reload
at a 59.6221 ms frame on captured XM8 and SPAS settings. The same execution with
the existing scoped inhibition byte admitted up to 100 ms stays empty. This is
a shared family policy change, not a new weapon calibration.

The runtime, diagnostic reason decoder and family eligibility now use
`ValidManualReloadDelta`. Exact owner/generation, original evidence expiry,
current firing state, actual input flags, configuration and caller/stack checks
remain in force. The Step delta must not exceed its parent Update delta. The
original native Step executes once; context byte +0x28 is restored exactly.
Explicit reload intent still advances, and the underbarrel remains automatic.
No zero-delta or unbounded frame admission was added.

## Repeatable evidence without headset input

`tools/capture_bc2_emulation_fixture.py` captures the inventory's reflected firing
configuration and client objects read-only, tied to an exact process creation
FILETIME. It verifies unchanged configuration and ownership across the reads;
it does not claim an atomic simulation snapshot. Captures include game bytes:
keep them outside the repository and never distribute them.

`tools/probe_bc2_reload_machine.py` loads the installed x86 executable in Unicorn,
maps those private configurations, and runs original machine instructions.
Effects/listener lists are explicitly empty mocks. Initial states, counts,
input and the scoped interception are supplied by the experiment. Native owner
selection, callback scheduling, prediction, actual effects, rendering and XR
are not emulated. The production C++ eligibility tests cover those policy inputs
separately; machine-code results do not establish live gameplay acceptance.

The suite passes with both the earlier read-only capture and a fresh campaign
capture made after restarting BC2:

- 23 empty-state cases: stock, old 50 ms guard and bounded 100 ms guard; normal,
  measured-hitch and boundary deltas; deliberate reload; stock 40 mm launcher.
- 10 magazine cases: original partial/full return with zero/positive reserve,
  discard with and without inhibition, and deliberate replacement after discard.
- A separate 24-case entry-state/timer sweep found no zero-input Step bypass.
  This is bounded evidence, not exhaustive exploration of every native state.

The old guard is a negative control: hitch cases must reproduce its failure.
The bounded variant must preserve counts until explicit reload or return, and
all scoped context and callee-preserved register checks must pass.

## Full-magazine accounting research

`tools/probe_bc2_magazine_adjustment.py` identifies the secondary firing interface
and its signed loaded-round adjustment (installed build VA 0x6d70b0, 71 bytes,
secondary vtable slot 3, `this = firing + 4`, `__thiscall`, `ret 4`). It derives
the unique function signature and constructor/vtable relationship from the
installed executable rather than treating that address as portable authority.

68 partial/full removal-and-return cases preserve every firing-object byte
except loaded rounds, including reserve ammunition, across capacities and both
capacity lookup paths. Four negative controls expose native underflow, signed
overflow, infinite-capacity and upper-clamp behavior. The new value-only
`Bc2MagazineAmmoMove.h` validates bounded whole-magazine removal/return and the
complete object postcondition. It is **not connected to a live native call**.

14 explicit three-object snapshot round trips also pass. A stale pre-removal
snapshot restores its old rounds, proving that a successful local adjustment
alone cannot provide persistent removal. The emulator explicitly supplies
snapshot propagation; it does not prove engine replication ordering.

Full-magazine discard therefore remains unresolved in gameplay. Its current
presentation-only recovery restores the visible original magazine because the
native rounds were never removed. Enabling the replacement requires an exact
server callback operation, resource accounting, client prediction convergence
and stale-snapshot handling. Neither a visual hide nor an isolated native call
is sufficient. No new native ammo write is enabled in 212.

## Builds and operation

Four focused C++ suites and all **191 suites on each of x86 and x64** pass.
The log-reporting tests pass 45 checks; an initial sandbox temp-directory failure
was retained and the same tests passed outside that restriction. Reporters now
accept explicit `--delta-policy bounded100` for 212. Historical traces retain
the default `legacy50`; do not reinterpret old failures as new-build passes.

`Build-Checkpoint.ps1` selects `profiles/checkpoint212`, retaining geometry 202.
The x86 directory is still named `x86-checkpoint208`; use the frozen receipt and
binary hashes. The private stage is `headset212-20261008`. Display code is
unchanged, so the previous HUD software-D3D result is explicitly reused, not
represented as a fresh display test. The two exact compiled magazine profiles
remain scoped XM8 and AEK; prepared other-family data is not enabled coverage.

## Live background test

The fresh campaign launched without clicks and reached on-foot SPAS gameplay on
the left monitor. `spas-empty212-01` supplied emulated controller input, fired
eight shells and held at zero for 3.003 seconds without reload input or an ammo
increase. The strict empty-fire audit verifies all three firing branches and
matching guard application/restoration counts (3715, 3718, 3717), with zero patch
or restoration failures. All 240 stereo pairs arrived without transport timeout.
Mapped executable sections match the frozen 212 probe; hooks were disabled and
the game remained responsive after the finite test.

The first audit refused the producer's existing 131-entry layout because its
reader still required 128. The reporter now accepts exactly those two known
layouts, with overflow/unknown-layout regressions. A separate audit of the
unchanged trace passes; the rejected audit is retained. This reporting-only
correction is recorded separately from the frozen runtime build.

The immediate post-detach read raced native ownership; a later read found stock
BC2 had reloaded to 8/16, as expected after removing the inhibition hook. From
8/24 initially, the eight fired shells leave 24 total. Those late counts are not
manual reload evidence. Retained empty-Step rows contain no >50 ms frame, so the
live test proves bounded depletion/hold behavior, not live hitch acceptance.
Both client and server post-detach drains had 31 accepted reads with no rejection.

Research tools require local Python and `unicorn==2.1.4`. Example private run:

```powershell
python tools/capture_bc2_emulation_fixture.py --pid PID --creation-filetime FILETIME --output PRIVATE/fixture.json
python tools/probe_bc2_reload_machine.py --exe GAME/BFBC2Game.exe --fixture PRIVATE/fixture.json --output PRIVATE/reload-emulation.json
python tools/probe_bc2_magazine_adjustment.py --exe GAME/BFBC2Game.exe --output PRIVATE/adjustment-emulation.json
```

Output files are new evidence, not overwrite targets. Machine-code tests require
the exact installed executable matching the private fixture. No game bytes are
included in the resulting reports or in source control.

Headset HUD/chest continuity, support-grip feel, AEK live acceptance and full-mag
discard remain open. No GitHub update or broader release is claimed.
