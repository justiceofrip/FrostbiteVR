# Weapon profile capture and batch pipeline

The aim is to make the next ordinary gun a data review, not another implementation
of arm IK, reference recovery, grip input, or firing hooks. Portable weapon policy
and BC2's exact asset registry stay separate from this offline measurement tool.
Dynamic native animation still supplies authored attachments at runtime.

A profile does not mean every item uses the same mechanics. A launcher sight,
underbarrel mode relationship, articulated handle, or unusual reload still needs
evidence for that interaction. The pipeline never copies XM8 offsets into an
unknown launcher or infers a weapon family from its skeleton alone.

## Capture once, audit in batches

The BC2 native collector emits gameplay.rig_publication.weapon_profile_samples
before tracked-pose publication. It retains bounded per-item data, including items
whose VR posing is unsupported. Capacity and dropped/invalid sample counters are
preserved in the audit.

Each raw row carries exact asset name, capture sequence/episode, actor/equipment/
reference-space identity, stable skeleton fingerprint, validated bone-role names,
source matrices, units per metre, equip age, settling state and timing. Numeric
actor and weapon pointers identify objects only within that capture. They are not
portable profile IDs.

The new array takes precedence over the old hand_evidence debug ring. If both
exist, samples are not added together. An empty new collector is reported as
empty, not silently replaced with old debug evidence.

From the project directory:

~~~powershell
$python = 'python.exe'
& $python -B tools\weapon_profile_pipeline.py --trace reports\native-trace-YYYYMMDD-HHMMSS-NNN --output reports\weapon-profile-batch
~~~

Repeat --trace to audit several captures. The --reports-root reports option
discovers immediate native-trace-*/native-trace.json captures without recursively
ingesting checkpoint copies. Explicit and discovered paths are deduplicated.
Historical captures may lack names or matrices; the audit shows these gaps.

Outputs:

- candidates.json: versioned fvr.weapon_profile_candidates schema, profile IDs,
  measured attachments, per-segment statistics, capture hashes and review status.
- audit.md: readable item/rig coverage table, source integrity and rejected,
  unsettled or insufficient evidence counts.

The --strict option exits nonzero when input/identity errors, unmapped assets,
failed capture checks, collector capacity/invalid-sample drops, or incomplete/
ambiguous attachment candidates remain.
Normal mode still writes a partial audit so broad discovery can identify missing
work. Invalid or duplicate/out-of-order rows fail strict mode even when enough
valid samples remain for a partial candidate. Missing native completion is
reported as incomplete and does not prohibit a geometric measurement; strict
success therefore does not imply native-feature or headset acceptance.
Neither mode injects code, opens a game process, changes the runtime
registry, or installs generated output.

## What a candidate means

The measured relation is:

~~~text
wrist_in_weapon = native_wrist_world * inverse(native_weapon_world)
~~~

Matrices are canonical row-major, row-vector, left-handed affine transforms.
New rows require finite positive units_per_meter. Derived local translations
are divided by it, so output distances and translations are metres. Historical
hand_evidence uses BC2's original adapter scale of one native unit per metre;
the segment explicitly records historical_bc2_adapter_scale_1 as its units basis.

The pipeline:

1. Validates finite, affine, proper rigid matrices before inversion. Degenerate,
   reflected, projective, malformed and non-finite transforms are rejected.
2. Splits episodes by asset, actor, owner generation, equipment pointer, reference
   space, skeleton identity and scale. Duplicate/out-of-order capture sequence
   values are rejected. Legacy rows use publication generation.
3. Excludes rows marked attachment_pending or missing that state. New equipment
   identity cannot learn the previous gun's still-active pose.
4. Requires eight settled samples, eight consecutive inliers, at least 80%
   agreement within 3 mm / 2 degrees, and a 150 ms inlier span where timestamps
   exist. Legacy rows without timing explicitly report a null span.
