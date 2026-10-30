# Physical feed reload components

The portable reload coordinator, measured gesture recognizer and their combined
`FeedReloadSession` are implemented. The session turns actual linear/hinge motion
into one-shot typed requests, retains pending/accepted work through interruption,
and requires exact acknowledgement plus fresh authoritative post-state before
another physical operation. All nine feed operations remain distinct.

This completes these source assignments, **not CHAT15 or manual LMG support in
BC2**. No BC2 adapter invokes the session yet. No native capability, ammunition
write, firing permission or installation was enabled by this batch.

Root validation: FeedReload 22 cases, FeedInteraction 25 cases and
FeedReloadSession 17 cases passed independently on x86 and x64. Session tests drive
the real gesture/coordinator composition with explicit synthetic adapter evidence.
The focused independent source review found no actionable issues.

Reports are under `reports/coordinator-20261030-implementation15`:
`feedreload-{x86,x64}-check01`, `feedinteraction-{x86,x64}-check01`,
`feedsession-x86-check03`, and `feedsession-x64-check01`.
The two earlier x86 session failures remain preserved: initial missing capability
returns NeutralRequired before binding admission; a rejected future input sequence
also rejects the next equal sequence. Both corrections changed test expectations,
not production behavior. `source-manifest.json` pins the final sources and results.

Exact M60 authored assets now establish top-cover, pouch and articulated belt
presentation geometry, plus the side slider's measured stroke. The CHAT14 geometry
handoff retains hashes and limits. Game-derived binary assets/rendered PNGs remain
private. Native stage ownership, resource transfer, feed socket and charge/fire
semantics still require the prepared bounded actual-game observation.

Both full normal builds remain part of the held integration step. Canonical source,
pushed source, installed private0.1.17, rollback0.1.12 and user settings are unchanged.
The user's offline-only PC instruction remains in force; no game/input/XR ran.
