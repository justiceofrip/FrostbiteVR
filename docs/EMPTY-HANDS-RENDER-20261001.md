The empty-hands render candidate now connects exact asset coverage to the existing

## Sampled SPAS native visibility acceptance — October 1, 23:03 UTC

Trace 225926-435 completed hide/show. Direct inspection of both eye captures at
pairs 0/48/72 confirms SPAS visible/hidden/restored with both arms preserved.
The private palette and exact source checks pass; one recovered frame request
timeout keeps the combined audit's delivery gate false. See
../reports/weapon-visibility-pixel-review-225926.json. This establishes the sampled
SPAS renderer primitive only. Full native holster input suppression, independent
free-right lifecycle, XM8 coverage and headset acceptance remain separate.
owned private-palette publisher and native Pack callbacks. It remains off unless
Gameplay explicitly calls PublishWeaponVisibility with a fresh request.

Derived asset coverage:

| Asset | Sections / LODs | Vertices | Weighted bones |
| --- | --- | --- | --- |
| SPAS12 | 6 / 1 | 10,509 | 9 |
| XM8 including launcher | 6 / 1 | 15,910 | 15 |
| ACOG attachment | 4,2,2 / 3 | 7,887 | jntWpn_10 |
| Standard first-person arms | 4 / 1 | 6,041 | Arm/hand/spine only |
| Assault first-person arms | 4 / 1 | 3,384 | Arm/hand only |

All 46 weighted arm hashes are disjoint from all 16 weighted weapon/attachment
hashes. Every vertex and triangle index was checked, including SPAS Z-only
geometry, XM8 launcher parts, and all ACOG LODs. The nonstandard native names
jntWpnwpnJnt_14 and jntWpnwpnJnt_16 are included explicitly. The names and ancestry
match the complete saved native rig a7f219a1426216ab. No mesh assets are exported.

BuildWeaponVisibilityPalette requires exact fresh selected assets, including
attachments, full native owner, original physical input/equip identity, and the
known complete rig fingerprint. It zeros only the twelve consumed xyz scalars
of the weighted final skin matrices. All other palette entries, both hands,
original native source bytes and SIMD padding are preserved. This is a private
render palette; the native hierarchy and animation are never modified.

RigPublication now builds this plan after ordinary hand/weapon posing. It applies
it only while the exact current request and native pose source remain valid.
Sight/reload previews cannot overlap. Release, expiry, owner change or tracking
loss chooses the ordinary palette, including between the two Pack callbacks.
Only successful original-source and actual destination-byte comparisons for BOTH
callbacks mint WeaponVisibilityReceipt. Its original deadline is never renewed.

APIs in Bc2RigPublication.h:

    PublishWeaponVisibility(const WeaponVisibilityRequest&)
    ReadWeaponVisibilityReceipt(const ReloadStateOwner&, uint64_t request, int64_t nowNs)

A disabled request clears immediately. Request carries enabled/hide/request,
full native owner, original HandInteractionSample and immutable selected snapshot.
Receipt carries owner/rig/request, physical equip generation, input sequence,
native draw serial, verifiedCopyMask=3 and original expiry. hidden=false means the
ordinary private palette was copied. It does not claim that every section was
submitted, or that native authored hidden parts should become visible.

Validation: six focused C++ groups pass x86 and x64, including nonstandard
attachments, mixed skin weights, both-hand byte preservation, malformed indices,
hash aliases, input restamping and expiry/owner replacement. Actual x86
RigPublication translation unit compiles. Four Python parser groups validate
half/float positions, all influences, palette aliases and malformed geometry.
No native rendering or headset hide test has been run for this candidate.

Native acceptance still required before BodyInventory may acknowledge EmptyHands:

1. Root publishes a bounded hide request for the actual selected SPAS, then XM8
   with its ACOG/launcher attachment. Both Pack calls must verify the same original
   source and request, with all nonweapon bytes preserved.
2. Observe the game/eye output: no weapon, shell/Z-only residue, launcher or scope;
   both tracked arms/hands remain. The source remains unchanged, and disabling or
   expiring the request restores the ordinary selected weapon on the next copy.
3. Repeat native selection/reference loss and show; never reuse an old hide receipt
   for a replacement weapon or a freshly acquired resource.

Auxiliary muzzle flashes, casing particles, native scope/HUD overlays and other
effects outside these mesh sets are NOT covered. Gameplay must suppress fire and
scope activation while holstered; hiding bones alone does not disable gameplay.
Current native arms asset association and material/shader behavior remain part of
the actual hide observation, not an invented proof from the configured mesh list.
Normal shoulder-switch acknowledgement can continue using the existing ordinary
render path. This candidate grants no GPU draw-suppression or headset acceptance.