5. Selects a real observed transform as representative; it does not average
   unrelated reload, equip or grip poses. Isolated outliers remain enumerated.
   A second stable pose with eight samples makes the result ambiguous.
6. Keeps different rig fingerprints separate. Repeated stable observations of
   the same item/rig must agree across actors, episodes and captures. Conflicting
   stable offsets are retained without a merged matrix. A separate ambiguous
   episode also blocks a combined matrix; good samples cannot hide it.

These are audit thresholds, not runtime reach/contact tolerances. A measured left
wrist relation is authored anatomy, not proof of a usable support-grab interaction.
The native muzzle bone does not establish projectile origin, barrel direction or
sight mechanics. Those features remain unverified without separate evidence.

When old debug rows have authored and published matrices, the audit reports their
positional/angular difference. This is diagnostic only: recoil, reach limits and
explicit support publication have separate behavior checks.

Source mutation, packing failure and failed cleanup are reported. For the new raw
collector, tracked-pose fallback failures are publication warnings; they do not
erase independently valid anatomy gathered before publication. For historical
debug rows, those failures block combined candidate promotion. This distinction
does not classify the cause of a fallback or grant a capability.

## Mapping old captures safely

Legacy traces often have only an equipment pointer. Use a capture-hash-bound map
instead of treating it as a persistent identity:

~~~json
{
  "schema": "fvr.weapon_capture_map",
  "schema_version": 1,
  "captures": [
    {
      "trace": "../native-trace-YYYYMMDD-HHMMSS-NNN/native-trace.json",
      "sha256": "<SHA-256 of that exact trace>",
      "items": [
        {
          "weapon": 129298256,
          "owner_generation": 1,
          "first_row": 0,
          "last_row": 255,
          "asset_id": "XM8_sp_s"
        }
      ]
    }
  ]
}
~~~

Pass --mapping path\to\mapping.json. Paths are relative to that file. A pointer
can be decimal or a string such as "0x07B4EF50". Actor-generation and row bounds
are optional selectors. If a pointer was reused, scope it to the correct rows.
Conflicting matches or disagreements with embedded asset names are rejected.
The name must come from trusted native discovery; the tool cannot invent it.

Stable IDs match the runtime registry: bc2:SPAS12_sp, bc2:XM8_sp_s, or
bc2:<exact asset name>. An unknown asset may have candidate measurements while
every runtime capability stays disabled.

## Verification and headset acceptance are separate

Generated profiles have status candidate_only, runtime_enabled false, and
unverified feature states by default. Stable matrices and clean completion
do not establish headset feel or enable runtime features.

An optional explicit review manifest carries previously established,
feature-specific evidence. This is a caller-supplied review assertion, not
acceptance inferred by the tool:

~~~json
{
  "schema": "fvr.weapon_profile_reviews",
  "schema_version": 1,
  "reviews": [
    {
      "stable_id": "bc2:XM8_sp_s",
      "applies_to_checkpoint": "reports/weapon-alignment-20261001/checkpoint.json",
      "features": {
        "support_grip": {
          "verification": "headset_accepted",
          "native_evidence": {
            "path": "../native-validation.json",
            "sha256": "<exact file SHA-256>",
            "scope": "XM8 support attachment, release and source restoration"
          },
          "headset_evidence": {
            "path": "../headset-grips-success-20261001-072621/acceptance.json",
            "sha256": "<exact file SHA-256>",
            "scope": "User accepted XM8 grips; orientation polish deferred"
          }
        }
      }
    }
  ]
}
~~~

Pass --reviews path\to\reviews.json. Feature keys are aim_alignment, support_grip,
and translated_muzzle; states are unverified, native_verified, and
headset_accepted. Verified declarations require a named checkpoint and native
evidence. Headset acceptance additionally requires its own evidence. Every
reference needs a scope and matching SHA-256.

