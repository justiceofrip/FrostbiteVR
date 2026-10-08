# Equipment integration 238 — in progress

The user's current request is one substantial combined test, with automated
actual-game checks before another headset session. This checkpoint is not a
headset acceptance claim.

Full normal238 builds passed225 C++ suites on both x86 and x64. Frozen source:
`80b9328cb8fe0bb85f62664210e1fe4bad8a497682904a97618b4b78b08a2cab`.
The normal build still leaves native pump and bolt activation off. Separate
frozen diagnostic builds passed20 combined and28 bolt-focused suites per
architecture.

## Actual238 follow-up

- `resource-scar238-01`: both SCAR magazine cycles completed, including original
  return and discarded-magazine replacement; four native operations plus discard
  completed. Pump and shell insertion then reached the same support-return
  failure22 as XM8. The former active read-view cancellation did not recur.
  Read-ring overwrites prevent localizing five early no-view reads; no admission
  policy change follows from this retry.
- Follow-up239 fixes queued support ownership to use current processing time
  without changing original input or contact deadlines. The regression preserves
  the actual failure22 with old ordering and completes with corrected ordering.
  Completed bolt gun placement now survives native debt retirement; the new
  regression models the renderer fallback that caused the actual gun jump.

- `resource-pump238-01`: hand-clock correction worked; shell ownership survived
  103 geometry samples and reached0.996681 insertion progress. A125ms simulated
  input gap expired the original100ms input lifetime and cancelled the attempt.
  Compilation was running concurrently; its causal role is not established.
- `resource-pump238-02`: same binary, no root compilation. Pump and shell loading
  progressed to the post-shell support-return phase14, which failed22 before
  the final shoulder/rifle steps. This is a different failure from input expiry.
- `m95-physical238-01`: one actual physical bolt cycle completed,5/45→4/45, with
  exact owner retained. Three native branches held250/250/251 times, restored,
  then completed their own original tails. One release and readiness ack,
  eight paired target edges, and atomic custody return were retained. The
  requested second cycle failed at support settlement. The whole test failed.
  Review identified missing right-hand weapon placement after native retirement;
  a narrow correction is being staged, with a renderer-fallback regression.
- All three runs delivered240 matching stereo pairs and detached cleanly with
  BC2 responsive. Native recorder loss is reported separately; no full recording
  or headset acceptance is inferred from the completed portions.

## Native evidence retained

- `resource-pump237-01`: one physical SPAS pump completed with native readiness
  acknowledgement and support-hand return. The following shell operation failed
  because an inactive physical-cycle consumer updated hand ownership with an
  earlier timestamp. All recorded native holds were restored. The test did not
  complete the final holster/rifle steps.
- `resource-scar237-03`: exact `SCAR_sp` was provisioned into the rifle slot while
  preserving the SPAS. Actual native magazine remove/return/discard operations
  completed, but the replacement-held consumer cancelled after a resource-view
  read returned no value. No refill was submitted. The combined sequence failed;
  neither this weapon nor the general magazine path is accepted by this result.
- Both runs used simulated tracked inputs in the actual campaign, without OS
  mouse/keyboard control. Both ended with BC2 responsive and restored hooks.
- The two earlier SCAR237 setup attempts rejected before injection. Their raw
  reports remain alongside the actual attempt.

## Shared corrections and parallel work

- Merged the inactive physical-cycle hand-clock correction and regressions:
  idle/completed cycles without a mechanism claim do not mutate shared hands.
  Active reversal, expiry, cancellation and ownership checks remain intact.
- The private provisioning helper now reads the engine's reflected ammunition
  layout correctly: capacity at primary+0x184; spare-magazine count at +0x180.
  This fixes the test helper, not the already-correct main reload reader.
- Physical M95 work includes measured bolt movement, atomic gun-hand transfer,
  native hold/release/readiness and one/two-cycle simulated controller checks.
  Actual manual M95 operation remains to be verified.
- MP443 SingleFire compiler/instruction proof is merged as disabled data and
  offline tests. It establishes neither a live chamber nor native ownership.
  Magazine/contact extraction and native admission remain separate work.
- Ordinary SPAS activation is staged behind a default-off build option. It
  must pass the combined pump-to-shell sequence before entering a player build.

All raw evidence and private asset caches remain outside the source repository,
under the local `bc2vr-recovery` directory. Existing GitHub remains a forkable
snapshot; no publication is authorized by this integration step.
