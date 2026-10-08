# Checkpoint 207 headset investigation and candidate 208

## Observed result

The October 8 checkpoint 207 headset run was mixed. The user reported that XM8
magazine ghosting was fixed, but chest ammo still flickered and no ammunition
counter was visible. Releasing a removed full magazine restored a magazine in
the gun. AEK reloads were unreliable and the left hand sometimes moved from
magazine handling into the fore-end grip. The recording was sampled around
the XM8 and AEK interactions; this is not acceptance of all other behavior.

The authorized mod stop completed with hooks disabled, no new crash report,
and BC2 responsive. The game was not closed. The old DLL remains mapped, so
updated native code requires a fresh game before its next runtime test.

Private evidence: `headset207-20261008/reports/session-20261008-072739-332`
and `native-trace-20261008-072741-689`. The user recording is dated
`2026-10-08-03-27-55`; sampled review artifacts are under `headset207-review`.

- HUD initialization succeeded, but all 5,114 samples were invalid: zero
  visible frames and zero texture uploads. The missing counter is confirmed.
- The host recorded zero invalid body-prop instances and zero renderer failures.
  That does not establish uninterrupted upstream chest-ammo publication.
- Two replacement-magazine operations and four shell submissions completed.
  Five magazine starts were classified as not started; late native start
  rejection causes were not captured by the old callback recording window.
- Detached handling recorded four grabs, two returns and two releases. Its
  release recovery explains the user seeing the magazine reappear.

## Shared candidate changes

1. The ammo counter consumes a published immutable reserve observation instead
   of calling native reload observation from each eye. Its validation clock is
   sampled after reading the publication. The prior timestamp preceded the
   native observation, making fresh counts appear future-dated. Native reserve
   reads also entered reload coordination and could request cancellation during
   start exclusion. That interference path is removed; causation of the AEK
   failures is not established by the old evidence. Original observation times,
   owner identity and expiry still apply. Owner invalidation rejects earlier
   reads that finish late; rendering cannot renew counts or authorize ammo.
2. Full-magazine detached handling and ordinary magazine handling use the same
   chest-ammo display calculation from the final tracking publication. Previously
   only the latter supplied the chest prop, so transient route changes could
   make it appear/disappear. Missing final evidence still hides the prop; there
   is no indefinite cached fallback. This repairs a source inconsistency, not
   a proven sole cause of all visible chest flicker.
3. A squeeze consumed by magazine handling cannot become a delayed support
   grab. A genuine neutral hand packet followed by a new squeeze still permits
   an immediate fore-end grab. This is shared support-grip policy.
4. Accepted magazine insertion releases the exact inserted object's hand claim
   immediately, including when the subsequent native observation is deferred.
   Shared insertion emits its attached prop in the same seat-request packet;
   retaining the former hand target until another observation was a reproduced
   support-blocking gap, caught by the focused consumer regression.
   Its ammunition reservation remains pending until the actual native receipt.
   No native reload duration or loaded count is changed.
5. Dedicated bounded journals retain consumer pre-start failures and native
   start gates throughout the session. Input/request/cycle identities, original
   times, owner/callback revisions and cancellation state can be joined without
   relying on the first seconds of generic callback recording. Rejection and
   rollback behavior is unchanged; private `policy_start` remains an aggregate
   rejection category.

## Unresolved work

Full-magazine discard is **not fixed** by this candidate. The current full or
positive-loaded/zero-reserve path supports pulling out and returning the same
magazine without a native reload transaction. Releasing it invokes recovery,
which restores attachment. Correct discard/replacement requires persistent
magazine ownership and verified accounting across all native firing copies;
merely hiding the mesh or deducting one predicted count would be incorrect.

The bounded operation audit found the reusable boundary: native replacement
currently transfers only `min(capacity - loaded, reserve)`, a positive amount.
Full and zero-reserve admission are explicitly rejected by `ResourceCounts`
in `Bc2MagazineReloadTests`. Original-magazine return conserves counts because
those rounds never left the native weapon. The missing operation must remove
loaded ammunition with an exact-owner server receipt and convergence of both
client copies, record whether those rounds are dropped/held/credited, and
define chamber treatment and rollback. Once it produces a verified empty
source, the existing empty-magazine replacement pipeline can be reused.
`Bc2MagazineEmptyConsumerTests` and `Bc2MagazinePredictionRestoreTests` cover
parts of that downstream path; neither proves the missing removal operation.

The AEK start failures need an actual-game check with the new passive HUD path
and per-attempt evidence. Chest flicker, HUD legibility and immediate support
regripping also retain headset acceptance gaps. No wider weapon coverage is
claimed: scoped XM8 and AEK remain the two enabled exact magazine configurations,
with the SPAS shell consumer separate. Prepared profiles remain candidates.

## Validation

All six focused regression suites pass. Full checkpoint builds pass **190 C++
suites on x86 and 190 on x64**. The HUD also passes software-D3D texture readback
with mocked XR lifecycle. These checks include original evidence expiry,
delayed seat observations and immediate hand release without ammo completion.
At the initial freeze, no checkpoint 208 native session or headset test had run.
The later `magazine-player208-01` native sequence passed the strict original-return
and replacement audit with conserved 22/191 to 30/183 ammunition and 240 stereo
pairs. See [209 monitor evidence](MONITOR-PRESENTATION-209.md). Headset acceptance
and full-magazine discard remain unresolved.

Matching private artifacts are frozen in `headset208-20261008`, with source and
binary hashes in `pipeline-runtime-20261005/normal-recovery208-build-receipt.json`.
Native output is `build/x86-checkpoint208`; host output is `build/x64`.
Staging is not native acceptance or completion of the unresolved work above.
The reviewed native operations and geometry are unchanged; the new source-bound
receipt is in `profiles/checkpoint208` with checkpoint 202 calibration headers.
Builds run sequentially with one compiler job. No GitHub update is included.

Before another headset request, verify the counter produces valid observations
in the real game and join any failed reload start to its explicit gate. Offline
tests alone do not establish native timing or visual acceptance.
