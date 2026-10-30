# Support43 fixed authored contact additions

Five exact support selectors cover eight configuration DBX records:

| Native name | Exact mesh suffix | Source configurations |
| --- | --- | --- |
| AN94 | BU_rif_AN94Abakan/BU_rif_AN94Abakan_Mesh | Base, Kobra, Scoped |
| AN94_sp_k | BU_rif_AN94Abakan/BU_rif_AN94Abakan_Mesh | SP_rif_AN94_Kobra |
| AN94_sp_s | BU_rif_AN94Abakan/BU_rif_AN94Abakan_Mesh | SP_rif_AN94_Scoped |
| Garand | Beach/Jap_Type5/Jap_Type5_Mesh | Type5 |
| Garand | Beach/M1Garand/M1Garand_Mesh | M1Garand, M1Garand_Assault |

`authoring-policy.json` explicitly defines a fixed mod support contact from the
exact absolute HandsIkPose LeftHand relative to jntWpn_1 at t=0. The input stays
`decoded_spline_candidate`; output is `mod_authored_fixed_contact`. No coordinates
are guessed and no existing static or bounded gate is relaxed. The rejected
full-curve bounds remain in `derivation.json`: AN94 46.023 mm / 5.324 degrees,
Type5 3.695 mm / 0.827 degrees, and M1Garand 3.946 mm / 1.102 degrees.

`generate.py` uses the existing bounded archive pool, DBX resolver, skeleton/clip
decoder, configuration/mesh join and header writer. It validates pinned cached
documents against fresh exact archive indices, DBX hashes/GUIDs, all replacement
animation roles, sole state, mesh identities, clip hashes and skeleton topology.
The old M1 wrapper rejection is superseded only by current decoding of the same
pinned bytes. Missing M1 geometry is joined from its exact mp_common mesh/LOD;
Type5 is a separate source. No archive inventory survey or game assets were saved.

Root composition: macro-rename `AuthoredSupports` to `Weapons43Supports` while
including `profiles/weapons43/Support.additions.generated.h`, then append that
array to the existing opening426 composition. None of these five keys occur in
the prior four input headers. `BindAuthoredSupport` already selects asset plus
exact configured mesh and rejects ambiguity; its existing test checks same-name
different-mesh selection in both orders. No runtime file changed in this task.

Storage admission and the metadata generation completed. Root owns focused tests,
both builds and combination. Run `py -3 -m unittest discover -s tests -p
test_bc2_explicit_support_policy.py` plus existing support/pose suites. A bounded
reproduction uses `generate.py --game <installation> --verify` after StorageBudget.
No native or headset check, contact-feel acceptance, ammunition/bolt capability,
or full-firearm-roster completion is claimed by this handoff.