Evidence content still needs review. The tool checks file identity and schema,
not whether arbitrary text proves its asserted scope. Review provenance and
checkpoint stay attached and never automatically apply to a new build. Grip
acceptance does not imply reconnect, launcher, muzzle-origin, impact-accuracy,
or sight-interaction acceptance.

## Regression coverage and remaining work

~~~powershell
& $python -B tests\test_weapon_profile_pipeline.py
~~~

Twenty-four synthetic regressions cover delayed equip pose, identity separation,
multiple skeletons, actor-dependent conflict, outliers, ambiguous poses, matrix
rejection, duplicate samples, insufficient timing, unit conversion, generation
rollback, capture hashes, collector/debug deduplication, source failure,
raw/publication distinction and scoped acceptance provenance. Fixtures contain
no game assets.

Read-only extraction of the existing 071112 weapon-transition and 071537 support
captures produces stable SPAS and XM8 attachments, with within-segment translation
variation below 0.4 mm. This validates the pipeline; it is not a new gameplay or
headset test.

Per-gun numerical offsets are not automatically baked into runtime code. Review
ordinary item candidates in batches, run shared native behavior checks, and seek
focused headset confirmation for handling. Novel mechanisms such as the
grenade-launcher sight need reusable interactions with adapter-specific bindings
after the relevant native relationships are discovered.

## First end-to-end named capture

The new collector-to-pipeline check passes strict audit using
reports/native-trace-20261001-074605-958. Output is
reports/weapon-profile-batch-20261001/{candidates.json,audit.md}.

It has 73 named samples, two exact assets, zero invalid/capacity-dropped samples,
zero unmapped identities, and clean native completion. The initial XM8 episode
has only three settled rows and remains explicitly insufficient. Later SPAS and
XM8 episodes supply 35 and 17 settled rows, respectively, so both items have
candidates without borrowing the initial inadequate episode.

Maximum observed translation spread is 0.277 mm at the SPAS right wrist and
0.370 mm at its left wrist; XM8 is 0.330 mm / 0.317 mm. These are observed-data
consistency measurements, not accuracy claims about authored anatomy or user
comfort. All three gameplay features remain unverified in this generated output
because no external review manifest was supplied.

## Launcher mechanism extension

Current source capture also retains bounded named weapon-subtree matrices and
explicit hidden/incomplete state; capacity is96 samples per item/rig group.
weapon_mechanism_pipeline.py compares stable root-relative child poses across
exact-asset roundtrips, reports repeatable movement and optional unnamed pivot-line
fits, and never assigns a sight role or native write capability by itself.
The scoped-XM8 pilot separately correlates its two90-degree changes to rendered
sights and validates native selection/physical interaction. Read
LAUNCHER-PILOT-20261001.md; final strict results are in
reports/launcher-mechanisms-acceptance-20261001/audit.md.

Short native name40mmgl is reused. Current runtime requires the full scoped-XM8
asset/persistent family. Extend raw capture grouping with those stable fields
before promoting multiple rifles' launchers; do not treat short-name equality as
proof that their native mechanism or muzzle is shared.

## User coverage checklist — October1

The user supplied these planning categories and counts; the exact installed-game
roster, campaign/multiplayer variants and duplicate unlocks still need inventory
verification. These counts are not claims of implemented or verified support.

| Category | User-provided count |
| --- | ---: |
| Assault rifles | 9 |
| SMGs | 7 |
| LMGs | 8 |
| Sniper rifles | 7 |
| Shotguns | 6 |
| Sidearms | 5 |
| Miscellaneous/gadgets/signature unlocks | Unspecified |

Capture and audit by exact asset plus compatible rig/handling family. Ordinary
weapon adoption should reuse tracking, aiming, recenter and IK. Each asset still
needs feature-specific validation; category membership does not enable it.
Pump/bolt actions, scopes, folding sights, launcher relationships and gadgets can
require dedicated interaction evidence. Share those mechanisms where verified,
rather than building one complete control system per weapon. Keep measured,
native-verified and headset-accepted statuses separate.
