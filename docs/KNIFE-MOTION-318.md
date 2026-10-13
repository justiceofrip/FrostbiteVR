# Knife motion groundwork and native evidence

The shared `MeleeStroke` recognizer uses the existing hand arbiter and a measured
blade-tip input. It emits one intent after a deliberate forward stroke. Drawing,
repeated packets, tracking loss, recentering, stale custody, pose jumps and native
attack unavailability cannot grant an attack. Either hand is supported. A new
draw or hold must settle before it can stab, and another stroke requires quiet
rearming. Thresholds are explicit comfort settings, not measured damage range.

The recognizer owns motion history only. The adapter must retain submitted native
attacks independently and report them unavailable until their outcome settles.
Use a fixed tracking reference when supplying tip positions; never derive motion
from a moving camera or an interpolated rendered hand. Native actor movement and
head turns must not manufacture a stroke. Reference-space changes retire motion.

Both full builds pass **244 suites** (x86 135.34s, x64 38.56s). The native player
dependency graph and capability digest remain identical to312; the new gesture
is compiled by its regressions and the private diagnostic, not enabled in the
normal player. Main-build SCAR, SPAS, pistol, holster and menu evidence retains
its earlier scope. This is not a new completed campaign or headset acceptance.

## Native proof

- 317 extends the proven draw gate through one attack. A copied native update
  context supplies a bounded diagnostic fire pulse. Exact client/server automatic
  return calls are suppressed only for the owned held knife; explicit return
  remains intact. All three firing copies traverse attack and return to idle.
- 318 supplies197 simulated controller samples through the actual shared
  `HandInteraction` and `MeleeStroke`. Drawing produces no attack. Two strokes
  produce exactly two observed attack cycles in each firing copy. The knife stays
  selected for roughly4seconds and returns to the original rifle with unchanged
  ammunition. The native-input delivery remains a private copied context, not
  the production XR/body-inventory path.
- Both diagnostics remove their hooks and restore checked native code; BC2 remains
  alive/responding.317 drops3 ordinary firing records,318 drops5 at the nonblocking
  logger; neither claims complete callback coverage. Their independent audits
  validate observed transitions, sequence timing and selection continuity.
- The first318 attempt failed its coherent owner read before installing hooks or
  issuing actions. One unchanged retry after an independent read confirmed the
  actor succeeded. Later observers use a bounded100ms read-only preflight.

The existing native slot7 is used; no item is spawned or assigned. Persistent
holding requires both FireOnDeploy suppression and suppression of the specific
automatic return callers. Shared weapon data is not changed. See KNIFE-DRAW-316.md.

## Geometry and shot-origin investigation

The installed SP common archive supplies an exact KNV-1 mesh and static authored
right-hand grasp. The shared reference pipeline resolves its animation/skeleton
chain with no gaps. `knife316-references` holds the measured data locally; no game
mesh or animation bytes are included in the source repository. The authored left
hand is a separate native animation pose, not permission to lock the VR support
hand to the knife.

319's firearm-only RTTI filter matched no shot rows. Read-only inspection shows
the actual objects are `ClientMeleeFiringEffects` and `ServerMeleeFiringEffects`.
320 uses those exact identities. It observes two calls on each existing shoot
hook and two client composition results.321 identifies the rejected server
prerequisite: the owned melee effect calls the shared shoot function with a null
context. Its configuration/data/type checks match, but it has no firearm-style
shot token.

322 records that exact owned null-context server call as observation only. Two
client and two server shoot callbacks each reach the shared composition path,
at return addresses0x81c364 and0x579f1a. Their observed native matrices match;
the shot/composition journals lose no records. Server token0 means **absent**,
never a cross-thread matching key. Temporal proximity and equal matrices alone
do not provide a reusable transaction identity. No aim or damage override is made.

The independent322 audit also catches an additional branch1 idle2->recovery6->idle2
transition lasting38.20ms after the second attack. There are still exactly two
observed shoot calls per side, but the strict two-cycle state-sequence assertion
fails. Preserve this as unresolved prediction/recovery behavior;322 passes only
shot-origin observation, not another full attack-lifetime acceptance. Four ordinary
firing samples miss the nonblocking logger, separately from the lossless shot log.

323 decodes both knife mesh sections using the shared geometry pipeline and the
authored static part transform. Three orthographic mesh views place the blade tip
at approximately(0,0.004730,-0.250916) metres in the pipeline's canonical reflected-Z
frame; the authored right wrist is(0.019257,0.072308,-0.036900). The flash bone is
near the weapon origin inside the handle, not the tip. Any future knife binding
must use a reviewed blade landmark rather than the firearm muzzle convention.
This private geometry review does not enroll a player render asset or distribute
the installed mesh.

330 subsequently verifies two different synthetic directions in both native
composition outputs while preserving the source matrices and rifle ammunition.
See [private330 evidence](KNIFE-AIM-330.md). This does not verify downstream
damage or enable a player knife; the original attack delay remains.

## Remaining integration

Chest contact and body rendering, tracked knife placement, player input routing,
hand-directed server hit origin, damage receipts and the native roughly500ms
attack delay remain open. Neither318 nor320 enables a player-facing knife. Third
native pistol capacity, pistol chambering and full belt-fed loading remain separate
tasks; physical grenades remain deferred.

Local evidence:

- `knife317-stab01/stab-audit.json`
- `knife318-motion02/motion-audit.json`
- `knife319-shot01/{effects,server-effects}.json`
- `knife322-shot-origin01/shot-origin-audit.json`
- `knife323-geometry/geometry-review.json` and the private orthographic review
- frozen prototype sources and build receipts in the corresponding source folders

The ready player build should be tested for the combined rifle/pistol support,
manual magazines, SPAS pumping/reload, shoulders/hip and menu/restart behavior.
Do not add knife interaction to that test list until its production path is wired.
