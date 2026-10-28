# CHAT02: retain rejected health baselines

This is a private diagnostic prerequisite, not a game-crash fix. Status is
**needs-native-evidence**. No game or native session ran; headset/tester availability
was explicitly denied. The normal runtime, profiles and canonical shared files
remain unchanged. The queue switched to isolated workers during this run; the
five tested Python sources were copied byte-for-byte into this worktree. The
helper-pin JSON has the same values with line endings normalized for Git.

## Concrete failure

Local `performance436/death06/death-observer/completion.json` confirms successful
seat exit, shoulder draw and stock grenade route/ammo preflight, then rejection:
`Positive coherent pre-action health required`. The reader required positivity,
matching client/server health bytes and the static bound before saving its
baseline. Consequently the failed values and which predicate failed were lost.
No grenade was issued, no scheduled health samples were collected, and neither
death nor checkpoint recovery was tested. Do not reconstruct those missing values.

Death05's earlier `server_damage` live-code refusal is separate: R2 already
classified that knife-hooked function as disk evidence only, while retaining all
27 disk spans and the 18 live spans needed for health reads. Death06 also had
normal knife/contextual tools disabled; its old wrapper cannot establish normal
feature coexistence. The startup AV in death03 and R48's request184 cancellation
remain unresolved; later request185 success does not overwrite either failure.

## Source change and tests

[R3](diagnostic/CaptureHealthSeries436R3.py) calls
[publish_baseline](diagnostic/HealthBaselineEvidence436.py) before ready-file
publication. It retains `baseline_candidate` and ordered `failed_checks`, then
uses the original three-condition gate. Rejection never populates accepted
`baseline` or publishes ready. Native owner/read/snapshot functions are unchanged,
as are deadlines, sample count, proof policy and ready protocol.

The current server-reader dependency changed from SHA `e952be32...` to
`9da3b354...`. Exact predecessor Git `a820f76:tools/capture_reload_server.py`
matches the historical pin. `require`, `discover`, `chain` and original imports
and constants are AST-identical; additions concern unused capture/main mismatch
diagnostics. Only that dependency is rebound in `helper-pins-r3.json`; historical
pins and wrappers remain untouched. The reader remains tied to the explicitly
pinned local proof/helper paths; this is not a portable production feature.

Deterministic: **18/18 passed** (14 baseline-publication tests plus four proof-policy
tests). They cover all seven rejected-predicate combinations, retained raw values
and owner, accepted ready compatibility, exclusive temporary publication, and
strict JSON. The first attempt had 20 filesystem permission errors across 18
tests; its log remains. The identical tests passed outside the sandbox. Native
sources/binaries did not change, so previous normal x86/x64 builds were not rerun.

Evidence folder (local, not uploaded):
`G:\Unity\VRMMO\bc2vr-recovery\performance436\chat02-death-recovery-prep\run01`.
It contains `source-runtime-identity.json`, `health-reader-r3.patch`,
`helper-dependency-audit.json`, `diagnostic-preparation.json`,
`deterministic-tests.log`, `deterministic-tests02.log`, `independent-review.json`,
`worktree-source-binding.json`, `storage-admission.json` and `completion.json`.
Identity/audit confirms all576 native source pins, 110 normal flags and six
frozen/build binary pairs on `unreleased-checkpoint436-custody-frame01`.
The independent review reran no tests/builds/native sessions.

## Bounded request to the integrator

Collect **one** R3 pre-action baseline in a fresh root-owned normal R56 session,
at a live-named SP01 on-foot checkpoint, using exact process PID and birth. No
grenade is authorized by a rejected baseline. Keep ordinary controllers, hands,
motion/body tracking, muzzle fire, two-hand grip, sight flip, physical/magazine
reload, body inventory, PlayerGrenades, PlayerKnife and ContextualTools enabled.
Preserve the existing normal vehicle settings too. No isolated feature flags or
old R14 wrapper: its hashes/runtime/flag set have not been rebound to R3.

Call StorageBudget before capture/output creation, route TEMP/TMP/TMPDIR to G:,
reserve16MiB metadata with zero raw capture bytes, and use a fresh directory.
Run the retained G: Python with `-B` and this reader's required arguments:
`--pid`, `--process-start-filetime`, `--output`, `--start-tick-ms`, `--ready`.
The start tick must remain2–13s ahead after binding. The existing hard limits
remain400 slots at30ms, 30s whole observation,300000 reads and16MiB read bytes.
Return the report, candidate/failed_checks, exact command, storage admission,
normal-runtime receipt and process/scene identity. No raw images are needed to
answer this baseline question. Headset availability is not required for this
read-only observation, but integration exclusively owns the live session.

On rejection, inspect values/raw bytes and owner; do not relax positivity,
client/server equality, static bounds, owner identity or deadlines. On admission,
actual death still requires ordinary input and independent presentation evidence.
Rebinding the automated controller fixture remains pending; this commit does not
make the old pinned launcher use R3 automatically.

## Acceptance still pending

Plan six real deaths: at each of two verified named campaign checkpoints, one
idle/on-foot, one with support grip, one during unfinished manual reload. Every
recovery must demonstrate both eyes, real fire/ammo consumption, fresh support
grip, holster stow/draw and a permitted native Use target. Completed here:0/6.
The known SP01 saved-body seed identifies only the level, not `jeepRideStart`;
both live checkpoint names remain to be confirmed. Do not count a menu-only
checkpoint test toward this matrix or gate independent tasks on its completion.

Retain PID/birth/exit/exception evidence, actor replacement, scene/game-thread
progress, stereo retirement/source-transition state, control-publication results,
and paired delivered-eye observations. A live PID and black pixels alone cannot
distinguish renderer stall from a blocked game thread; zero health alone is not
death. Budget any later image capture by dimensions ×4 bytes ×eyes ×pairs ×streams.
Separate deterministic, actual-game and headset evidence. Preserve every failed
episode and untested case. No new native or physical-headset acceptance is claimed.
